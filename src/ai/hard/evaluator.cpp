#include "hard/evaluator.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include "analysis/charge_odds.hpp"
#include "analysis/threat.hpp"
#include "bounds.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/charge.hpp"
#include "core/serialization/json.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"
#include "directions.hpp"
#include "normal/value.hpp"
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
// D-067 (M57): 38 buckets (76"), so a line as long as a 60"x44" battlefield's diagonal (74.4") fits. Before M57: 24
// (48"); no line on the 24"x24" boards (at most their 34" diagonal, 17 buckets) ever reached either.
constexpr int kMaxLine = 38;
// D-078 (M69): a side of a table whose unit type starts with more wounds than this gets the coarse wounds grid (at most
// 21 levels); every unit of V1, the bot scenarios and Strike starts with 20 or fewer.
constexpr int kExactWounds = 20;
// D-078: the army's tables are stored as floats when, at full precision, the game's pairs would take more than this
// (Strike's one table: 23 MB; the rosters' 30: about 1.45 GB).
constexpr std::size_t kCompactAboveBytes = std::size_t{256} << 20;
// D-078: the cap on a cache's stored bytes.
constexpr std::size_t kCacheCapBytes = std::size_t{640} << 20;
// D-078: at most this many threads build missing tables.
constexpr std::size_t kMaxWorkers = 4;

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
            // D-074 part 3: the edge by the base's half extents at its facing (round: its radius, as before).
            const core::BaseExtents ext = edge_extents(m.base, m.facing_deg);
            const float rx = ext.half_x_mm;
            const float ry = ext.half_y_mm;
            const core::Position& p = m.position;
            if (d.x > 1e-6f) t = std::min(t, (w - rx - p.x) / d.x);
            if (d.x < -1e-6f) t = std::min(t, (rx - p.x) / d.x);
            if (d.y > 1e-6f) t = std::min(t, (h - ry - p.y) / d.y);
            if (d.y < -1e-6f) t = std::min(t, (ry - p.y) / d.y);
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

// D-078: a unit whose models differ in loadout but share one W: the chain counts its loadout groups (each model's own
// weapons) instead of copies of its most common model. Every unit of V1, the bot scenarios and Strike has one loadout.
bool mixed_loadouts(const core::Unit& u) {
    if (u.models.empty()) return false;
    bool differ = false;
    for (const core::Model& m : u.models) {
        if (m.characteristics.wounds != u.models.front().characteristics.wounds) return false;  // one W, or the old way
        differ = differ || !same_loadout(m, u.models.front());
    }
    return differ;
}

