#pragma once

// M72 (D-079 part 2): army lists for tests: the faction data, a caching datasheet resolver, the stock rosters as army
// lists, and two legal non-stock "showcase" armies with loadouts no shipped scenario plays (Havoc autocannon, reaper
// chaincannon and missile launcher, Terminator power fists and paired accursed weapons, Legionary specials, a Hive
// Tyrant with two scything talons, Termagant specials, six Tyrant Guard, both Tyrannofex cannons).

#include <string>
#include <vector>

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

// A custom battle's initial state (not started), side A first unless `first` says otherwise.
inline core::GameState custom_battle(const core::ArmyList& a, const core::ArmyList& b,
                                     core::Side first = core::Side::A) {
    return core::build_game_state(core::custom_battle_scenario(a, b, first, factions(), army_resolver()),
                                  army_resolver());
}

// The random armies of an arena game with `seed` (wh40k_bot_arena --random-armies 1): Chaos Space Marines for side A
// and Tyranids for side B.
inline core::GameState random_armies_battle(std::uint64_t seed) {
    return custom_battle(core::random_army(csm(), army_resolver(), core::random_army_seed(core::Side::A, seed)),
                         core::random_army(tyranids(), army_resolver(), core::random_army_seed(core::Side::B, seed)));
}

}  // namespace wh40k::test
