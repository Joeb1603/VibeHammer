#include "hard/evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <variant>

#include "analysis/charge_odds.hpp"
#include "analysis/threat.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "directions.hpp"
#include "state_hash.hpp"

namespace wh40k::ai::hard {

namespace {

constexpr float kIn = core::MM_PER_INCH;
// 24.16 [HEAVY]: remained stationary or moved no more than 3" (strategic knowledge, as analysis::allowance_after).
constexpr float kHeavyMoveMm = 3.0f * kIn;
// A unit ending a move just outside engagement range (09.05) stands this far beyond it.
constexpr float kJustOutsideMm = core::kEngagementRangeMm + 0.1f * kIn;
constexpr double kTie = 1e-12;  // an option must beat an earlier one by more than this
constexpr std::size_t kMaxTables = 8;
constexpr int kMinLine = 2;   // buckets
constexpr int kMaxLine = 24;  // buckets (48")

enum Kind : std::size_t { kHeavy = 0, kMoved = 1, kAssault = 2, kCloseQuarters = 3, kMelee = 4, kKinds = 5 };

struct Node {
    double j = 0.0;   // score from side A's view
    double pw = 0.0;  // P(side A wins)
    double pl = 0.0;  // P(side B wins)
};

void add(Node& acc, const Node& n, double w) {
    acc.j += w * n.j;
    acc.pw += w * n.pw;
    acc.pl += w * n.pl;
}

Node mix(const Node& a, const Node& b, double wa) {
    Node out;
    add(out, a, wa);
    add(out, b, 1.0 - wa);
    return out;
}

std::size_t si(core::Side s) { return core::side_index(s); }

core::Position centroid_of(const core::Unit& u) {
    core::Position c;
    for (const core::Model& m : u.models) {
        c.x += m.position.x;
        c.y += m.position.y;
    }
    const float n = static_cast<float>(std::max<std::size_t>(1, u.models.size()));
    return core::Position{c.x / n, c.y / n, 0.0f};
}

// How far `u` could back away from `from` (a centroid) before a base meets a battlefield edge, along the best of
// the 16 fixed directions within 60 degrees of straight away (as M43's MoveScoreContext::room_away), measured
// along the line away from `from`.
float room_away(const core::GameState& gs, const core::Unit& u, const core::Position& from) {
    if (u.models.empty()) return 0.0f;
    const core::Position c = centroid_of(u);
    float ax = c.x - from.x;
    float ay = c.y - from.y;
    const float len = std::sqrt(ax * ax + ay * ay);
    if (!(len > 0.0f)) return 0.0f;
    ax /= len;
    ay /= len;
    const float w = gs.battlefield.width_mm;
    const float h = gs.battlefield.depth_mm;
    float best = 0.0f;
    for (const client::UnitVector& d : client::kDirections16) {
        const float dot = d.x * ax + d.y * ay;
        if (dot < 0.5f) continue;
        float t = 1.0e6f;
        for (const core::Model& m : u.models) {
            const float r = core::base_radius(m.base);
            const core::Position& p = m.position;
            if (d.x > 1e-6f) t = std::min(t, (w - r - p.x) / d.x);
            if (d.x < -1e-6f) t = std::min(t, (r - p.x) / d.x);
            if (d.y > 1e-6f) t = std::min(t, (h - r - p.y) / d.y);
            if (d.y < -1e-6f) t = std::min(t, (r - p.y) / d.y);
        }
        best = std::max(best, std::max(0.0f, t) * dot);
    }
    return best;
}

int wounds_of(const core::Unit& u) {
    int w = 0;
    for (const core::Model& m : u.models) w += std::max(0, m.wounds_remaining);
    return w;
}

bool same_loadout(const core::Model& a, const core::Model& b) {
    return a.characteristics == b.characteristics && a.weapons == b.weapons && a.base == b.base;
}

// The unit's most common loadout (characteristics, weapons, base); ties go to the first in unit order. The chain
// treats the unit as copies of it (every V1 unit has one loadout).
const core::Model& representative(const core::Unit& u) {
    std::size_t best = 0;
    std::size_t best_count = 0;
    for (std::size_t i = 0; i < u.models.size(); ++i) {
        std::size_t count = 0;
        for (const core::Model& m : u.models) count += same_loadout(u.models[i], m) ? 1 : 0;
        if (count > best_count) {
            best = i;
            best_count = count;
        }
    }
    return u.models[best];
}

// The unit as `n` copies of its representative model, unwounded (ids 1..n).
core::Unit prototype(const core::Unit& u, int n) {
    core::Unit p = u;
    const core::Model rep = representative(u);
    p.models.clear();
    for (int i = 0; i < n; ++i) {
        core::Model m = rep;
        m.id = core::ModelId{static_cast<std::uint32_t>(i + 1)};
        m.wounds_remaining = m.characteristics.wounds;
        m.position = core::Position{};
        p.models.push_back(std::move(m));
    }
    return p;
}

std::vector<float> usable_ranges(const core::Model& m) {
    std::vector<float> out;
    for (const core::Weapon& w : m.weapons) {
        for (const core::WeaponProfile& p : w.profiles) {
            if (p.kind == core::WeaponKind::Ranged && p.range_mm && analysis::profile_supported(p)) {
                out.push_back(*p.range_mm);
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ---- the static description of a duel (the table's key) ----

struct SideSpec {
    core::Unit proto;          // max_models copies of the representative model
    int wounds_per_model = 1;  // W
    int wounds_start = 1;      // starting strength x W (material is a share of it)
    int max_wounds = 0;        // the region: wounds left at the root
    int max_models = 0;
    int move_buckets = 0;                // M
    std::array<int, 7> advance_buckets{};  // [f]: M + f (09.06)
    std::vector<float> ranges;           // usable ranged profiles' ranges (the bands)
};

struct Spec {
    std::array<SideSpec, 2> sides;
    int half_turns = 10;
    core::Side first = core::Side::A;
    int h0 = 0;
    int line = 10;  // buckets
    ChainWeights weights;
    std::uint64_t key = 0;  // everything but the region (h0, max wounds)
};

float bucket_gap_mm(int g, float bucket) {
    return std::max(static_cast<float>(g) * bucket, kJustOutsideMm);
}

}  // namespace

// ---- the table ----

class ChainTable {
public:
    explicit ChainTable(analysis::OddsCache& odds, Spec spec, float bucket);

    [[nodiscard]] const Spec& spec() const noexcept { return spec_; }
    [[nodiscard]] bool covers(const Spec& s) const noexcept {
        return s.key == spec_.key && s.h0 >= spec_.h0 && s.sides[0].max_wounds <= spec_.sides[0].max_wounds &&
               s.sides[1].max_wounds <= spec_.sides[1].max_wounds;
    }
    [[nodiscard]] int line() const noexcept { return spec_.line; }
    [[nodiscard]] int half_turns() const noexcept { return spec_.half_turns; }
    [[nodiscard]] core::Side side_at(int h) const noexcept {
        return h % 2 == 0 ? spec_.first : core::opponent(spec_.first);
    }

    // Grid lookups (side A's view). Positions: engaged (g = 0) rA in [0, L]; unengaged g in [1, L], rA in
    // [0, L - g].
    [[nodiscard]] Node start(int h, int wa, int wb, int g, int ra) const;
    [[nodiscard]] Node end_turn(int h, int wa, int wb, int g, int ra) const { return start(h + 1, wa, wb, g, ra); }
    [[nodiscard]] Node fight(int h, int wa, int wb, int ra) const;  // engaged, the side whose turn it is first
    [[nodiscard]] Node reply(int h, int wa, int wb, int ra) const;  // engaged, the other side strikes back
    [[nodiscard]] Node win(core::Side winner, int h, int wa, int wb) const;
    [[nodiscard]] Node draw(int wa, int wb) const;

    // The pieces of a half-turn, on the grid (computed from the stored nodes).
    [[nodiscard]] Node charge_best(int h, int wa, int wb, int g, int ra) const;
    [[nodiscard]] Node shoot_then(int h, Kind kind, int wa, int wb, int g, int ra) const;
    [[nodiscard]] double p_charge(int g) const { return p_charge_[static_cast<std::size_t>(g)]; }
    [[nodiscard]] bool better_for(core::Side mover, const Node& a, const Node& b) const {
        return mover == core::Side::A ? a.j > b.j + kTie : a.j < b.j - kTie;
    }
    [[nodiscard]] const SideSpec& side(core::Side s) const { return spec_.sides[si(s)]; }
    [[nodiscard]] int models_of(core::Side s, int wounds) const {
        const int w = std::max(1, side(s).wounds_per_model);
        return (wounds + w - 1) / w;
    }
    // [w'] = P(the target is left with w' wounds) when `attacker`'s models alive with `attacker_wounds` attack
    // a target with `target_wounds` left.
    [[nodiscard]] const double* pmf(core::Side attacker, Kind kind, int g, int attacker_wounds,
                                    int target_wounds) const;

private:
    struct DistSet {
        int max_n = 0;
        int max_w = 0;
        std::vector<std::size_t> offset;  // (n - 1) * max_w + (w - 1)
        std::vector<double> data;
    };
    [[nodiscard]] std::size_t pos_index(int g, int ra) const {
        if (g == 0) return static_cast<std::size_t>(ra);
        return static_cast<std::size_t>(spec_.line + 1) + row_offset_[static_cast<std::size_t>(g)] +
               static_cast<std::size_t>(ra);
    }
    [[nodiscard]] std::size_t idx(int wa, int wb, int g, int ra) const {
        return (static_cast<std::size_t>(wa) * static_cast<std::size_t>(spec_.sides[1].max_wounds + 1) +
                static_cast<std::size_t>(wb)) *
                   positions_ +
               pos_index(g, ra);
    }
    [[nodiscard]] std::size_t eidx(int wa, int wb, int ra) const {
        return (static_cast<std::size_t>(wa) * static_cast<std::size_t>(spec_.sides[1].max_wounds + 1) +
                static_cast<std::size_t>(wb)) *
                   static_cast<std::size_t>(spec_.line + 1) +
               static_cast<std::size_t>(ra);
    }
    void build_distributions(analysis::OddsCache& odds, float bucket);
    void build_half_turn(int h);

    Spec spec_;
    std::size_t positions_ = 0;
    std::vector<std::size_t> row_offset_;  // [g]: positions before row g among the unengaged ones
    std::vector<double> p_charge_;         // [g]
    std::array<std::vector<int>, 2> band_of_;  // [side][g]: the shooting band (0..)
    // [side][kind][band] -> an index into pool_ (attacks with the same volley share one set).
    std::array<std::array<std::vector<std::size_t>, kKinds>, 2> dist_index_;
    std::vector<DistSet> pool_;
    std::vector<std::vector<Node>> start_;  // [h - h0][idx]
    std::vector<std::vector<Node>> fight_;  // [h - h0][eidx]
    std::vector<std::vector<Node>> reply_;
};

ChainTable::ChainTable(analysis::OddsCache& odds, Spec spec, float bucket) : spec_(std::move(spec)) {
    const int line = spec_.line;
    row_offset_.assign(static_cast<std::size_t>(line + 2), 0);
    std::size_t unengaged = 0;
    for (int g = 1; g <= line; ++g) {
        row_offset_[static_cast<std::size_t>(g)] = unengaged;
        unengaged += static_cast<std::size_t>(line - g + 1);
    }
    positions_ = static_cast<std::size_t>(line + 1) + unengaged;
    p_charge_.assign(static_cast<std::size_t>(line + 1), 0.0);
    for (int g = 1; g <= line; ++g) {
        p_charge_[static_cast<std::size_t>(g)] = analysis::p_charge_roll_covers(bucket_gap_mm(g, bucket));
    }
    build_distributions(odds, bucket);
    const int h0 = std::max(0, spec_.h0);
    const auto count = static_cast<std::size_t>(std::max(0, spec_.half_turns - h0));
    start_.resize(count);
    fight_.resize(count);
    reply_.resize(count);
    for (int h = spec_.half_turns - 1; h >= h0; --h) build_half_turn(h);
}

void ChainTable::build_distributions(analysis::OddsCache& odds, float bucket) {
    for (const core::Side x : {core::Side::A, core::Side::B}) {
        const core::Side y = core::opponent(x);
        const SideSpec& sx = side(x);
        const SideSpec& sy = side(y);
        // Bands: each usable range's "within range" and "within half range" answers (as M43's band_of).
        std::vector<std::uint64_t> keys;
        std::vector<float> band_gap;
        band_of_[si(x)].assign(static_cast<std::size_t>(spec_.line + 1), 0);
        for (int g = 1; g <= spec_.line; ++g) {
            const float gap = bucket_gap_mm(g, bucket);
            std::uint64_t key = 0;
            for (std::size_t i = 0; i < sx.ranges.size() && i < 32; ++i) {
                if (core::within(gap, sx.ranges[i])) key |= 1ull << (2 * i);
                if (core::within(gap, sx.ranges[i] * 0.5f)) key |= 1ull << (2 * i + 1);
            }
            const auto it = std::find(keys.begin(), keys.end(), key);
            if (it == keys.end()) {
                keys.push_back(key);
                band_gap.push_back(gap);
                band_of_[si(x)][static_cast<std::size_t>(g)] = static_cast<int>(keys.size() - 1);
            } else {
                band_of_[si(x)][static_cast<std::size_t>(g)] = static_cast<int>(it - keys.begin());
            }
        }
        if (keys.empty()) band_gap.push_back(kJustOutsideMm);
        core::Unit one = prototype(sx.proto, 1);
        const core::Unit& target = sy.proto;
        const auto volleys_for = [&](Kind kind, float gap) {
            switch (kind) {
                case kHeavy:
                    return analysis::unit_ranged_volley(odds, one, target,
                                                        analysis::ShotContext{core::ShootingType::Normal, true, gap});
                case kMoved:
                    return analysis::unit_ranged_volley(odds, one, target,
                                                        analysis::ShotContext{core::ShootingType::Normal, false, gap});
                case kAssault:
                    return analysis::unit_ranged_volley(odds, one, target,
                                                        analysis::ShotContext{core::ShootingType::Assault, false, gap});
                case kCloseQuarters:
                    return analysis::unit_ranged_volley(
                        odds, one, target, analysis::ShotContext{core::ShootingType::CloseQuarters, false, 0.0f});
                case kMelee:
                case kKinds: break;
            }
            return analysis::unit_melee_volley(odds, one, target);
        };
        // The target with w wounds left (the wounded model first, 05.04), for w = 1..max.
        const int wpm = std::max(1, sy.wounds_per_model);
        const int max_w = std::max(1, sy.max_wounds);
        std::vector<analysis::TargetSnapshot> snaps;
        snaps.reserve(static_cast<std::size_t>(max_w));
        for (int w = 1; w <= max_w; ++w) {
            analysis::TargetSnapshot snap = analysis::snapshot_with_models(sy.proto, (w + wpm - 1) / wpm);
            if (w % wpm != 0 && !snap.slots.empty()) snap.slots.front().wounds_left = w % wpm;
            snaps.push_back(std::move(snap));
        }
        std::vector<std::pair<analysis::Volley, std::size_t>> seen;  // this side's volleys and their sets
        for (std::size_t k = 0; k < kKinds; ++k) {
            const Kind kind = static_cast<Kind>(k);
            const bool banded = kind == kHeavy || kind == kMoved || kind == kAssault;
            const std::size_t bands = banded ? band_gap.size() : 1;
            std::vector<std::size_t>& index = dist_index_[si(x)][k];
            index.assign(bands, 0);
            for (std::size_t b = 0; b < bands; ++b) {
                const analysis::Volley single = volleys_for(kind, banded ? band_gap[b] : 0.0f);
                const auto same = std::find_if(seen.begin(), seen.end(), [&](const auto& v) { return v.first == single; });
                if (same != seen.end()) {
                    index[b] = same->second;
                    continue;
                }
                index[b] = pool_.size();
                seen.emplace_back(single, pool_.size());
                DistSet& set = pool_.emplace_back();
                set.max_n = std::max(1, sx.max_models);
                set.max_w = max_w;
                set.offset.assign(static_cast<std::size_t>(set.max_n * set.max_w), 0);
                for (int n = 1; n <= set.max_n; ++n) {
                    analysis::Volley volley = single;  // n copies of the representative model
                    for (analysis::VolleyBatch& vb : volley) vb.attack_dice *= n;
                    for (int w = 1; w <= set.max_w; ++w) {
                        const analysis::AttackOutcome out =
                            analysis::expected_outcome(odds, volley, snaps[static_cast<std::size_t>(w - 1)]);
                        set.offset[static_cast<std::size_t>((n - 1) * set.max_w + (w - 1))] = set.data.size();
                        for (int left = 0; left <= w; ++left) {
                            const auto l = static_cast<std::size_t>(left);
                            set.data.push_back(l < out.wounds_left_pmf.size() ? out.wounds_left_pmf[l] : 0.0);
                        }
                    }
                }
            }
        }
    }
}

const double* ChainTable::pmf(core::Side attacker, Kind kind, int g, int attacker_wounds, int target_wounds) const {
    const bool banded = kind == kHeavy || kind == kMoved || kind == kAssault;
    const std::size_t band =
        banded ? static_cast<std::size_t>(band_of_[si(attacker)][static_cast<std::size_t>(std::clamp(g, 1, spec_.line))])
               : 0;
    const DistSet& set = pool_[dist_index_[si(attacker)][kind][band]];
    const int n = std::clamp(models_of(attacker, attacker_wounds), 1, set.max_n);
    const int w = std::clamp(target_wounds, 1, set.max_w);
    return &set.data[set.offset[static_cast<std::size_t>((n - 1) * set.max_w + (w - 1))]];
}

Node ChainTable::win(core::Side winner, int h, int wa, int wb) const {
    const double tempo =
        spec_.weights.tempo * static_cast<double>(spec_.half_turns - h) / static_cast<double>(spec_.half_turns);
    Node n = draw(winner == core::Side::A ? wa : 0, winner == core::Side::B ? wb : 0);
    if (winner == core::Side::A) {
        n.j += 1.0 + tempo;
        n.pw = 1.0;
    } else {
        n.j -= 1.0 + tempo;
        n.pl = 1.0;
    }
    return n;
}

Node ChainTable::draw(int wa, int wb) const {
    Node n;
    n.j = spec_.weights.material * (static_cast<double>(wa) / std::max(1, spec_.sides[0].wounds_start) -
                                    static_cast<double>(wb) / std::max(1, spec_.sides[1].wounds_start));
    return n;
}

Node ChainTable::start(int h, int wa, int wb, int g, int ra) const {
    if (h >= spec_.half_turns) return draw(wa, wb);
    if (wa <= 0) return win(core::Side::B, h, wa, wb);
    if (wb <= 0) return win(core::Side::A, h, wa, wb);
    const int hh = std::max(h, spec_.h0) - spec_.h0;
    return start_[static_cast<std::size_t>(hh)][idx(wa, wb, g, ra)];
}

Node ChainTable::fight(int h, int wa, int wb, int ra) const {
    if (h >= spec_.half_turns) return draw(wa, wb);
    const int hh = std::max(h, spec_.h0) - spec_.h0;
    return fight_[static_cast<std::size_t>(hh)][eidx(wa, wb, ra)];
}

Node ChainTable::reply(int h, int wa, int wb, int ra) const {
    if (h >= spec_.half_turns) return draw(wa, wb);
    const int hh = std::max(h, spec_.h0) - spec_.h0;
    return reply_[static_cast<std::size_t>(hh)][eidx(wa, wb, ra)];
}

Node ChainTable::charge_best(int h, int wa, int wb, int g, int ra) const {
    const core::Side x = side_at(h);
    const Node hold = end_turn(h, wa, wb, g, ra);
    const double p = p_charge(g);
    if (!(p > 0.0)) return hold;
    const int ra_after = x == core::Side::A ? std::min(spec_.line, ra + g) : ra;
    const Node charged = mix(fight(h, wa, wb, ra_after), hold, p);
    return better_for(x, charged, hold) ? charged : hold;
}

Node ChainTable::shoot_then(int h, Kind kind, int wa, int wb, int g, int ra) const {
    const core::Side x = side_at(h);
    const bool xa = x == core::Side::A;
    const int wx = xa ? wa : wb;
    const int wy = xa ? wb : wa;
    const double* p = pmf(x, kind, g, wx, wy);
    Node out;
    for (int left = 0; left <= wy; ++left) {
        const double q = p[left];
        if (q == 0.0) continue;
        if (left == 0) {
            add(out, win(x, h, wa, wb), q);
            continue;
        }
        const int na = xa ? wa : left;
        const int nb = xa ? left : wb;
        add(out, kind == kAssault ? end_turn(h, na, nb, g, ra) : charge_best(h, na, nb, g, ra), q);
    }
    return out;
}

void ChainTable::build_half_turn(int h) {
    const core::Side x = side_at(h);
    const core::Side y = core::opponent(x);
    const bool xa = x == core::Side::A;
    const int line = spec_.line;
    const int wa_max = spec_.sides[0].max_wounds;
    const int wb_max = spec_.sides[1].max_wounds;
    const auto hh = static_cast<std::size_t>(h - spec_.h0);
    const std::size_t grid = static_cast<std::size_t>(wa_max + 1) * static_cast<std::size_t>(wb_max + 1);
    std::vector<Node>& start = start_[hh];
    std::vector<Node>& fight = fight_[hh];
    std::vector<Node>& reply = reply_[hh];
    start.assign(grid * positions_, Node{});
    fight.assign(grid * static_cast<std::size_t>(line + 1), Node{});
    reply.assign(fight.size(), Node{});

    // The other side strikes back in the fight (engaged; the next half-turn starts engaged).
    for (int wa = 1; wa <= wa_max; ++wa) {
        for (int wb = 1; wb <= wb_max; ++wb) {
            const int wx = xa ? wa : wb;
            const int wy = xa ? wb : wa;
            const double* p = pmf(y, kMelee, 1, wy, wx);
            for (int ra = 0; ra <= line; ++ra) {
                Node out;
                for (int left = 0; left <= wx; ++left) {
                    const double q = p[left];
                    if (q == 0.0) continue;
                    if (left == 0) {
                        add(out, win(y, h, xa ? 0 : wa, xa ? wb : 0), q);
                        continue;
                    }
                    add(out, end_turn(h, xa ? left : wa, xa ? wb : left, 0, ra), q);
                }
                reply[eidx(wa, wb, ra)] = out;
            }
        }
    }
    // The side whose turn it is fights first (after a charge, 11.04; or as the player whose turn it is, 12.04).
    for (int wa = 1; wa <= wa_max; ++wa) {
        for (int wb = 1; wb <= wb_max; ++wb) {
            const int wx = xa ? wa : wb;
            const int wy = xa ? wb : wa;
            const double* p = pmf(x, kMelee, 1, wx, wy);
            for (int ra = 0; ra <= line; ++ra) {
                Node out;
                for (int left = 0; left <= wy; ++left) {
                    const double q = p[left];
                    if (q == 0.0) continue;
                    if (left == 0) {
                        add(out, win(x, h, wa, wb), q);
                        continue;
                    }
                    add(out, reply[eidx(xa ? wa : left, xa ? left : wb, ra)], q);
                }
                fight[eidx(wa, wb, ra)] = out;
            }
        }
    }
    // Shooting then (unless it advanced) a charge, per band reached; then the start of the half-turn. The same
    // sums as shoot_then / charge_best (the same terms in the same order, so the same values), with the charge
    // decision and the next half-turn's nodes looked up once per position.
    const SideSpec& sx = side(x);
    const int dm = sx.move_buckets;
    const std::size_t wb_stride = static_cast<std::size_t>(wb_max + 1);
    const bool last = h + 1 >= spec_.half_turns;
    const Node* next = last ? nullptr : start_[hh + 1].data();
    std::vector<Node> draws;
    if (last) {
        draws.resize(grid);
        for (int wa = 0; wa <= wa_max; ++wa) {
            for (int wb = 0; wb <= wb_max; ++wb) {
                draws[static_cast<std::size_t>(wa) * wb_stride + static_cast<std::size_t>(wb)] = draw(wa, wb);
            }
        }
    }
    const auto end_at = [&](std::size_t cell, std::size_t pos) -> const Node& {
        return last ? draws[cell] : next[cell * positions_ + pos];
    };
    const Node* fight_data = fight.data();
    const std::size_t engaged_positions = static_cast<std::size_t>(line + 1);
    std::vector<Node> charge(grid * positions_, Node{});
    for (int wa = 1; wa <= wa_max; ++wa) {
        for (int wb = 1; wb <= wb_max; ++wb) {
            const std::size_t cell = static_cast<std::size_t>(wa) * wb_stride + static_cast<std::size_t>(wb);
            for (int g = 1; g <= line; ++g) {
                const double p = p_charge_[static_cast<std::size_t>(g)];
                for (int ra = 0; ra <= line - g; ++ra) {
                    const std::size_t pos = pos_index(g, ra);
                    const Node& hold = end_at(cell, pos);
                    Node best = hold;
                    if (p > 0.0) {
                        const int ra_after = xa ? std::min(line, ra + g) : ra;
                        const Node charged =
                            mix(fight_data[cell * engaged_positions + static_cast<std::size_t>(ra_after)], hold, p);
                        if (better_for(x, charged, hold)) best = charged;
                    }
                    charge[cell * positions_ + pos] = best;
                }
            }
        }
    }
    std::vector<Node> shoot[3];
    for (auto& s : shoot) s.assign(grid * positions_, Node{});
    for (std::size_t k = 0; k < 3; ++k) {
        const bool advanced = k == kAssault;
        Node* out_data = shoot[k].data();
        for (int wa = 1; wa <= wa_max; ++wa) {
            for (int wb = 1; wb <= wb_max; ++wb) {
                const std::size_t cell = static_cast<std::size_t>(wa) * wb_stride + static_cast<std::size_t>(wb);
                const int wx = xa ? wa : wb;
                const int wy = xa ? wb : wa;
                const Node won = win(x, h, wa, wb);
                for (int g = 1; g <= line; ++g) {
                    const double* p = pmf(x, static_cast<Kind>(k), g, wx, wy);
                    for (int ra = 0; ra <= line - g; ++ra) {
                        const std::size_t pos = pos_index(g, ra);
                        Node out;
                        for (int left = 0; left <= wy; ++left) {
                            const double q = p[left];
                            if (q == 0.0) continue;
                            if (left == 0) {
                                add(out, won, q);
                                continue;
                            }
                            const std::size_t to = xa ? static_cast<std::size_t>(wa) * wb_stride + static_cast<std::size_t>(left)
                                                      : static_cast<std::size_t>(left) * wb_stride + static_cast<std::size_t>(wb);
                            add(out, advanced ? end_at(to, pos) : charge[to * positions_ + pos], q);
                        }
                        out_data[cell * positions_ + pos] = out;
                    }
                }
            }
        }
    }
    const auto& d6 = analysis::d6_pmf();
    for (int wa = 0; wa <= wa_max; ++wa) {
        for (int wb = 0; wb <= wb_max; ++wb) {
            if (wa == 0 || wb == 0) {
                const Node t = wa == 0 ? win(core::Side::B, h, wa, wb) : win(core::Side::A, h, wa, wb);
                for (std::size_t p = 0; p < positions_; ++p) {
                    start[(static_cast<std::size_t>(wa) * static_cast<std::size_t>(wb_max + 1) +
                           static_cast<std::size_t>(wb)) *
                              positions_ +
                          p] = t;
                }
                continue;
            }
            const int wx = xa ? wa : wb;
            const int wy = xa ? wb : wa;
            // Engaged: stay (close-quarters shooting, then fight first) or Fall Back (09.07).
            const double* cq = pmf(x, kCloseQuarters, 1, wx, wy);
            for (int ra = 0; ra <= line; ++ra) {
                Node stay;
                for (int left = 0; left <= wy; ++left) {
                    const double q = cq[left];
                    if (q == 0.0) continue;
                    if (left == 0) {
                        add(stay, win(x, h, wa, wb), q);
                        continue;
                    }
                    add(stay, fight[eidx(xa ? wa : left, xa ? left : wb, ra)], q);
                }
                Node best = stay;
                const int room = xa ? ra : line - ra;
                const int d = std::min(dm, room);
                if (d >= 1) {
                    const Node back = end_turn(h, wa, wb, d, xa ? ra - d : ra);
                    if (better_for(x, back, best)) best = back;
                }
                start[idx(wa, wb, 0, ra)] = best;
            }
            // Unengaged: a Normal move to any band in reach, or an Advance.
            for (int g = 1; g <= line; ++g) {
                for (int ra = 0; ra <= line - g; ++ra) {
                    const int room = xa ? ra : line - g - ra;
                    const auto at = [&](std::size_t kind, int d) {  // d > 0: towards the enemy
                        return shoot[kind][idx(wa, wb, g - d, xa ? ra + d : ra)];
                    };
                    Node best = at(kHeavy, 0);
                    const int towards = std::min(dm, g - 1);
                    const int back = std::min(dm, room);
                    for (int step = 1; step <= std::max(towards, back); ++step) {
                        const std::size_t kind =
                            static_cast<float>(step) * Evaluator::bucket_mm() <= kHeavyMoveMm + core::EPS_MM
                                ? static_cast<std::size_t>(kHeavy)
                                : static_cast<std::size_t>(kMoved);
                        if (step <= towards) {
                            const Node v = at(kind, step);
                            if (better_for(x, v, best)) best = v;
                        }
                        if (step <= back) {
                            const Node v = at(kind, -step);
                            if (better_for(x, v, best)) best = v;
                        }
                    }
                    if (dm > 0) {
                        // 09.06: the band it likes best for each D6 result (the reach only grows with the roll).
                        Node advance;
                        Node best_so_far = at(kAssault, 0);
                        int reach = 0;
                        for (std::size_t f = 1; f <= 6; ++f) {
                            const int r = sx.advance_buckets[f];
                            for (int step = reach + 1; step <= r; ++step) {
                                if (step <= g - 1) {
                                    const Node v = at(kAssault, step);
                                    if (better_for(x, v, best_so_far)) best_so_far = v;
                                }
                                if (step <= room) {
                                    const Node v = at(kAssault, -step);
                                    if (better_for(x, v, best_so_far)) best_so_far = v;
                                }
                            }
                            reach = std::max(reach, r);
                            add(advance, best_so_far, d6[f]);
                        }
                        if (better_for(x, advance, best)) best = advance;
                    }
                    start[idx(wa, wb, g, ra)] = best;
                }
            }
        }
    }
}

// ---- duels and the evaluator ----

std::optional<Duel> duel_of(const core::GameState& state) {
    Duel d;
    std::array<int, 2> count{};
    for (const core::Unit& u : state.units) {
        if (u.models.empty()) continue;
        ++count[si(u.side)];
        d.units[si(u.side)] = u.id;
    }
    if (count[0] != 1 || count[1] != 1) return std::nullopt;
    return d;
}

namespace {

std::uint64_t hash_model(const core::Model& m) {
    core::Model copy = m;
    copy.id = core::ModelId{0};
    copy.position = core::Position{};
    copy.wounds_remaining = 0;
    copy.type_name.clear();
    core::Json j = copy;
    const std::string text = j.dump();
    Fnv1a h;
    h.bytes(text.data(), text.size());
    return h.value();
}

int half_turn_of(const core::GameState& s) {
    return std::max(0, (s.flow.battle_round - 1) * 2 + s.flow.turn_in_round);
}

}  // namespace

Evaluator::Evaluator(analysis::OddsCache& odds, const core::GameState& root, core::Side side, ChainWeights weights,
                     ChainCache* cache)
    : odds_(odds), side_(side), weights_(weights) {
    const std::optional<Duel> d = duel_of(root);
    if (!d) return;
    duel_ = *d;
    const core::Unit* ua = root.find_unit(duel_.units[0]);
    const core::Unit* ub = root.find_unit(duel_.units[1]);
    if (ua == nullptr || ub == nullptr || root.battle_rounds <= 0) return;
    Spec spec;
    spec.half_turns = 2 * root.battle_rounds;
    spec.first = root.first_turn;
    spec.h0 = std::min(half_turn_of(root), spec.half_turns - 1);
    spec.weights = weights;
    const float gap = core::unit_distance(*ua, *ub).value_or(0.0f);
    const float ra = room_away(root, *ua, centroid_of(*ub));
    const float rb = room_away(root, *ub, centroid_of(*ua));
    spec.line = std::clamp(static_cast<int>(std::lround((gap + ra + rb) / kBucketMm)), kMinLine, kMaxLine);
    Fnv1a key;
    for (const core::Unit* u : {ua, ub}) {
        SideSpec& s = spec.sides[si(u->side)];
        const core::Model& rep = representative(*u);
        s.wounds_per_model = std::max(1, rep.characteristics.wounds);
        s.max_wounds = std::max(1, wounds_of(*u));
        s.max_models = (s.max_wounds + s.wounds_per_model - 1) / s.wounds_per_model;
        // The prototype (and so each model's profile choice against it) has the unit's starting strength, not what
        // is left now, so a state's value is the same whichever table answers it.
        const int strength = std::max(u->starting_strength, static_cast<int>(u->models.size()));
        s.wounds_start = std::max(1, strength * s.wounds_per_model);
        s.proto = prototype(*u, std::max(strength, s.max_models));
        const analysis::ThreatRanges t = analysis::threat_ranges(s.proto);
        s.move_buckets = static_cast<int>(std::floor((t.move_mm + core::EPS_MM) / kBucketMm));
        for (std::size_t f = 1; f <= 6; ++f) {
            s.advance_buckets[f] = static_cast<int>(std::floor((t.advance_mm[f] + core::EPS_MM) / kBucketMm));
        }
        s.ranges = usable_ranges(rep);
        key.u64(hash_model(rep));
        for (const std::string& k : u->keywords) key.bytes(k.data(), k.size());
        key.i32(s.wounds_start);
        key.i32(static_cast<int>(s.proto.models.size()));
    }
    key.i32(spec.half_turns);
    key.i32(spec.first == core::Side::A ? 0 : 1);
    key.i32(spec.line);
    key.u64(static_cast<std::uint64_t>(std::llround(weights.tempo * 1e9)));
    key.u64(static_cast<std::uint64_t>(std::llround(weights.material * 1e9)));
    spec.key = key.value();
    if (cache != nullptr) {
        for (auto it = cache->tables_.begin(); it != cache->tables_.end(); ++it) {
            if ((*it)->covers(spec)) {
                table_ = *it;
                // Most recently used last.
                std::rotate(it, it + 1, cache->tables_.end());
                return;
            }
        }
    }
    table_ = std::make_shared<const ChainTable>(odds_, std::move(spec), kBucketMm);
    if (cache != nullptr) {
        ++cache->builds_;
        cache->tables_.push_back(table_);
        if (cache->tables_.size() > kMaxTables) cache->tables_.erase(cache->tables_.begin());
    }
}

Evaluator::~Evaluator() = default;

int Evaluator::line_buckets() const noexcept { return table_ ? table_->line() : 0; }

UnitView Evaluator::view(const core::GameState& state, core::Side of) const {
    UnitView v;
    const core::Unit* u = state.find_unit(duel_.units[si(of)]);
    const core::Unit* e = state.find_unit(duel_.units[si(core::opponent(of))]);
    if (u == nullptr) return v;
    v.wounds = wounds_of(*u);
    if (e != nullptr && !e->models.empty()) v.room_mm = room_away(state, *u, centroid_of(*e));
    return v;
}

namespace {

// A real position on the table's line: gap and side A's room in buckets (continuous). Engaged: gap 0.
struct LinePos {
    bool engaged = false;
    double g = 1.0;
    double ra = 0.0;
};

LinePos line_pos(int line, float bucket, bool engaged, float gap_mm, float room_a, float room_b) {
    LinePos p;
    p.engaged = engaged;
    const double total = static_cast<double>(line) * bucket;
    const double rooms = static_cast<double>(room_a) + static_cast<double>(room_b);
    if (engaged) {
        p.g = 0.0;
        p.ra = (rooms > 0.0 ? total * room_a / rooms : total / 2.0) / bucket;
        return p;
    }
    const double gap = std::clamp(static_cast<double>(gap_mm), static_cast<double>(bucket), total);
    const double rest = total - gap;
    p.g = gap / bucket;
    p.ra = (rooms > 0.0 ? rest * room_a / rooms : rest / 2.0) / bucket;
    return p;
}

// Bilinear interpolation over the grid (g, rA) with the triangle rA <= L - g; `at(g, ra)` gives a grid node.
template <typename At>
Node interpolate(int line, const LinePos& p, At at) {
    const auto along_ra = [&](int g, double ra) {
        const double hi = static_cast<double>(g == 0 ? line : line - g);
        const double r = std::clamp(ra, 0.0, hi);
        const int r0 = static_cast<int>(std::floor(r));
        const double u = r - r0;
        if (u <= 1e-9 || r0 >= static_cast<int>(hi)) return at(g, std::min(r0, static_cast<int>(hi)));
        return mix(at(g, r0 + 1), at(g, r0), u);
    };
    if (p.engaged) return along_ra(0, p.ra);
    const double g = std::clamp(p.g, 1.0, static_cast<double>(line));
    const int g0 = static_cast<int>(std::floor(g));
    const double t = g - g0;
    if (t <= 1e-9 || g0 >= line) return along_ra(std::min(g0, line), p.ra);
    return mix(along_ra(g0 + 1, p.ra), along_ra(g0, p.ra), t);
}

Evaluation to_eval(const Node& n, core::Side side) {
    Evaluation e;
    if (side == core::Side::A) {
        e.p_win = n.pw;
        e.p_loss = n.pl;
        e.score = n.j;
    } else {
        e.p_win = n.pl;
        e.p_loss = n.pw;
        e.score = -n.j;
    }
    e.p_win = std::clamp(e.p_win, 0.0, 1.0);
    e.p_loss = std::clamp(e.p_loss, 0.0, 1.0);
    e.p_draw = std::clamp(1.0 - e.p_win - e.p_loss, 0.0, 1.0);
    e.v = std::clamp(e.p_win - e.p_loss, -1.0, 1.0);
    return e;
}

struct Real {
    int h = 0;
    std::array<int, 2> wounds{};
    bool engaged = false;
    float gap_mm = 0.0f;
    std::array<float, 2> rooms{};
};

}  // namespace

Evaluation Evaluator::at(int half_turn, std::array<int, 2> wounds, float gap_mm, std::array<float, 2> rooms_mm) const {
    if (!table_) return {};
    const ChainTable& t = *table_;
    const LinePos p = line_pos(t.line(), kBucketMm, !(gap_mm > core::kEngagementRangeMm + core::EPS_MM), gap_mm,
                               rooms_mm[0], rooms_mm[1]);
    const int wa = std::clamp(wounds[0], 0, t.side(core::Side::A).max_wounds);
    const int wb = std::clamp(wounds[1], 0, t.side(core::Side::B).max_wounds);
    const int h = std::max(half_turn, t.spec().h0);
    return to_eval(interpolate(t.line(), p, [&](int g, int ra) { return t.start(h, wa, wb, g, ra); }), side_);
}

std::optional<Evaluation> Evaluator::terminal(const core::GameState& state) const {
    // D-007: the game is over, or (in a hypothetical position) a side has no models left: it lost, now.
    std::optional<core::Side> winner;
    bool over = false;
    if (state.result) {
        over = true;
        winner = state.result->winner;
    } else {
        const core::Unit* ua = state.find_unit(duel_.units[0]);
        const core::Unit* ub = state.find_unit(duel_.units[1]);
        const bool a_gone = ua == nullptr || ua->models.empty();
        const bool b_gone = ub == nullptr || ub->models.empty();
        if (a_gone || b_gone) {
            over = true;
            if (a_gone != b_gone) winner = a_gone ? core::Side::B : core::Side::A;
        }
    }
    if (!over) return std::nullopt;
    Evaluation e;
    if (!winner) {
        e.p_draw = 1.0;
    } else if (*winner == side_) {
        e.p_win = 1.0;
        e.v = 1.0;
    } else {
        e.p_loss = 1.0;
        e.v = -1.0;
    }
    const ChainTable& t = *table_;
    const int h = std::min(half_turn_of(state), t.half_turns());
    const int wa = std::clamp(view(state, core::Side::A).wounds, 0, t.side(core::Side::A).max_wounds);
    const int wb = std::clamp(view(state, core::Side::B).wounds, 0, t.side(core::Side::B).max_wounds);
    const Node n = !winner ? t.draw(wa, wb) : t.win(*winner, h, wa, wb);
    e.score = side_ == core::Side::A ? n.j : -n.j;
    return e;
}

Evaluation Evaluator::at_turn_start(const core::GameState& state) const {
    if (!table_) return {};
    if (const std::optional<Evaluation> e = terminal(state)) return *e;
    const core::Unit* ua = state.find_unit(duel_.units[0]);
    const core::Unit* ub = state.find_unit(duel_.units[1]);
    const float gap = core::unit_distance(*ua, *ub).value_or(0.0f);
    return at(half_turn_of(state), {wounds_of(*ua), wounds_of(*ub)}, gap,
              {room_away(state, *ua, centroid_of(*ub)), room_away(state, *ub, centroid_of(*ua))});
}

Evaluation Evaluator::evaluate(const core::GameState& state) const {
    if (!table_) return {};
    if (const std::optional<Evaluation> e = terminal(state)) return *e;
    const ChainTable& t = *table_;
    const core::Unit* ua = state.find_unit(duel_.units[0]);
    const core::Unit* ub = state.find_unit(duel_.units[1]);
    const int h = half_turn_of(state);
    if (h >= t.half_turns()) return to_eval(t.draw(wounds_of(*ua), wounds_of(*ub)), side_);
    const core::Side x = core::turn_side(state);
    const core::Unit& ux = x == core::Side::A ? *ua : *ub;
    const core::Unit& uy = x == core::Side::A ? *ub : *ua;
    const int wa = std::clamp(wounds_of(*ua), 1, t.side(core::Side::A).max_wounds);
    const int wb = std::clamp(wounds_of(*ub), 1, t.side(core::Side::B).max_wounds);
    const float room_a = room_away(state, *ua, centroid_of(*ub));
    const float room_b = room_away(state, *ub, centroid_of(*ua));
    const bool engaged = core::is_engaged(state, ux);
    const float gap = core::unit_distance(*ua, *ub).value_or(0.0f);
    const LinePos here = line_pos(t.line(), kBucketMm, engaged, gap, room_a, room_b);
    const bool xa = x == core::Side::A;

    const auto start_next = [&](int na, int nb, const LinePos& p) {
        return interpolate(t.line(), p, [&](int g, int ra) { return t.end_turn(h, na, nb, g, ra); });
    };
    const auto fight_now = [&](int na, int nb, double ra) {
        return interpolate(t.line(), LinePos{true, 0.0, ra}, [&](int, int r) { return t.fight(h, na, nb, r); });
    };
    const auto reply_now = [&](int na, int nb, double ra) {
        return interpolate(t.line(), LinePos{true, 0.0, ra}, [&](int, int r) { return t.reply(h, na, nb, r); });
    };
    // After the shooting of the side whose turn it is, at a real gap: its charge (P from the 2D6 roll covering the
    // gap, 11.02), if it wants one, else the turn ends there.
    const auto after_shoot = [&](int na, int nb, float gap_mm, bool may_charge) {
        const LinePos p = line_pos(t.line(), kBucketMm, false, gap_mm, room_a, room_b);
        const Node hold = start_next(na, nb, p);
        const double pc = may_charge ? analysis::p_charge_roll_covers(gap_mm) : 0.0;
        if (!(pc > 0.0)) return hold;
        const Node charged = mix(fight_now(na, nb, xa ? std::min<double>(t.line(), p.ra + p.g) : p.ra), hold, pc);
        return t.better_for(x, charged, hold) ? charged : hold;
    };
    // The shooting of the side whose turn it is, from the engine (best_shooting), then the charge or the fight.
    const auto before_shoot = [&](bool may_charge) {
        const std::optional<analysis::ShootingPlan> plan = analysis::best_shooting(odds_, state, ux.id, uy.id);
        const int wy = xa ? wb : wa;
        std::vector<double> pmf(static_cast<std::size_t>(wy) + 1, 0.0);
        if (plan && plan->outcome.wounds_left_pmf.size() == pmf.size()) {
            pmf = plan->outcome.wounds_left_pmf;
        } else {
            pmf.back() = 1.0;
        }
        const int wpm = std::max(1, t.side(core::opponent(x)).wounds_per_model);
        const int models = static_cast<int>(uy.models.size());
        Node out;
        for (int left = 0; left <= wy; ++left) {
            const double q = pmf[static_cast<std::size_t>(left)];
            if (q == 0.0) continue;
            if (left == 0) {
                add(out, t.win(x, h, wa, wb), q);
                continue;
            }
            const int na = xa ? wa : left;
            const int nb = xa ? left : wb;
            if (engaged) {
                add(out, fight_now(na, nb, here.ra), q);
                continue;
            }
            // The defender removes its front models (the worst case for a charge, M40's distance_after_casualties).
            const int destroyed = std::clamp(models - (left + wpm - 1) / wpm, 0, std::max(0, models - 1));
            const float gap_k = analysis::kth_closest_model_distance(ux, uy, destroyed + 1).value_or(gap);
            add(out, after_shoot(na, nb, gap_k, may_charge), q);
        }
        return out;
    };

    const core::Stage st = state.flow.stage;
    const core::Phase ph = core::phase_of(st);
    const core::UnitTurnFlags& tf = ux.turn_flags;
    const core::UnitPhaseFlags& pf = ux.phase_flags;
    const bool may_charge = !tf.advanced && !tf.fell_back;
    Node n;
    if (ph == core::Phase::None || ph == core::Phase::Command ||
        (ph == core::Phase::Movement && !pf.selected_to_move && !state.flow.move)) {
        if (st == core::Stage::EndOfTurn || st == core::Stage::EndOfBattleRound) {
            n = start_next(wa, wb, here);
        } else {
            n = interpolate(t.line(), here, [&](int g, int ra) { return t.start(h, wa, wb, g, ra); });
        }
    } else if (ph == core::Phase::Movement || (ph == core::Phase::Shooting && !pf.selected_to_shoot &&
                                               !state.flow.attack && st != core::Stage::EndOfShootingPhase)) {
        if (tf.fell_back || (state.flow.move && state.flow.move->type == core::MoveType::FallBack)) {
            n = start_next(wa, wb, here);  // 09.07: no shooting, no charge
        } else {
            const bool advancing = tf.advanced || (state.flow.move && state.flow.move->type == core::MoveType::Advance);
            n = before_shoot(may_charge && !advancing);
        }
    } else if (ph == core::Phase::Shooting) {
        n = engaged ? fight_now(wa, wb, here.ra) : after_shoot(wa, wb, gap, may_charge);
    } else if (ph == core::Phase::Charge) {
        if (tf.made_charge_move || (engaged && st == core::Stage::EndOfChargePhase)) {
            n = fight_now(wa, wb, here.ra);
        } else if (engaged) {
            n = fight_now(wa, wb, here.ra);
        } else if (pf.declared_charge || st == core::Stage::EndOfChargePhase) {
            n = start_next(wa, wb, here);  // the charge failed or was not made
        } else {
            n = after_shoot(wa, wb, gap, may_charge);
        }
    } else {  // Fight phase
        if (engaged && !pf.selected_to_fight && st != core::Stage::EndOfFightPhase) {
            n = fight_now(wa, wb, here.ra);
        } else if (engaged && !uy.phase_flags.selected_to_fight && st != core::Stage::EndOfFightPhase) {
            n = reply_now(wa, wb, here.ra);
        } else {
            n = start_next(wa, wb, here);
        }
    }
    return to_eval(n, side_);
}

Evaluator::TurnChoice Evaluator::turn_choice(const core::GameState& state) const {
    TurnChoice out;
    if (!table_ || state.result) return out;
    const ChainTable& t = *table_;
    const core::Unit* ua = state.find_unit(duel_.units[0]);
    const core::Unit* ub = state.find_unit(duel_.units[1]);
    if (ua == nullptr || ub == nullptr || ua->models.empty() || ub->models.empty()) return out;
    const int h = half_turn_of(state);
    if (h >= t.half_turns()) return out;
    const core::Side x = core::turn_side(state);
    const bool xa = x == core::Side::A;
    const int wa = std::clamp(wounds_of(*ua), 1, t.side(core::Side::A).max_wounds);
    const int wb = std::clamp(wounds_of(*ub), 1, t.side(core::Side::B).max_wounds);
    const core::Unit& ux = xa ? *ua : *ub;
    const bool engaged = core::is_engaged(state, ux);
    const float room_a = room_away(state, *ua, centroid_of(*ub));
    const float room_b = room_away(state, *ub, centroid_of(*ua));
    const float gap = core::unit_distance(*ua, *ub).value_or(0.0f);
    const LinePos here = line_pos(t.line(), kBucketMm, engaged, gap, room_a, room_b);
    const SideSpec& sx = t.side(x);
    const double room = xa ? here.ra : (engaged ? t.line() - here.ra : t.line() - here.g - here.ra);
    if (engaged) {
        // Stay or Fall Back, as the chain's start node does.
        const auto stay = [&] {
            const double* cq = t.pmf(x, kCloseQuarters, 1, xa ? wa : wb, xa ? wb : wa);
            const int wy = xa ? wb : wa;
            Node s;
            for (int left = 0; left <= wy; ++left) {
                if (cq[left] == 0.0) continue;
                if (left == 0) {
                    add(s, t.win(x, h, wa, wb), cq[left]);
                    continue;
                }
                add(s, interpolate(t.line(), here,
                                   [&](int, int r) { return t.fight(h, xa ? wa : left, xa ? left : wb, r); }),
                    cq[left]);
            }
            return s;
        }();
        const double d = std::min<double>(sx.move_buckets, std::floor(room));
        if (d >= 1.0) {
            const LinePos p{false, d, xa ? here.ra - d : here.ra};
            const Node back = interpolate(t.line(), p, [&](int g, int ra) { return t.end_turn(h, wa, wb, g, ra); });
            out.fall_back = t.better_for(x, back, stay);
        }
        return out;
    }
    const auto value_at = [&](Kind kind, double d) {  // d > 0: towards
        const LinePos p{false, here.g - d, xa ? here.ra + d : here.ra};
        return interpolate(t.line(), p, [&](int g, int ra) { return t.shoot_then(h, kind, wa, wb, g, ra); });
    };
    // How far it can go towards the enemy (it must end unengaged, 09.05) and back (its room), in buckets.
    const double towards_limit = std::max(0.0, here.g - 1.0);
    const double back_limit = std::max(0.0, room);
    const auto kind_for = [](double d) {
        return static_cast<float>(std::fabs(d)) * kBucketMm <= kHeavyMoveMm + core::EPS_MM ? kHeavy : kMoved;
    };
    Node best = value_at(kHeavy, 0.0);
    double best_d = 0.0;
    for (int step = 1; step <= sx.move_buckets; ++step) {
        for (const double d : {std::min<double>(step, towards_limit), -std::min<double>(step, back_limit)}) {
            if (std::fabs(d) < 1e-9) continue;
            const Node v = value_at(kind_for(d), d);
            if (t.better_for(x, v, best)) {
                best = v;
                best_d = d;
            }
        }
    }
    if (sx.move_buckets > 0) {
        Node advance;
        std::array<double, 7> best_by{};
        for (std::size_t f = 1; f <= 6; ++f) {
            Node bf = value_at(kAssault, 0.0);
            double bd = 0.0;
            for (int step = 1; step <= sx.advance_buckets[f]; ++step) {
                for (const double d : {std::min<double>(step, towards_limit), -std::min<double>(step, back_limit)}) {
                    if (std::fabs(d) < 1e-9) continue;
                    const Node v = value_at(kAssault, d);
                    if (t.better_for(x, v, bf)) {
                        bf = v;
                        bd = d;
                    }
                }
            }
            best_by[f] = bd;
            add(advance, bf, analysis::d6_pmf()[f]);
        }
        if (t.better_for(x, advance, best)) {
            out.advance = true;
            for (std::size_t f = 1; f <= 6; ++f) out.advance_towards_mm[f] = static_cast<float>(best_by[f]) * kBucketMm;
            return out;
        }
    }
    out.towards_mm = static_cast<float>(best_d) * kBucketMm;
    return out;
}

Evaluator::ChargeValues Evaluator::charge_values(const core::GameState& state, double p_success) const {
    ChargeValues out;
    if (!table_ || state.result) return out;
    const ChainTable& t = *table_;
    const core::Unit* ua = state.find_unit(duel_.units[0]);
    const core::Unit* ub = state.find_unit(duel_.units[1]);
    if (ua == nullptr || ub == nullptr || ua->models.empty() || ub->models.empty()) return out;
    const int h = half_turn_of(state);
    if (h >= t.half_turns()) return out;
    const core::Side x = core::turn_side(state);
    const bool xa = x == core::Side::A;
    const int wa = std::clamp(wounds_of(*ua), 1, t.side(core::Side::A).max_wounds);
    const int wb = std::clamp(wounds_of(*ub), 1, t.side(core::Side::B).max_wounds);
    const float room_a = room_away(state, *ua, centroid_of(*ub));
    const float room_b = room_away(state, *ub, centroid_of(*ua));
    const float gap = core::unit_distance(*ua, *ub).value_or(0.0f);
    const LinePos here = line_pos(t.line(), kBucketMm, false, gap, room_a, room_b);
    const Node hold = interpolate(t.line(), here, [&](int g, int ra) { return t.end_turn(h, wa, wb, g, ra); });
    const double ra_after = xa ? std::min<double>(t.line(), here.ra + here.g) : here.ra;
    const Node fight = interpolate(t.line(), LinePos{true, 0.0, ra_after}, [&](int, int r) { return t.fight(h, wa, wb, r); });
    const double p = std::clamp(p_success, 0.0, 1.0);
    const Node charged = mix(fight, hold, p);
    out.hold = to_eval(hold, side_);
    out.fight = to_eval(fight, side_);
    out.charge = to_eval(charged, side_);
    out.wanted = p > 0.0 && t.better_for(x, charged, hold);
    return out;
}

}  // namespace wh40k::ai::hard
