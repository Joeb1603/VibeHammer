# Bot arena and the identity set

Moved from `CLAUDE.md` in M84; rewritten for one bot in M85 (D-085). `CLAUDE.md` keeps a pointer here; read this file
when the task needs it.

There is one bot (Normal, D-085). Easy, Hard and the hidden regression levels are in git under the tag
`bots-three-levels`.

Bot strength is measured with the Release arena: `cmake --build --preset release --target wh40k_bot_arena`, then
`.\build\default\src\ai\Release\wh40k_bot_arena.exe --games 200`. Flags:
- `--games`, `--scenario`, `--seed`, `--step-cap` (default 5000 steps; a Strike game takes about 300).
- `--per-game 1`: one line per game with hashes of its actions, final state and decision trace, for proving play
  unchanged (M54).
- `--trace <seed>`: one game's bot decisions with their reasons and times.
- `--weights key=value,...` sets the move score's weights (`normal::MoveWeights`) for both sides, and `--weights-a` /
  `--weights-b` for one side, applied in the order given (D-085). The keys are `delay`, `survival`, `edge`, `coherency`,
  `approach`, `cohesion`, `follow` and `approach_alone`; any other key exits 2. The header line names each side's weights
  ("default" when none are given). M87 (D-086) adds `objective`, `objective_approach` and `objective_fire`: the objective
  terms, the approach to an area and the shooting at enemy holders, under a mission only; with all three 0 a mission
  game plays exactly as at M86. M89 (D-090) adds `cover`, 0 to 1 (another value exits 2): how far the move
  score believes what terrain does to its shots, on a battlefield with terrain only. At 0 a terrain game plays exactly
  as at M88, so the M88 bot is `--weights-a cover=0` (or `--weights-b`). M91 (D-092) adds five keys, all 0 by default
  (none measured better, so the bot at the defaults is the M90 bot): `plan_terrain` and `aim_terrain` (0 to 1: terrain in
  the army plan and the shooting aim's reply), `hold_fire` (0 or 1: holding fire to stay hidden, 13.09),
  `objective_charge` and `objective_fight` (0 or more, x `objective`: the objective terms in charges and in pile-ins
  and consolidations). Any other value exits 2. The features on: `--weights
  plan_terrain=1,aim_terrain=1,hold_fire=1,objective_charge=1,objective_fight=1`. M93 (D-094) adds `gone_to_ground`
  (0 or 1, default 0: measured +8 games in 400, not better): 13.11.01 in the move score's guess of the enemy's reply.
  M95 (D-096) adds `command_reroll` and `insane_bravery` (0 or 1 each): the bot plays Command Re-roll (15.02) and
  Insane Bravery (15.04). With both 0 its session declines every reaction window for it, so it never sees one.
  `command_reroll` defaults to 1 (measured +44 games in 400), `insane_bravery` to 0 (+8, not better). M96 (D-097) adds
  `fire_overwatch`, `heroic_intervention` and `counteroffensive` (0 or 1 each): the opponent-turn stratagems (15.08 with
  15.09's snap shot, 15.11, 15.12). The session declines a window that offers the bot only stratagems whose key is 0,
  so at 0 it never sees one. `fire_overwatch` defaults to 1 (measured +26 games in 400), `heroic_intervention` and
  `counteroffensive` to 0 (+2 and +0, not better). M97 (D-099) adds `explosives` and `crushing_impact` (0 or 1 each):
  the own-turn stratagems that end in mortal wounds (15.05 at the Shoot decision, 15.06 just after a charge move). At 0
  the session hides Explosives' answers from the bot (`GameSession::legal_offered`; the bot's own list leaves them out
  too) and declines Crushing Impact's window, so the bot never sees either. Both default to 0 (Explosives measured +6
  games in 400, Crushing Impact -10, both -4: not better). M98 (D-100): with `crushing_impact` at 1 the charge planning
  also prices Crushing Impact at each planned charge move's end (measured again: -8, both -2; still 0).
