#include "core/data/army_list.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "core/data/army_rules.hpp"
#include "core/data/load_error.hpp"
#include "core/data/zone_layout.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/attacks.hpp"

namespace wh40k::core {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

namespace {

constexpr int kArmyFormat = 1;
constexpr const char* kArmyKind = "vibehammer-army";
constexpr const char* kFactionsKind = "vibehammer-factions";

// SplitMix64: random_army's own generator (deterministic, independent of the game's dice).
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    std::size_t below(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }

private:
    std::uint64_t state_;
};

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool has_sheet_keyword(const Datasheet& ds, std::string_view keyword) {
    const auto match = [&](const std::string& k) { return iequals(k, keyword); };
    return std::any_of(ds.keywords.begin(), ds.keywords.end(), match) ||
           std::any_of(ds.faction_keywords.begin(), ds.faction_keywords.end(), match);
}

std::string quoted_list(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + ("'" + names[i] + "'");
    }
    return out;
}

std::string ordinal(int n) {
    const int tens = n % 100;
    const char* suffix = (tens >= 11 && tens <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suffix;
}

// ---- Loadouts: one choice per slot of a model type ----

struct Loadout {
    std::vector<std::size_t> options;  // per slot
    std::vector<std::string> weapons;  // fixed weapons, then the options' weapons in slot order
    std::vector<std::string> option_names;
};

Loadout make_loadout(const ModelWargear& w, const std::vector<std::size_t>& options) {
    Loadout l;
    l.options = options;
    l.weapons = loadout_weapons(w, options);
    for (std::size_t s = 0; s < options.size(); ++s) l.option_names.push_back(w.slots[s].options[options[s]].name);
    return l;
}

// Every combination of slot options, the first slot varying slowest.
std::vector<Loadout> all_loadouts(const ModelWargear& w) {
    std::vector<Loadout> out;
    std::vector<std::size_t> idx(w.slots.size(), 0);
    if (std::any_of(w.slots.begin(), w.slots.end(), [](const WargearSlot& s) { return s.options.empty(); })) {
        return out;
    }
    while (true) {
        out.push_back(make_loadout(w, idx));
        std::size_t s = idx.size();
        while (s > 0) {
            --s;
            if (++idx[s] < w.slots[s].options.size()) break;
            idx[s] = 0;
            if (s == 0) return out;
        }
        if (idx.empty()) return out;
    }
}

// How many of the limit's things one model with these weapons / options counts (a model-type limit counts no
// models: it is a unit limit).
int count_in_model(const WargearLimit& l, const std::vector<std::string>& weapons,
                   const std::vector<std::string>& option_names) {
    int n = 0;
    if (l.counts == LimitCounts::Weapons) {
        for (const std::string& w : weapons) n += std::find(l.names.begin(), l.names.end(), w) != l.names.end() ? 1 : 0;
    } else if (l.counts == LimitCounts::Option) {
        for (const std::string& o : option_names) n += o == l.names.front() ? 1 : 0;
    }
    return n;
}

std::string limit_what(const WargearLimit& l) {
    switch (l.counts) {
        case LimitCounts::Weapons:
            return l.names.size() == 1 ? "'" + l.names.front() + "'" : quoted_list(l.names) + " in all";
        case LimitCounts::Option:
            return "model(s) with the option '" + l.names.front() + "'";
        case LimitCounts::ModelType:
            return "'" + l.names.front() + "' model(s)";
    }
    return "";
}

// D-079: a model's own limits.
std::optional<std::string> model_limit_problem(const ModelWargear& w, const std::vector<std::string>& weapons,
                                               const std::vector<std::string>& option_names) {
    for (const WargearLimit& l : w.limits) {
        const int n = count_in_model(l, weapons, option_names);
        if (n > l.max) {
            return "D-079: each '" + w.model_type + "' may carry at most " + std::to_string(l.max) + " " +
                   limit_what(l) + " (it carries " + std::to_string(n) + ")";
        }
    }
    return std::nullopt;
}

bool loadout_pickable(const Datasheet& ds, const ModelWargear& w, const Loadout& l) {
    for (const std::string& weapon : l.weapons) {
        if (weapon_refusal(ds, weapon)) return false;
    }
    return !model_limit_problem(w, l.weapons, l.option_names);
}

// The playable loadouts of a model type, in all_loadouts order.
std::vector<Loadout> playable_loadouts(const Datasheet& ds, const ModelWargear& w) {
    std::vector<Loadout> out;
    for (Loadout& l : all_loadouts(w)) {
        if (loadout_pickable(ds, w, l)) out.push_back(std::move(l));
    }
    return out;
}

// D-079: per slot the BSData default if pickable, else the first pickable option; if that breaks a per-model limit,
// the first playable loadout. nullopt: the model type is unavailable.
std::optional<Loadout> default_loadout(const Datasheet& ds, const ModelWargear& w) {
    std::vector<std::size_t> choice;
    bool ok = true;
    for (const WargearSlot& s : w.slots) {
        std::optional<std::size_t> pick;
        for (std::size_t i = 0; i < s.options.size(); ++i) {
            if (s.default_option && s.options[i].name == *s.default_option && !option_refusal(ds, s.options[i])) pick = i;
        }
        for (std::size_t i = 0; i < s.options.size() && !pick; ++i) {
            if (!option_refusal(ds, s.options[i])) pick = i;
        }
        if (!pick) {
            ok = false;
            break;
        }
        choice.push_back(*pick);
    }
    if (ok) {
        Loadout l = make_loadout(w, choice);
        if (loadout_pickable(ds, w, l)) return l;
    }
    std::vector<Loadout> playable = playable_loadouts(ds, w);
    if (playable.empty()) return std::nullopt;
    return playable.front();
}

// The unit limits' counts over models given as (model type wargear, loadout weapons, option names, count).
struct CountedModels {
    const ModelWargear* wargear;
    const std::vector<std::string>* weapons;
    const std::vector<std::string>* option_names;
    int count;
};

int count_in_unit(const WargearLimit& l, const std::vector<CountedModels>& models) {
    int n = 0;
    for (const CountedModels& m : models) {
        const int per_model = l.counts == LimitCounts::ModelType
                                  ? (m.wargear->model_type == l.names.front() ? 1 : 0)
                                  : count_in_model(l, *m.weapons, *m.option_names);
        n += per_model * m.count;
    }
    return n;
}

std::optional<std::string> unit_limit_problem(const ListBuilding& lb, const std::vector<CountedModels>& models,
                                              int unit_size) {
    for (const WargearLimit& l : lb.unit_limits) {
        const int n = count_in_unit(l, models);
        const int max = limit_max(l, unit_size);
        if (n > max) {
            return "D-079: a unit of " + std::to_string(unit_size) + " models may have at most " + std::to_string(max) +
                   " " + limit_what(l) + " (it has " + std::to_string(n) + ")";
        }
    }
    return std::nullopt;
}

std::vector<Base> unit_bases(const Datasheet& ds, const ArmyUnit& unit) {
    std::vector<Base> bases;
    for (const ArmyModelGroup& g : unit.models) {
        const ModelTypeSheet* mt = ds.find_model_type(g.type);
        if (mt == nullptr) continue;
        for (int i = 0; i < g.count; ++i) bases.push_back(mt->base);
    }
    return bases;
}

Battlefield custom_field() {
    return Battlefield{inches_to_mm(kCustomBattleWidthIn), inches_to_mm(kCustomBattleDepthIn)};
}

DeploymentZone custom_zone(Side side) {
    return DeploymentZone{side == Side::A ? BattlefieldEdge::South : BattlefieldEdge::North,
                          inches_to_mm(kCustomBattleZoneDepthIn)};
}

// ---- Random units ----

struct TypeChoices {
    const ModelTypeSheet* sheet = nullptr;
    const ModelWargear* wargear = nullptr;
    std::vector<Loadout> loadouts;  // playable
};

std::optional<ArmyUnit> random_unit_once(const Datasheet& ds, const std::string& file, SplitMix64& rng) {
    const ListBuilding& lb = *ds.list_building;
    std::vector<TypeChoices> types;
    for (const ModelTypeSheet& mt : ds.model_types) {
        const ModelWargear* w = lb.find_wargear(mt.name);
        if (w == nullptr) return std::nullopt;
        types.push_back(TypeChoices{&mt, w, playable_loadouts(ds, *w)});
    }
    const int n = ds.min_models + static_cast<int>(rng.below(static_cast<std::size_t>(ds.max_models - ds.min_models + 1)));
    std::vector<std::pair<std::size_t, std::size_t>> picks;  // (type, loadout)
    std::vector<int> counts(types.size(), 0);
    const auto limits_ok = [&] {
        std::vector<CountedModels> models;
        for (const auto& [t, l] : picks) {
            models.push_back(CountedModels{types[t].wargear, &types[t].loadouts[l].weapons,
                                           &types[t].loadouts[l].option_names, 1});
        }
        return !unit_limit_problem(lb, models, n);
    };
    const auto try_add = [&](std::size_t t) {
        if (types[t].loadouts.empty() || counts[t] >= types[t].sheet->max) return false;
        picks.emplace_back(t, rng.below(types[t].loadouts.size()));
        if (!limits_ok()) {
            picks.pop_back();
            return false;
        }
        ++counts[t];
        return true;
    };
    for (std::size_t t = 0; t < types.size(); ++t) {
        for (int k = 0; k < types[t].sheet->min; ++k) {
            bool added = false;
            for (int tries = 0; tries < 12 && !added; ++tries) added = try_add(t);
            if (!added) return std::nullopt;
        }
    }
    while (static_cast<int>(picks.size()) < n) {
        bool added = false;
        for (int tries = 0; tries < 16 && !added; ++tries) added = try_add(rng.below(types.size()));
        if (!added) break;
    }
    // Groups: by model type in model_types order, then by loadout.
    std::map<std::pair<std::size_t, std::size_t>, int> grouped;
    for (const auto& p : picks) ++grouped[p];
    ArmyUnit unit;
    unit.unit_file = file;
    for (const auto& [key, count] : grouped) {
        unit.models.push_back(ArmyModelGroup{types[key.first].sheet->name, count, types[key.first].loadouts[key.second].weapons});
    }
    if (!unit_problems(ds, unit).empty()) return std::nullopt;
    return unit;
}

std::optional<ArmyUnit> random_unit(const Datasheet& ds, const std::string& file, SplitMix64& rng) {
    for (int tries = 0; tries < 20; ++tries) {
        if (auto u = random_unit_once(ds, file, rng)) return u;
    }
    return default_unit(ds, file);
}

[[noreturn]] void bad_army(const std::string& what) { throw DataError("army: " + what); }

}  // namespace

