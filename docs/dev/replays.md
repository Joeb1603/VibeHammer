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
# and m95_stratagems.json (each stratagem used, and each window declined, a step of its own, seed 266) likewise (M95):
./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D096_Replay.DISABLED_RegenerateM95Replay
# and m96_reactions.json (Fire Overwatch, Heroic Intervention and Counteroffensive, seed 1) likewise (M96):
./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D097_Replay.DISABLED_RegenerateM96Replay
# and m97_stratagems.json (Explosives and Crushing Impact, seed 1) likewise (M97):
./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D099_Replay.DISABLED_RegenerateM97Replay
# and m99_critical_wounds.json ([ANTI], [DEVASTATING WOUNDS] and [MELTA] with the roster weapons, seed 7) likewise (M99):
./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D101_Replay.DISABLED_RegenerateM99Replay
# M96's check that a format change adds only declines: the old files (git show HEAD:) in a directory, each new file
# must be its old file converted by with_roll_steps:
WH40K_FORMAT3_DIR=<dir> ./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D097_Replays.DISABLED_CompareWithFormat3Files
# M97's check, the same for format 4 -> 5 (the old files in a directory; it also counts the declines added):
WH40K_FORMAT4_DIR=<dir> ./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D099_Replays.DISABLED_CompareWithFormat4Files
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
- M95 (D-096): **format 3.** A reaction window is a `UseStratagems` decision, and a format-3 replay records each answer as
  a step (`DeclineStratagems` or `UseStratagem`). A random D is rolled per attack that inflicts damage (D-069), each roll
  a `RollDice{Damage}` step of its own. All 29 replays were regenerated on purpose as format 3. A script checked each
  file against `git show HEAD:`: the same initial state and `expect` block, and the same steps once the `RollDice` and
  `DeclineStratagems` steps are taken out. The files gained 958 decline steps and 8 damage rolls, and no other roll.
  Only `m94_paced_rolls.json`'s description changed: its damage rolls are "one per attack since M95", and it has one.
  Formats 1 and 2 still load:
  - `run_replay`, `with_roll_steps`, `Timeline` and `GameSession::from_save_json` convert them.
  - Format 1 has every roll made and every window declined, each as a step.
  - Format 2 is converted step by step:
    - a window before a recorded step is declined;
    - after a recorded damage roll, the batch's further damage rolls are made;
    - windows at the end are declined, but a pending roll is left waiting.
  - `tests/data/legacy/` adds format-2 copies of `m8_shooting` and `m62_random_characteristics` and two M94 saves
    (`*_format2.json`). `D095_Replays` and `M94_Saves` check that they turn into the current files.

  `m95_stratagems.json` (`D096_Replay`, seed 266) uses each stratagem once:
  - Side A re-rolls a missed hit and, as the defender, a failed save.
  - Side B's Insane Bravery passes a battle-shock roll.
  - Side B re-rolls a damage roll of 1 and a charge roll that had no target, which then reaches its target.

  `D095_Closure.EverySavedReplayPlaysTheSamePacedAndInline` checks it paced only: inline play declines every window,
  so a game that uses a stratagem has no inline twin. The bot plays Command Re-roll by default since M95 (D-096). The
  bot-game generators (m46, m68, m73, m77, m87, m88, m89, m91) play the bot without stratagems
  (`tests/ai/bot_configs.hpp`), so their files and `TheSavedFileMatchesTheGame` checks stayed as they were. The M94 and M95 `DISABLED_DumpReplayEvents` dumps, with
  `WH40K_EVENT_DUMP_WITHOUT=DamageRolled`, are byte-identical for all 29 older replays.
