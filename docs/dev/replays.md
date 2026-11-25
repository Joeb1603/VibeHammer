# Replays: regenerating and checking them

Moved verbatim from `CLAUDE.md` in M84. `CLAUDE.md` keeps a pointer here; read this file when the task needs it.

## Regeneration commands

```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux && (cd build/linux && ctest)
# Regenerate tests/replays/ on purpose only, then check the diff:
./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=Replay.DISABLED_RegenerateReplays
# The bot-game replay m46_bot_fight_moves.json (20 Termagants, seed 1) comes from the ai tests instead (M46):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M46_Replay.DISABLED_RegenerateFightMoveReplay
# and m68_rosters_normal_bot_game.json (the rosters, seed 5) likewise (M68):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M68_Replay.DISABLED_RegenerateRostersNormalBotGameReplay
# and m73_custom_bot_game.json (two custom lists, seed 1) likewise (M73):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M73_Replay.DISABLED_RegenerateCustomBotGameReplay
# and m77_custom_hand_deploy_bot_game.json (custom lists set up by hand, seed 1) likewise (M77):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M77_Replay.DISABLED_RegenerateCustomHandDeployBotGameReplay
# and m88_terrain.json (the terrain scenario, seed 80: bot play with one scripted detour) likewise (M88):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M88_Replay.DISABLED_RegenerateTerrainReplay
# and m89_cover_bot_game.json (the terrain scenario, a whole bot game playing for cover) likewise (M89):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M89_Replay.DISABLED_RegenerateCoverBotGameReplay
# and m87_objectives_bot_game.json (the objective scenario, seed 29, a whole bot game playing the mission) (M87):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M87_Replay.DISABLED_RegenerateObjectivesBotGameReplay
# and m91_bot_game.json (the terrain scenario, seed 6, the bot with the five M91 features on) (M91):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M91_Replay.DISABLED_RegenerateM91BotGameReplay
# m94_paced_rolls.json (every kind of roll a step of its own, seed 1) comes from wh40k_tests (M94):
./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D095_Replay.DISABLED_RegenerateM94Replay
```
(The M73 and M77 tests also rewrite their fixture scenario under `tests/data/scenarios/`. M75's builder-battle scenario
is rewritten by `M75_Builder.DISABLED_RegenerateBuilderBattleScenario`.)

Every bot game is the bot (Normal) on both sides (D-085, M85: one bot). The Easy and Hard games m45, m47, m58, m70 and
m75 went with those levels; they are in git under the tag `bots-three-levels`.

On Windows the same two tests are `build/default/tests/Debug/wh40k_tests.exe` and
`build/default/tests/ai/Debug/wh40k_ai_tests.exe` with the same flags.

## Rules for saved replays

- Replays are regenerated only on purpose (`Replay.DISABLED_RegenerateReplays`, and `M46_Replay.DISABLED_RegenerateFightMoveReplay` / `M68_Replay.DISABLED_RegenerateRostersNormalBotGameReplay` / `M73_Replay.DISABLED_RegenerateCustomBotGameReplay` / `M77_Replay.DISABLED_RegenerateCustomHandDeployBotGameReplay` in the ai tests for `m46_bot_fight_moves.json` / `m68_rosters_normal_bot_game.json` / `m73_custom_bot_game.json` / `m77_custom_hand_deploy_bot_game.json`; see Build and test). Existing replay files must come out byte-identical; check with `git diff tests/replays`. The `TheSavedFileMatchesTheGame` tests check that the bot files are what the bot plays now:
  - `M68_Replay`: `m68_rosters_normal_bot_game.json` is what the bot plays.
  - `M73_Replay`: `m73_custom_bot_game.json` is what the bot plays on two custom lists.
  - `M77_Replay`: `m77_custom_hand_deploy_bot_game.json` is what the bot plays, its deployment by hand included.
  - The Teamwork batch (M80–M83, D-083) regenerated m68, m73 and m77 on purpose; m46 stayed byte-identical. (The tests
    that played the pre-batch games with `Generation::M77` went with the switch at M85, D-085.)
