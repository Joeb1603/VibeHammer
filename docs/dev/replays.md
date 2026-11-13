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
