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
  game plays exactly as at M86.
- For custom battles instead of `--scenario` (M72, M74): `--army-a <file>` / `--army-b <file>` (army lists),
  `--random-armies 1` (a random legal army per side, drawn from each game's seed), `--first A|B` and `--deploy auto|hand`
  (hand: the bots set the units up in the deployment step).
  Since M86 (D-084) these battles play the objective mission, its block read from
  `data/scenarios/incursion_objectives_44x60.json`, as custom battles do in the client. `--mission <scenario file>`
  (M87) reads the block from another scenario instead (for tuning the mission's numbers); with `--scenario` it exits 2.

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