- M88: `m88_terrain.json` is bot play on the terrain scenario from battle round 1 with one scripted move (a MONSTER's
  two straight lines round a dense wall, 13.06; the bot itself detours from M89). It is checked by the engine replay
  only (`M88_Replay` in `wh40k_tests` checks its cover, its detour and its hidden target with core alone), with no
  `TheSavedFileMatchesTheGame` test, so bot changes leave it valid. Regenerate it only if the engine's terrain rules
  change. Since M89 (D-090) its generator plays the bot at `cover` 0 (the M88 bot), so regenerating it gives the same
  file.
- M89: `m89_cover_bot_game.json` is a whole bot game on the terrain scenario, the bot playing for cover (D-090): shots
  into cover, a hidden unit refused to a shooter, and the bot's own two-leg moves round a wall. `M89_Replay` replays it
  with core alone, and its `TheSavedFileMatchesTheGame` checks that it is what the bot plays now.
- M90 (D-091): `m90_objective_consolidation.json` is scripted by `Replay.DISABLED_RegenerateReplays` from an embedded
  initial state built from the objective scenario (seed 1): the Chaos Terminators destroy two Termagants south of the
  Centre and make an Objective Consolidation onto it (12.08). `D091_Replay` checks it and that the file is what the
  builder plays now. M90 regenerated `m87_objectives_bot_game.json` on purpose: Objective Consolidation gives Side B a
  Consolidate decision at step 550 (6 added lines, the same seed and checks); every other replay stayed byte-identical.
- M91 (D-092): `m91_bot_game.json` is a whole bot game on the terrain scenario (seed 6) with the five M91 features
  switched on (they default to 0): charges onto an objective and an Objective Consolidation. `M91_Replay` replays it with
  core alone, and its `TheSavedFileMatchesTheGame` checks it is what the bot with those weights plays now. The m87 and
  m89 generators are pinned to the five keys at 0, and every older replay stayed byte-identical.
- M92 (D-093): `m92_retarget.json` is scripted by `Replay.DISABLED_RegenerateReplays` (seed 2, picked by
  `Replay.DISABLED_SearchM92Seed`) from an embedded V1 state with a second Termagant unit: Focused Fire's target is
  destroyed by the first batch and the attacker gives it the other unit (04.03.03). `D093_Replay` checks it. Every older
  replay stayed byte-identical.
- M93 (D-094): the objective scenario's objectives became 14.01.01 markers, so two replays were regenerated on purpose:
  `m87_objectives_bot_game.json` (the same seed and checks, the game played over the markers) and
  `m90_objective_consolidation.json` (its setup 5" further north, so the Terminators start within range of the Centre
  marker and consolidate staying within range; for a marker "within 3" of the objective" is being within range). Every
  other replay stayed byte-identical; the terrain scenario's bot games did not change (`gone_to_ground` defaults to 0).
- M94 (D-095): **format 2.** Every dice roll is a `RollDice` decision of its roller's, and a format-2 replay records
  each one as a step (`run_replay` plays it paced: `start_game_paced`, `apply_action_paced`); format 1 (every roll made
  inside the step that leads to it) still runs inline. All 28 replays were regenerated on purpose as format 2, with
  their description, initial state and `expect` block unchanged and the same steps once the 1009 roll steps are taken
  out (a script checked each file against `git show HEAD:`). The core builders still script the players' choices with
  the rolls inline; `save_if_clean` writes them through `with_roll_steps`, and the "the file is what the builder plays
  now" checks and every `TheSavedFileMatchesTheGame` compare both sides as format 2. Code that re-applies a file's
  steps follows its format (`M88_Replay`). `m94_paced_rolls.json` (`D095_Replay`) answers every kind of roll, a
  Desperate Escape's hazard and battle-shock rolls among them, and is checked against the same game played inline.
  `tests/data/legacy/` keeps `m8_shooting` and `m62_random_characteristics` as M93 wrote them (format 1) and an M93
  save: `D095_Replays` and `M94_Saves` load them. `M94_Legacy` checks the save byte for byte against the same game's
  save now with its roll steps taken out (its generator writes that, so re-running it rewrites the same file). `D095_Closure.DISABLED_DumpReplayEvents` (with
  `WH40K_EVENT_DUMP_DIR`) writes every replay's events and final state: the M93 and M94 dumps were byte-identical.