// ---- JSON ----

void to_json(Json& j, const ArmyModelGroup& v) {
    j = Json{{"type", v.type}, {"count", v.count}, {"weapons", v.weapons}};
}
void from_json(const Json& j, ArmyModelGroup& v) {
    v.type = j.at("type").get<std::string>();
    v.count = j.at("count").get<int>();
    v.weapons = j.at("weapons").get<std::vector<std::string>>();
}
void to_json(Json& j, const ArmyUnit& v) { j = Json{{"unit_file", v.unit_file}, {"models", v.models}}; }
void from_json(const Json& j, ArmyUnit& v) {
    v.unit_file = j.at("unit_file").get<std::string>();
    v.models = j.at("models").get<std::vector<ArmyModelGroup>>();
}
void to_json(Json& j, const ArmyList& v) {
    j = Json{{"format", kArmyFormat}, {"kind", kArmyKind}, {"name", v.name}, {"faction", v.faction}, {"units", v.units}};
}
void from_json(const Json& j, ArmyList& v) {
    if (!j.is_object() || !j.contains("kind") || !j.at("kind").is_string() || j.at("kind").get<std::string>() != kArmyKind) {
        bad_army("D-079: not an army list (\"kind\" must be \"" + std::string(kArmyKind) + "\")");
    }
    if (!j.contains("format") || !j.at("format").is_number_integer() || j.at("format").get<int>() != kArmyFormat) {
        bad_army("D-079: unsupported army list format " + (j.contains("format") ? j.at("format").dump() : "(none)"));
    }
    v.name = j.at("name").get<std::string>();
    v.faction = j.at("faction").get<std::string>();
    v.units = j.at("units").get<std::vector<ArmyUnit>>();
}
void to_json(Json& j, const Faction& v) {
    j = Json{{"id", v.id},
             {"name", v.name},
             {"faction_keyword", v.faction_keyword},
             {"unit_files", v.unit_files},
             {"default_army", v.default_army}};
}
void from_json(const Json& j, Faction& v) {
    v.id = j.at("id").get<std::string>();
    v.name = j.at("name").get<std::string>();
    v.faction_keyword = j.at("faction_keyword").get<std::string>();
    v.unit_files = j.at("unit_files").get<std::vector<std::string>>();
    v.default_army = j.at("default_army").get<std::string>();
}

