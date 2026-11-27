#pragma once

// M72 (D-079 part 2): army lists for tests: the faction data, a caching datasheet resolver, the stock rosters as army
// lists, and two legal non-stock "showcase" armies with loadouts no shipped scenario plays (Havoc autocannon, reaper
// chaincannon and missile launcher, Terminator power fists and paired accursed weapons, Legionary specials, a Hive
// Tyrant with two scything talons, Termagant specials, six Tyrant Guard, both Tyrannofex cannons).

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/data/army_list.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/state/game_state.hpp"
#include "test_helpers.hpp"

namespace wh40k::test {

inline const std::vector<core::Faction>& factions() {
    static const std::vector<core::Faction> f = core::load_factions(repo_root());
    return f;
}

inline const core::Faction& faction(const std::string& id) {
    const core::Faction* f = core::find_faction(factions(), id);
    if (f == nullptr) throw std::logic_error("no faction " + id);
    return *f;
}

inline const core::Faction& csm() { return faction("chaos_space_marines"); }
inline const core::Faction& tyranids() { return faction("tyranids"); }

inline const core::DatasheetResolver& army_resolver() {
    static const core::DatasheetResolver r = core::caching_datasheet_resolver(repo_root());
    return r;
}

inline core::ArmyList stock_army(const core::Faction& f) { return core::load_army(repo_root() / f.default_army); }

// M99 (D-101): the refusal tests' data. The engine refuses a weapon with an ability it does not implement (D-026,
// D-071), and the list building greys it out (D-079). Each milestone that implements an ability takes weapons off the
// real refused list, so the tests of the mechanism run on a copy of data/ where D-079 part 1's refused weapons (as at
// M90) carry an ability the engine will never know, [CONVERSION] here: their cases stay whatever the engine learns.
// Built once per test process, under the temp directory.
inline constexpr const char* kStandInAbility = "Conversion";
// The directory is removed when the test process ends.
struct TempDataRoot {
    std::filesystem::path dir;
    ~TempDataRoot() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
inline const std::filesystem::path& refused_data_root() {
    static const TempDataRoot root = [] {
        namespace fs = std::filesystem;
        // A name of its own per test process (ctest runs them side by side): test support, not game logic.
        std::random_device rd;
        const fs::path dir = fs::temp_directory_path() /
                             ("vibehammer_refused_data_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        fs::remove_all(dir);
        fs::create_directories(dir / "data");
        fs::copy(repo_root() / "data" / "factions.json", dir / "data" / "factions.json");
        for (const char* sub : {"units", "armies", "scenarios"}) {
            fs::copy(repo_root() / "data" / sub, dir / "data" / sub, fs::copy_options::recursive);
        }
        const std::vector<std::string> weapons = {"Daemon hammer", "Combi-weapon",  "Chainfist",  "Reaper autocannon",
                                                  "Meltagun",      "Balefire tome", "Strangleweb"};
        for (const auto& entry : fs::directory_iterator(dir / "data" / "units")) {
            std::ifstream in(entry.path(), std::ios::binary);
            nlohmann::json j = nlohmann::json::parse(in);
            in.close();
            bool changed = false;
            for (const std::string& w : weapons) {
                if (!j.contains("weapons") || !j["weapons"].contains(w)) continue;
                for (auto& profile : j["weapons"][w]["profiles"]) profile["abilities"].push_back(kStandInAbility);
                changed = true;
            }
            if (!changed) continue;
            std::ofstream out(entry.path(), std::ios::binary | std::ios::trunc);
            out << j.dump(2);
        }
        return TempDataRoot{dir};
    }();
    return root.dir;
}
inline const core::DatasheetResolver& refused_resolver() {
    static const core::DatasheetResolver r = core::caching_datasheet_resolver(refused_data_root());
    return r;
}
inline const std::vector<core::Faction>& refused_factions() {
    static const std::vector<core::Faction> f = core::load_factions(refused_data_root());
    return f;
}

inline core::ArmyModelGroup group(std::string type, int count, std::vector<std::string> weapons) {
    return core::ArmyModelGroup{std::move(type), count, std::move(weapons)};
}

// 875 points: a Chaos Lord with an accursed weapon and a power fist; Havocs with a champion's two power fists and one
// each of autocannon, reaper chaincannon, missile launcher and heavy bolter; 10 Terminators (paired accursed weapons
// on the champion and one more, 5 power fists); 10 Legionaries with a reaper chaincannon, a missile launcher, a heavy
// melee weapon and a plasma pistol champion; 5 Legionaries with the plasma pistol and chainsword special.
inline core::ArmyList showcase_csm() {
    core::ArmyList l{"Chaos Space Marines (showcase)", "chaos_space_marines", {}};
    l.units.push_back({"data/units/chaos_lord.json", {group("Chaos Lord", 1, {"Accursed weapon", "Power fist"})}});
    l.units.push_back({"data/units/havocs.json",
                       {group("Havoc Champion", 1, {"Power fist", "Power fist"}),
                        group("Havoc w/ autocannon", 1, {"Havoc autocannon", "Close combat weapon"}),
                        group("Havoc w/ reaper chaincannon", 1, {"Havoc reaper chaincannon", "Close combat weapon"}),
                        group("Havoc w/ missile launcher", 1, {"Havoc missile launcher", "Close combat weapon"}),
                        group("Havoc w/ heavy bolter", 1, {"Havoc heavy bolter", "Close combat weapon"})}});
    l.units.push_back({"data/units/chaos_terminator_squad.json",
                       {group("Terminator Champion", 1, {"Paired accursed weapons"}),
                        group("Power fist and combi-bolter", 5, {"Combi-bolter", "Power fist"}),
                        group("Paired accursed weapons", 1, {"Paired accursed weapons"}),
                        group("Accursed weapon and combi-bolter", 3, {"Combi-bolter", "Accursed weapon"})}});
    l.units.push_back({"data/units/legionaries.json",
                       {group("Aspiring Champion", 1, {"Plasma pistol", "Accursed weapon", "Close combat weapon"}),
                        group("Legionary w/ other weapon", 1, {"Bolt pistol", "Close combat weapon", "Reaper chaincannon"}),
                        group("Legionary w/ other weapon", 1, {"Bolt pistol", "Close combat weapon", "Missile launcher"}),
                        group("Legionary w/ heavy melee weapon", 1, {"Heavy melee weapon", "Bolt pistol", "Close combat weapon"}),
                        group("Legionary w/ chainsword", 6, {"Astartes chainsword", "Bolt pistol", "Close combat weapon"})}});
    l.units.push_back({"data/units/legionaries.json",
                       {group("Aspiring Champion", 1, {"Heavy melee weapon", "Boltgun", "Close combat weapon"}),
                        group("Legionary w/ other weapon", 1,
                              {"Bolt pistol", "Close combat weapon", "Astartes chainsword", "Plasma pistol"}),
                        group("Legionary w/ boltgun", 3, {"Boltgun", "Bolt pistol", "Close combat weapon"})}});
    return l;
}

// 900 points: a Hive Tyrant with two Monstrous scything talons; 20 Termagants (spinefists, devourers, two
// Shardlaunchers, two Spike rifles); 6 Tyrant Guard, two of each loadout; a Tyrannofex with a Rupture cannon and one
// with Acid spray; 10 Termagants with Fleshborers.
inline core::ArmyList showcase_tyranids() {
    core::ArmyList l{"Tyranids (showcase)", "tyranids", {}};
    l.units.push_back({"data/units/hive_tyrant.json",
                       {group("Hive Tyrant", 1, {"Monstrous scything talons", "Monstrous scything talons"})}});
    l.units.push_back({"data/units/termagants.json",
                       {group("Termagants", 8, {"Chitinous claws and teeth", "Termagant spinefist"}),
                        group("Termagants", 8, {"Chitinous claws and teeth", "Termagant devourer"}),
                        group("Termagant w/ Shardlauncher", 2, {"Shardlauncher", "Chitinous claws and teeth"}),
                        group("Termagant w/ Spike rifle", 2, {"Spike rifle", "Chitinous claws and teeth"})}});
    l.units.push_back({"data/units/tyrant_guard.json",
                       {group("Tyrant Guard", 2, {"Scything talons and rending claws"}),
                        group("Tyrant Guard", 2, {"Bone cleaver, lash whip and rending claws"}),
                        group("Tyrant Guard", 2, {"Crushing claws and rending claws"})}});
    l.units.push_back({"data/units/tyrannofex.json",
                       {group("Tyrannofex", 1, {"Powerful limbs", "Stinger salvoes", "Rupture cannon"})}});
    l.units.push_back({"data/units/tyrannofex.json",
                       {group("Tyrannofex", 1, {"Powerful limbs", "Stinger salvoes", "Acid spray"})}});
    l.units.push_back({"data/units/termagants.json",
                       {group("Termagants", 10, {"Fleshborer", "Chitinous claws and teeth"})}});
    return l;
}

// A custom battle's initial state (not started), side A first unless `first` says otherwise. M74 (D-081): with
// DeploymentMode::Hand its units wait to be set up by hand.
inline core::GameState custom_battle(const core::ArmyList& a, const core::ArmyList& b,
                                     core::Side first = core::Side::A,
                                     core::DeploymentMode mode = core::DeploymentMode::Auto) {
    return core::build_game_state(core::custom_battle_scenario(a, b, first, factions(), army_resolver(), mode),
                                  army_resolver());
}

// The random armies of an arena game with `seed` (wh40k_bot_arena --random-armies 1): Chaos Space Marines for side A
// and Tyranids for side B.
inline core::GameState random_armies_battle(std::uint64_t seed,
                                            core::DeploymentMode mode = core::DeploymentMode::Auto,
                                            core::Side first = core::Side::A) {
    return custom_battle(core::random_army(csm(), army_resolver(), core::random_army_seed(core::Side::A, seed)),
                         core::random_army(tyranids(), army_resolver(), core::random_army_seed(core::Side::B, seed)),
                         first, mode);
}

// M99 (D-101): the random armies as they were drawn before M99, from the refusal tests' data (the weapons M99 made
// pickable are still refused there, so they are never drawn): the same armies, weapon for weapon, as
// random_armies_battle gave at M90-M98. For a statistic whose bound was chosen on those draws.
inline core::GameState random_armies_battle_m90_pool(std::uint64_t seed,
                                                     core::DeploymentMode mode = core::DeploymentMode::Auto,
                                                     core::Side first = core::Side::A) {
    const core::Faction* a = core::find_faction(refused_factions(), "chaos_space_marines");
    const core::Faction* b = core::find_faction(refused_factions(), "tyranids");
    if (a == nullptr || b == nullptr) throw std::logic_error("no faction in the refusal tests' data");
    const core::ArmyList army_a = core::random_army(*a, refused_resolver(), core::random_army_seed(core::Side::A, seed));
    const core::ArmyList army_b = core::random_army(*b, refused_resolver(), core::random_army_seed(core::Side::B, seed));
    return core::build_game_state(
        core::custom_battle_scenario(army_a, army_b, first, refused_factions(), refused_resolver(), mode),
        refused_resolver());
}

}  // namespace wh40k::test