- For custom battles instead of `--scenario` (M72, M74): `--army-a <file>` / `--army-b <file>` (army lists),
  `--random-armies 1` (a random legal army per side, drawn from each game's seed), `--first A|B` and `--deploy auto|hand`
  (hand: the bots set the units up in the deployment step).
  Since M86 (D-084) these battles play the objective mission, its block read from
  `data/scenarios/incursion_objectives_44x60.json`, as custom battles do in the client. `--mission <scenario file>`
  (M87) reads the block from another scenario instead (for tuning the mission's numbers); with `--scenario` it exits 2.
  M93 (D-094): with `--terrain` and no `--mission` the block is read from the terrain file (its objectives are its
  terrain areas; the objective scenario's are 14.01.01 markers, which no terrain area can be).
  `--terrain <scenario file>` (M88, D-087) fights these battles over that scenario's terrain block (e.g.
  `data/scenarios/incursion_terrain_44x60.json`, whose areas link to the mission's objectives); without it the
  battlefield is bare as before, and with `--scenario` it exits 2 (a scenario has its own terrain).

It exits 1 on any rejected action, reverted move or unfinished game, and 2 on a bad flag (`--a` / `--b` included: they
went with the levels).

The bot scenarios are `data/scenarios/bot_termagants_first.json` and `bot_20_termagants.json` (M44), and the
several-unit `strike_44x60.json` (M53) and `rosters_44x60.json` (M67). The objective scenario
`incursion_objectives_44x60.json` (M86, D-084) is the rosters plus the mission. Since M87 (D-086) the bot plays for the
objectives there and in custom battles; with the objective weights 0 it plays the objective scenario exactly as the
rosters (the same actions, game for game), as at M86. With one unit per side the bot plays exactly as
before M56; the roster work (M68, D-077) is gated on facts the older scenarios don't have, such as the points rule, mixed
loadouts, [HAZARDOUS], MONSTERS and units over 20 wounds. Every Teamwork behaviour (M78–M83, D-083) is off with one unit
a side.

## The identity set

A bot change that should not change play must keep the `--per-game` lines (`game ...`, everything but the timing line)
identical, before and after, for 200 games each of:
- V1 (`v1_skirmish.json`), `bot_termagants_first.json`, `bot_20_termagants.json`, `strike_44x60.json` and
  `rosters_44x60.json`;
- `--random-armies 1`, and `--random-armies 1 --deploy hand`;
- `--army-a tests/data/armies/m73_chaos_space_marines.json --army-b tests/data/armies/m73_tyranids.json`.

Run the old and the new Release arena on the same flags (keep a copy of the old exe) and `diff` the `game` lines. M85
proved its changes this way: all 1,600 games identical.

M86 (D-084) made the random-armies and army-list battles play the mission. Their `actions` and `trace` hashes stayed
identical, but their `state` hashes and results changed, so the M86 binary is the new reference for those three runs.
Add `incursion_objectives_44x60.json` to the set from M86 on.

M87 (D-086) changes play only under a mission. The proof that nothing else changed: V1, both bot scenarios, Strike and
the rosters at the default weights, and the four mission runs (the objective scenario, random armies auto and hand, the
M73 lists) with `--weights objective=0,objective_approach=0,objective_fire=0`, all identical to the M86 binary (M87:
1,800 of 1,800 games). With the default weights the M87 binary is the reference for the four mission runs.

M88 (D-087–D-089) changes nothing without terrain. All nine runs (the eight above and the objective scenario) at the
default weights were identical to the M87 binary (1,800 of 1,800 games). The M88 binary is the next reference. The
terrain scenario (`incursion_terrain_44x60.json`) and the terrain runs (`--terrain`) are new; M89 changes them on
purpose.

M89 (D-090) changes play only on a battlefield with terrain. The proof: the nine runs above at the default weights,
and the four terrain runs (the terrain scenario with `--first A` and with `--first B`, `--random-armies 1 --terrain
data/scenarios/incursion_terrain_44x60.json` with `--deploy auto` and `--deploy hand`) at `--weights cover=0`, all
identical to the M88 binary. At the default weights the M89 binary is the reference for the four terrain runs; add
them to the set from M89 on.

M90 (D-091) changes the rules under a mission (12.08 Objective Consolidation) and makes the flamers selectable (24.18
[IGNORES COVER]), so the random armies draw them. The proof that nothing else changed: V1, both bot scenarios, Strike
and the rosters (no mission, no flamer) identical to the M89 binary (M90: 1,000 of 1,000 games). Every mission run (the
objective scenario, the terrain runs, the M73 lists) and every `--random-armies` run changes on purpose; the M90 binary
is their reference. The M89 identity step "the terrain runs at `cover=0` as the M88 binary" no longer holds under M90's
rules (`M89_Regression` pins the M88 bot's game under M90's rules instead).

M91 (D-092) changes no play at the defaults (its five keys default to 0). The proof: all thirteen runs above (V1, both
bot scenarios, Strike, the rosters, the objective scenario, the terrain scenario with `--first A` and `--first B`,
random armies auto and hand, with and without `--terrain`, the M73 lists) at the defaults are identical to the M90
binary, and the M90 binary stays the reference for every run. The head-to-head procedure for a feature (as M89's and
M91's): the variant with `--weights-a <keys>` against the reference with `--weights-b <keys at 0>` and the reverse, each
with `--first A` and `--first B`, 100 games each (seeds 1–100); one standard deviation is about 10 games in the 400.

M94 (D-095) makes every dice roll a step of its own in a `GameSession` (the arena plays through one: the session makes
both bots' rolls), so each game's `steps` and `actions` change by construction, and nothing else may. The proof: all
thirteen runs above, 200 games each (2,600 games), identical to the M93 binary in every field but `steps` and `actions`
(every `state` and `trace` the same; every game has more steps). From M94 on, compare the per-game lines without those
two fields against an older binary, or whole against the M94 binary, which is the reference for every run. The M91,
M89 and M87 pins re-pinned `actions` only (`M78_Arena`'s whole line its steps too). The bot's simulations roll inline
(`apply_action`), so its decisions do the same work as before.

M95 (D-096) adds a step for each reaction window the session declines for a bot whose stratagem keys are off (at the
defaults, every window), and rolls a random D per attack, so `steps` and `actions` change again and nothing else may.
The proof: all thirteen runs, 200 games each (2,600 games), with both stratagem keys off are identical to the M94
binary in every field but `steps` and `actions` (every `state` and `trace` the same; no game has fewer steps). The
M91, M89 and M87 pins re-pinned `actions` only (`M78_Arena`'s whole line its steps too).

M99 (D-101) implements [ANTI-X Y+], [DEVASTATING WOUNDS], [MELTA X] and [LANCE], so the weapons with them can be picked
and the random armies draw them. No shipped scenario and no stock or M73 list equips one, so the proof that nothing else
changed is the nine runs with fixed loadouts (V1, both bot scenarios, Strike, the rosters, the objective scenario, the
terrain scenario with `--first A` and `--first B`, the M73 lists): 200 games each, identical to the M98 binary in every
field (1,800 of 1,800 games). The four `--random-armies` runs (auto and hand, with and without `--terrain`) change on
purpose; the M99 binary is their reference (each 200 of 200 clean).
From M99 on, each milestone that makes a weapon ability usable changes the random-armies runs again; the nine fixed runs
stay the identity set for them.

M95's head-to-head (D-092's procedure on the terrain scenario, 400 games per variant against the bot playing no
stratagem) gave Command Re-roll +44 (222-178), Insane Bravery +8 (204-196) and both +46 (223-177). So Command Re-roll
is on by default and the default bot's games change on purpose: the M95 binary at the defaults is the reference for
every run, and `--weights command_reroll=0` plays the identity runs above. The older bots' pins and the bot-game
replays play without stratagems (`tests/ai/bot_configs.hpp`).

M96 (D-097) adds a step for each window that follows no roll (Fire Overwatch, Heroic Intervention, Counteroffensive),
declined by the session for a bot that doesn't play them, so `steps` and `actions` change again and nothing else may.
The proof: all thirteen runs, 200 games each (2,600 games), at `--weights command_reroll=0` are identical to the M95
binary at `command_reroll=0` in every field but `steps` and `actions` (every `state` and `trace` the same; 19,179 steps
added, no game with fewer). At the defaults every per-game field but `steps`, `actions` and `trace` is identical to the
M95 binary (every `state`): the default bot's session now declines Insane Bravery's windows itself, so its trace loses
those decline lines and gains none. The M78, M87, M89 and M91 pins re-pinned `actions` (and `M78_Arena`'s steps).

M96's head-to-head (D-092's procedure on the terrain scenario, 400 games per variant against the bot at M96's other
defaults) gave Fire Overwatch +26 (213-187), Heroic Intervention +2 (201-199), Counteroffensive +0 (200-200) and all
three +24 (212-188). So Fire Overwatch is on by default and the default bot's games change on purpose again: the M96
binary at the defaults is the reference for every run, and `--weights fire_overwatch=0` plays the default identity runs
above. The older bots' pins and the bot-game replays play without stratagems (`without_stratagems()` turns the new keys
off too).

M97 (D-099) adds a step for each Crushing Impact window (just after a MONSTER or VEHICLE ends a charge move in its own
Charge phase, with CP to spend) declined by the session for a bot whose `crushing_impact` key is 0, and hides Explosives'
answers from it, so `steps` and `actions` change and nothing else may. The proof: all thirteen runs, 200 games each
(2,600 games), at the defaults are identical to the M96 binary in every field but `steps` and `actions` (every `state`
and `trace` the same; 1,170 steps added, no game with fewer; `actions` changed in exactly the 696 games that gained a
step, none in V1, the two bot scenarios or Strike). The M89 and M91 pins re-pinned `actions` (the M89 terrain game and the
M91 objective game have such windows).

M97's head-to-head (D-092's procedure on the terrain scenario, 400 games per variant against the bot at M97's other
defaults) gave Explosives +6 (203-197), Crushing Impact -10 (195-205) and both -4 (198-202), every run clean. So both
keys stay off by default and the default bot plays as at M96: the M96 binary at the defaults stays the reference for every
run (`steps` and `actions` aside). `without_stratagems()` turns the new keys off too.

M98 (D-100) prices Crushing Impact in the charge planning only with `crushing_impact` at 1, so at the defaults nothing is
computed: all thirteen runs (2,600 games) are identical to the M97 binary in every field, `steps` and `actions` included.
Its head-to-head (the same procedure) gave Crushing Impact -8 (196-204) and both keys -2 (199-201), every run clean, so
both keys stay off, and the M97 binary at the defaults is the reference for every field from here.

## Deployment and the summary

- The deployment step only exists in a scenario with `"deployment": "alternating"` (D-081). The bot sets up by M77's
  heuristic; M74's policy (Auto-place's spots in id order) answers when the heuristic has none.
- For a mission scenario (D-084, M86) the summary adds:
  - the games decided on VP and on points destroyed (equal VP), and the draws;
  - the mean objective VP, kill VP and points destroyed per side by the end.
- The summary has three teamwork lines per game and side (M78): gang-ups (an enemy unit engaged by two or more of ours
  at a Fight phase's start) and lone engagements; shooting spread (distinct targets per Shoot, the top target's share of
  a phase's selections); contact (units never within 12" of an enemy unit, first engagement round). The `--per-game`
  line is unchanged.
- M87 (D-086) adds, for a mission scenario, each area's mean VP per side, and for every scenario the largest melee: at
  each Fight phase's start, the most units (both sides) in one connected group of engagements, as a count and as a share
  of the units on the battlefield (each game's maximum, averaged).
- M88 (D-087–D-089) adds, on a battlefield with terrain, the `terrain (M88)` line, by the shooting side:
  - the ranged weapon selections whose target had the benefit of cover (13.08), as a count and a share;
  - per game, the enemy units within a shooting unit's longest range that none of its models could see because they
    were hidden (13.09), or because of obscuring areas or dense walls (13.10 / 13.11), counted before each Shoot.

  The `--per-game` line is unchanged.
- M89 (D-090) adds, on a battlefield with terrain, the `cover (M89)` line: the same counts as the `terrain (M88)` line
  read by the target's side (the enemy's ranged weapon selections into each side's units that had cover, and each
  side's units refused to an enemy shooter for Hidden or for terrain, per game). The `--per-game` line is unchanged.
- M90 (D-091) adds, for a mission scenario, `objective consolidations a game (M90): A x, B y`: the Objective
  Consolidations (12.08) committed per game and side. The `--per-game` line is unchanged.
- M91 (D-092) adds, for a mission scenario, `charges onto an objective a game (M91)` (committed charge moves with a model
  within range of an area), and with terrain `holds of fire a game (M91)` (units able to shoot, every model hidden, when
  the bot ended a Shoot step). The `--per-game` line is unchanged.