- M96 (D-097): **format 4.** The windows that follow no roll (15.08 Fire Overwatch and 15.11 Heroic Intervention at the
  end of the opponent's Movement and Charge phases, 15.12 Counteroffensive just after an enemy unit has fought) are
  `UseStratagems` decisions too, and a format-4 replay records each answer, with what it leads to (a snap shot's
  `Shoot`, a heroic charge's roll and move). All 30 replays were regenerated on purpose as format 4.
  `D097_Replays.DISABLED_CompareWithFormat3Files` checked each against its format-3 file (`git show HEAD:`): every new
  file is exactly the old one converted by `with_roll_steps` (the same description, initial state and `expect`, and
  only the new windows' declines added: 129 of them). Formats 1 to 3 still load:
  - `run_replay`, `with_roll_steps`, `Timeline` and `GameSession::from_save_json` convert them;
  - format 3 is converted by declining every new window before each recorded step and at the end
    (`with_roll_steps_from_format3`); formats 1 and 2 as before (every window declined).
  - `tests/data/legacy/` adds format-3 copies of `m8_shooting` and `m62_random_characteristics` (`D095_Replays` turns
    each into the current file) and three M95 saves (`M96_Saves`: the same game played now, a save still waiting at its
    Command Re-roll window, and a save after a Movement phase that gains Side B's declined Fire Overwatch window).

  `m96_reactions.json` (`D097_Replay`, seed 1, picked by `D097_Replay.DISABLED_SearchM96Seed`): the V1 scenario with
  the Termagants 4.9" north of the Intercessors and a one-Termagant unit beside them; Side B starts with 4 CP.
  - At the end of Side A's Movement phase Side B uses Fire Overwatch: the Termagants snap shoot, and a 6 hits.
  - The Intercessors charge; at the end of the Charge phase Side B uses Heroic Intervention, Into the Fray: the lone
    Termagant's roll above 6 becomes 6, and it charges.
  - In the Fight step Side B uses Counteroffensive on the Termagants after the Intercessors fight: they fight next,
    before the lone Termagant (Fights First from its charge).

  The bot plays Fire Overwatch by default since M96; the bot-game generators still play the bot without stratagems
  (`without_stratagems()` turns the new keys off too), so their files changed only by the new windows' declines.
  `D095_Closure.EverySavedReplayPlaysTheSamePacedAndInline` checks the two stratagem replays paced only. The M95 and M96
  `DISABLED_DumpReplayEvents` dumps of the 30 older replays are byte-identical with no filter: a game without the new
  stratagems gains no event, and 14.02.01's move (objective control on entering the End of Phase step) changes no
  event's order.
- M97 (D-099): **format 5.** The window just after a charge move (15.06 Crushing Impact, `WindowTiming::AfterChargeMove`)
  is a `UseStratagems` decision too, and a format-5 replay records each answer, with what it leads to (the selection,
  `SelectModelAndEnemy`; the roll, `RollDice{CrushingImpact}`; the casualties). 15.05 Explosives answers the Shoot
  decision (`UseStratagem` beside the Shoot templates), so it needed no format change of its own. All 31 replays were
  regenerated on purpose as format 5. `D099_Replays.DISABLED_CompareWithFormat4Files` checked each against its format-4
  file (`git show HEAD:`): every new file is exactly the old one converted by `with_roll_steps` (the same description,
  initial state and `expect`), with only declines added: 11 of them, in the 8 replays where a MONSTER ends a charge move
  in its own Charge phase with CP to spend (m60, m61, m63, m65, m68, m73, m87, m91). Formats 1 to 4 still load:
  - `run_replay`, `with_roll_steps`, `Timeline` and `GameSession::from_save_json` convert them;
  - format 4 is converted by declining every AfterChargeMove window before each recorded step and at the end
    (`with_roll_steps_from_format4`); format 3 declines those and D-097's; formats 1 and 2 as before.
  - `tests/data/legacy/` adds the M96 files `m8_shooting_format4.json`, `m62_random_characteristics_format4.json` (their
    games have no such window: `D095_Replays` turns each into the current file),
    `m60_hazard_and_extra_attacks_format4.json` (its Hive Tyrant's charge gains a decline: `D099_Replays`) and an M96
    save after the Tyrant's charge, `m96_save_monster_charge_format4.json` (`M97_Saves`: it loads with the window
    declined, the same game played now).

  `m97_stratagems.json` (`D099_Replay`, seed 1, picked by `D099_Replay.DISABLED_SearchM97Seed`): the monsters fixture
  (D-073) with Side A first.
  - Side A's Shoot step: Explosives on the Legionaries (GRENADES), one of them and the Hive Tyrant 2.04" away selected;
    six D6, each 4+ a mortal wound on the Tyrant; then the Legionaries shoot at it (Explosives doesn't select a unit to
    shoot).
  - Side B's Charge step: the Hive Tyrant charges the Legionaries and Side B uses Crushing Impact just after the charge
    move: ten D6 (its T), each 5+ a mortal wound on the Legionaries (Side A's casualty order), then each 1 one on the
    Tyrant (Side B's). The replay stops at the Pile In step.

  The bot plays neither stratagem by default (its keys are 0 until measured, D-099); the bot-game generators play
  without stratagems anyway (`without_stratagems()` turns the new keys off too), so their files changed only by the
  new windows' declines. `D095_Closure.EverySavedReplayPlaysTheSamePacedAndInline` checks the three stratagem replays
  paced only. The M96 and M97 `DISABLED_DumpReplayEvents` dumps of the 31 older replays are byte-identical with no
  filter: declining a window emits nothing.
