# Bot arena, bot levels and the identity set

Moved verbatim from `CLAUDE.md` in M84. `CLAUDE.md` keeps a pointer here; read this file when the task needs it.

Bot strength is measured with the Release arena: `cmake --build --preset release --target wh40k_bot_arena`, then `.\build\default\src\ai\Release\wh40k_bot_arena.exe --games 200 --a easy --b easy` (flags `--games`, `--a`, `--b` (levels `easy`, `normal`, `hard`, plus the hidden `easy-m55` / `normal-m55`, which play as at M55 for regression comparisons, D-066, and `normal-m77` / `hard-m77`, which play as before the Teamwork batch, D-083), `--scenario`, `--seed`, `--step-cap` (default 5000 steps; a Strike game takes about 300), `--per-game 1` (one line per game with hashes of its actions, final state and decision trace, for proving play unchanged, M54), `--trace <seed>` (one game's bot decisions with their reasons and times) `--weights key=value,...` (overrides level Hard's `BotWeights` for tuning, M44), and for custom battles instead of `--scenario` (M72, M74) `--army-a <file>` / `--army-b <file>` (army lists), `--random-armies 1` (a random legal army per side, drawn from each game's seed), `--first A|B` and `--deploy auto|hand` (hand: the bots set the units up in the deployment step); it exits 1 on any rejected action, reverted move or unfinished game). The bot scenarios are `data/scenarios/bot_termagants_first.json` and `bot_20_termagants.json` (M44), and the several-unit `strike_44x60.json` (M53) and `rosters_44x60.json` (M67). With one unit per side the bots play exactly as before M56/M57; changes to the multi-unit path must keep 1v1 `--per-game` output identical. The roster work (M68 D-077, M69 D-078) is gated on facts the older scenarios don't have, such as the points rule, mixed loadouts, [HAZARDOUS], MONSTERS, units over 20 wounds and sides of more than 7 units. V1, both bot scenarios and Strike played exactly as before it (proved with `--per-game`).
Custom battles and hand deployment (M71–M77) changed nothing there either. The Teamwork batch (M78–M83, D-083)
changed Normal and Hard on Strike, the rosters and custom battles on purpose (from M80). The "identity set", checked
with `--per-game` against the pre-batch baseline, is now:
- V1 and both bot scenarios (every pairing) and every Easy vs Easy game play exactly as before;
- the hidden levels `normal-m77` / `hard-m77` play as the pre-batch bots on every scenario
  (`M78_Regression.TheM77LevelsMatchThePreBatchHashes` pins five of their games to the pre-batch hashes).
- Every batch behaviour is gated on `!pre_teamwork(generation)` (`src/ai/generation.hpp`) and is off with one unit a side.

The deployment step only exists in a scenario with `"deployment": "alternating"` (D-081); Easy sets up at Auto-place's
spots, Normal and Hard by M77's heuristic (never building Hard's chain tables during deployment).
- In the arena, two Hard bots share one chain cache. A cache never changes a decision.
- Hard builds missing chain tables on up to 4 threads, all joined before a decision returns (`--weights table_workers=1` builds them serially).
- On the rosters Hard uses about 0.6 GB, and a side of more than 7 units (the Tyranids) moves as Normal does (`move_search_units`).
- The summary has three teamwork lines per game and side (M78): gang-ups (an enemy unit engaged by two or more of
  ours at a Fight phase's start) and lone engagements; shooting spread (distinct targets per Shoot, the top
  target's share of a phase's selections); contact (units never within 12" of an enemy unit, first engagement round).
  The `--per-game` line is unchanged.
