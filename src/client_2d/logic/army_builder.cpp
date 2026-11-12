#include "army_builder.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <system_error>
#include <utility>

#include "core/data/army_rules.hpp"
#include "core/data/load_error.hpp"

namespace wh40k::client {
namespace {

namespace fs = std::filesystem;
using core::Side;

std::string side_label(Side side) { return side == Side::A ? "Side A" : "Side B"; }

bool is_citation_char(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-' || c == 'D' || c == ' ' ||
           c == '(' || c == ')' || c == ',';
}

// The group's slot choices: its weapons' decomposition, else (weapons that don't fit) the type's default loadout's.
std::optional<std::vector<std::size_t>> current_options(const core::Datasheet& sheet, const core::ModelWargear& w,
                                                        const core::ArmyModelGroup& g) {
    core::SlotFit fit = core::loadout_fits_slots(w, g.weapons);
    if (fit.fits()) return fit.options;
    if (const auto weapons = core::default_model_weapons(sheet, g.type)) {
        fit = core::loadout_fits_slots(w, *weapons);
        if (fit.fits()) return fit.options;
    }
    return std::nullopt;
}

bool same_path(const fs::path& a, const fs::path& b) {
    std::error_code ec1;
    std::error_code ec2;
    const fs::path ca = fs::weakly_canonical(a, ec1);
    const fs::path cb = fs::weakly_canonical(b, ec2);
    if (ec1 || ec2) return a.lexically_normal() == b.lexically_normal();
    return ca == cb;
}

}  // namespace

core::GameState custom_battle_state(const CustomBattleSetup& setup, const std::vector<core::Faction>& factions,
                                    const core::DatasheetResolver& resolve, const nlohmann::json* mission) {
    return core::build_game_state(core::custom_battle_scenario(setup.armies[0], setup.armies[1], setup.first_turn,
                                                               factions, resolve, setup.deployment, mission),
                                  resolve);
}

std::string plain_first(const std::string& reason) {
    // A leading citation: "02.06: ", "D-079: ", "02.07 (D-079): ", "24.10: " ...
    const std::size_t colon = reason.find(": ");
    if (colon == std::string::npos || colon == 0 || colon > 16) return reason;
    const std::string cite = reason.substr(0, colon);
    if (!std::all_of(cite.begin(), cite.end(), is_citation_char)) return reason;
    if (std::none_of(cite.begin(), cite.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        return reason;
    }
    std::string text = reason.substr(colon + 2);
    if (text.empty()) return reason;
    text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text + " (" + cite + ")";
}

// ---- Construction and settings ----

ArmyBuilder::ArmyBuilder(fs::path data_root)
    : data_root_(std::move(data_root)),
      factions_(core::load_factions(data_root_)),
      resolve_(core::caching_datasheet_resolver(data_root_)) {
    if (factions_.size() < 2) throw core::DataError("D-079: a custom battle needs two factions in data/factions.json");
    for (const Side side : {Side::A, Side::B}) {
        const std::size_t i = core::side_index(side);
        armies_[i] = stock_list(factions_[i]);
        start_stock_[i] = armies_[i];
        source_[i] = factions_[i].default_army;
    }
    refresh_files();
}

void ArmyBuilder::apply(const CustomBattleSettings& settings) {
    for (const Side side : {Side::A, Side::B}) {
        const std::size_t i = core::side_index(side);
        const std::optional<core::ArmyList>& saved = side == Side::A ? settings.army_a : settings.army_b;
        if (saved && faction_of(saved->faction) != nullptr) {
            armies_[i] = *saved;
            source_[i].clear();
        } else {
            armies_[i] = start_stock_[i];
            source_[i] = factions_[i].default_army;
        }
        edited_[i] = false;
    }
    first_turn_ = settings.first_turn;
    deployment_ = settings.deployment;
    play_mode_ = settings.play_mode;
    selected_side_ = Side::A;
    selected_unit_.reset();
}

CustomBattleSettings ArmyBuilder::settings() const {
    CustomBattleSettings s;
    if (armies_[0] != start_stock_[0]) s.army_a = armies_[0];
    if (armies_[1] != start_stock_[1]) s.army_b = armies_[1];
    s.first_turn = first_turn_;
    s.deployment = deployment_;
    s.play_mode = play_mode_;
    return s;
}

// ---- Helpers ----

const core::Faction* ArmyBuilder::faction_of(const std::string& id) const { return core::find_faction(factions_, id); }

core::ArmyList ArmyBuilder::stock_list(const core::Faction& f) const { return core::load_army(data_root_ / f.default_army); }

const core::ArmyList& ArmyBuilder::list(Side side) const { return armies_[core::side_index(side)]; }

const core::Faction& ArmyBuilder::faction(Side side) const {
    const core::Faction* f = faction_of(list(side).faction);
    // A list always holds a known faction (apply, load and set_faction keep it so).
    return f != nullptr ? *f : factions_[core::side_index(side)];
}

std::optional<std::string> ArmyBuilder::check_unit(Side side, std::size_t unit) const {
    if (unit >= list(side).units.size()) return "no unit " + std::to_string(unit + 1) + " in " + side_label(side) + "'s list";
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::check_group(Side side, std::size_t unit, std::size_t group) const {
    if (auto p = check_unit(side, unit)) return p;
    if (group >= list(side).units[unit].models.size()) return "no such model group";
    return std::nullopt;
}

int ArmyBuilder::copies(Side side, const std::string& unit_file) const {
    const auto& units = list(side).units;
    return static_cast<int>(
        std::count_if(units.begin(), units.end(), [&](const core::ArmyUnit& u) { return u.unit_file == unit_file; }));
}

void ArmyBuilder::touched(Side side) { edited_[core::side_index(side)] = true; }

std::optional<core::Datasheet> ArmyBuilder::datasheet(Side side, std::size_t unit) const {
    if (check_unit(side, unit)) return std::nullopt;
    try {
        return resolve_(list(side).units[unit].unit_file);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// ---- Edits ----

std::optional<std::string> ArmyBuilder::set_faction(Side side, const std::string& faction_id) {
    const core::Faction* f = faction_of(faction_id);
    if (f == nullptr) return "D-079: no faction '" + faction_id + "'";
    const std::size_t i = core::side_index(side);
    const std::size_t o = 1 - i;
    if (armies_[i].faction == faction_id) return std::nullopt;
    if (armies_[o].faction == faction_id) {
        // D-079 (no mirror matches): the sides swap armies.
        std::swap(armies_[i], armies_[o]);
        std::swap(edited_[i], edited_[o]);
        std::swap(source_[i], source_[o]);
        selected_unit_.reset();
        return std::nullopt;
    }
    armies_[i] = stock_list(*f);
    edited_[i] = false;
    source_[i] = f->default_army;
    if (selected_side_ == side) selected_unit_.reset();
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::rename(Side side, const std::string& name) {
    if (list(side).name == name) return std::nullopt;
    list_mut(side).name = name;
    touched(side);
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::add_unit(Side side, const std::string& unit_file) {
    const core::Faction& f = faction(side);
    if (std::find(f.unit_files.begin(), f.unit_files.end(), unit_file) == f.unit_files.end()) {
        return "D-079: '" + unit_file + "' is not a " + f.name + " datasheet";
    }
    core::Datasheet sheet;
    try {
        sheet = resolve_(unit_file);
    } catch (const std::exception& e) {
        return std::string("D-079: ") + e.what();
    }
    if (sheet.list_building && copies(side, unit_file) >= sheet.list_building->max_copies_incursion) {
        return "D-079: an Incursion army may include at most " +
               std::to_string(sheet.list_building->max_copies_incursion) + " '" + sheet.name + "' units";
    }
    std::optional<core::ArmyUnit> unit = core::default_unit(sheet, unit_file);
    if (!unit) return "D-079: no playable '" + sheet.name + "' unit can be made";
    list_mut(side).units.push_back(std::move(*unit));
    touched(side);
    select(side, list(side).units.size() - 1);
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::remove_unit(Side side, std::size_t unit) {
    if (auto p = check_unit(side, unit)) return p;
    auto& units = list_mut(side).units;
    units.erase(units.begin() + static_cast<std::ptrdiff_t>(unit));
    touched(side);
    if (selected_side_ == side && selected_unit_) {
        if (*selected_unit_ == unit) {
            selected_unit_.reset();
        } else if (*selected_unit_ > unit) {
            --*selected_unit_;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::duplicate_unit(Side side, std::size_t unit) {
    if (auto p = check_unit(side, unit)) return p;
    const core::ArmyUnit copy = list(side).units[unit];
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    if (sheet && sheet->list_building && copies(side, copy.unit_file) >= sheet->list_building->max_copies_incursion) {
        return "D-079: an Incursion army may include at most " +
               std::to_string(sheet->list_building->max_copies_incursion) + " '" + sheet->name + "' units";
    }
    auto& units = list_mut(side).units;
    units.insert(units.begin() + static_cast<std::ptrdiff_t>(unit + 1), copy);
    touched(side);
    select(side, unit + 1);
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::set_count(Side side, std::size_t unit, std::size_t group, int count) {
    if (auto p = check_group(side, unit, group)) return p;
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    if (!sheet) return "the unit's datasheet can't be loaded";
    core::ArmyUnit& u = list_mut(side).units[unit];
    core::ArmyModelGroup& g = u.models[group];
    if (count == g.count) return std::nullopt;
    if (count < 1) return "a model group has at least one model (remove the group instead)";
    const core::ModelTypeSheet* mt = sheet->find_model_type(g.type);
    int type_total = 0;
    int unit_total = 0;
    for (const core::ArmyModelGroup& other : u.models) {
        unit_total += other.count;
        if (other.type == g.type) type_total += other.count;
    }
    const int delta = count - g.count;
    if (mt != nullptr && (type_total + delta > mt->max || type_total + delta < mt->min)) {
        return "02.06: '" + g.type + "' count must stay within " + std::to_string(mt->min) + "-" + std::to_string(mt->max);
    }
    if (delta > 0 && unit_total + delta > sheet->max_models) {
        return "02.06: '" + sheet->name + "' has at most " + std::to_string(sheet->max_models) + " models";
    }
    g.count = count;
    touched(side);
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::add_model_type(Side side, std::size_t unit, const std::string& type) {
    if (auto p = check_unit(side, unit)) return p;
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    if (!sheet) return "the unit's datasheet can't be loaded";
    const core::ModelTypeSheet* mt = sheet->find_model_type(type);
    if (mt == nullptr) return "02.06: '" + sheet->name + "' has no model type '" + type + "'";
    if (auto r = core::unavailable_reason(*sheet, type)) return r;
    const std::optional<std::vector<std::string>> weapons = core::default_model_weapons(*sheet, type);
    if (!weapons) return "D-079: '" + type + "' has no playable loadout";
    core::ArmyUnit& u = list_mut(side).units[unit];
    int type_total = 0;
    int unit_total = 0;
    for (const core::ArmyModelGroup& g : u.models) {
        unit_total += g.count;
        if (g.type == type) type_total += g.count;
    }
    if (type_total + 1 > mt->max) {
        return "02.06: '" + type + "' count must stay within " + std::to_string(mt->min) + "-" + std::to_string(mt->max);
    }
    if (unit_total + 1 > sheet->max_models) {
        return "02.06: '" + sheet->name + "' has at most " + std::to_string(sheet->max_models) + " models";
    }
    // One more model of a group that already has the default loadout; else a new group after the type's last group,
    // or in model_types order among the other groups.
    const auto sorted = [](std::vector<std::string> v) {
        std::sort(v.begin(), v.end());
        return v;
    };
    for (core::ArmyModelGroup& g : u.models) {
        if (g.type == type && sorted(g.weapons) == sorted(*weapons)) {  // the same weapons in any order (02.07)
            ++g.count;
            touched(side);
            return std::nullopt;
        }
    }
    std::size_t at = u.models.size();
    for (std::size_t i = 0; i < u.models.size(); ++i) {
        if (u.models[i].type == type) at = i + 1;
    }
    if (type_total == 0) {
        const auto order = [&](const std::string& name) {
            for (std::size_t i = 0; i < sheet->model_types.size(); ++i) {
                if (sheet->model_types[i].name == name) return i;
            }
            return sheet->model_types.size();
        };
        at = u.models.size();
        for (std::size_t i = 0; i < u.models.size(); ++i) {
            if (order(u.models[i].type) > order(type)) {
                at = i;
                break;
            }
        }
    }
    u.models.insert(u.models.begin() + static_cast<std::ptrdiff_t>(at), core::ArmyModelGroup{type, 1, *weapons});
    touched(side);
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::split_group(Side side, std::size_t unit, std::size_t group) {
    if (auto p = check_group(side, unit, group)) return p;
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    core::ArmyUnit& u = list_mut(side).units[unit];
    core::ArmyModelGroup& g = u.models[group];
    const core::ModelWargear* w =
        sheet && sheet->list_building ? sheet->list_building->find_wargear(g.type) : nullptr;
    if (w == nullptr || w->slots.empty()) return "'" + g.type + "' has no wargear options to vary";
    if (g.count < 2) return "a group of one model can't be split";
    --g.count;
    core::ArmyModelGroup copy{g.type, 1, g.weapons};
    u.models.insert(u.models.begin() + static_cast<std::ptrdiff_t>(group + 1), std::move(copy));
    touched(side);
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::merge_group(Side side, std::size_t unit, std::size_t group) {
    if (auto p = check_group(side, unit, group)) return p;
    core::ArmyUnit& u = list_mut(side).units[unit];
    for (std::size_t i = 0; i < u.models.size(); ++i) {
        if (i == group || u.models[i].type != u.models[group].type) continue;
        u.models[i].count += u.models[group].count;
        u.models.erase(u.models.begin() + static_cast<std::ptrdiff_t>(group));
        touched(side);
        return std::nullopt;
    }
    return "no other '" + u.models[group].type + "' group to merge with";
}

std::optional<std::string> ArmyBuilder::remove_group(Side side, std::size_t unit, std::size_t group) {
    if (auto p = check_group(side, unit, group)) return p;
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    core::ArmyUnit& u = list_mut(side).units[unit];
    const core::ArmyModelGroup& g = u.models[group];
    if (sheet) {
        int type_total = 0;
        for (const core::ArmyModelGroup& other : u.models) {
            if (other.type == g.type) type_total += other.count;
        }
        const core::ModelTypeSheet* mt = sheet->find_model_type(g.type);
        if (mt != nullptr && type_total - g.count < mt->min) {
            return "02.06: '" + g.type + "' count must stay within " + std::to_string(mt->min) + "-" +
                   std::to_string(mt->max);
        }
    }
    if (u.models.size() == 1) return "a unit keeps at least one model group (remove the unit instead)";
    u.models.erase(u.models.begin() + static_cast<std::ptrdiff_t>(group));
    touched(side);
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::set_option(Side side, std::size_t unit, std::size_t group, std::size_t slot,
                                                   std::size_t option) {
    if (auto p = check_group(side, unit, group)) return p;
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    if (!sheet || !sheet->list_building) return "the unit's datasheet has no list data";
    core::ArmyModelGroup& g = list_mut(side).units[unit].models[group];
    const core::ModelWargear* w = sheet->list_building->find_wargear(g.type);
    if (w == nullptr || slot >= w->slots.size() || option >= w->slots[slot].options.size()) return "no such option";
    if (auto r = core::option_refusal(*sheet, w->slots[slot].options[option])) return r;
    std::optional<std::vector<std::size_t>> choice = current_options(*sheet, *w, g);
    if (!choice) return "02.07 (D-079): the group's weapons don't fit its slots";
    if ((*choice)[slot] == option && core::loadout_fits_slots(*w, g.weapons).fits()) return std::nullopt;
    (*choice)[slot] = option;
    g.weapons = core::loadout_weapons(*w, *choice);
    touched(side);
    return std::nullopt;
}

void ArmyBuilder::reset_to_stock(Side side) {
    const std::size_t i = core::side_index(side);
    const core::Faction& f = faction(side);
    armies_[i] = stock_list(f);
    edited_[i] = false;
    source_[i] = f.default_army;
    if (selected_side_ == side) selected_unit_.reset();
}

std::optional<std::string> ArmyBuilder::load(Side side, const fs::path& path) {
    fs::path file = path;
    std::error_code ec;
    if (!fs::exists(file, ec) && fs::exists(data_root_ / file, ec)) file = data_root_ / file;
    core::ArmyList loaded;
    try {
        loaded = core::load_army(file);
    } catch (const std::exception& e) {
        return std::string("could not load ") + path.generic_string() + ": " + e.what();
    }
    if (faction_of(loaded.faction) == nullptr) return "D-079: the list's faction '" + loaded.faction + "' is unknown";
    const std::size_t i = core::side_index(side);
    if (armies_[1 - i].faction == loaded.faction) {
        return "D-079: " + side_label(core::opponent(side)) + " plays " + faction_of(loaded.faction)->name +
               "; a custom battle needs two different factions";
    }
    armies_[i] = std::move(loaded);
    edited_[i] = false;
    const fs::path rel = file.lexically_relative(data_root_);
    source_[i] = !rel.empty() && rel.generic_string().rfind("..", 0) != 0 ? rel.generic_string() : file.generic_string();
    if (selected_side_ == side) selected_unit_.reset();
    return std::nullopt;
}

std::optional<std::string> ArmyBuilder::save(Side side, const fs::path& path) {
    if (same_path(path.parent_path(), data_root_ / "data" / "armies")) {
        return "the lists in data/armies are the stock lists and are never overwritten; save to " +
               (data_root_ / "armies").generic_string() + " instead";
    }
    try {
        std::error_code ec;
        if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
        core::save_army(list(side), path);
    } catch (const std::exception& e) {
        return std::string("could not save: ") + e.what();
    }
    const std::size_t i = core::side_index(side);
    edited_[i] = false;
    const fs::path rel = path.lexically_relative(data_root_);
    source_[i] = !rel.empty() && rel.generic_string().rfind("..", 0) != 0 ? rel.generic_string() : path.generic_string();
    refresh_files();
    return std::nullopt;
}

fs::path ArmyBuilder::user_army_path(Side side) const {
    std::string stem;
    for (const char c : list(side).name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0) {
            stem += static_cast<char>(std::tolower(u));
        } else if (!stem.empty() && stem.back() != '_') {
            stem += '_';
        }
    }
    while (!stem.empty() && stem.back() == '_') stem.pop_back();
    if (stem.empty()) stem = "army";
    return data_root_ / "armies" / (stem + ".json");
}

std::optional<std::string> ArmyBuilder::save_user(Side side) { return save(side, user_army_path(side)); }

void ArmyBuilder::select(Side side, std::optional<std::size_t> unit) {
    selected_side_ = side;
    selected_unit_ = unit && *unit < list(side).units.size() ? unit : std::nullopt;
}

// ---- Views ----

int ArmyBuilder::points(Side side) const { return core::army_points(list(side), resolve_); }

int ArmyBuilder::unit_points(Side side, std::size_t unit) const {
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    if (!sheet || !sheet->list_building) return 0;
    const auto& units = list(side).units;
    int earlier = 0;
    for (std::size_t i = 0; i < unit; ++i) earlier += units[i].unit_file == units[unit].unit_file ? 1 : 0;
    return core::unit_points(*sheet, units[unit], earlier);
}

int ArmyBuilder::models(Side side, std::size_t unit) const {
    if (check_unit(side, unit)) return 0;
    int n = 0;
    for (const core::ArmyModelGroup& g : list(side).units[unit].models) n += g.count;
    return n;
}

std::vector<BuilderProblem> ArmyBuilder::problems(Side side) const {
    std::vector<BuilderProblem> out;
    for (const core::ArmyProblem& p : core::army_problems(list(side), faction(side), resolve_)) {
        out.push_back(BuilderProblem{p.unit, p.reason});
    }
    for (const std::string& p : core::battle_problems(armies_[0], armies_[1])) out.push_back(BuilderProblem{std::nullopt, p});
    return out;
}

std::vector<std::string> ArmyBuilder::unit_problems(Side side, std::size_t unit) const {
    std::vector<std::string> out;
    for (const BuilderProblem& p : problems(side)) {
        if (p.unit == unit) out.push_back(p.text);
    }
    return out;
}

std::vector<GroupView> ArmyBuilder::groups(Side side, std::size_t unit) const {
    std::vector<GroupView> out;
    if (check_unit(side, unit)) return out;
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    for (const core::ArmyModelGroup& g : list(side).units[unit].models) {
        GroupView v{g.type, g.count, g.weapons, {}, true};
        const core::ModelWargear* w =
            sheet && sheet->list_building ? sheet->list_building->find_wargear(g.type) : nullptr;
        if (w != nullptr) {
            const core::SlotFit fit = core::loadout_fits_slots(*w, g.weapons);
            v.fits = fit.fits();
            for (std::size_t s = 0; s < w->slots.size(); ++s) {
                SlotView slot;
                slot.name = w->slots[s].name;
                for (const core::WargearOption& o : w->slots[s].options) {
                    slot.options.push_back(OptionView{o.name, o.points, core::option_refusal(*sheet, o)});
                }
                if (fit.fits()) slot.chosen = fit.options[s];
                v.slots.push_back(std::move(slot));
            }
        }
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<ModelTypeView> ArmyBuilder::model_types(Side side, std::size_t unit) const {
    std::vector<ModelTypeView> out;
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    if (!sheet) return out;
    for (const core::ModelTypeSheet& mt : sheet->model_types) {
        ModelTypeView v{mt.name, mt.min, mt.max, 0, core::unavailable_reason(*sheet, mt.name), false};
        for (const core::ArmyModelGroup& g : list(side).units[unit].models) {
            if (g.type == mt.name) v.count += g.count;
        }
        if (sheet->list_building) {
            if (const core::ModelWargear* w = sheet->list_building->find_wargear(mt.name)) v.has_options = !w->slots.empty();
        }
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<AddUnitView> ArmyBuilder::add_list(Side side) const {
    std::vector<AddUnitView> out;
    for (const std::string& file : faction(side).unit_files) {
        AddUnitView v;
        v.unit_file = file;
        v.copies = copies(side, file);
        try {
            const core::Datasheet sheet = resolve_(file);
            v.name = sheet.name;
            if (!sheet.list_building) {
                v.refusal = "D-079: '" + sheet.name + "' has no list-building data";
            } else {
                v.limit = sheet.list_building->max_copies_incursion;
                if (const std::optional<core::ArmyUnit> u = core::default_unit(sheet, file)) {
                    v.points = core::unit_points(sheet, *u, 0);
                } else {
                    v.refusal = "D-079: no playable '" + sheet.name + "' unit can be made";
                }
                if (!v.refusal && v.copies >= v.limit) {
                    v.refusal = "D-079: an Incursion army may include at most " + std::to_string(v.limit) + " '" +
                                sheet.name + "' units";
                }
            }
        } catch (const std::exception& e) {
            v.name = file;
            v.refusal = std::string("D-079: ") + e.what();
        }
        out.push_back(std::move(v));
    }
    return out;
}

void ArmyBuilder::refresh_files() {
    files_.clear();
    for (const auto& [dir, stock] : {std::pair{std::string("data/armies"), true}, std::pair{std::string("armies"), false}}) {
        for (const std::string& path : list_json_files(data_root_, dir)) {
            try {
                const core::ArmyList l = core::load_army(data_root_ / path);
                files_.push_back(ArmyFile{path, l.name, l.faction, stock});
            } catch (const std::exception&) {
                // Not an army list: not offered.
            }
        }
    }
}

std::vector<ArmyFile> ArmyBuilder::army_files(Side side) const {
    std::vector<ArmyFile> out;
    for (const ArmyFile& f : files_) {
        if (f.faction == list(side).faction) out.push_back(f);
    }
    return out;
}

std::optional<core::Unit> ArmyBuilder::preview_unit(Side side, std::size_t unit) const {
    const std::optional<core::Datasheet> sheet = datasheet(side, unit);
    if (!sheet) return std::nullopt;
    if (!core::unit_problems(*sheet, list(side).units[unit]).empty()) return std::nullopt;
    // D-081's hand deployment: the unit is built without a formation (into GameState::undeployed).
    using Json = nlohmann::json;
    const auto side_json = [](const char* name, const char* edge, Json units) {
        Json zone = Json::object();
        zone["edge"] = edge;
        zone["depth_in"] = core::kCustomBattleZoneDepthIn;
        Json j = Json::object();
        j["name"] = name;
        j["deployment_zone"] = std::move(zone);
        j["units"] = std::move(units);
        return j;
    };
    Json entry = Json::object();
    entry["unit_file"] = list(side).units[unit].unit_file;
    entry["models"] = list(side).units[unit].models;
    Json battlefield = Json::object();
    battlefield["width_in"] = core::kCustomBattleWidthIn;
    battlefield["depth_in"] = core::kCustomBattleDepthIn;
    Json sides = Json::object();
    sides["A"] = side_json("Preview", "south", Json::array({entry}));
    sides["B"] = side_json("-", "north", Json::array());
    Json scenario = Json::object();
    scenario["name"] = "Datasheet preview";
    scenario["battlefield"] = std::move(battlefield);
    scenario["battle_rounds"] = core::kCustomBattleRounds;
    scenario["first_turn"] = "A";
    scenario["deployment"] = "alternating";
    scenario["sides"] = std::move(sides);
    try {
        core::GameState gs = core::build_game_state(scenario, resolve_);
        if (gs.undeployed.size() != 1) return std::nullopt;
        core::Unit u = std::move(gs.undeployed.front());
        u.side = side;
        return u;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::string> ArmyBuilder::start_problem() const {
    for (const Side side : {Side::A, Side::B}) {
        const std::vector<BuilderProblem> p = problems(side);
        if (!p.empty()) return side_label(side) + ": " + plain_first(p.front().text);
    }
    return std::nullopt;
}

bool ArmyBuilder::can_start() const { return !start_problem().has_value(); }

CustomBattleSetup ArmyBuilder::setup() const { return CustomBattleSetup{armies_, first_turn_, deployment_}; }

nlohmann::json ArmyBuilder::battle_scenario() const {
    return core::custom_battle_scenario(armies_[0], armies_[1], first_turn_, factions_, resolve_, deployment_, mission());
}

core::GameState ArmyBuilder::battle_state() const { return custom_battle_state(setup(), factions_, resolve_, mission()); }

void ArmyBuilder::load_mission(const fs::path& scenario_file) { mission_ = core::load_mission_block(scenario_file); }

}  // namespace wh40k::client
