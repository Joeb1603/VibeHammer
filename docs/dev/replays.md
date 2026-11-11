# Replays: regenerating and checking them

Moved verbatim from `CLAUDE.md` in M84. `CLAUDE.md` keeps a pointer here; read this file when the task needs it.

## Regeneration commands

```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux && (cd build/linux && ctest)
# Regenerate tests/replays/ on purpose only, then check the diff:
./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=Replay.DISABLED_RegenerateReplays
# The bot-game replay m45_bot_game.json (Hard vs Hard, seed 9) comes from the ai tests instead (M45):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M45_Replay.DISABLED_RegenerateBotGameReplay
# and m46_bot_fight_moves.json (Normal vs Normal, 20 Termagants, seed 1) likewise (M46):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M46_Replay.DISABLED_RegenerateFightMoveReplay
# and m47_default_casualties.json (Easy vs Easy, 20 Termagants, seed 121) likewise (M47):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M47_Replay.DISABLED_RegenerateDefaultCasualtiesReplay
# and m58_strike_bot_game.json (Hard vs Hard on the Strike scenario) likewise (M58):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M58_Replay.DISABLED_RegenerateStrikeBotGameReplay
# and m68_rosters_normal_bot_game.json (Normal vs Normal on the rosters, seed 5) likewise (M68):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M68_Replay.DISABLED_RegenerateRostersNormalBotGameReplay
# and m70_rosters_bot_game.json (Hard vs Hard on the rosters, seed 93) likewise (M70):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M70_Replay.DISABLED_RegenerateRostersBotGameReplay
# and m73_custom_bot_game.json (Normal vs Normal on two custom lists, seed 1) likewise (M73):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M73_Replay.DISABLED_RegenerateCustomBotGameReplay
# and m75_builder_battle.json (Easy vs Easy on an army-builder battle, seed 1) likewise (M75):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M75_Replay.DISABLED_RegenerateBuilderBattleReplay
# and m77_custom_hand_deploy_bot_game.json (Normal vs Normal, custom lists set up by hand, seed 1) likewise (M77):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M77_Replay.DISABLED_RegenerateCustomHandDeployBotGameReplay
```
(The M73, M75 and M77 tests also rewrite their fixture scenario under `tests/data/scenarios/`.)

On Windows the same two tests are `build/default/tests/Debug/wh40k_tests.exe` and
`build/default/tests/ai/Debug/wh40k_ai_tests.exe` with the same flags.

## Rules for saved replays

- Replays are regenerated only on purpose (`Replay.DISABLED_RegenerateReplays`, and `M45_Replay.DISABLED_RegenerateBotGameReplay` / `M46_Replay.DISABLED_RegenerateFightMoveReplay` / `M47_Replay.DISABLED_RegenerateDefaultCasualtiesReplay` / `M58_Replay.DISABLED_RegenerateStrikeBotGameReplay` / `M68_Replay.DISABLED_RegenerateRostersNormalBotGameReplay` / `M70_Replay.DISABLED_RegenerateRostersBotGameReplay` / `M73_Replay.DISABLED_RegenerateCustomBotGameReplay` / `M75_Replay.DISABLED_RegenerateBuilderBattleReplay` / `M77_Replay.DISABLED_RegenerateCustomHandDeployBotGameReplay` in the ai tests for `m45_bot_game.json` / `m46_bot_fight_moves.json` / `m47_default_casualties.json` / `m58_strike_bot_game.json` / `m68_rosters_normal_bot_game.json` / `m70_rosters_bot_game.json` / `m73_custom_bot_game.json` / `m75_builder_battle.json` / `m77_custom_hand_deploy_bot_game.json`; see Build and test). Existing replay files must come out byte-identical; check with `git diff tests/replays`. The `TheSavedFileMatchesTheGame` tests check that the bot files are what the bots play now:
  - `M47_Replay`: `m47_default_casualties.json` is what Easy plays.
  - `M58_Replay`: `m58_strike_bot_game.json` is what Hard plays, through its first battle rounds.
  - `M68_Replay`: `m68_rosters_normal_bot_game.json` is what Normal plays.
  - `M70_Replay`: `m70_rosters_bot_game.json` is what Hard plays, through round 1. `M70_Replay.BotsWithChainCachesOfTheirOwnPlayTheSameActions` checks that the arena's shared Hard chain cache changes no decision.
  - `M73_Replay`: `m73_custom_bot_game.json` is what Normal plays on two custom lists.
  - `M75_Replay`: `m75_builder_battle.json` is what Easy plays, through round 1.
  - `M77_Replay`: `m77_custom_hand_deploy_bot_game.json` is what Normal plays, its deployment by hand included.
  - The whole M58 and M70 games are their `DISABLED_TheWholeSavedFileMatchesTheGame` tests.
  - The Teamwork batch (M80–M83, D-083) regenerated m58, m68, m70, m73 and m77 on purpose; m45, m46, m47 and m75
    stayed byte-identical. The pre-batch games are still checked by `M58_Replay.ThePreBatchGameIsAWipeOutInBattleRoundThree`,
    `M68_Replay.ThePreBatchGameShootsSuperchargedAndFightsOnBothSides`, `M70_Replay.ThePreBatchGameFightsAndLosesAUnitOnBothSides`
    and `M73_Replay.ThePreBatchGameShootsAndFightsOnBothSides`: each plays the replay's seed with `Generation::M77`
    (the pre-batch bots), replays it through core and checks what the old file was chosen for.