std::string army_file_text(const ArmyList& list) {
    OrderedJson j;
    j["format"] = kArmyFormat;
    j["kind"] = kArmyKind;
    j["name"] = list.name;
    j["faction"] = list.faction;
    j["units"] = OrderedJson::array();
    for (const ArmyUnit& u : list.units) {
        OrderedJson uj;
        uj["unit_file"] = u.unit_file;
        uj["models"] = OrderedJson::array();
        for (const ArmyModelGroup& g : u.models) {
            OrderedJson gj;
            gj["type"] = g.type;
            gj["count"] = g.count;
            gj["weapons"] = g.weapons;
            uj["models"].push_back(std::move(gj));
        }
        j["units"].push_back(std::move(uj));
    }
    return j.dump(2) + "\n";
}

ArmyList load_army(const std::filesystem::path& path) {
    const Json j = read_json_file(path);
    try {
        return j.get<ArmyList>();
    } catch (const Json::exception& e) {
        bad_army(path.string() + ": " + e.what());
    }
}

void save_army(const ArmyList& list, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw DataError("army: cannot write " + path.string());
    out << army_file_text(list);
    if (!out) throw DataError("army: cannot write " + path.string());
}

std::vector<Faction> load_factions(const std::filesystem::path& repo_root) {
    const std::filesystem::path path = repo_root / "data" / "factions.json";
    const Json j = read_json_file(path);
    try {
        if (!j.is_object() || j.value("kind", std::string()) != kFactionsKind || j.value("format", 0) != 1) {
            throw DataError("factions: " + path.string() + " is not a format 1 \"" + kFactionsKind + "\" file");
        }
        std::vector<Faction> out = j.at("factions").get<std::vector<Faction>>();
        std::set<std::string> ids;
        for (const Faction& f : out) {
            if (f.id.empty() || !ids.insert(f.id).second) {
                throw DataError("factions: D-079: faction ids must be unique and not empty ('" + f.id + "')");
            }
            if (f.unit_files.empty()) throw DataError("factions: D-079: faction '" + f.id + "' has no datasheets");
        }
        return out;
    } catch (const Json::exception& e) {
        throw DataError("factions: " + path.string() + ": " + e.what());
    }
}

