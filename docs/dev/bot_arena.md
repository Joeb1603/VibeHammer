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
  ("default" when none are given).
- For custom battles instead of `--scenario` (M72, M74): `--army-a <file>` / `--army-b <file>` (army lists),
  `--random-armies 1` (a random legal army per side, drawn from each game's seed), `--first A|B` and `--deploy auto|hand`
  (hand: the bots set the units up in the deployment step).
  Since M86 (D-084) these battles play the objective mission, its block read from
  `data/scenarios/incursion_objectives_44x60.json`, as custom battles do in the client.

It exits 1 on any rejected action, reverted move or unfinished game, and 2 on a bad flag (`--a` / `--b` included: they
went with the levels).

The bot scenarios are `data/scenarios/bot_termagants_first.json` and `bot_20_termagants.json` (M44), and the
several-unit `strike_44x60.json` (M53) and `rosters_44x60.json` (M67). The objective scenario
`incursion_objectives_44x60.json` (M86, D-084) is the rosters plus the mission. Until M87 the bot ignores objectives and
plays it exactly as the rosters (the same actions, game for game). With one unit per side the bot plays exactly as
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