// D-078: a mixed unit at full strength for the chain: its models (in unit order), then copies of its most common
// loadout up to `n` (the models lost so far taken as rank and file), unwounded, ids 1..n. Not yet in keep order.
core::Unit mixed_prototype(const core::Unit& u, int n) {
    core::Unit p = u;
    p.models.clear();
    const core::Model rep = representative(u);
    for (int i = 0; i < n; ++i) {
        core::Model m = i < static_cast<int>(u.models.size()) ? u.models[static_cast<std::size_t>(i)] : rep;
        m.id = core::ModelId{static_cast<std::uint32_t>(i + 1)};
        m.wounds_remaining = m.characteristics.wounds;
        m.position = core::Position{};
        m.facing_deg = 0.0f;
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

// D-078: every model's usable ranges (a mixed unit's bands come from all its loadouts).
std::vector<float> usable_ranges(const core::Unit& u) {
    std::vector<float> out;
    for (const core::Model& m : u.models) {
        const std::vector<float> r = usable_ranges(m);
        out.insert(out.end(), r.begin(), r.end());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool has_hazardous(const core::Unit& u) {
    for (const core::Model& m : u.models) {
        for (const core::Weapon& w : m.weapons) {
            for (const core::WeaponProfile& p : w.profiles) {
                if (p.has<core::ability::Hazardous>()) return true;
            }
        }
    }
    return false;
}

// D-078: one side's wounds grid from its unit type's starting wounds: every wound (exact), or above kExactWounds a level
// every ceil(W / 20) wounds and at W itself. [0] is 0. `top`: the region's highest wounds (the grid up to the first
// level at or above it).
std::vector<int> grid_levels(int wounds_start, int top) {
    std::vector<int> out;
    if (wounds_start <= kExactWounds) {
        for (int w = 0; w <= top; ++w) out.push_back(w);
        return out;
    }
    const int step = (wounds_start + kExactWounds - 1) / kExactWounds;
    for (int w = 0; w < wounds_start; w += step) {
        out.push_back(w);
        if (w >= top) return out;
    }
    out.push_back(wounds_start);
    return out;
}

// A non-negative double's FNV-1a key part (as the weights are keyed).
std::uint64_t rate_key(double r) { return static_cast<std::uint64_t>(std::llround(r * 1e9)); }

}  // namespace

// ---- the static description of a duel (the table's key) ----

struct SideSpec {
    core::Unit proto;          // max_models models: copies of the representative, or (D-078) the unit's loadout groups
    int wounds_per_model = 1;  // W
    int wounds_start = 1;      // starting strength x W (material is a share of it)
    int max_wounds = 0;        // the region: wounds left at the root (D-078: the grid level at or above them)
    int max_models = 0;
    int move_buckets = 0;                // M
    std::array<int, 7> advance_buckets{};  // [f]: M + f (09.06)
    std::vector<float> ranges;           // usable ranged profiles' ranges (the bands)
    // D-078 (M69):
    std::vector<int> levels;   // the wounds grid of the region ([0] = 0, back() = max_wounds)
    bool exact = true;         // levels[i] == i
    bool mixed = false;        // proto holds the unit's loadout groups, in keep order once finalized
    double hazard_rate = 0.0;  // its [HAZARDOUS] profiles priced at this rate against the other side (D-077)
};

struct Spec {
    std::array<SideSpec, 2> sides;
    int half_turns = 10;
    core::Side first = core::Side::A;
    int h0 = 0;
    int line = 10;  // buckets
    ChainWeights weights;
    std::uint64_t key = 0;  // everything but the region (h0, max wounds)
    bool compact = false;   // D-078: stored as floats
};

namespace {

float bucket_gap_mm(int g, float bucket) {
    return std::max(static_cast<float>(g) * bucket, kJustOutsideMm);
}

int level_count(const SideSpec& s) { return static_cast<int>(s.levels.size()); }

}  // namespace

// ---- the table ----

// D-078 (M69): the nodes are stored by half-turn, then by line position, then by wounds cell (both sides' grid levels
// above 0; a side at 0 wounds is a decided game and is never stored), three values a node: as doubles (exact, every
// table of V1, the bot scenarios and Strike) or, for an army whose tables would be too big, as floats (compact). The
// arithmetic is in double either way, and on the exact form it is the M68 arithmetic: the same terms in the same order.
class ChainTable {
public:
    explicit ChainTable(analysis::OddsCache& odds, Spec spec, float bucket);

    [[nodiscard]] const Spec& spec() const noexcept { return spec_; }
    [[nodiscard]] bool covers(const Spec& s) const noexcept {
        return s.key == spec_.key && s.compact == spec_.compact && s.h0 >= spec_.h0 &&
               s.sides[0].max_wounds <= spec_.sides[0].max_wounds && s.sides[1].max_wounds <= spec_.sides[1].max_wounds;
    }
    [[nodiscard]] int line() const noexcept { return spec_.line; }
    [[nodiscard]] int half_turns() const noexcept { return spec_.half_turns; }
    [[nodiscard]] core::Side side_at(int h) const noexcept {
        return h % 2 == 0 ? spec_.first : core::opponent(spec_.first);
    }

    // Lookups (side A's view) at any wounds: an exact side's are grid points, a coarse side's (D-078) interpolated
    // between the levels around them. Positions: engaged (g = 0) rA in [0, L]; unengaged g in [1, L], rA in [0, L - g].
    [[nodiscard]] Node start(int h, int wa, int wb, int g, int ra) const;
    [[nodiscard]] Node end_turn(int h, int wa, int wb, int g, int ra) const { return start(h + 1, wa, wb, g, ra); }
    [[nodiscard]] Node fight(int h, int wa, int wb, int ra) const;  // engaged, the side whose turn it is first
    [[nodiscard]] Node reply(int h, int wa, int wb, int ra) const;  // engaged, the other side strikes back
    [[nodiscard]] Node win(core::Side winner, int h, int wa, int wb) const;
    [[nodiscard]] Node draw(int wa, int wb) const;

    // The pieces of a half-turn, on the grid (computed from the stored nodes; wounds at grid levels).
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
    // [l] = P(the target is left at grid level l) when `attacker`'s models alive with `attacker_wounds` attack a target
    // at the level with `target_wounds` (a level's wounds; on an exact side any wounds, levels being wounds).
    [[nodiscard]] const double* pmf(core::Side attacker, Kind kind, int g, int attacker_wounds,
                                    int target_wounds) const;
    // D-078: grid levels. The level of `wounds` (a level's wounds), the wounds of `level`, and the level nearest to
    // `wounds` (a tie to the lower): an exact side's are the wounds themselves.
    [[nodiscard]] int level_of(core::Side s, int wounds) const;
    [[nodiscard]] int level_wounds(core::Side s, int level) const {
        return side(s).levels[static_cast<std::size_t>(level)];
    }
    [[nodiscard]] int snap(core::Side s, int wounds) const;
    // D-067: the wounds one volley of `attacker` (`attacker_wounds` left) takes from a whole unit of the other side,
    // the better of its shooting at close range after a move and its fight: its pace in each of its turns once a
    // target is in reach (the army's attention timeline).
    [[nodiscard]] double volley_wounds(core::Side attacker, int attacker_wounds) const {
        const core::Side target = core::opponent(attacker);
        const int full = std::max(1, side(target).max_wounds);
        const int levels = level_of(target, full);
        const auto removed = [&](Kind kind) {
            const double* p = pmf(attacker, kind, 1, attacker_wounds, full);
            double left = 0.0;
            for (int l = 0; l <= levels; ++l) left += p[l] * static_cast<double>(level_wounds(target, l));
            return static_cast<double>(full) - left;
        };
        return std::max(removed(kMoved), removed(kMelee));
    }

    // D-078: memory, trimming, tests.
    [[nodiscard]] std::size_t bytes() const noexcept;
    [[nodiscard]] static std::size_t estimate_bytes(const Spec& s) noexcept;
    void trim_before(int h);
    [[nodiscard]] std::uint64_t fingerprint(int from) const;
    [[nodiscard]] ChainTableInfo info() const;
    [[nodiscard]] static ChainTableInfo info_of(const Spec& s);

private:
    struct DistSet {
        int max_n = 0;
        int max_t = 0;  // D-078: target levels above 0
        std::vector<std::size_t> offset;  // (n - 1) * max_t + (t - 1)
        std::vector<double> data;         // over target levels 0..t
    };
    // A half-turn's stored nodes, three values each (one of the two vectors is used).
    struct Stored {
        std::vector<double> d;
        std::vector<float> f;
    };
    template <typename T>
    static std::vector<T>& values(Stored& s) {
        if constexpr (std::is_same_v<T, double>) {
            return s.d;
        } else {
            return s.f;
        }
    }
    template <typename T>
    static const std::vector<T>& values(const Stored& s) {
        if constexpr (std::is_same_v<T, double>) {
            return s.d;
        } else {
            return s.f;
        }
    }
    [[nodiscard]] Node node(const Stored& s, std::size_t i) const {
        if (spec_.compact) return Node{s.f[3 * i], s.f[3 * i + 1], s.f[3 * i + 2]};
        return Node{s.d[3 * i], s.d[3 * i + 1], s.d[3 * i + 2]};
    }
    [[nodiscard]] std::size_t pos_index(int g, int ra) const {
        if (g == 0) return static_cast<std::size_t>(ra);
        return static_cast<std::size_t>(spec_.line + 1) + row_offset_[static_cast<std::size_t>(g)] +
               static_cast<std::size_t>(ra);
    }
    // A wounds cell: both sides' level indices, each at least 1.
    [[nodiscard]] std::size_t cell(int ia, int ib) const {
        return static_cast<std::size_t>(ia - 1) * nb_ + static_cast<std::size_t>(ib - 1);
    }
    // A position and level indices (a side at level 0 has won or lost): the node.
    [[nodiscard]] Node start_at(int h, int ia, int ib, int g, int ra) const;
    [[nodiscard]] Node engaged_at(const std::vector<Stored>& rows, int h, int ia, int ib, int ra) const;
    // D-078: a lookup at any wounds from one at level indices: exact sides directly, coarse ones interpolated.
    template <class F>
    [[nodiscard]] Node at_wounds(int wa, int wb, F at_levels) const;
    [[nodiscard]] const double* pmf_level(core::Side attacker, Kind kind, int g, int attacker_wounds,
                                          int target_level) const;
    void build_distributions(analysis::OddsCache& odds, float bucket);
    template <typename T>
    void build_half_turn(int h);

    Spec spec_;
    std::size_t positions_ = 0;
    std::size_t nb_ = 0;    // side B's levels above 0 (the cell stride)
    std::size_t grid_ = 0;  // cells: side A's levels above 0 x nb_
    std::vector<std::size_t> row_offset_;  // [g]: positions before row g among the unengaged ones
    std::vector<double> p_charge_;         // [g]
    std::array<std::vector<int>, 2> band_of_;  // [side][g]: the shooting band (0..)
    // [side][kind][band] -> an index into pool_ (attacks with the same volley share one set).
    std::array<std::array<std::vector<std::size_t>, kKinds>, 2> dist_index_;
    std::vector<DistSet> pool_;
    std::vector<Stored> start_;  // [h - h0]
    std::vector<Stored> fight_;
    std::vector<Stored> reply_;
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
    nb_ = static_cast<std::size_t>(std::max(0, level_count(spec_.sides[1]) - 1));
    grid_ = static_cast<std::size_t>(std::max(0, level_count(spec_.sides[0]) - 1)) * nb_;
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
    for (int h = spec_.half_turns - 1; h >= h0; --h) {
        if (spec_.compact) {
            build_half_turn<float>(h);
        } else {
            build_half_turn<double>(h);
        }
    }
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
        const core::Unit& target = sy.proto;
        // The attackers: one copy of the representative (its volley times n models), or (D-078) for a unit of loadout
        // groups the first n of its models, each n its own volley (each model's own picks, identical attacks merged).
        const int max_n = std::max(1, sx.max_models);
        std::vector<core::Unit> attackers;
        if (sx.mixed) {
            for (int n = 1; n <= max_n; ++n) {
                core::Unit a = sx.proto;
                a.models.resize(static_cast<std::size_t>(std::min<int>(n, static_cast<int>(sx.proto.models.size()))));
                attackers.push_back(std::move(a));
            }
        } else {
            attackers.push_back(prototype(sx.proto, 1));
        }
        const auto volleys_for = [&](const core::Unit& one, Kind kind, float gap) {
            analysis::ShotContext ctx;
            switch (kind) {
                case kHeavy:
                    ctx = analysis::ShotContext{core::ShootingType::Normal, true, gap};
                    break;
                case kMoved:
                    ctx = analysis::ShotContext{core::ShootingType::Normal, false, gap};
                    break;
                case kAssault:
                    ctx = analysis::ShotContext{core::ShootingType::Assault, false, gap};
                    break;
                case kCloseQuarters:
                    ctx = analysis::ShotContext{core::ShootingType::CloseQuarters, false, 0.0f};
                    break;
                case kMelee:
                case kKinds:
                    return analysis::unit_melee_volley(odds, one, target, sx.hazard_rate);
            }
            ctx.hazard_rate = sx.hazard_rate;  // D-078: [HAZARDOUS] priced as Normal prices it (0: as before)
            return analysis::unit_ranged_volley(odds, one, target, ctx);
        };
        // The target at each grid level t (its wounds, the wounded model first, 05.04).
        const int wpm = std::max(1, sy.wounds_per_model);
        const int max_t = std::max(1, level_count(sy) - 1);
        std::vector<analysis::TargetSnapshot> snaps;
        snaps.reserve(static_cast<std::size_t>(max_t));
        for (int t = 1; t <= max_t; ++t) {
            const int w = sy.levels[static_cast<std::size_t>(t)];
            analysis::TargetSnapshot snap = analysis::snapshot_with_models(sy.proto, (w + wpm - 1) / wpm);
            if (w % wpm != 0 && !snap.slots.empty()) snap.slots.front().wounds_left = w % wpm;
            snaps.push_back(std::move(snap));
        }
        std::vector<std::pair<std::vector<analysis::Volley>, std::size_t>> seen;  // this side's volleys and their sets
        for (std::size_t k = 0; k < kKinds; ++k) {
            const Kind kind = static_cast<Kind>(k);
            const bool banded = kind == kHeavy || kind == kMoved || kind == kAssault;
            const std::size_t bands = banded ? band_gap.size() : 1;
            std::vector<std::size_t>& index = dist_index_[si(x)][k];
            index.assign(bands, 0);
            for (std::size_t b = 0; b < bands; ++b) {
                std::vector<analysis::Volley> singles;
                for (const core::Unit& a : attackers) singles.push_back(volleys_for(a, kind, banded ? band_gap[b] : 0.0f));
                const auto same =
                    std::find_if(seen.begin(), seen.end(), [&](const auto& v) { return v.first == singles; });
                if (same != seen.end()) {
                    index[b] = same->second;
                    continue;
                }
                index[b] = pool_.size();
                seen.emplace_back(singles, pool_.size());
                DistSet& set = pool_.emplace_back();
                set.max_n = max_n;
                set.max_t = max_t;
                set.offset.assign(static_cast<std::size_t>(set.max_n * set.max_t), 0);
                for (int n = 1; n <= set.max_n; ++n) {
                    analysis::Volley volley;
                    if (sx.mixed) {
                        volley = singles[static_cast<std::size_t>(n - 1)];  // D-078: the first n models' own volley
                    } else {
                        volley = singles.front();  // n copies of the representative model
                        for (analysis::VolleyBatch& vb : volley) {
                            vb.attack_dice *= n;
                            // D-069: each copy rolls its own random A.
                            const std::vector<core::DiceExpr> one = vb.random_attacks;
                            for (int i = 1; i < n; ++i) {
                                vb.random_attacks.insert(vb.random_attacks.end(), one.begin(), one.end());
                            }
                        }
                    }
                    for (int t = 1; t <= set.max_t; ++t) {
                        const analysis::AttackOutcome out =
                            analysis::expected_outcome(odds, volley, snaps[static_cast<std::size_t>(t - 1)]);
                        set.offset[static_cast<std::size_t>((n - 1) * set.max_t + (t - 1))] = set.data.size();
                        if (sy.exact) {
                            for (int left = 0; left <= t; ++left) {
                                const auto l = static_cast<std::size_t>(left);
                                set.data.push_back(l < out.wounds_left_pmf.size() ? out.wounds_left_pmf[l] : 0.0);
                            }
                            continue;
                        }
                        // D-078: a coarse target's outcome on its grid: wounds left between two levels split between
                        // them in proportion (the expected wounds kept; destroyed and each level exact).
                        const std::size_t base = set.data.size();
                        set.data.resize(base + static_cast<std::size_t>(t + 1), 0.0);
                        int i = 0;
                        const int w_t = sy.levels[static_cast<std::size_t>(t)];
                        for (int left = 0; left <= w_t; ++left) {
                            const auto l = static_cast<std::size_t>(left);
                            const double q = l < out.wounds_left_pmf.size() ? out.wounds_left_pmf[l] : 0.0;
                            if (q == 0.0) continue;
                            while (i < t && sy.levels[static_cast<std::size_t>(i + 1)] <= left) ++i;
                            const int lo = sy.levels[static_cast<std::size_t>(i)];
                            if (left == lo || i >= t) {
                                set.data[base + static_cast<std::size_t>(i)] += q;
                                continue;
                            }
                            const int hi = sy.levels[static_cast<std::size_t>(i + 1)];
                            const double up = static_cast<double>(left - lo) / static_cast<double>(hi - lo);
                            set.data[base + static_cast<std::size_t>(i + 1)] += q * up;
                            set.data[base + static_cast<std::size_t>(i)] += q * (1.0 - up);
                        }
                    }
                }
            }
        }
    }
}

int ChainTable::level_of(core::Side s, int wounds) const {
    const SideSpec& sp = side(s);
    if (sp.exact) return std::clamp(wounds, 0, level_count(sp) - 1);
    const auto it = std::lower_bound(sp.levels.begin(), sp.levels.end(), wounds);
    if (it == sp.levels.end()) return level_count(sp) - 1;
    return static_cast<int>(it - sp.levels.begin());
}

int ChainTable::snap(core::Side s, int wounds) const {
    const SideSpec& sp = side(s);
    if (sp.exact) return wounds;
    const int top = sp.levels.back();
    if (wounds >= top) return top;
    if (wounds <= 0) return 0;
    const auto it = std::upper_bound(sp.levels.begin(), sp.levels.end(), wounds);  // the first level above
    const int hi = *it;
    const int lo = *(it - 1);
    return wounds - lo <= hi - wounds ? lo : hi;
}

const double* ChainTable::pmf_level(core::Side attacker, Kind kind, int g, int attacker_wounds,
                                    int target_level) const {
    const bool banded = kind == kHeavy || kind == kMoved || kind == kAssault;
    const std::size_t band =
        banded ? static_cast<std::size_t>(band_of_[si(attacker)][static_cast<std::size_t>(std::clamp(g, 1, spec_.line))])
               : 0;
    const DistSet& set = pool_[dist_index_[si(attacker)][kind][band]];
    const int n = std::clamp(models_of(attacker, attacker_wounds), 1, set.max_n);
    const int t = std::clamp(target_level, 1, set.max_t);
    return &set.data[set.offset[static_cast<std::size_t>((n - 1) * set.max_t + (t - 1))]];
}

const double* ChainTable::pmf(core::Side attacker, Kind kind, int g, int attacker_wounds, int target_wounds) const {
    return pmf_level(attacker, kind, g, attacker_wounds, level_of(core::opponent(attacker), target_wounds));
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

template <class F>
Node ChainTable::at_wounds(int wa, int wb, F at_levels) const {
    if (spec_.sides[0].exact && spec_.sides[1].exact) return at_levels(wa, wb);  // levels are wounds: grid points
    struct Span {
        int lo = 0;
        int hi = 0;
        double up = 0.0;  // the weight of hi
    };
    const auto span = [&](core::Side s, int w) {
        const SideSpec& sp = side(s);
        Span out;
        if (sp.exact) {
            out.lo = out.hi = w;
            return out;
        }
        const int l = level_of(s, w);  // the first level at or above w
        if (l == 0 || sp.levels[static_cast<std::size_t>(l)] == w || w >= sp.levels.back()) {
            out.lo = out.hi = w >= sp.levels.back() ? level_count(sp) - 1 : l;
            return out;
        }
        out.lo = l - 1;
        out.hi = l;
        const int lw = sp.levels[static_cast<std::size_t>(l - 1)];
        const int hw = sp.levels[static_cast<std::size_t>(l)];
        out.up = static_cast<double>(w - lw) / static_cast<double>(hw - lw);
        return out;
    };
    const Span a = span(core::Side::A, wa);
    const Span b = span(core::Side::B, wb);
    const auto along_b = [&](int ia) {
        if (b.lo == b.hi) return at_levels(ia, b.lo);
        return mix(at_levels(ia, b.hi), at_levels(ia, b.lo), b.up);
    };
    if (a.lo == a.hi) return along_b(a.lo);
    return mix(along_b(a.hi), along_b(a.lo), a.up);
}

Node ChainTable::start_at(int h, int ia, int ib, int g, int ra) const {
    if (ia <= 0) return win(core::Side::B, h, 0, level_wounds(core::Side::B, ib));
    if (ib <= 0) return win(core::Side::A, h, level_wounds(core::Side::A, ia), 0);
    const int hh = std::max(h, spec_.h0) - spec_.h0;
    return node(start_[static_cast<std::size_t>(hh)], pos_index(g, ra) * grid_ + cell(ia, ib));
}

Node ChainTable::engaged_at(const std::vector<Stored>& rows, int h, int ia, int ib, int ra) const {
    if (ia <= 0) return win(core::Side::B, h, 0, level_wounds(core::Side::B, ib));
    if (ib <= 0) return win(core::Side::A, h, level_wounds(core::Side::A, ia), 0);
    const int hh = std::max(h, spec_.h0) - spec_.h0;
    return node(rows[static_cast<std::size_t>(hh)], static_cast<std::size_t>(ra) * grid_ + cell(ia, ib));
}

Node ChainTable::start(int h, int wa, int wb, int g, int ra) const {
    if (h >= spec_.half_turns) return draw(wa, wb);
    if (wa <= 0) return win(core::Side::B, h, wa, wb);
    if (wb <= 0) return win(core::Side::A, h, wa, wb);
    return at_wounds(wa, wb, [&](int ia, int ib) { return start_at(h, ia, ib, g, ra); });
}

Node ChainTable::fight(int h, int wa, int wb, int ra) const {
    if (h >= spec_.half_turns) return draw(wa, wb);
    return at_wounds(wa, wb, [&](int ia, int ib) { return engaged_at(fight_, h, ia, ib, ra); });
}

Node ChainTable::reply(int h, int wa, int wb, int ra) const {
    if (h >= spec_.half_turns) return draw(wa, wb);
    return at_wounds(wa, wb, [&](int ia, int ib) { return engaged_at(reply_, h, ia, ib, ra); });
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
    const core::Side y = core::opponent(x);
    const bool xa = x == core::Side::A;
    const int wx = xa ? wa : wb;
    const int wy = xa ? wb : wa;
    const double* p = pmf(x, kind, g, wx, wy);
    const int ty = level_of(y, wy);
    Node out;
    for (int left = 0; left <= ty; ++left) {
        const double q = p[left];
        if (q == 0.0) continue;
        if (left == 0) {
            add(out, win(x, h, wa, wb), q);
            continue;
        }
        const int lw = level_wounds(y, left);
        const int na = xa ? wa : lw;
        const int nb = xa ? lw : wb;
        add(out, kind == kAssault ? end_turn(h, na, nb, g, ra) : charge_best(h, na, nb, g, ra), q);
    }
    return out;
}

// One half-turn's nodes, built from the next half-turn's (D-078: position-major; the sums of M68's build in the same
// order, so an exact table holds the same values to the bit). T is the stored type: double (exact) or float (compact).
template <typename T>
void ChainTable::build_half_turn(int h) {
    const core::Side x = side_at(h);
    const core::Side y = core::opponent(x);
    const bool xa = x == core::Side::A;
    const int line = spec_.line;
    const SideSpec& sa = spec_.sides[0];
    const SideSpec& sb = spec_.sides[1];
    const int na_levels = level_count(sa);
    const int nb_levels = level_count(sb);
    const auto hh = static_cast<std::size_t>(h - spec_.h0);
    const std::size_t grid = grid_;
    const std::size_t E = static_cast<std::size_t>(line + 1);  // engaged positions: pos_index(0, ra) == ra
    std::vector<T>& start = values<T>(start_[hh]);
    std::vector<T>& fight = values<T>(fight_[hh]);
    std::vector<T>& reply = values<T>(reply_[hh]);
    start.assign(3 * grid * positions_, T{});
    fight.assign(3 * grid * E, T{});
    reply.assign(3 * grid * E, T{});
    const bool last = h + 1 >= spec_.half_turns;
    const T* next = last ? nullptr : values<T>(start_[hh + 1]).data();
    const auto load = [](const T* v, std::size_t i) {
        return Node{static_cast<double>(v[3 * i]), static_cast<double>(v[3 * i + 1]), static_cast<double>(v[3 * i + 2])};
    };
    const auto put = [](std::vector<T>& v, std::size_t i, const Node& n) {
        v[3 * i] = static_cast<T>(n.j);
        v[3 * i + 1] = static_cast<T>(n.pw);
        v[3 * i + 2] = static_cast<T>(n.pl);
    };
    const auto wa_of = [&](int ia) { return sa.levels[static_cast<std::size_t>(ia)]; };
    const auto wb_of = [&](int ib) { return sb.levels[static_cast<std::size_t>(ib)]; };
    // A pmf and its nonzero range (the original sums skip the zeros; the range keeps the same terms).
    struct Pm {
        const double* p = nullptr;
        int lo = 0;
        int hi = -1;
    };
    const auto pm_of = [](const double* p, int top) {
        Pm m;
        m.p = p;
        m.lo = 0;
        m.hi = top;
        while (m.lo <= top && p[m.lo] == 0.0) ++m.lo;
        while (m.hi >= m.lo && p[m.hi] == 0.0) --m.hi;
        return m;
    };
    std::vector<Pm> pm(grid);
    std::vector<Node> draws(grid);
    std::vector<Node> won(grid);
    for (int ia = 1; ia < na_levels; ++ia) {
        for (int ib = 1; ib < nb_levels; ++ib) {
            draws[cell(ia, ib)] = draw(wa_of(ia), wb_of(ib));
            won[cell(ia, ib)] = win(x, h, wa_of(ia), wb_of(ib));
        }
    }

    // The other side strikes back in the fight (engaged; the next half-turn starts engaged).
    for (int ia = 1; ia < na_levels; ++ia) {
        for (int ib = 1; ib < nb_levels; ++ib) {
            pm[cell(ia, ib)] = pm_of(pmf_level(y, kMelee, 1, xa ? wb_of(ib) : wa_of(ia), xa ? ia : ib), xa ? ia : ib);
        }
    }
    for (std::size_t ra = 0; ra < E; ++ra) {
        for (int ia = 1; ia < na_levels; ++ia) {
            for (int ib = 1; ib < nb_levels; ++ib) {
                const std::size_t c = cell(ia, ib);
                const Pm& m = pm[c];
                Node out;
                for (int left = m.lo; left <= m.hi; ++left) {
                    const double q = m.p[left];
                    if (q == 0.0) continue;
                    if (left == 0) {
                        add(out, win(y, h, xa ? 0 : wa_of(ia), xa ? wb_of(ib) : 0), q);
                        continue;
                    }
                    const std::size_t to = xa ? cell(left, ib) : cell(ia, left);
                    add(out, last ? draws[to] : load(next, ra * grid + to), q);
                }
                put(reply, ra * grid + c, out);
            }
        }
    }
    // The side whose turn it is fights first (after a charge, 11.04; or as the player whose turn it is, 12.04).
    for (int ia = 1; ia < na_levels; ++ia) {
        for (int ib = 1; ib < nb_levels; ++ib) {
            pm[cell(ia, ib)] = pm_of(pmf_level(x, kMelee, 1, xa ? wa_of(ia) : wb_of(ib), xa ? ib : ia), xa ? ib : ia);
        }
    }
    for (std::size_t ra = 0; ra < E; ++ra) {
        for (int ia = 1; ia < na_levels; ++ia) {
            for (int ib = 1; ib < nb_levels; ++ib) {
                const std::size_t c = cell(ia, ib);
                const Pm& m = pm[c];
                Node out;
                for (int left = m.lo; left <= m.hi; ++left) {
                    const double q = m.p[left];
                    if (q == 0.0) continue;
                    if (left == 0) {
                        add(out, won[c], q);
                        continue;
                    }
                    add(out, load(reply.data(), ra * grid + (xa ? cell(ia, left) : cell(left, ib))), q);
                }
                put(fight, ra * grid + c, out);
            }
        }
    }
    // A charge from each band, if the side whose turn it is wants one (11.02: P from the roll covering the gap).
    std::vector<Node> charge(grid * positions_);
    for (int g = 1; g <= line; ++g) {
        const double p = p_charge_[static_cast<std::size_t>(g)];
        for (int ra = 0; ra <= line - g; ++ra) {
            const std::size_t pos = pos_index(g, ra);
            const std::size_t fight_row = static_cast<std::size_t>(xa ? std::min(line, ra + g) : ra) * grid;
            for (std::size_t c = 0; c < grid; ++c) {
                const Node hold = last ? draws[c] : load(next, pos * grid + c);
                Node best = hold;
                if (p > 0.0) {
                    const Node charged = mix(load(fight.data(), fight_row + c), hold, p);
                    if (better_for(x, charged, hold)) best = charged;
                }
                charge[pos * grid + c] = best;
            }
        }
    }
    // Shooting then (unless it advanced) a charge, per band reached. A kind with the same volley as the stationary
    // shooting (a unit without [HEAVY] weapons) makes the same sums: copied.
    std::vector<Node> shoot[3];
    for (auto& s : shoot) s.assign(grid * positions_, Node{});
    std::vector<const double*> heavy(grid);
    for (std::size_t k = 0; k < 3; ++k) {
        const bool advanced = k == kAssault;
        for (int g = 1; g <= line; ++g) {
            bool as_heavy = k == kMoved;
            for (int ia = 1; ia < na_levels; ++ia) {
                for (int ib = 1; ib < nb_levels; ++ib) {
                    const std::size_t c = cell(ia, ib);
                    const int ty = xa ? ib : ia;
                    const double* p = pmf_level(x, static_cast<Kind>(k), g, xa ? wa_of(ia) : wb_of(ib), ty);
                    if (k == kHeavy) heavy[c] = p;
                    if (as_heavy && p != heavy[c]) as_heavy = false;
                    pm[c] = pm_of(p, ty);
                }
            }
            for (int ra = 0; ra <= line - g; ++ra) {
                const std::size_t pos = pos_index(g, ra);
                Node* out = shoot[k].data() + pos * grid;
                if (as_heavy) {
                    const Node* same = shoot[kHeavy].data() + pos * grid;
                    std::copy(same, same + grid, out);
                    continue;
                }
                for (int ia = 1; ia < na_levels; ++ia) {
                    for (int ib = 1; ib < nb_levels; ++ib) {
                        const std::size_t c = cell(ia, ib);
                        const Pm& m = pm[c];
                        Node acc;
                        for (int left = m.lo; left <= m.hi; ++left) {
                            const double q = m.p[left];
                            if (q == 0.0) continue;
                            if (left == 0) {
                                add(acc, won[c], q);
                                continue;
                            }
                            const std::size_t to = xa ? cell(ia, left) : cell(left, ib);
                            if (advanced) {
                                add(acc, last ? draws[to] : load(next, pos * grid + to), q);
                            } else {
                                add(acc, charge[pos * grid + to], q);
                            }
                        }
                        out[c] = acc;
                    }
                }
            }
        }
    }
    // Engaged: stay (close-quarters shooting, then fight first) or Fall Back (09.07).
    const SideSpec& sx = side(x);
    const int dm = sx.move_buckets;
    for (int ia = 1; ia < na_levels; ++ia) {
        for (int ib = 1; ib < nb_levels; ++ib) {
            const int ty = xa ? ib : ia;
            Pm& m = pm[cell(ia, ib)];
            m.p = pmf_level(x, kCloseQuarters, 1, xa ? wa_of(ia) : wb_of(ib), ty);
            m.lo = 0;
            m.hi = ty;
        }
    }
    for (int ra = 0; ra <= line; ++ra) {
        const std::size_t row = static_cast<std::size_t>(ra) * grid;
        const int room = xa ? ra : line - ra;
        const int d = std::min(dm, room);
        const std::size_t back_pos = d >= 1 ? pos_index(d, xa ? ra - d : ra) : 0;
        for (int ia = 1; ia < na_levels; ++ia) {
            for (int ib = 1; ib < nb_levels; ++ib) {
                const std::size_t c = cell(ia, ib);
                const Pm& m = pm[c];
                Node stay;
                for (int left = 0; left <= m.hi; ++left) {
                    const double q = m.p[left];
                    if (q == 0.0) continue;
                    if (left == 0) {
                        add(stay, won[c], q);
                        continue;
                    }
                    add(stay, load(fight.data(), row + (xa ? cell(ia, left) : cell(left, ib))), q);
                }
                Node best = stay;
                if (d >= 1) {
                    const Node back = last ? draws[c] : load(next, back_pos * grid + c);
                    if (better_for(x, back, best)) best = back;
                }
                put(start, row + c, best);
            }
        }
    }
    // Unengaged: a Normal move to any band in reach, or an Advance; the same choices as before for every cell, in the
    // same order (the bounds depend on the position only).
    const auto& d6 = analysis::d6_pmf();
    std::vector<Node> best(grid);
    std::vector<Node> advance(grid);
    std::vector<Node> best_so_far(grid);
    const auto take = [&](std::vector<Node>& acc, const Node* src) {
        for (std::size_t c = 0; c < grid; ++c) {
            if (better_for(x, src[c], acc[c])) acc[c] = src[c];
        }
    };
    for (int g = 1; g <= line; ++g) {
        for (int ra = 0; ra <= line - g; ++ra) {
            const int room = xa ? ra : line - g - ra;
            const auto at = [&](std::size_t kind, int dd) {  // dd > 0: towards the enemy
                return shoot[kind].data() + pos_index(g - dd, xa ? ra + dd : ra) * grid;
            };
            {
                const Node* r0 = at(kHeavy, 0);
                std::copy(r0, r0 + grid, best.begin());
            }
            const int towards = std::min(dm, g - 1);
            const int back = std::min(dm, room);
            for (int step = 1; step <= std::max(towards, back); ++step) {
                const std::size_t kind = static_cast<float>(step) * Evaluator::bucket_mm() <= kHeavyMoveMm + core::EPS_MM
                                             ? static_cast<std::size_t>(kHeavy)
                                             : static_cast<std::size_t>(kMoved);
                if (step <= towards) take(best, at(kind, step));
                if (step <= back) take(best, at(kind, -step));
            }
            if (dm > 0) {
                // 09.06: the band it likes best for each D6 result (the reach only grows with the roll).
                std::fill(advance.begin(), advance.end(), Node{});
                {
                    const Node* r0 = at(kAssault, 0);
                    std::copy(r0, r0 + grid, best_so_far.begin());
                }
                int reach = 0;
                for (std::size_t f = 1; f <= 6; ++f) {
                    const int r = sx.advance_buckets[f];
                    for (int step = reach + 1; step <= r; ++step) {
                        if (step <= g - 1) take(best_so_far, at(kAssault, step));
                        if (step <= room) take(best_so_far, at(kAssault, -step));
                    }
                    reach = std::max(reach, r);
                    for (std::size_t c = 0; c < grid; ++c) add(advance[c], best_so_far[c], d6[f]);
                }
                take(best, advance.data());
            }
            const std::size_t row = pos_index(g, ra) * grid;
            for (std::size_t c = 0; c < grid; ++c) put(start, row + c, best[c]);
        }
    }
}

namespace {

// D-078: a table's stored values: three a node, for every wounds cell (both sides' grid levels above 0) at every line
// position (engaged and unengaged, plus the fight and the reply), in every half-turn from h0 on. D-080 (M73): also what
// chain_plan_size counts, from the levels alone.
std::size_t table_bytes(int line, int levels_a, int levels_b, int half_turns, int h0, bool compact) noexcept {
    const std::size_t positions =
        static_cast<std::size_t>(line + 1) + static_cast<std::size_t>(line) * static_cast<std::size_t>(line + 1) / 2;
    const std::size_t cells =
        static_cast<std::size_t>(std::max(0, levels_a - 1)) * static_cast<std::size_t>(std::max(0, levels_b - 1));
    const std::size_t per_half_turn = 3 * cells * (positions + 2 * static_cast<std::size_t>(line + 1));
    const std::size_t count = static_cast<std::size_t>(std::max(0, half_turns - std::max(0, h0)));
    return per_half_turn * count * (compact ? sizeof(float) : sizeof(double));
}

}  // namespace

std::size_t ChainTable::estimate_bytes(const Spec& s) noexcept {
    return table_bytes(s.line, level_count(s.sides[0]), level_count(s.sides[1]), s.half_turns, s.h0, s.compact);
}

std::size_t ChainTable::bytes() const noexcept {
    std::size_t total = 0;
    for (const std::vector<Stored>* rows : {&start_, &fight_, &reply_}) {
        for (const Stored& r : *rows) total += r.d.size() * sizeof(double) + r.f.size() * sizeof(float);
    }
    for (const DistSet& d : pool_) total += d.data.size() * sizeof(double) + d.offset.size() * sizeof(std::size_t);
    return total;
}

void ChainTable::trim_before(int h) {
    const int to = std::min(h, spec_.half_turns - 1);
    if (to <= spec_.h0) return;
    const auto drop = static_cast<std::ptrdiff_t>(to - spec_.h0);
    for (std::vector<Stored>* rows : {&start_, &fight_, &reply_}) rows->erase(rows->begin(), rows->begin() + drop);
    spec_.h0 = to;
}

std::uint64_t ChainTable::fingerprint(int from) const {
    // By the lookups, so the layout and the form don't matter: an exact table's values at every grid point.
    Fnv1a f;
    const auto put = [&](const Node& n) {
        f.u64(std::bit_cast<std::uint64_t>(n.j));
        f.u64(std::bit_cast<std::uint64_t>(n.pw));
        f.u64(std::bit_cast<std::uint64_t>(n.pl));
    };
    const int line = spec_.line;
    const SideSpec& sa = spec_.sides[0];
    const SideSpec& sb = spec_.sides[1];
    const int first = std::max(std::max(0, spec_.h0), from);
    for (int h = first; h < spec_.half_turns; ++h) {
        for (std::size_t ia = 1; ia < sa.levels.size(); ++ia) {
            for (std::size_t ib = 1; ib < sb.levels.size(); ++ib) {
                const int wa = sa.levels[ia];
                const int wb = sb.levels[ib];
                for (int ra = 0; ra <= line; ++ra) put(start(h, wa, wb, 0, ra));
                for (int g = 1; g <= line; ++g) {
                    for (int ra = 0; ra <= line - g; ++ra) put(start(h, wa, wb, g, ra));
                }
                for (int ra = 0; ra <= line; ++ra) {
                    put(fight(h, wa, wb, ra));
                    put(reply(h, wa, wb, ra));
                }
            }
        }
    }
    f.i32(from > 0 ? first : spec_.h0);
    f.i32(spec_.half_turns);
    f.i32(line);
    f.i32(sa.max_wounds);
    f.i32(sb.max_wounds);
    return f.value();
}

ChainTableInfo ChainTable::info_of(const Spec& spec) {
    ChainTableInfo i;
    i.key = spec.key;
    i.h0 = spec.h0;
    i.half_turns = spec.half_turns;
    i.line = spec.line;
    for (std::size_t s = 0; s < 2; ++s) {
        i.max_wounds[s] = spec.sides[s].max_wounds;
        i.levels[s] = level_count(spec.sides[s]);
        i.exact[s] = spec.sides[s].exact;
    }
    i.compact = spec.compact;
    i.bytes = estimate_bytes(spec);
    return i;
}

ChainTableInfo ChainTable::info() const {
    ChainTableInfo i = info_of(spec_);
    i.bytes = bytes();
    return i;
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

std::optional<Duel> duel_play(const core::GameState& state) {
    if (state.result_rule == core::ResultRule::PointsDestroyed) return std::nullopt;  // D-078: the army path
    return duel_of(state);
}

std::int64_t points_increment(const core::Unit& unit, std::int64_t denominator) {
    if (unit.points <= 0 || unit.starting_wounds <= 0 || core::is_destroyed(unit) || denominator <= 0) return 0;
    // D-076's formula: a destroyed unit counts points x (D / starting wounds) x starting wounds, a unit on the
    // battlefield points x (D / starting wounds) x wounds lost; the difference is its wounds remaining.
    const std::int64_t scale = denominator / unit.starting_wounds;
    const std::int64_t remaining = std::int64_t{unit.starting_wounds} - std::int64_t{core::wounds_lost(unit)};
    return std::int64_t{unit.points} * scale * remaining;
}

double points_lead(double mu, double variance) noexcept {
    if (!(variance > 0.0)) return mu > 0.0 ? 1.0 : (mu < 0.0 ? -1.0 : 0.0);
    const double x = std::fabs(mu) / std::sqrt(2.0 * variance);
    // Abramowitz and Stegun 7.1.28: erf(x) = 1 - 1 / (1 + a1 x + ... + a6 x^6)^16, |error| <= 3e-7, x >= 0.
    const double p = 1.0 + x * (0.0705230784 + x * (0.0422820123 + x * (0.0092705272 +
                                     x * (0.0001520143 + x * (0.0002765672 + x * 0.0000430638)))));
    double t = p * p;  // p^2
    t *= t;            // p^4
    t *= t;            // p^8
    t *= t;            // p^16
    const double e = 1.0 - 1.0 / t;
    return mu > 0.0 ? e : -e;
}

int max_line_buckets() noexcept { return kMaxLine; }

const core::Unit* closest_charge_target(const core::GameState& state, const core::Unit& unit) {
    const core::Unit* best = nullptr;
    float best_gap = 0.0f;
    const int range = static_cast<int>(core::kChargeRangeInches);
    for (const core::UnitId id : core::eligible_charge_targets(state, unit, range)) {
        const core::Unit* t = state.find_unit(id);
        if (t == nullptr || t->models.empty()) continue;
        const float gap = core::unit_distance(unit, *t).value_or(0.0f);
        if (best == nullptr || gap < best_gap) {
            best = t;
            best_gap = gap;
        }
    }
    return best;
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

int strength_of(const core::Unit& u) { return std::max(u.starting_strength, static_cast<int>(u.models.size())); }

// D-078: a mixed unit's composition at full strength, as a key: each model's loadout hash, sorted (the order is
// decided later, against the other unit type), so the key holds while rank and file die.
void hash_composition(Fnv1a& key, const core::Unit& u) {
    const core::Unit proto = mixed_prototype(u, strength_of(u));
    std::vector<std::uint64_t> h;
    for (const core::Model& m : proto.models) h.push_back(hash_model(m));
    std::sort(h.begin(), h.end());
    for (const std::uint64_t v : h) key.u64(v);
}

// ---- D-067: the army's tables ----

// A unit's type for the army's tables: its representative model (D-078: a mixed unit's composition), keywords and
// starting strength (what a duel table's key holds for one side). Units with models only.
std::uint64_t side_key(const core::Unit& u) {
    Fnv1a key;
    const core::Model& rep = representative(u);
    if (mixed_loadouts(u)) {
        key.u64(0x6d69786564ull);  // "mixed"
        hash_composition(key, u);
    } else {
        key.u64(hash_model(rep));
    }
    for (const std::string& k : u.keywords) key.bytes(k.data(), k.size());
    const int wpm = std::max(1, rep.characteristics.wounds);
    const int strength = strength_of(u);
    key.i32(std::max(1, strength * wpm));
    key.i32(strength);
    return key.value();
}

// The army's line: the battlefield's diagonal in buckets, rounded up (so any gap and rooms fit), within the
// table's limits.
int army_line(const core::GameState& root, float bucket) {
    const double w = root.battlefield.width_mm;
    const double d = root.battlefield.depth_mm;
    const double diagonal = std::sqrt(w * w + d * d);
    return std::clamp(static_cast<int>(std::ceil(diagonal / bucket - 1e-9)), kMinLine, kMaxLine);
}

// One side of a table spec for unit `u` (against `other`): the static description (D-078: the grid, a mixed unit's
// composition not yet in keep order, the [HAZARDOUS] rate), the key's part for it, and the region's top `max_wounds`.
void side_spec(SideSpec& s, Fnv1a& key, const core::GameState& root, const core::Unit& u, const core::Unit& other,
               int max_wounds) {
    const core::Model& rep = representative(u);
    s.wounds_per_model = std::max(1, rep.characteristics.wounds);
    const int strength = strength_of(u);
    s.wounds_start = std::max(1, strength * s.wounds_per_model);
    s.levels = grid_levels(s.wounds_start, std::max(1, max_wounds));
    s.exact = s.wounds_start <= kExactWounds;
    s.max_wounds = s.levels.back();
    s.max_models = (s.max_wounds + s.wounds_per_model - 1) / s.wounds_per_model;
    s.mixed = mixed_loadouts(u);
    // The prototype (and so each model's profile choice against it) has the unit's starting strength, not what is
    // left now, so a state's value is the same whichever table answers it.
    const int count = std::max(strength, s.max_models);
    s.proto = s.mixed ? mixed_prototype(u, count) : prototype(u, count);
    const analysis::ThreatRanges t = analysis::threat_ranges(s.proto);
    s.move_buckets = static_cast<int>(std::floor((t.move_mm + core::EPS_MM) / Evaluator::bucket_mm()));
    for (std::size_t f = 1; f <= 6; ++f) {
        s.advance_buckets[f] = static_cast<int>(std::floor((t.advance_mm[f] + core::EPS_MM) / Evaluator::bucket_mm()));
    }
    s.ranges = s.mixed ? usable_ranges(s.proto) : usable_ranges(rep);
    // D-078: [HAZARDOUS] at Normal's exchange rate (D-077), only for a unit that has such a profile (else 0, as before).
    s.hazard_rate = has_hazardous(u) ? normal::hazard_rate(root, u, other) : 0.0;
    if (s.mixed) {
        key.u64(0x6d69786564ull);
        hash_composition(key, u);
    } else {
        key.u64(hash_model(rep));
    }
    for (const std::string& k : u.keywords) key.bytes(k.data(), k.size());
    key.i32(s.wounds_start);
    key.i32(static_cast<int>(s.proto.models.size()));
    if (s.hazard_rate > 0.0) key.u64(rate_key(s.hazard_rate));
}

void spec_key_tail(Spec& spec, Fnv1a& key) {
    key.i32(spec.half_turns);
    key.i32(spec.first == core::Side::A ? 0 : 1);
    key.i32(spec.line);
    key.u64(static_cast<std::uint64_t>(std::llround(spec.weights.tempo * 1e9)));
    key.u64(static_cast<std::uint64_t>(std::llround(spec.weights.material * 1e9)));
    spec.key = key.value();
}

// The table's spec for units of the types of `ua` (side A) and `ub` (side B): the whole unit (every later state of
// a unit of that type), from the root's half-turn on, on the army's line.
Spec army_spec(const core::GameState& root, const core::Unit& ua, const core::Unit& ub, const ChainWeights& weights,
               int line) {
    Spec spec;
    spec.half_turns = 2 * root.battle_rounds;
    spec.first = root.first_turn;
    spec.h0 = std::min(half_turn_of(root), spec.half_turns - 1);
    spec.weights = weights;
    spec.line = line;
    Fnv1a key;
    for (const core::Unit* u : {&ua, &ub}) {
        const core::Unit& other = u == &ua ? ub : ua;
        const int wounds_start = std::max(1, strength_of(*u) * std::max(1, representative(*u).characteristics.wounds));
        side_spec(spec.sides[si(u->side)], key, root, *u, other, wounds_start);
    }
    spec_key_tail(spec, key);
    return spec;
}

// D-078: a mixed side's models in keep order: the most valuable against the other side's unit at full strength first
// (Normal's model_worth, as its casualty order keeps them; a tie keeps the order), so the first n are the n alive.
void finalize_spec(Spec& spec, analysis::OddsCache& odds) {
    // Both sides' worth first, each against the other side's models as specified (so neither order depends on the
    // other's sorting).
    std::array<std::map<core::ModelId, double>, 2> worth;
    for (std::size_t s = 0; s < 2; ++s) {
        if (spec.sides[s].mixed) worth[s] = normal::model_worth(odds, spec.sides[s].proto, spec.sides[1 - s].proto);
    }
    for (std::size_t s = 0; s < 2; ++s) {
        SideSpec& x = spec.sides[s];
        if (!x.mixed) continue;
        const std::map<core::ModelId, double>& w = worth[s];
        std::stable_sort(x.proto.models.begin(), x.proto.models.end(),
                         [&](const core::Model& a, const core::Model& b) { return w.at(a.id) > w.at(b.id); });
    }
}

bool spec_has_mixed(const Spec& s) { return s.sides[0].mixed || s.sides[1].mixed; }

std::size_t default_workers() {
    const unsigned hw = std::thread::hardware_concurrency();
    return std::clamp<std::size_t>(hw == 0 ? 1 : hw, 1, kMaxWorkers);
}

}  // namespace

// ---- D-078: the cache ----

struct ChainCache::Store {
    std::vector<std::shared_ptr<ChainTable>> tables;  // most recently used last
    std::size_t builds = 0;
    std::size_t capacity = kMaxTables;
    std::size_t workers = 0;  // 0: default_workers()
    std::size_t cap = kCacheCapBytes;
};

ChainCache::ChainCache() : store_(std::make_shared<Store>()) {}

std::size_t ChainCache::size() const noexcept { return store_->tables.size(); }
std::size_t ChainCache::builds() const noexcept { return store_->builds; }
void ChainCache::clear() noexcept { store_->tables.clear(); }
std::size_t ChainCache::cap_bytes() noexcept { return kCacheCapBytes; }
std::size_t ChainCache::capacity() const noexcept { return store_->capacity; }
void ChainCache::share_with(ChainCache& other) { store_ = other.store_; }
void ChainCache::set_workers(std::size_t workers) noexcept { store_->workers = workers; }
void ChainCache::set_cap_bytes(std::size_t cap) noexcept { store_->cap = cap == 0 ? kCacheCapBytes : cap; }
std::size_t ChainCache::workers() const noexcept {
    return store_->workers == 0 ? default_workers() : store_->workers;
}

std::size_t ChainCache::bytes() const noexcept {
    std::size_t total = 0;
    for (const auto& t : store_->tables) total += t->bytes();
    return total;
}

void ChainCache::trim_before(int half_turn) {
    for (const auto& t : store_->tables) {
        if (t.use_count() == 1) t->trim_before(half_turn);  // no Evaluator holds it
    }
}

std::vector<ChainTableInfo> ChainCache::info() const {
    std::vector<ChainTableInfo> out;
    for (const auto& t : store_->tables) out.push_back(t->info());
    return out;
}

std::uint64_t ChainCache::fingerprint(int from) const {
    std::vector<std::uint64_t> each;
    for (const auto& t : store_->tables) each.push_back(t->fingerprint(from));
    std::sort(each.begin(), each.end());
    Fnv1a f;
    for (const std::uint64_t v : each) f.u64(v);
    return f.value();
}

namespace {

// D-078: room for `adding` tables of `adding_bytes`: the least recently used tables no Evaluator holds are evicted while
// the cache would hold more tables than its capacity or more bytes than the cap. A function of the requests alone.
void make_room(ChainCache::Store& s, std::size_t adding, std::size_t adding_bytes) {
    std::size_t total = 0;
    for (const auto& t : s.tables) total += t->bytes();
    for (std::size_t i = 0; i < s.tables.size();) {
        const bool over = s.tables.size() + adding > s.capacity || total + adding_bytes > s.cap;
        if (!over) break;
        if (s.tables[i].use_count() == 1) {
            total -= s.tables[i]->bytes();
            s.tables.erase(s.tables.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

// D-078: the tables of `specs` at the indices `which`, built by up to `workers` threads. Each thread uses its own odds
// cache (a value never depends on a cache's contents, M40) and writes only its own tables; the calling thread works too
// and joins every thread before returning, then rethrows the first error in spec order. One table, or one worker: built
// on the calling thread with `odds`, as before M69.
std::vector<std::shared_ptr<ChainTable>> build_tables(analysis::OddsCache& odds, const std::vector<Spec>& specs,
                                                      const std::vector<std::size_t>& which, std::size_t workers,
                                                      float bucket) {
    const std::size_t n = which.size();
    std::vector<std::shared_ptr<ChainTable>> built(n);
    const std::size_t threads = std::min(std::max<std::size_t>(1, workers), n);
    if (threads <= 1) {
        for (std::size_t k = 0; k < n; ++k) built[k] = std::make_shared<ChainTable>(odds, specs[which[k]], bucket);
        return built;
    }
    std::vector<std::exception_ptr> errors(n);
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        analysis::OddsCache own;
        for (std::size_t k = next.fetch_add(1); k < n; k = next.fetch_add(1)) {
            try {
                built[k] = std::make_shared<ChainTable>(own, specs[which[k]], bucket);
            } catch (...) {
                errors[k] = std::current_exception();
            }
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    try {
        for (std::size_t t = 1; t < threads; ++t) pool.emplace_back(work);
    } catch (...) {
        // No more threads: the ones started and the calling thread share the work.
    }
    work();
    for (std::thread& t : pool) t.join();
    for (const std::exception_ptr& e : errors) {
        if (e) std::rethrow_exception(e);
    }
    return built;
}

}  // namespace

std::vector<std::shared_ptr<const ChainTable>> Evaluator::find_or_build(std::vector<Spec> specs,
                                                                        ChainCache* cache) const {
    std::vector<std::shared_ptr<const ChainTable>> out(specs.size());
    ChainCache::Store* store = cache != nullptr ? cache->store_.get() : nullptr;
    std::vector<std::size_t> missing;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (store != nullptr) {
            const auto it = std::find_if(store->tables.begin(), store->tables.end(),
                                         [&](const std::shared_ptr<ChainTable>& t) { return t->covers(specs[i]); });
            if (it != store->tables.end()) {
                out[i] = *it;
                std::rotate(it, it + 1, store->tables.end());  // most recently used last
                continue;
            }
        }
        missing.push_back(i);
    }
    if (missing.empty()) return out;
    for (const std::size_t i : missing) {
        if (spec_has_mixed(specs[i])) finalize_spec(specs[i], odds_);  // D-078: the keep order, only for a build
    }
    if (store != nullptr) {
        std::size_t adding_bytes = 0;
        for (const std::size_t i : missing) adding_bytes += ChainTable::estimate_bytes(specs[i]);
        make_room(*store, missing.size(), adding_bytes);
    }
    const std::size_t workers = cache != nullptr ? cache->workers() : default_workers();
    std::vector<std::shared_ptr<ChainTable>> built = build_tables(odds_, specs, missing, workers, kBucketMm);
    for (std::size_t k = 0; k < missing.size(); ++k) {
        out[missing[k]] = built[k];
        if (store != nullptr) {
            ++store->builds;
            store->tables.push_back(std::move(built[k]));
        }
    }
    return out;
}

std::shared_ptr<const ChainTable> Evaluator::find_or_build(Spec spec, ChainCache* cache) const {
    std::vector<Spec> one;
    one.push_back(std::move(spec));
    return find_or_build(std::move(one), cache).front();
}

namespace {

// D-067: the army's unit types (each side's, in unit order: units with models) and D-078 the specs of every pair, side
// A's types outermost; compact storage when the game's tables would be too big at full precision (the whole game, from
// the pairs present: Strike's one table is far below, the rosters' 30 far above).
struct ArmyTypes {
    std::vector<std::pair<std::uint64_t, const core::Unit*>> a_types;
    std::vector<std::pair<std::uint64_t, const core::Unit*>> b_types;
    std::vector<std::pair<core::UnitId, std::uint64_t>> unit_keys;
    std::array<double, 2> start_wounds{};
};

struct TablePlan : ArmyTypes {
    std::vector<Spec> specs;
};

// The army's unit types (units with models, each side's in unit order; the first unit of a type stands for it). D-080
// (M73): shared by table_plan and chain_plan_size, so both count the same types.
std::optional<ArmyTypes> army_types(const core::GameState& root) {
    if (root.battle_rounds <= 0) return std::nullopt;
    std::vector<const core::Unit*> a_units;
    std::vector<const core::Unit*> b_units;
    for (const core::Unit& u : root.units) {
        if (u.models.empty()) continue;
        (u.side == core::Side::A ? a_units : b_units).push_back(&u);
    }
    if (a_units.empty() || b_units.empty()) return std::nullopt;
    ArmyTypes out;
    for (const auto& [units, types] : {std::pair{&a_units, &out.a_types}, std::pair{&b_units, &out.b_types}}) {
        for (const core::Unit* u : *units) {
            const int wpm = std::max(1, representative(*u).characteristics.wounds);
            out.start_wounds[si(u->side)] +=
                std::max(1, std::max(u->starting_strength, static_cast<int>(u->models.size())) * wpm);
            const std::uint64_t k = side_key(*u);
            out.unit_keys.emplace_back(u->id, k);
            if (std::none_of(types->begin(), types->end(), [&](const auto& t) { return t.first == k; })) {
                types->emplace_back(k, u);
            }
        }
    }
    return out;
}

std::optional<TablePlan> table_plan(const core::GameState& root, const ChainWeights& weights, float bucket) {
    std::optional<ArmyTypes> types = army_types(root);
    if (!types) return std::nullopt;
    TablePlan plan;
    static_cast<ArmyTypes&>(plan) = std::move(*types);
    const int line = army_line(root, bucket);
    for (const auto& [ka, ua] : plan.a_types) {
        for (const auto& [kb, ub] : plan.b_types) plan.specs.push_back(army_spec(root, *ua, *ub, weights, line));
    }
    std::size_t full = 0;
    for (const Spec& s : plan.specs) {
        Spec whole = s;
        whole.h0 = 0;
        full += ChainTable::estimate_bytes(whole);
    }
    if (full > kCompactAboveBytes) {
        for (Spec& s : plan.specs) s.compact = true;
    }
    return plan;
}

// D-080 (M73): a unit type's wounds grid levels in the army's tables (side_spec's, with the region's top at the type's
// starting wounds, as army_spec sets it).
int army_levels(const core::Unit& u) {
    const int wounds_start = std::max(1, strength_of(u) * std::max(1, representative(u).characteristics.wounds));
    return static_cast<int>(grid_levels(wounds_start, wounds_start).size());
}

// The duel's table spec (M44): the two units' wounds now, from the root's half-turn on, on a line as long as the gap
// and both rooms.
Spec duel_spec(const core::GameState& root, const core::Unit& ua, const core::Unit& ub, const ChainWeights& weights,
               float bucket) {
    Spec spec;
    spec.half_turns = 2 * root.battle_rounds;
    spec.first = root.first_turn;
    spec.h0 = std::min(half_turn_of(root), spec.half_turns - 1);
    spec.weights = weights;
    const float gap = core::unit_distance(ua, ub).value_or(0.0f);
    const float ra = room_away(root, ua, centroid_of(ub));
    const float rb = room_away(root, ub, centroid_of(ua));
    spec.line = std::clamp(static_cast<int>(std::lround((gap + ra + rb) / bucket)), kMinLine, kMaxLine);
    Fnv1a key;
    for (const core::Unit* u : {&ua, &ub}) {
        side_spec(spec.sides[si(u->side)], key, root, *u, u == &ua ? ub : ua, std::max(1, wounds_of(*u)));
    }
    spec_key_tail(spec, key);
    return spec;
}

}  // namespace

std::vector<ChainTableInfo> plan_chain_tables(const core::GameState& root, ChainWeights weights) {
    std::vector<ChainTableInfo> out;
    if (const std::optional<Duel> d = duel_play(root)) {
        const core::Unit* ua = root.find_unit(d->units[0]);
        const core::Unit* ub = root.find_unit(d->units[1]);
        if (ua != nullptr && ub != nullptr && root.battle_rounds > 0) {
            out.push_back(ChainTable::info_of(duel_spec(root, *ua, *ub, weights, Evaluator::bucket_mm())));
        }
        return out;
    }
    if (const std::optional<TablePlan> plan = table_plan(root, weights, Evaluator::bucket_mm())) {
        for (const Spec& s : plan->specs) out.push_back(ChainTable::info_of(s));
    }
    return out;
}

ChainPlanSize chain_plan_size(const core::GameState& root) {
    ChainPlanSize out;
    if (const std::optional<Duel> d = duel_play(root)) {
        // The duel's one table (its spec is cheap: one pair, no army).
        const core::Unit* ua = root.find_unit(d->units[0]);
        const core::Unit* ub = root.find_unit(d->units[1]);
        if (ua != nullptr && ub != nullptr && root.battle_rounds > 0) {
            out.tables = 1;
            out.bytes = ChainTable::estimate_bytes(duel_spec(root, *ua, *ub, ChainWeights{}, Evaluator::bucket_mm()));
        }
        return out;
    }
    const std::optional<ArmyTypes> types = army_types(root);
    if (!types) return out;
    // As table_plan and army_spec: the army's line, the game's half-turns from the root's on, compact when the whole
    // game's tables at full precision would take more than kCompactAboveBytes.
    const int line = army_line(root, Evaluator::bucket_mm());
    const int half_turns = 2 * root.battle_rounds;
    const int h0 = std::min(half_turn_of(root), half_turns - 1);
    std::vector<int> a_levels;
    std::vector<int> b_levels;
    for (const auto& [k, u] : types->a_types) a_levels.push_back(army_levels(*u));
    for (const auto& [k, u] : types->b_types) b_levels.push_back(army_levels(*u));
    std::size_t full = 0;
    for (const int la : a_levels) {
        for (const int lb : b_levels) full += table_bytes(line, la, lb, half_turns, 0, false);
    }
    const bool compact = full > kCompactAboveBytes;
    for (const int la : a_levels) {
        for (const int lb : b_levels) {
            ++out.tables;
            out.bytes += table_bytes(line, la, lb, half_turns, h0, compact);
        }
    }
    return out;
}

ArmyPlans army_plans(analysis::OddsCache& cache, const core::GameState& root, Generation generation) {
    ArmyPlans out;
    if (pre_teamwork(generation)) return out;
    for (const core::Side s : {core::Side::A, core::Side::B}) {
        out.sides[si(s)] = normal::army_plan(cache, root, s, generation);
    }
    return out;
}

const AttentionQueue* Attention::queue_of(core::UnitId attacker) const {
    for (const AttentionQueue& q : queues) {
        if (q.attacker == attacker) return &q;
    }
    return nullptr;
}

std::optional<int> Attention::wiped(core::UnitId unit) const {
    for (const auto& [id, turn] : wiped_turn) {
        if (id == unit) return turn;
    }
    return std::nullopt;
}

const GroupPace* Attention::group_on(core::UnitId target) const {
    for (const GroupPace& g : groups) {
        if (g.target == target) return &g;
    }
    return nullptr;
}

Evaluator::Evaluator(analysis::OddsCache& odds, const core::GameState& root, core::Side side, ChainWeights weights,
                     ChainCache* cache, bool force_army, Generation generation, const ArmyPlans* plans)
    : odds_(odds), side_(side), weights_(weights) {
    const std::optional<Duel> d = duel_play(root);
    if (!d) {
        // D-067: the army. One table per pair of unit types present (both sides need a unit with models).
        std::optional<TablePlan> plan = table_plan(root, weights, kBucketMm);
        if (!plan) return;
        army_ = true;
        unit_keys_ = std::move(plan->unit_keys);
        start_wounds_ = plan->start_wounds;
        if (cache != nullptr) cache->store_->capacity = std::max(kMaxTables, plan->specs.size());
        std::vector<std::shared_ptr<const ChainTable>> tables = find_or_build(std::move(plan->specs), cache);
        std::size_t k = 0;
        for (const auto& [ka, ua] : plan->a_types) {
            for (const auto& [kb, ub] : plan->b_types) army_tables_.push_back(PairTable{ka, kb, tables[k++]});
        }
        // D-083 part 4 (M82): the decision's plans (the caller's, worked out once for it); none for a pre-teamwork
        // generation.
        if (pre_teamwork(generation)) return;
        plans_ = plans != nullptr ? *plans : army_plans(odds, root, generation);
        return;
    }
    duel_ = *d;
    const core::Unit* ua = root.find_unit(duel_.units[0]);
    const core::Unit* ub = root.find_unit(duel_.units[1]);
    if (ua == nullptr || ub == nullptr || root.battle_rounds <= 0) return;
    table_ = find_or_build(duel_spec(root, *ua, *ub, weights, kBucketMm), cache);
    if (force_army && table_) {
        // D-067 (tests): the army's combination over the duel's own table, each pair read the duel's way.
        army_ = true;
        duel_reading_ = true;
        const std::uint64_t ka = side_key(*ua);
        const std::uint64_t kb = side_key(*ub);
        unit_keys_ = {{ua->id, ka}, {ub->id, kb}};
        army_tables_.push_back(PairTable{ka, kb, table_});
        for (const core::Unit* u : {ua, ub}) {
            const int wpm = std::max(1, representative(*u).characteristics.wounds);
            const int strength = std::max(u->starting_strength, static_cast<int>(u->models.size()));
            start_wounds_[si(u->side)] += std::max(1, strength * wpm);
        }
        table_.reset();
    }
}

Evaluator::~Evaluator() = default;

int Evaluator::line_buckets() const noexcept {
    if (table_) return table_->line();
    return army_tables_.empty() ? 0 : army_tables_.front().table->line();
}

std::vector<ChainTableInfo> Evaluator::tables_info() const {
    std::vector<ChainTableInfo> out;
    if (table_) out.push_back(table_->info());
    for (const PairTable& p : army_tables_) out.push_back(p.table->info());
    return out;
}

std::vector<double> Evaluator::volley_outcome(const core::Unit& attacker, const core::Unit& target, bool melee, int g,
                                              int attacker_wounds, int target_wounds) const {
    if (attacker.side == target.side) return {};
    const core::Unit& a = attacker.side == core::Side::A ? attacker : target;
    const core::Unit& b = attacker.side == core::Side::A ? target : attacker;
    const ChainTable* t = table_ ? table_.get() : table_for(a, b);
    if (t == nullptr) return {};
    const core::Side x = attacker.side;
    const double* p = t->pmf(x, melee ? kMelee : kHeavy, g, attacker_wounds, target_wounds);
    const int levels = t->level_of(core::opponent(x), target_wounds);
    return std::vector<double>(p, p + levels + 1);
}

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

// D-067: the army's fixed line (longer than a pair's gap and rooms): the real gap and the smaller room exactly,
// the rest of the line to the larger room; when the gap and both rooms don't fit, shared as line_pos shares them.
LinePos line_pos_army(int line, float bucket, bool engaged, float gap_mm, float room_a, float room_b) {
    LinePos p;
    p.engaged = engaged;
    const double total = static_cast<double>(line) * bucket;
    const double gap = engaged ? 0.0 : std::clamp(static_cast<double>(gap_mm), static_cast<double>(bucket), total);
    const double rest = total - gap;
    const double a = std::max(0.0, static_cast<double>(room_a));
    const double b = std::max(0.0, static_cast<double>(room_b));
    double ra = 0.0;
    if (a + b >= rest) {
        ra = a + b > 0.0 ? rest * a / (a + b) : rest / 2.0;
    } else {
        ra = a <= b ? a : rest - b;
    }
    p.g = gap / bucket;
    p.ra = ra / bucket;
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

struct ShotMemo;

// How a pair of units is read from a state: the duel's way (M44), or the army's (D-067).
struct PairMode {
    // D-067: the stage tests look at this pair's units only (a move or an attack in progress may be another
    // unit's), "engaged" means with each other, and positions go on the army's fixed line (line_pos_army).
    bool army = false;
    // D-067 charge_values: this unit of the side whose turn it is charges no more this phase.
    std::optional<core::UnitId> no_charge;
    // D-078: each shooting unit's shot options in the state evaluated, worked out once for every pair (the same options in
    // the same order, so the same plans); nullptr: worked out per pair, as before.
    ShotMemo* shots = nullptr;
};

// D-078: the shot options of each unit of the state evaluated (core::shot_options for each shooting type it is eligible
// for), computed at most once per unit for an army evaluation's pairs.
struct ShotMemo {
    const core::GameState* state = nullptr;
    std::vector<std::pair<core::UnitId, std::vector<analysis::ShotOptionSet>>> by_unit;
    const std::vector<analysis::ShotOptionSet>& of(const core::Unit& u) {
        for (const auto& [id, sets] : by_unit) {
            if (id == u.id) return sets;
        }
        by_unit.emplace_back(u.id, analysis::shot_option_sets(*state, u));
        return by_unit.back().second;
    }
    // D-083 part 4 (M82): each pair's best shooting (the shooter at the target, [HAZARDOUS] at Normal's rate), worked out
    // once: a group member's slice duel reads its pair's (the same plan, so the same values). A deque keeps the
    // references.
    struct Plan {
        core::UnitId shooter;
        core::UnitId target;
        std::optional<analysis::ShootingPlan> plan;
    };
    std::deque<Plan> plans;
    const std::optional<analysis::ShootingPlan>& best(analysis::OddsCache& odds, const core::Unit& ux,
                                                       const core::Unit& uy) {
        for (const Plan& p : plans) {
            if (p.shooter == ux.id && p.target == uy.id) return p.plan;
        }
        const double rate = normal::hazard_rate(*state, ux, uy);
        plans.push_back(Plan{ux.id, uy.id, analysis::best_shooting(odds, *state, ux.id, uy.id, rate, of(ux))});
        return plans.back().plan;
    }
};

// D-083 part 4 (M82): one unit of a pair read as a slice of itself (a group's target, each member's duel with its share):
// `wounds` in the chain (at most its own, at least 1), and `share` of its attacks in the half-turn in progress (its
// shooting's outcome with that probability, no damage otherwise: its share of the expected wounds).
struct Slice {
    core::Side side = core::Side::A;
    int wounds = 1;
    double share = 1.0;
};

LinePos pos_on_line(const PairMode& mode, int line, float bucket, bool engaged, float gap_mm, float room_a,
                    float room_b) {
    return mode.army ? line_pos_army(line, bucket, engaged, gap_mm, room_a, room_b)
                     : line_pos(line, bucket, engaged, gap_mm, room_a, room_b);
}

bool engaged_with(const core::GameState& state, const core::Unit& u, const core::Unit& other) {
    const std::vector<core::UnitId> e = core::engaged_enemy_units(state, u);
    return std::find(e.begin(), e.end(), other.id) != e.end();
}

// The pair's engagement as the mode reads it: the duel asks whether the unit whose turn it is is engaged at all.
bool pair_engaged(const PairMode& mode, const core::GameState& state, const core::Unit& ux, const core::Unit& uy) {
    return mode.army ? engaged_with(state, ux, uy) : core::is_engaged(state, ux);
}

// The value (side A's view) of the duel between `ua` (side A) and `ub` (side B) in `state`, by where the turn is
// (Evaluator::evaluate). Precondition: both have models, the half-turn is within the game.
// D-083 part 4: `slice`, one of the two read as a slice of itself (nullptr: both whole, as before M82).
Node pair_node(const ChainTable& t, analysis::OddsCache& odds, const core::GameState& state, const core::Unit& ua,
               const core::Unit& ub, const PairMode& mode, const Slice* slice = nullptr) {
    const float bucket = Evaluator::bucket_mm();
    const int h = half_turn_of(state);
    const core::Side x = core::turn_side(state);
    const core::Unit& ux = x == core::Side::A ? ua : ub;
    const core::Unit& uy = x == core::Side::A ? ub : ua;
    const int whole_a = std::clamp(wounds_of(ua), 1, t.side(core::Side::A).max_wounds);
    const int whole_b = std::clamp(wounds_of(ub), 1, t.side(core::Side::B).max_wounds);
    const bool slice_a = slice != nullptr && slice->side == core::Side::A;
    const bool slice_b = slice != nullptr && slice->side == core::Side::B;
    const int wa = slice_a ? std::clamp(slice->wounds, 1, whole_a) : whole_a;
    const int wb = slice_b ? std::clamp(slice->wounds, 1, whole_b) : whole_b;
    const float room_a = room_away(state, ua, centroid_of(ub));
    const float room_b = room_away(state, ub, centroid_of(ua));
    const bool engaged = pair_engaged(mode, state, ux, uy);
    const float gap = core::unit_distance(ua, ub).value_or(0.0f);
    const LinePos here = pos_on_line(mode, t.line(), bucket, engaged, gap, room_a, room_b);
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
        const LinePos p = pos_on_line(mode, t.line(), bucket, false, gap_mm, room_a, room_b);
        const Node hold = start_next(na, nb, p);
        const double pc = may_charge ? analysis::p_charge_roll_covers(gap_mm) : 0.0;
        if (!(pc > 0.0)) return hold;
        const Node charged = mix(fight_now(na, nb, xa ? std::min<double>(t.line(), p.ra + p.g) : p.ra), hold, pc);
        return t.better_for(x, charged, hold) ? charged : hold;
    };
    // The shooting of the side whose turn it is, from the engine (best_shooting), then the charge or the fight.
    const auto before_shoot = [&](bool may_charge) {
        // D-078: [HAZARDOUS] priced at Normal's rate (D-077; nothing changes without such a profile); the options shared.
        // D-083 part 4: the shared options' plan worked out once per pair (ShotMemo::best).
        std::optional<analysis::ShootingPlan> own;
        const std::optional<analysis::ShootingPlan>* plan = &own;
        if (mode.shots != nullptr) {
            plan = &mode.shots->best(odds, ux, uy);
        } else {
            own = analysis::best_shooting(odds, state, ux.id, uy.id, normal::hazard_rate(state, ux, uy));
        }
        // The target's whole wounds (the engine's outcome is over them) and, D-083 part 4, its slice's in the chain.
        const int wy = xa ? whole_b : whole_a;
        const int wy_chain = xa ? wb : wa;
        const bool target_slice = xa ? slice_b : slice_a;
        // D-083 part 4: a shooter read as a slice hits with its share (no damage otherwise).
        const double hit = (xa ? slice_a : slice_b) ? std::clamp(slice->share, 0.0, 1.0) : 1.0;
        std::vector<double> pmf(static_cast<std::size_t>(wy) + 1, 0.0);
        if (*plan && (*plan)->outcome.wounds_left_pmf.size() == pmf.size()) {
            pmf = (*plan)->outcome.wounds_left_pmf;
        } else {
            pmf.back() = 1.0;
        }
        const int wpm = std::max(1, t.side(core::opponent(x)).wounds_per_model);
        const int models = static_cast<int>(uy.models.size());
        Node out;
        for (int whole_left = 0; whole_left <= wy; ++whole_left) {
            double q = pmf[static_cast<std::size_t>(whole_left)];
            if (hit < 1.0) {
                q *= hit;
                if (whole_left == wy) q += 1.0 - hit;
            }
            if (q == 0.0) continue;
            // D-083 part 4: a target read as a slice loses what the whole unit would.
            const int left = target_slice ? std::max(0, wy_chain - (wy - whole_left)) : whole_left;
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
            const int destroyed = std::clamp(models - (whole_left + wpm - 1) / wpm, 0, std::max(0, models - 1));
            const float gap_k = analysis::kth_closest_model_distance(ux, uy, destroyed + 1).value_or(gap);
            add(out, after_shoot(na, nb, gap_k, may_charge), q);
        }
        return out;
    };

    const core::Stage st = state.flow.stage;
    const core::Phase ph = core::phase_of(st);
    const core::UnitTurnFlags& tf = ux.turn_flags;
    const core::UnitPhaseFlags& pf = ux.phase_flags;
    // D-067: no further charge by this unit (charge_values' holding).
    const bool may_charge = !tf.advanced && !tf.fell_back && !(mode.no_charge && *mode.no_charge == ux.id);
    // The move and the attack in progress, as the stage tests see them: the duel's (its only unit's), or (D-067)
    // this pair's unit's own.
    const bool moving = mode.army ? state.flow.move && state.flow.move->unit == ux.id : state.flow.move.has_value();
    const bool attacking =
        mode.army ? state.flow.attack && state.flow.attack->attacker == ux.id : state.flow.attack.has_value();
    Node n;
    if (ph == core::Phase::None || ph == core::Phase::Command ||
        (ph == core::Phase::Movement && !pf.selected_to_move && !moving)) {
        if (st == core::Stage::EndOfTurn || st == core::Stage::EndOfBattleRound) {
            n = start_next(wa, wb, here);
        } else {
            n = interpolate(t.line(), here, [&](int g, int ra) { return t.start(h, wa, wb, g, ra); });
        }
    } else if (ph == core::Phase::Movement || (ph == core::Phase::Shooting && !pf.selected_to_shoot && !attacking &&
                                               st != core::Stage::EndOfShootingPhase)) {
        if (tf.fell_back || (moving && state.flow.move->type == core::MoveType::FallBack)) {
            n = start_next(wa, wb, here);  // 09.07: no shooting, no charge
        } else {
            const bool advancing = tf.advanced || (moving && state.flow.move->type == core::MoveType::Advance);
            n = before_shoot(may_charge && !advancing);
        }
    } else if (ph == core::Phase::Shooting) {
        n = engaged ? fight_now(wa, wb, here.ra) : after_shoot(wa, wb, gap, may_charge);
    } else if (ph == core::Phase::Charge) {
        if (tf.made_charge_move || (engaged && st == core::Stage::EndOfChargePhase)) {
            // D-067: a charge move into another unit leaves this pair apart for the rest of the turn.
            n = mode.army && !engaged ? start_next(wa, wb, here) : fight_now(wa, wb, here.ra);
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
    return n;
}

// The chain's own choice for the side whose turn it is in the duel of `ua` and `ub` (Evaluator::turn_choice).
// Precondition: both have models, the half-turn is within the game.
Evaluator::TurnChoice pair_turn_choice(const ChainTable& t, const core::GameState& state, const core::Unit& ua,
                                       const core::Unit& ub, const PairMode& mode) {
    Evaluator::TurnChoice out;
    const float bucket = Evaluator::bucket_mm();
    const int h = half_turn_of(state);
    const core::Side x = core::turn_side(state);
    const bool xa = x == core::Side::A;
    // D-078: a coarse side's wounds at the nearest grid level (a choice, not a value: nothing to interpolate).
    const int wa = std::max(1, t.snap(core::Side::A, std::clamp(wounds_of(ua), 1, t.side(core::Side::A).max_wounds)));
    const int wb = std::max(1, t.snap(core::Side::B, std::clamp(wounds_of(ub), 1, t.side(core::Side::B).max_wounds)));
    const core::Unit& ux = xa ? ua : ub;
    const bool engaged = pair_engaged(mode, state, ux, xa ? ub : ua);
    const float room_a = room_away(state, ua, centroid_of(ub));
    const float room_b = room_away(state, ub, centroid_of(ua));
    const float gap = core::unit_distance(ua, ub).value_or(0.0f);
    const LinePos here = pos_on_line(mode, t.line(), bucket, engaged, gap, room_a, room_b);
    const SideSpec& sx = t.side(x);
    const double room = xa ? here.ra : (engaged ? t.line() - here.ra : t.line() - here.g - here.ra);
    if (engaged) {
        // Stay or Fall Back, as the chain's start node does.
        const auto stay = [&] {
            const double* cq = t.pmf(x, kCloseQuarters, 1, xa ? wa : wb, xa ? wb : wa);
            const core::Side y = core::opponent(x);
            const int ty = t.level_of(y, xa ? wb : wa);
            Node s;
            for (int left = 0; left <= ty; ++left) {
                if (cq[left] == 0.0) continue;
                if (left == 0) {
                    add(s, t.win(x, h, wa, wb), cq[left]);
                    continue;
                }
                const int lw = t.level_wounds(y, left);  // D-078: the grid level's wounds (an exact side's: left)
                add(s, interpolate(t.line(), here,
                                   [&](int, int r) { return t.fight(h, xa ? wa : lw, xa ? lw : wb, r); }),
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
    const auto kind_for = [bucket](double d) {
        return static_cast<float>(std::fabs(d)) * bucket <= kHeavyMoveMm + core::EPS_MM ? kHeavy : kMoved;
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
            for (std::size_t f = 1; f <= 6; ++f) out.advance_towards_mm[f] = static_cast<float>(best_by[f]) * bucket;
            return out;
        }
    }
    out.towards_mm = static_cast<float>(best_d) * bucket;
    return out;
}

// The pair's hold and fight nodes for a charge by the side whose turn it is (Evaluator::charge_values).
struct PairCharge {
    Node hold;
    Node fight;
};
// D-083 part 4: `slice`, one of the two read as a slice of itself (nullptr: both whole).
PairCharge pair_charge(const ChainTable& t, const core::GameState& state, const core::Unit& ua, const core::Unit& ub,
                       const PairMode& mode, const Slice* slice = nullptr) {
    const float bucket = Evaluator::bucket_mm();
    const int h = half_turn_of(state);
    const core::Side x = core::turn_side(state);
    const bool xa = x == core::Side::A;
    int wa = std::clamp(wounds_of(ua), 1, t.side(core::Side::A).max_wounds);
    int wb = std::clamp(wounds_of(ub), 1, t.side(core::Side::B).max_wounds);
    if (slice != nullptr) {
        int& w = slice->side == core::Side::A ? wa : wb;
        w = std::clamp(slice->wounds, 1, w);
    }
    const float room_a = room_away(state, ua, centroid_of(ub));
    const float room_b = room_away(state, ub, centroid_of(ua));
    const float gap = core::unit_distance(ua, ub).value_or(0.0f);
    const LinePos here = pos_on_line(mode, t.line(), bucket, false, gap, room_a, room_b);
    PairCharge out;
    out.hold = interpolate(t.line(), here, [&](int g, int ra) { return t.end_turn(h, wa, wb, g, ra); });
    const double ra_after = xa ? std::min<double>(t.line(), here.ra + here.g) : here.ra;
    out.fight =
        interpolate(t.line(), LinePos{true, 0.0, ra_after}, [&](int, int r) { return t.fight(h, wa, wb, r); });
    return out;
}

}  // namespace

Evaluation Evaluator::at(int half_turn, std::array<int, 2> wounds, float gap_mm, std::array<float, 2> rooms_mm) const {
    const ChainTable* tp = table_ ? table_.get() : (army_tables_.empty() ? nullptr : army_tables_.front().table.get());
    if (tp == nullptr) return {};
    const ChainTable& t = *tp;
    const LinePos p = army_ && !duel_reading_
                          ? line_pos_army(t.line(), kBucketMm, !(gap_mm > core::kEngagementRangeMm + core::EPS_MM),
                                            gap_mm, rooms_mm[0], rooms_mm[1])
                            : line_pos(t.line(), kBucketMm, !(gap_mm > core::kEngagementRangeMm + core::EPS_MM),
                                       gap_mm, rooms_mm[0], rooms_mm[1]);
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

// D-067: the army's end, exact: the game is over, a side has no models left (it lost, now; D-007), or every half-turn
// is played (a draw). Its score as a duel's: a win's tempo, then material over the whole army (each side's wounds
// left as a share of its starting wounds).
std::optional<Evaluation> Evaluator::army_terminal(const core::GameState& state) const {
    std::optional<core::Side> winner;
    bool over = false;
    std::array<bool, 2> alive{};
    std::array<double, 2> left{};
    for (const core::Unit& u : state.units) {
        if (!u.models.empty()) alive[si(u.side)] = true;
        left[si(u.side)] += wounds_of(u);
    }
    const std::array<double, 2>& start = start_wounds_;
    const int half_turns = 2 * state.battle_rounds;
    const int h = std::min(half_turn_of(state), half_turns);
    if (state.result) {
        over = true;
        winner = state.result->winner;  // D-076: the engine's result, the points decision included
    } else if (!alive[0] || !alive[1]) {
        over = true;
        if (alive[0] != alive[1]) winner = alive[0] ? core::Side::A : core::Side::B;
    } else if (h >= half_turns) {
        over = true;
        if (state.result_rule == core::ResultRule::PointsDestroyed) {
            // D-078: every half-turn played: decided on points destroyed exactly as the engine decides it (D-076).
            const core::PointsTally tally = core::points_destroyed(state);
            if (tally.destroyed[0] != tally.destroyed[1]) {
                winner = tally.destroyed[0] > tally.destroyed[1] ? core::Side::A : core::Side::B;
            }
        }
    }
    if (!over) return std::nullopt;
    Evaluation e;
    double j = weights_.material * (left[0] / std::max(1.0, start[0]) - left[1] / std::max(1.0, start[1]));
    const double tempo =
        weights_.tempo * static_cast<double>(half_turns - h) / static_cast<double>(std::max(1, half_turns));
    if (!winner) {
        e.p_draw = 1.0;
    } else {
        j += *winner == core::Side::A ? 1.0 + tempo : -1.0 - tempo;
        if (*winner == side_) {
            e.p_win = 1.0;
            e.v = 1.0;
        } else {
            e.p_loss = 1.0;
            e.v = -1.0;
        }
    }
    e.score = side_ == core::Side::A ? j : -j;
    return e;
}

const ChainTable* Evaluator::table_for(const core::Unit& a, const core::Unit& b) const {
    const auto key_of = [&](const core::Unit& u) {
        for (const auto& [id, k] : unit_keys_) {
            if (id == u.id) return k;
        }
        return side_key(u);  // a unit the root didn't have alive (not in a real game)
    };
    const std::uint64_t ka = key_of(a);
    const std::uint64_t kb = key_of(b);
    for (const PairTable& p : army_tables_) {
        if (p.key_a == ka && p.key_b == kb) return p.table.get();
    }
    return nullptr;
}

const core::Unit* Evaluator::main_enemy(const core::GameState& state, const core::Unit& unit) {
    // D-067: an enemy unit it is engaged with (the closest of them, a tie to the first in unit order), else the
    // closest enemy unit (01.04, D-010).
    const core::Unit* best = nullptr;
    float best_gap = 0.0f;
    for (const core::UnitId id : core::engaged_enemy_units(state, unit)) {
        const core::Unit* e = state.find_unit(id);
        if (e == nullptr || e->models.empty()) continue;
        const float g = core::unit_distance(unit, *e).value_or(0.0f);
        if (best == nullptr || g < best_gap) {
            best = e;
            best_gap = g;
        }
    }
    return best != nullptr ? best : normal::closest_enemy(state, unit);
}

Evaluation Evaluator::army_evaluate(const core::GameState& state, std::optional<core::UnitId> no_charge,
                                    const Charged* charged, std::vector<UnitFate>* fates, Attention* attention) const {
    if (const std::optional<Evaluation> e = army_terminal(state)) return *e;
    ShotMemo shots;
    shots.state = &state;
    PairMode mode;
    mode.army = !duel_reading_;
    mode.no_charge = no_charge;
    mode.shots = &shots;
    const int h = half_turn_of(state);
    const int half_turns = 2 * state.battle_rounds;
    std::vector<const core::Unit*> alive;
    for (const core::Unit& u : state.units) {
        if (!u.models.empty()) alive.push_back(&u);
    }
    // A pair's duel (side A's view) starting `delay` half-turns from now: now, the half-turn in progress as the duel
    // reads it (each pair once: two units that are each other's first target share it); later, the chain from the
    // start of that half-turn, the pair `gap_mm` apart (the attacker has moved meanwhile).
    struct Cached {
        core::UnitId a;
        core::UnitId b;
        Node n;
    };
    std::vector<Cached> cached;
    const auto node_at = [&](const core::Unit& a, const core::Unit& b, int delay,
                             float gap_mm) -> std::optional<Node> {
        if (delay == 0) {
            for (const Cached& c : cached) {
                if (c.a == a.id && c.b == b.id) return c.n;
            }
        }
        const ChainTable* t = table_for(a, b);
        if (t == nullptr) return std::nullopt;
        Node n;
        if (delay == 0) {
            n = pair_node(*t, odds_, state, a, b, mode);
            cached.push_back(Cached{a.id, b.id, n});
        } else {
            const int wa = std::clamp(wounds_of(a), 1, t->side(core::Side::A).max_wounds);
            const int wb = std::clamp(wounds_of(b), 1, t->side(core::Side::B).max_wounds);
            if (h + delay >= t->half_turns()) {
                n = t->draw(wa, wb);
            } else {
                const bool engaged = !(gap_mm > core::kEngagementRangeMm + core::EPS_MM);
                const float room_a = room_away(state, a, centroid_of(b));
                const float room_b = room_away(state, b, centroid_of(a));
                const LinePos here = line_pos_army(t->line(), kBucketMm, engaged, gap_mm, room_a, room_b);
                n = interpolate(t->line(), here, [&](int g, int ra) { return t->start(h + delay, wa, wb, g, ra); });
            }
        }
        return n;
    };
    // charge_values: the charged pair engaged, the charger striking first, each the other's first target.
    std::optional<Node> charged_node;
    if (charged != nullptr) {
        const core::Unit* cu = state.find_unit(charged->unit);
        const core::Unit* ct = state.find_unit(charged->target);
        if (cu != nullptr && ct != nullptr && !cu->models.empty() && !ct->models.empty() && cu->side != ct->side) {
            const core::Unit& a = cu->side == core::Side::A ? *cu : *ct;
            const core::Unit& b = cu->side == core::Side::A ? *ct : *cu;
            if (const ChainTable* t = table_for(a, b)) charged_node = pair_charge(*t, state, a, b, mode).fight;
        }
    }
    const auto is_charged_pair = [&](const core::Unit& x, const core::Unit& y) {
        return charged_node && ((x.id == charged->unit && y.id == charged->target) ||
                                (x.id == charged->target && y.id == charged->unit));
    };
    // D-083 part 4 (M82): our plan's groups at this state: a group whose target has models and two or more of whose
    // members have models. A member's queue starts with the target unless it is engaged with another enemy unit and not
    // with the target (an engaged unit deals with what it fights first, as its main enemy).
    struct Group {
        const core::Unit* target = nullptr;
        std::vector<const core::Unit*> members;  // with models, in the plan's (unit) order
    };
    std::vector<Group> groups;
    std::vector<std::pair<const core::Unit*, const core::Unit*>> leads;  // (member, its target) when it leads its queue
    const normal::ArmyPlan& ours = plan();
    if (ours.active && ours.side == side_) {
        for (const normal::PlanGroup& pg : ours.groups) {
            const core::Unit* target = state.find_unit(pg.target);
            if (target == nullptr || target->models.empty()) continue;
            Group g{target, {}};
            for (const normal::PlanMember& m : pg.members) {
                const core::Unit* u = state.find_unit(m.unit);
                if (u != nullptr && !u->models.empty() && u->side != target->side) g.members.push_back(u);
            }
            if (g.members.size() < 2) continue;
            for (const core::Unit* u : g.members) {
                if (!core::is_engaged(state, *u) || engaged_with(state, *u, *target)) leads.emplace_back(u, target);
            }
            groups.push_back(std::move(g));
        }
    }
    const auto focus_of = [&](const core::Unit* u) -> const core::Unit* {
        for (const auto& [m, t] : leads) {
            if (m == u) return t;
        }
        return nullptr;
    };
    // Attention: each unit deals with the enemy units closest to it first (D-010; engaged ones are the closest), so
    // its duel with a further one starts once it has wiped out the closer ones (ChainTable::wipe_half_turns).
    // A unit survives only if it survives every enemy unit's duel (the fates of the duels are independent).
    struct Fate {
        const core::Unit* unit;
        double survive = 1.0;
        const core::Unit* main = nullptr;  // its main enemy, whose duel gives its tempo and material terms
        std::optional<Node> main_node;
    };
    std::vector<Fate> fate;
    for (const core::Unit* u : alive) fate.push_back(Fate{u, 1.0, main_enemy(state, *u), std::nullopt});
    const auto fate_of = [&](const core::Unit* u) -> Fate& {
        for (Fate& f : fate) {
            if (f.unit == u) return f;
        }
        return fate.front();
    };
    // Each attacker's queue: the enemy units, closest first (the charged pair's target first). D-083 part 4: a group
    // member that leads with its group's target has it next (then the others by distance).
    struct Entry {
        float gap;  // the gap now (-1: the charged pair's target)
        const core::Unit* unit;
        float key;  // the queue's order
    };
    struct Queue {
        const core::Unit* attacker;
        std::vector<Entry> targets;
        std::vector<int> start;      // [k]: the attacker's turn (0: its next) in which it starts on targets[k]
        std::vector<double> dealt;   // [k]: D-083 part 4, the expected wounds it takes from targets[k] in the timeline
        const core::Unit* focus = nullptr;
    };
    constexpr float kFocusKey = -0.5f;  // after the charged pair's target (-1), before every real gap
    std::vector<Queue> queues;
    for (const core::Unit* e : alive) {
        Queue q{e, {}, {}, {}, focus_of(e)};
        for (const core::Unit* v : alive) {
            if (v->side == e->side) continue;
            const float gap = is_charged_pair(*e, *v) ? -1.0f : core::unit_distance(*e, *v).value_or(0.0f);
            q.targets.push_back(Entry{gap, v, v == q.focus && gap >= 0.0f ? kFocusKey : gap});
        }
        std::stable_sort(q.targets.begin(), q.targets.end(),
                         [](const Entry& x, const Entry& y) { return x.key < y.key; });
        q.start.assign(q.targets.size(), std::numeric_limits<int>::max());
        q.dealt.assign(q.targets.size(), 0.0);
        queues.push_back(std::move(q));
    }
    // The attention timeline, each side's attackers in their own turns: every attacker works on its closest target
    // still standing, at its pace (ChainTable::volley_wounds), once it is in reach (its M and its longest usable
    // range, or a charge, in each of its turns: a turn spent closing takes nothing), the units in unit order within a
    // turn; what a volley has left when its target falls goes on to the next one, which it starts this turn if at
    // least half of the volley is left (else next turn). So the attackers share the work: a unit three squads stand
    // in front of is reached once they have wiped out what is in front of it together, not each on its own. (The
    // duel that then starts models the approach to that unit itself.) D-083 part 4: a group's members, which start on
    // its target, wipe it out at their combined pace, and so start on their next targets sooner.
    const int turns_left = (std::max(0, half_turns - h) + 1) / 2 + 1;
    const auto arrival = [&](const Queue& q, std::size_t k) {
        const analysis::ThreatRanges r = analysis::threat_ranges(*q.attacker);
        const float reach = std::max(r.shooting_reach_mm(), r.move_mm + core::inches_to_mm(7.0f));
        const float gap = std::max(0.0f, q.targets[k].gap);
        if (!(gap > reach) || !(r.move_mm > 0.0f)) return 0;
        return static_cast<int>(std::ceil((gap - reach) / r.move_mm - 1e-6f));
    };
    std::vector<std::pair<const core::Unit*, std::optional<int>>> wiped_turn;  // D-083 part 4 (diagnostics)
    for (const core::Side s : {core::Side::A, core::Side::B}) {
        std::vector<std::pair<const core::Unit*, double>> left;  // wounds left of the side's targets
        for (const core::Unit* v : alive) {
            if (v->side != s) left.emplace_back(v, static_cast<double>(wounds_of(*v)));
        }
        const auto wounds_left = [&](const core::Unit* v) -> double& {
            for (auto& [u, w] : left) {
                if (u == v) return w;
            }
            return left.front().second;
        };
        const std::size_t first_wipe = wiped_turn.size();
        if (attention != nullptr) {
            for (const auto& [v, w] : left) wiped_turn.emplace_back(v, std::nullopt);
        }
        std::vector<std::size_t> at(queues.size(), 0);  // each attacker's position in its queue
        for (std::size_t qi = 0; qi < queues.size(); ++qi) {
            if (queues[qi].attacker->side == s && !queues[qi].start.empty()) queues[qi].start[0] = 0;
        }
        for (int turn = 0; turn < turns_left; ++turn) {
            for (std::size_t qi = 0; qi < queues.size(); ++qi) {
                Queue& q = queues[qi];
                if (q.attacker->side != s) continue;
                double volley = 1.0;  // the share of this turn's volley still to spend
                while (volley > 1e-9 && at[qi] < q.targets.size()) {
                    const core::Unit* v = q.targets[at[qi]].unit;
                    double& w = wounds_left(v);
                    if (w <= 1e-9) {  // wiped out already (by another attacker)
                        ++at[qi];
                        if (at[qi] < q.targets.size()) q.start[at[qi]] = volley >= 0.5 ? turn : turn + 1;
                        continue;
                    }
                    if (turn < arrival(q, at[qi])) break;  // still closing in
                    const core::Unit& a = s == core::Side::A ? *q.attacker : *v;
                    const core::Unit& b = s == core::Side::A ? *v : *q.attacker;
                    const ChainTable* t = table_for(a, b);
                    const double pace = t != nullptr ? t->volley_wounds(s, wounds_of(*q.attacker)) : 0.0;
                    if (!(pace > 1e-9)) break;
                    const double spend = std::min(volley, w / pace);
                    w -= spend * pace;
                    volley -= spend;
                    q.dealt[at[qi]] += spend * pace;
                    if (attention != nullptr && w <= 1e-9) {
                        for (std::size_t i = first_wipe; i < wiped_turn.size(); ++i) {
                            if (wiped_turn[i].first == v && !wiped_turn[i].second) wiped_turn[i].second = turn;
                        }
                    }
                }
            }
        }
    }
    // D-083 part 4: each group's slices: the members whose queue starts with the target and that take wounds from it in
    // the timeline, each with its share of what they take. With two or more, they fight it together: each member's duel
    // is with its slice (the target's wounds x its share), and the target's P(wiped) by them is the share-weighted
    // P(each slice is wiped), in place of those members' own duels with the whole target.
    struct Slices {
        const core::Unit* target = nullptr;
        std::vector<std::pair<std::size_t, double>> members;  // (queue index, share)
        double p_apart_survive = 1.0;  // (diagnostics) the target surviving those members' own duels
    };
    std::vector<Slices> slices;
    std::vector<bool> absorbed(queues.size(), false);  // the queue's first duel is one of a group's slices
    for (const Group& g : groups) {
        Slices sl{g.target, {}, 1.0};
        double total = 0.0;
        for (std::size_t qi = 0; qi < queues.size(); ++qi) {
            const Queue& q = queues[qi];
            if (std::find(g.members.begin(), g.members.end(), q.attacker) == g.members.end()) continue;
            if (q.targets.empty() || q.targets.front().unit != g.target || !(q.dealt.front() > 1e-9)) continue;
            const core::Unit& a = q.attacker->side == core::Side::A ? *q.attacker : *g.target;
            const core::Unit& b = q.attacker->side == core::Side::A ? *g.target : *q.attacker;
            if (table_for(a, b) == nullptr) continue;
            sl.members.emplace_back(qi, q.dealt.front());
            total += q.dealt.front();
        }
        if (sl.members.size() < 2 || !(total > 0.0)) continue;
        for (auto& [qi, share] : sl.members) {
            share /= total;
            absorbed[qi] = true;
        }
        slices.push_back(std::move(sl));
    }
    for (std::size_t qi = 0; qi < queues.size(); ++qi) {
        Queue& q = queues[qi];
        const core::Unit* e = q.attacker;
        // While it deals with the closer units the attacker closes on the first of them, its M in each of its turns
        // (never past it): a later duel starts from where that leaves it (centre to centre, as far as the gap goes).
        const core::Position from = centroid_of(*e);
        core::Position towards = from;
        if (!q.targets.empty()) towards = centroid_of(*q.targets.front().unit);
        const float span = std::hypot(towards.x - from.x, towards.y - from.y);
        const float move_mm = analysis::threat_ranges(*e).move_mm;
        for (std::size_t k = 0; k < q.targets.size(); ++k) {
            const float gap = q.targets[k].gap;
            const core::Unit* v = q.targets[k].unit;
            const int delay = q.start[k] >= turns_left ? half_turns : std::min(2 * q.start[k], half_turns);
            const core::Unit& a = e->side == core::Side::A ? *e : *v;
            const core::Unit& b = e->side == core::Side::A ? *v : *e;
            float gap_then = std::max(0.0f, gap);
            if (delay > 0 && span > 0.0f) {
                const float travel = std::min(move_mm * static_cast<float>(delay / 2),
                                              std::max(0.0f, q.targets.front().gap));
                const core::Position to{from.x + (towards.x - from.x) / span * travel,
                                        from.y + (towards.y - from.y) / span * travel, 0.0f};
                const core::Position cv = centroid_of(*v);
                const float before = std::hypot(cv.x - from.x, cv.y - from.y);
                const float after = std::hypot(cv.x - to.x, cv.y - to.y);
                gap_then = std::max(0.0f, gap_then + (after - before));
            }
            std::optional<Node> n = is_charged_pair(*e, *v) ? charged_node : node_at(a, b, delay, gap_then);
            if (!n) continue;
            Fate& f = fate_of(v);
            const double p_wipe = v->side == core::Side::A ? n->pl : n->pw;
            if (k == 0 && absorbed[qi]) {
                for (Slices& sl : slices) {
                    if (sl.target == v) sl.p_apart_survive *= 1.0 - p_wipe;
                }
            } else {
                f.survive *= 1.0 - p_wipe;
            }
            if (f.main == e) f.main_node = n;
        }
    }
    // D-083 part 4: each group's target, by its slices. A member's slice of wounds s (at least 1) is read between the
    // whole wounds below and above it (the chain's wounds are whole).
    std::vector<GroupPace> paces;
    for (const Slices& sl : slices) {
        const core::Unit& v = *sl.target;
        GroupPace pace{v.id, {}, {}, 0.0, 1.0 - sl.p_apart_survive};
        double p_group = 0.0;
        for (const auto& [qi, member_share] : sl.members) {
            const double share = member_share;  // (a plain variable: the lambda below captures it)
            const core::Unit& m = *queues[qi].attacker;
            const core::Unit& a = m.side == core::Side::A ? m : v;
            const core::Unit& b = m.side == core::Side::A ? v : m;
            const ChainTable* t = table_for(a, b);
            const int whole = std::clamp(wounds_of(v), 1, t->side(v.side).max_wounds);
            const double s_w = std::max(1.0, share * static_cast<double>(whole));
            const int lo = std::min(whole, static_cast<int>(std::floor(s_w)));
            const double up = lo < whole ? s_w - static_cast<double>(lo) : 0.0;
            const auto wiped_at = [&](int wounds) {
                const Slice slice{v.side, wounds, share};
                const Node n = is_charged_pair(m, v) ? pair_charge(*t, state, a, b, mode, &slice).fight
                                                     : pair_node(*t, odds_, state, a, b, mode, &slice);
                return v.side == core::Side::A ? n.pl : n.pw;
            };
            double p = wiped_at(lo);
            if (up > 1e-9) p = (1.0 - up) * p + up * wiped_at(lo + 1);
            p_group += share * p;
            pace.members.push_back(m.id);
            pace.shares.push_back(share);
        }
        pace.p_wiped = std::clamp(p_group, 0.0, 1.0);
        fate_of(sl.target).survive *= 1.0 - pace.p_wiped;
        paces.push_back(std::move(pace));
    }
    if (attention != nullptr) {
        for (const Queue& q : queues) {
            AttentionQueue aq{q.attacker->id, q.focus != nullptr ? std::optional<core::UnitId>(q.focus->id) : std::nullopt,
                              {}};
            for (std::size_t k = 0; k < q.targets.size(); ++k) {
                const std::optional<int> st = q.start[k] >= turns_left ? std::nullopt : std::optional<int>(q.start[k]);
                aq.targets.push_back(AttentionTarget{q.targets[k].unit->id, st, q.dealt[k]});
            }
            attention->queues.push_back(std::move(aq));
        }
        for (const core::Unit* u : alive) {
            for (const auto& [v, turn] : wiped_turn) {
                if (v == u) attention->wiped_turn.emplace_back(u->id, turn);
            }
        }
        // The groups in unit order of their targets (their members are in unit order: the queues are).
        const auto order = [&](core::UnitId id) {
            for (std::size_t i = 0; i < alive.size(); ++i) {
                if (alive[i]->id == id) return i;
            }
            return alive.size();
        };
        std::stable_sort(paces.begin(), paces.end(),
                         [&](const GroupPace& x, const GroupPace& y) { return order(x.target) < order(y.target); });
        attention->groups = std::move(paces);
    }
    double p_a = 1.0;  // P(side A wins): every unit of side B wiped out
    double p_b = 1.0;
    // D-007: a side's win or loss comes when its last unit falls, so the tempo and material terms are those of each
    // side's longest-lasting unit: the one whose main duel is best for its own side (side A's view: the highest j for
    // an A unit, the lowest for a B unit; a tie to the first in unit order), the mean of the two sides'.
    std::array<std::optional<Node>, 2> last;
    for (const Fate& f : fate) {
        const double wiped = 1.0 - f.survive;
        (f.unit->side == core::Side::A ? p_b : p_a) *= wiped;
        if (f.main_node) {
            std::optional<Node>& l = last[si(f.unit->side)];
            const bool better =
                !l || (f.unit->side == core::Side::A ? f.main_node->j > l->j : f.main_node->j < l->j);
            if (better) l = f.main_node;
        }
        if (fates != nullptr && f.main != nullptr) fates->push_back(UnitFate{f.unit->id, f.main->id, wiped});
    }
    if (!last[0] || !last[1]) return {};
    const auto terms = [](const Node& n) { return n.j - (n.pw - n.pl); };
    const double total = p_a + p_b;
    if (total > 1.0) {
        p_a /= total;
        p_b /= total;
    }
    Node army;
    army.pw = p_a;
    army.pl = p_b;
    if (state.result_rule == core::ResultRule::PointsDestroyed) {
        // D-078: the points tiebreak (D-076). With no wipe-out the game is decided on points destroyed: each unit is
        // destroyed by the end with its fate above (independently), else keeps its losses now. Mean and variance of the
        // margin (side A's points destroyed minus side B's) in the engine's units, then T = P(A ahead) - P(B ahead).
        const core::PointsTally tally = core::points_destroyed(state);
        double mu = static_cast<double>(tally.destroyed[0]) - static_cast<double>(tally.destroyed[1]);
        double variance = 0.0;
        for (const Fate& f : fate) {
            const double inc = static_cast<double>(points_increment(*f.unit, tally.denominator));
            if (!(inc > 0.0)) continue;
            const double p = 1.0 - f.survive;
            mu += f.unit->side == core::Side::B ? p * inc : -p * inc;  // destroying a side B unit adds to A's tally
            variance += p * (1.0 - p) * inc * inc;
        }
        const double rest = std::max(0.0, 1.0 - p_a - p_b);
        const bool tie = !(variance > 0.0) && mu == 0.0;
        if (!tie) {
            const double lead = points_lead(mu, variance);
            army.pw += rest * 0.5 * (1.0 + lead);
            army.pl += rest * 0.5 * (1.0 - lead);
        }
    }
    army.j = (army.pw - army.pl) + 0.5 * (terms(*last[0]) + terms(*last[1]));
    return to_eval(army, side_);
}

std::vector<UnitFate> Evaluator::fates(const core::GameState& state) const {
    std::vector<UnitFate> out;
    if (army_) (void)army_evaluate(state, std::nullopt, nullptr, &out);
    return out;
}

Attention Evaluator::attention(const core::GameState& state) const {
    Attention out;
    if (army_) (void)army_evaluate(state, std::nullopt, nullptr, nullptr, &out);
    return out;
}

Evaluation Evaluator::at_turn_start(const core::GameState& state) const {
    if (army_) {
        // The army's value with every pair at the start of its half-turn (the chain alone).
        if (const std::optional<Evaluation> e = army_terminal(state)) return *e;
        core::GameState start = state;
        start.flow.stage = core::Stage::StartOfTurn;
        start.flow.move.reset();
        start.flow.attack.reset();
        for (core::Unit& u : start.units) {
            u.turn_flags = {};
            u.phase_flags = {};
        }
        return army_evaluate(start);
    }
    if (!table_) return {};
    if (const std::optional<Evaluation> e = terminal(state)) return *e;
    const core::Unit* ua = state.find_unit(duel_.units[0]);
    const core::Unit* ub = state.find_unit(duel_.units[1]);
    const float gap = core::unit_distance(*ua, *ub).value_or(0.0f);
    return at(half_turn_of(state), {wounds_of(*ua), wounds_of(*ub)}, gap,
              {room_away(state, *ua, centroid_of(*ub)), room_away(state, *ub, centroid_of(*ua))});
}

Evaluation Evaluator::evaluate(const core::GameState& state) const {
    if (army_) return army_evaluate(state);
    if (!table_) return {};
    if (const std::optional<Evaluation> e = terminal(state)) return *e;
    const ChainTable& t = *table_;
    const core::Unit* ua = state.find_unit(duel_.units[0]);
    const core::Unit* ub = state.find_unit(duel_.units[1]);
    const int h = half_turn_of(state);
    if (h >= t.half_turns()) return to_eval(t.draw(wounds_of(*ua), wounds_of(*ub)), side_);
    return to_eval(pair_node(t, odds_, state, *ua, *ub, PairMode{}), side_);
}

Evaluator::TurnChoice Evaluator::turn_choice(const core::GameState& state) const {
    if (army_ || !table_ || state.result) return {};
    const ChainTable& t = *table_;
    const core::Unit* ua = state.find_unit(duel_.units[0]);
    const core::Unit* ub = state.find_unit(duel_.units[1]);
    if (ua == nullptr || ub == nullptr || ua->models.empty() || ub->models.empty()) return {};
    if (half_turn_of(state) >= t.half_turns()) return {};
    return pair_turn_choice(t, state, *ua, *ub, PairMode{});
}

Evaluator::TurnChoice Evaluator::turn_choice(const core::GameState& state, core::UnitId unit,
                                             std::optional<core::UnitId> target) const {
    if (!army_) return turn_choice(state);
    if (state.result || half_turn_of(state) >= 2 * state.battle_rounds) return {};
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr || u->models.empty()) return {};
    const core::Unit* e = target ? state.find_unit(*target) : main_enemy(state, *u);
    if (e == nullptr || e->models.empty() || e->side == u->side) return {};
    const core::Unit& a = u->side == core::Side::A ? *u : *e;
    const core::Unit& b = u->side == core::Side::A ? *e : *u;
    const ChainTable* t = table_for(a, b);
    if (t == nullptr) return {};
    PairMode mode;
    mode.army = !duel_reading_;
    return pair_turn_choice(*t, state, a, b, mode);
}

Evaluator::ChargeValues Evaluator::charge_values(const core::GameState& state, double p_success) const {
    ChargeValues out;
    if (army_ || !table_ || state.result) return out;
    const ChainTable& t = *table_;
    const core::Unit* ua = state.find_unit(duel_.units[0]);
    const core::Unit* ub = state.find_unit(duel_.units[1]);
    if (ua == nullptr || ub == nullptr || ua->models.empty() || ub->models.empty()) return out;
    if (half_turn_of(state) >= t.half_turns()) return out;
    const core::Side x = core::turn_side(state);
    const PairCharge pc = pair_charge(t, state, *ua, *ub, PairMode{});
    const double p = std::clamp(p_success, 0.0, 1.0);
    const Node charged = mix(pc.fight, pc.hold, p);
    out.hold = to_eval(pc.hold, side_);
    out.fight = to_eval(pc.fight, side_);
    out.charge = to_eval(charged, side_);
    out.wanted = p > 0.0 && t.better_for(x, charged, pc.hold);
    return out;
}

Evaluator::ChargeValues Evaluator::charge_values(const core::GameState& state, core::UnitId unit, core::UnitId target,
                                                 double p_success) const {
    if (!army_) return charge_values(state, p_success);
    ChargeValues out;
    if (state.result || half_turn_of(state) >= 2 * state.battle_rounds) return out;
    const core::Unit* u = state.find_unit(unit);
    const core::Unit* e = state.find_unit(target);
    if (u == nullptr || e == nullptr || u->models.empty() || e->models.empty() || u->side == e->side) return out;
    const double p = std::clamp(p_success, 0.0, 1.0);
    const Charged charged{unit, target};
    out.hold = army_evaluate(state, unit);
    out.fight = army_evaluate(state, unit, &charged);
    const auto blend = [p](double f, double h) { return p * f + (1.0 - p) * h; };
    out.charge.p_win = blend(out.fight.p_win, out.hold.p_win);
    out.charge.p_draw = blend(out.fight.p_draw, out.hold.p_draw);
    out.charge.p_loss = blend(out.fight.p_loss, out.hold.p_loss);
    out.charge.v = blend(out.fight.v, out.hold.v);
    out.charge.score = blend(out.fight.score, out.hold.score);
    // The side whose turn it is (the charger's) prefers it: its own score, by more than the chain's tie.
    const double sign = u->side == side_ ? 1.0 : -1.0;
    out.wanted = p > 0.0 && sign * (out.charge.score - out.hold.score) > kTie;
    return out;
}

}  // namespace wh40k::ai::hard