const Faction* find_faction(const std::vector<Faction>& factions, std::string_view id) {
    for (const Faction& f : factions) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

DatasheetResolver caching_datasheet_resolver(const std::filesystem::path& repo_root) {
    auto cache = std::make_shared<std::map<std::string, Datasheet>>();
    return [cache, repo_root](const std::string& unit_file) {
        auto it = cache->find(unit_file);
        if (it == cache->end()) it = cache->emplace(unit_file, load_datasheet(repo_root / unit_file)).first;
        return it->second;
    };
}

// ---- Points ----

int unit_points(const Datasheet& sheet, const ArmyUnit& unit, int earlier_copies) {
    if (!sheet.list_building) throw DataError("D-079: '" + sheet.name + "' has no list-building data");
    const ListBuilding& lb = *sheet.list_building;
    int models = 0;
    int options = 0;
    for (const ArmyModelGroup& g : unit.models) {
        models += std::max(g.count, 0);
        const ModelWargear* w = lb.find_wargear(g.type);
        if (w == nullptr || g.count < 1) continue;
        options += g.count * fit_option_points(*w, loadout_fits_slots(*w, g.weapons));
    }
    int points = bracket_points(lb, models) + options;
    if (lb.surcharge && earlier_copies >= lb.surcharge->earlier_copies) points += lb.surcharge->points;
    return points;
}

int army_points(const ArmyList& list, const DatasheetResolver& resolve) {
    int total = 0;
    std::map<std::string, int> copies;
    for (const ArmyUnit& u : list.units) {
        const int earlier = copies[u.unit_file]++;
        try {
            const Datasheet sheet = resolve(u.unit_file);
            if (sheet.list_building) total += unit_points(sheet, u, earlier);
        } catch (const DataError&) {
            // army_problems reports a datasheet that can't be loaded.
        }
    }
    return total;
}

// ---- Problems ----

std::optional<std::string> weapon_refusal(const Datasheet& sheet, std::string_view weapon) {
    const Weapon* w = sheet.find_weapon(weapon);
    if (w == nullptr) return "D-079: '" + sheet.name + "' has no weapon '" + std::string(weapon) + "'";
    for (const WeaponProfile& p : w->profiles) {
        if (auto problem = unimplemented_ability_problem(p)) return problem;
    }
    return std::nullopt;
}

std::optional<std::string> option_refusal(const Datasheet& sheet, const WargearOption& option) {
    for (const std::string& w : option.weapons) {
        if (auto r = weapon_refusal(sheet, w)) return r;
    }
    return std::nullopt;
}

std::vector<std::string> unavailable_model_types(const Datasheet& sheet) {
    std::vector<std::string> out;
    if (!sheet.list_building) return out;
    for (const ModelWargear& w : sheet.list_building->wargear) {
        if (!default_loadout(sheet, w)) out.push_back(w.model_type);
    }
    return out;
}

std::optional<std::vector<std::string>> default_model_weapons(const Datasheet& sheet, std::string_view model_type) {
    if (!sheet.list_building) return std::nullopt;
    const ModelWargear* w = sheet.list_building->find_wargear(model_type);
    if (w == nullptr) return std::nullopt;
    std::optional<Loadout> l = default_loadout(sheet, *w);
    if (!l) return std::nullopt;
    return std::move(l->weapons);
}

std::optional<std::string> unavailable_reason(const Datasheet& sheet, std::string_view model_type) {
    if (!sheet.list_building) return std::nullopt;
    const ModelWargear* w = sheet.list_building->find_wargear(model_type);
    if (w == nullptr || default_loadout(sheet, *w)) return std::nullopt;
    const std::string type(model_type);
    for (const std::string& weapon : w->fixed) {
        if (auto r = weapon_refusal(sheet, weapon)) {
            return "D-079: every '" + type + "' carries '" + weapon + "', which can't be picked: " + *r;
        }
    }
    for (const WargearSlot& slot : w->slots) {
        std::optional<std::string> first;
        bool any = false;
        for (const WargearOption& o : slot.options) {
            const std::optional<std::string> r = option_refusal(sheet, o);
            if (!r) any = true;
            if (r && !first) first = r;
        }
        if (!any) {
            return "D-079: no option of the '" + type + "' slot '" + slot.name + "' can be picked" +
                   (first ? ": " + *first : std::string());
        }
    }
    return "D-079: every pickable loadout of '" + type + "' breaks a per-model wargear limit";
}

std::vector<std::string> unit_problems(const Datasheet& ds, const ArmyUnit& unit) {
    std::vector<std::string> out;
    if (!ds.list_building) {
        out.push_back("D-079: '" + ds.name + "' has no list-building data, so it can't be taken in a custom army");
        return out;
    }
    const ListBuilding& lb = *ds.list_building;
    std::map<std::string, int> per_type;
    int total = 0;
    std::vector<SlotFit> fits(unit.models.size());
    std::vector<std::vector<std::string>> option_names(unit.models.size());
    std::vector<CountedModels> counted;
    for (std::size_t i = 0; i < unit.models.size(); ++i) {
        const ArmyModelGroup& g = unit.models[i];
        const ModelWargear* w = lb.find_wargear(g.type);
        if (ds.find_model_type(g.type) == nullptr || w == nullptr) {
            out.push_back("02.06: '" + ds.name + "' has no model type '" + g.type + "'");
            continue;
        }
        if (g.count < 1) {
            out.push_back("02.06: model group '" + g.type + "' count must be at least 1, not " + std::to_string(g.count));
            continue;
        }
        per_type[g.type] += g.count;
        total += g.count;
        std::set<std::string> seen;
        for (const std::string& weapon : g.weapons) {
            if (!seen.insert(weapon).second) continue;
            if (auto r = weapon_refusal(ds, weapon)) {
                out.push_back("D-079: '" + g.type + "' carries '" + weapon + "', which can't be picked: " + *r);
            }
        }
        fits[i] = loadout_fits_slots(*w, g.weapons);
        if (!fits[i].fits()) {
            out.push_back(fits[i].problem);
            continue;
        }
        for (std::size_t s = 0; s < fits[i].options.size(); ++s) {
            option_names[i].push_back(w->slots[s].options[fits[i].options[s]].name);
        }
        if (auto p = model_limit_problem(*w, g.weapons, option_names[i])) out.push_back(*p);
        counted.push_back(CountedModels{w, &g.weapons, &option_names[i], g.count});
    }
    // 02.06 unit composition (the scenario loader's check, same wording).
    for (const ModelTypeSheet& mt : ds.model_types) {
        const auto it = per_type.find(mt.name);
        const int n = it == per_type.end() ? 0 : it->second;
        if (n < mt.min || n > mt.max) {
            out.push_back("02.06: '" + mt.name + "' count " + std::to_string(n) + " outside " + std::to_string(mt.min) +
                          "-" + std::to_string(mt.max));
        }
    }
    if (total < ds.min_models || total > ds.max_models) {
        out.push_back("02.06: unit size " + std::to_string(total) + " outside " + std::to_string(ds.min_models) + "-" +
                      std::to_string(ds.max_models));
    }
    if (auto p = unit_limit_problem(lb, counted, total)) out.push_back(*p);
    return out;
}

std::vector<ArmyProblem> army_problems(const ArmyList& list, const Faction& faction, const DatasheetResolver& resolve,
                                       int points_limit) {
    std::vector<ArmyProblem> out;
    if (list.faction != faction.id) {
        out.push_back({std::nullopt, "D-079: the list is for faction '" + list.faction + "', not " + faction.name});
    }
    bool clean_units = true;
    bool character = false;
    std::map<std::string, int> copies;
    std::vector<std::vector<Base>> footprints;
    for (std::size_t i = 0; i < list.units.size(); ++i) {
        const ArmyUnit& u = list.units[i];
        if (std::find(faction.unit_files.begin(), faction.unit_files.end(), u.unit_file) == faction.unit_files.end()) {
            out.push_back({i, "D-079: '" + u.unit_file + "' is not a " + faction.name + " datasheet"});
            clean_units = false;
            continue;
        }
        Datasheet sheet;
        try {
            sheet = resolve(u.unit_file);
        } catch (const DataError& e) {
            out.push_back({i, std::string("D-079: ") + e.what()});
            clean_units = false;
            continue;
        }
        if (!has_sheet_keyword(sheet, faction.faction_keyword)) {
            out.push_back({i, "02.05 (D-079): '" + sheet.name + "' does not have the faction keyword '" +
                                  faction.faction_keyword + "'"});
            clean_units = false;
        }
        const std::vector<std::string> own = unit_problems(sheet, u);
        for (const std::string& p : own) out.push_back({i, p});
        clean_units = clean_units && own.empty();
        character = character || has_sheet_keyword(sheet, "Character");
        const int copy = ++copies[u.unit_file];
        if (sheet.list_building && copy > sheet.list_building->max_copies_incursion) {
            out.push_back({i, "D-079: an Incursion army may include at most " +
                                  std::to_string(sheet.list_building->max_copies_incursion) + " '" + sheet.name +
                                  "' units (this is the " + ordinal(copy) + ")"});
        }
        footprints.push_back(unit_bases(sheet, u));
    }
    if (list.units.empty()) out.push_back({std::nullopt, "D-079: an army needs at least one unit"});
    if (!list.units.empty() && !character) {
        std::vector<std::string> names;
        for (const std::string& file : faction.unit_files) {
            try {
                const Datasheet sheet = resolve(file);
                if (has_sheet_keyword(sheet, "Character")) names.push_back(sheet.name);
            } catch (const DataError&) {
            }
        }
        out.push_back({std::nullopt, "D-079: an army needs at least one CHARACTER unit" +
                                         (names.empty() ? std::string() : " (" + quoted_list(names) + ")")});
    }
    const int points = army_points(list, resolve);
    if (points > points_limit) {
        out.push_back({std::nullopt, "D-079: the army costs " + std::to_string(points) + " points, over the limit of " +
                                         std::to_string(points_limit)});
    }
    if (clean_units && !list.units.empty()) {
        const ZoneLayout layout = layout_zone(footprints, custom_field(), custom_zone(Side::A));
        if (!layout.fits()) out.push_back({std::nullopt, layout.problem});
    }
    return out;
}

std::vector<std::string> battle_problems(const ArmyList& a, const ArmyList& b) {
    std::vector<std::string> out;
    if (a.faction == b.faction) {
        out.push_back("D-079: both armies are '" + a.faction + "'; a custom battle needs two different factions");
    }
    return out;
}

// ---- New units and random armies ----

std::optional<ArmyUnit> default_unit(const Datasheet& sheet, const std::string& unit_file) {
    if (!sheet.list_building) return std::nullopt;
    const ListBuilding& lb = *sheet.list_building;
    struct TypeDefault {
        const ModelTypeSheet* mt;
        const ModelWargear* w;
        std::optional<Loadout> loadout;
        int count = 0;
    };
    std::vector<TypeDefault> types;
    int total = 0;
    for (const ModelTypeSheet& mt : sheet.model_types) {
        const ModelWargear* w = lb.find_wargear(mt.name);
        if (w == nullptr) return std::nullopt;
        TypeDefault t{&mt, w, default_loadout(sheet, *w), 0};
        if (mt.min > 0 && !t.loadout) return std::nullopt;
        t.count = mt.min;
        total += mt.min;
        types.push_back(std::move(t));
    }
    const auto counted = [&] {
        std::vector<CountedModels> out;
        for (const TypeDefault& t : types) {
            if (t.count > 0) out.push_back(CountedModels{t.w, &t.loadout->weapons, &t.loadout->option_names, t.count});
        }
        return out;
    };
    // The rest of the minimum size: one model at a time on the first available type it keeps within the limits.
    while (total < sheet.min_models) {
        bool added = false;
        for (TypeDefault& t : types) {
            if (!t.loadout || t.count >= t.mt->max) continue;
            ++t.count;
            if (!unit_limit_problem(lb, counted(), sheet.min_models)) {
                added = true;
                break;
            }
            --t.count;
        }
        if (!added) return std::nullopt;
        ++total;
    }
    ArmyUnit unit;
    unit.unit_file = unit_file;
    for (const TypeDefault& t : types) {
        if (t.count > 0) unit.models.push_back(ArmyModelGroup{t.mt->name, t.count, t.loadout->weapons});
    }
    if (!unit_problems(sheet, unit).empty()) return std::nullopt;
    return unit;
}

ArmyList random_army(const Faction& faction, const DatasheetResolver& resolve, std::uint64_t seed, int points_limit,
                     RandomArmyStats* stats) {
    SplitMix64 rng(seed);
    std::map<std::string, Datasheet> sheets;
    std::vector<std::string> files;       // the faction's datasheets with list data
    std::vector<std::string> characters;  // of those, the CHARACTER ones
    for (const std::string& file : faction.unit_files) {
        Datasheet ds = resolve(file);
        if (!ds.list_building) continue;
        files.push_back(file);
        if (has_sheet_keyword(ds, "Character")) characters.push_back(file);
        sheets.emplace(file, std::move(ds));
    }
    if (characters.empty()) throw DataError("D-079: faction '" + faction.id + "' has no CHARACTER datasheet");
    constexpr int kAttempts = 32;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (stats != nullptr) ++stats->attempts;
        ArmyList list{faction.name, faction.id, {}};
        std::map<std::string, int> copies;
        int points = 0;
        const auto try_add = [&](const std::string& file) {
            const Datasheet& ds = sheets.at(file);
            if (copies[file] >= ds.list_building->max_copies_incursion) return false;
            const std::optional<ArmyUnit> unit = random_unit(ds, file, rng);
            if (!unit) return false;
            const int cost = unit_points(ds, *unit, copies[file]);
            if (points + cost > points_limit) return false;
            list.units.push_back(*unit);
            points += cost;
            ++copies[file];
            return true;
        };
        if (!try_add(characters[rng.below(characters.size())])) continue;
        int failures = 0;
        while (failures < 10) {
            if (list.units.size() >= 2 && rng.below(10) == 0) break;  // armies of every size, not only full ones
            if (try_add(files[rng.below(files.size())])) {
                failures = 0;
            } else {
                ++failures;
            }
        }
        for (std::size_t i = list.units.size(); i > 1; --i) {  // Fisher-Yates: the CHARACTER isn't always first
            std::swap(list.units[i - 1], list.units[rng.below(i)]);
        }
        const std::vector<ArmyProblem> problems = army_problems(list, faction, resolve, points_limit);
        if (problems.empty()) return list;
        if (stats != nullptr) {
            ++stats->rejected;
            for (const ArmyProblem& p : problems) {
                if (p.reason.find("does not fit") != std::string::npos) {
                    ++stats->did_not_fit;
                    break;
                }
            }
        }
    }
    ArmyList fallback{faction.name, faction.id, {}};
    if (auto unit = default_unit(sheets.at(characters.front()), characters.front())) fallback.units.push_back(*unit);
    return fallback;
}

// ---- The battle ----

Json custom_battle_scenario(const ArmyList& a, const ArmyList& b, Side first_turn, const std::vector<Faction>& factions,
                            const DatasheetResolver& resolve, DeploymentMode mode, const Json* mission) {
    if (const auto p = battle_problems(a, b); !p.empty()) throw DataError("custom battle: " + p.front());
    const Battlefield field = custom_field();
    Json sides = Json::object();
    std::vector<std::string> names;
    for (const Side side : {Side::A, Side::B}) {
        const ArmyList& list = side == Side::A ? a : b;
        const std::string sname = side == Side::A ? "A" : "B";
        const Faction* faction = find_faction(factions, list.faction);
        if (faction == nullptr) throw DataError("custom battle: D-079: side " + sname + ": unknown faction '" + list.faction + "'");
        if (const auto p = army_problems(list, *faction, resolve); !p.empty()) {
            throw DataError("custom battle: side " + sname + ": " + p.front().reason);
        }
        const DeploymentZone zone = custom_zone(side);
        std::vector<Datasheet> sheets;
        std::vector<std::vector<Base>> footprints;
        for (const ArmyUnit& u : list.units) {
            sheets.push_back(resolve(u.unit_file));
            footprints.push_back(unit_bases(sheets.back(), u));
        }
        const ZoneLayout layout = layout_zone(footprints, field, zone);
        if (!layout.fits()) throw DataError("custom battle: side " + sname + ": " + layout.problem);
        Json units = Json::array();
        std::map<std::string, int> copies;
        for (std::size_t i = 0; i < list.units.size(); ++i) {
            const ArmyUnit& u = list.units[i];
            const LayoutSlot& slot = layout.slots[i];
            Json formation{{"ranks", slot.ranks}, {"gap_in", 0}, {"centre_x_in", slot.centre_x_in}, {"anchor", "zone_front"}};
            if (slot.depth_in != 0.0f) formation["depth_in"] = slot.depth_in;
            Json unit{{"unit_file", u.unit_file},
                      {"points", unit_points(sheets[i], u, copies[u.unit_file]++)},
                      {"models", u.models}};
            if (mode == DeploymentMode::Auto) unit["formation"] = formation;  // D-081: none when set up by hand
            units.push_back(std::move(unit));
        }
        const std::string name = list.name.empty() ? faction->name : list.name;
        names.push_back(name);
        sides[sname] = Json{{"name", name},
                            {"deployment_zone",
                             {{"edge", side == Side::A ? "south" : "north"}, {"depth_in", kCustomBattleZoneDepthIn}}},
                            {"units", units}};
    }
    Json out{{"name", "Custom battle - " + names[0] + " vs " + names[1]},
             {"battlefield", {{"width_in", kCustomBattleWidthIn}, {"depth_in", kCustomBattleDepthIn}}},
             {"battle_rounds", kCustomBattleRounds},
             {"first_turn", first_turn == Side::A ? "A" : "B"},
             {"result", "points_destroyed"},
             {"sides", sides},
             {"notes",
              "D-079: a custom battle (Incursion, 1000 points; agreed with the user): 44\"x60\" portrait, 12\" zones "
              "on the south (A) and north (B) edges, 5 battle rounds, decided on points destroyed (D-076). Units "
              "are placed by the auto-layout (D-079 part 2) in list order."}};
    if (mode == DeploymentMode::Hand) {
        // D-081: set up by hand, the sides alternating (the side taking the second turn first); Auto-place's spots are
        // the engine's proposals.
        out["deployment"] = "alternating";
        out["notes"] =
            "D-079: a custom battle (Incursion, 1000 points; agreed with the user): 44\"x60\" portrait, 12\" zones on "
            "the south (A) and north (B) edges, 5 battle rounds, decided on points destroyed (D-076). Units are set "
            "up by hand, the sides alternating one unit at a time, starting with the side that takes the second turn "
            "(D-081); the engine proposes the auto-layout's spots (D-079 part 2).";
    }
    if (mission != nullptr) {
        // D-084 (M86): the objective mission, its numbers from the scenario file the block came from.
        out["result"] = "mission";
        out["mission"] = *mission;
        out["notes"] = out["notes"].get<std::string>() +
                       " D-084: played as the objective mission (objective areas, victory points; points destroyed "
                       "settle equal VP) instead of on points destroyed alone.";
    }
    return out;
}

}  // namespace wh40k::core
