# VibeHammer Development Plan

Goal for V1: a Windows `.exe` where two people play Intercessor Squad (5) vs Termagants (10) hotseat on a
24"x24" board, through the full turn sequence (Command, Movement, Shooting, Charge, Fight, End of Turn),
following the 11th edition core rules. The rules come from `docs/rules/` and the units from `data/`.

Architecture, conventions and rules-lookup policy are in `CLAUDE.md`. This file is the order of work.

## How to run each milestone

1. `/clear`, then set the effort level shown for the milestone (e.g. `/effort high`).
2. If the milestone says **Plan mode**, enter plan mode (Shift+Tab) and paste the prompt. Review the design before approving.
3. Paste the prompt. Claude reads the listed rule files first.
4. The milestone is done when every acceptance check passes and `ctest` is green. Then ask Claude to commit.

Keep sessions small:
- Read only the current milestone's section of this file, and the decisions it cites (`docs/decisions/D-NNN.md`).
- `/clear` between milestones, batches included: a batch is approved up front, each milestone gets a fresh session.
- While iterating, run the tests for the work at hand (`ctest --preset debug -R <milestone or rule>`); run the full
  `ctest`, arena runs and `--per-game` checks once, at the end.

### Effort levels

| Level | Use for |
|---|---|
| `medium` | Scaffolding, wiring, UI layout, data plumbing: well-specified work with few rule subtleties. |
| `high` | Ordinary rules implementation: one rule area with clear text and good test coverage. |
| `xhigh` | Rule areas with many interacting clauses, "if possible" constraints, or alternating-player flow. |
| `max` | Core architecture decisions that everything else builds on. Use it sparingly. |

If your Claude Code version doesn't have `/effort`, put "think hard" (for high) or "ultrathink" (for xhigh/max) in the prompt.

---

## Done so far

V1 (M0–M13) and the post-V1 milestones M14–M83 are done. Their full text (prompts, acceptance checks, results
and open items) is in `docs/plan_archive.md`; read a section there only when the current milestone needs it.
Open items still carried are listed under "Later" below.

| Milestone | Title |
|---|---|
| M0 | Source material |
| M1 | Build skeleton |
| M2 | Core types, data loading, serialization |
| M3 | Geometry |
| M4 | Dice and the attack sequence (pure functions) |
| M5 | Game flow, PendingDecision and the action engine |
| M6 | Read-only 2D viewer |
| M7 | Movement phase |
| M8 | Shooting phase |
| M9 | Charge phase |
| M10 | Fight phase |
| M11 | Command phase, battle-shock, end of turn, game end |
| M12 | Interactive client |
| M13 | Save, load and replay polish |
| M14 | Close formation and no-overlap model moves |
| M15 | Client UI improvements |
| M16 | Click-on-the-board interaction |
| M17 | Action bar under the phase bar |
| M18 | Custom shots after picking the target |
| M19 | Target-then-weapon fights and custom casualty order |
| M20 | Dice roll pop-ups |
| M21 | Datasheet windows |
| M22 | Measuring ruler |
| M23 | Skip steps with nothing to do |
| M24 | Explain weapon abilities and keywords |
| M25 | Quality-of-life fixes |
| M26 | Undo by input, and "Undo last step" in a move |
| M27 | Per-model reach area |
| M28 | Movement area follow-ups, Cancel fixes, click-to-remove for coherency |
| M29 | Selection that follows the board; blocked paths shaded |
| M30 | Cancel on every click menu, kept advance roll, charge-move area |
| M31 | Skip pile-ins and consolidations too small to matter |
| M32 | Near base contact counts for step skipping |
| M33 | Skip moves that can only get marginally closer |
| M34 | Auto move for pile-ins and consolidations |
| M35 | Pick casualties on the board |
| M36 | Fix illegal moves |
| M37 | Engagement Range in the movement area; no Roll for rolls with no dice |
| M38 | Start screen and saved settings |
| M39 | Bot framework, driver, baseline bot, arena |
| M40 | Odds and threat analysis |
| M41 | Shooting, defensive and fight decisions (level Normal) |
| M42 | Charge planner |
| M43 | Movement planner and whole-turn plan |
| M44 | Game-aware evaluator, lookahead, tuning (level Hard) (open items) |
| M45 | Bot polish, packaging, docs |
| M46 | Bots pile in and consolidate when it costs nothing |
| M47 | Default casualty order: furthest from the action first, keeping coherency (open items) |
| M48 | Bot pacing and Pause in the sidebar |
| M49 | The Roll button only when the bot's dice wait for a click |
| M50 | Short ring legend, full text on hover |
| M51 | Bot dice follow Pause; no auto-dice setting |
| M52 | Release workflow, first GitHub release v0.1.0 |
| M53 | Scenario format for several units; the 44"x60" scenario |
| M54 | Speed for big games, with no change in play |
| M55 | Per-unit tint and board labels |
| M56 | Easy and Normal across several units |
| M57 | Hard across several units |
| M58 | Tuning, bot-game replay, docs |
| M59 | Fetch tool fixes and the nine new datasheets |
| M60 | Unknown abilities refused; Hazardous, Sustained Hits, Extra Attacks; mixed-loadout default plans |
| M61 | [TWIN-LINKED] with a re-roll decision |
| M62 | Random characteristics |
| M63 | Monsters and vehicles (17.01, 17.03) |
| M64 | Oval geometry kernel |
| M65 | Oval bases in the engine |
| M66 | Oval bases in the client and bots; big-base polish |
| M67 | The portrait scenario and the points tiebreak |
| M68 | Easy and Normal for the new units |
| M69 | Hard for the new units |
| M70 | Bot-game replay, docs, final verification |
| M71 | List-building data from BSData |
| M72 | Army lists, auto-layout, arena flags |
| M73 | Bots on custom armies (open items) |
| M74 | Deployment step in the engine |
| M75 | Army builder screen |
| M76 | Hand deployment in the client |
| M77 | Bot deployment heuristic, final verification, docs, PR (open items) |
| M78 | Measuring teamwork |
| M79 | The army plan (Normal) |
| M80 | Normal moves as a group |
| M81 | Focus fire and joint charges (Normal) |
| M82 | Hard on the army plan |
| M83 | Tuning, replays, docs, pull request |

---

## M84: Docs housekeeping ✅ (done)
**Effort:** `medium` · **Plan mode:** yes (agreed 2026-11-11)

> Cut the tokens every session spends on reading docs. Split `docs/decisions.md` into one file per decision
> (`docs/decisions/D-NNN.md`) with an index; move the finished milestones to `docs/plan_archive.md`; move
> reference material out of `CLAUDE.md` into `docs/dev/`; `/clear` between every milestone, batches included.
> Docs only: no code, data or replay changes.

**Acceptance**
- Splitting is lossless: the decision files (with the index's note) rebuild the old `decisions.md` byte for byte, and
  the archive holds every finished milestone verbatim.
- `CLAUDE.md` shrinks from 31 KB to about 18 KB (the rest is rules every task needs), `PLAN.md` is under 25 KB,
  `docs/decisions.md` under 15 KB.
- Every index link resolves; every block moved out of `CLAUDE.md` is in a `docs/dev/` file.
- Only `.md` files change, so no build or `ctest` run is needed (the docs-only exception added to `CLAUDE.md`).

---

## One bot, objectives, and the bot playing for them (M85–M87)

Agreed with the user on 2026-11-11, after a rosters game (Hard Tyranids) where the monsters rushed in alone and the
infantry backed off to its own edge. With points destroyed as the only score, staying out of range is often the best
move, and three bot levels plus the hidden M55/M77 copies make every bot change expensive. So: one bot (Normal),
objectives with a mission of our own (D-084: objectives first, kills still count), then the bot plays for them.
Each milestone is planned in detail in plan mode at the start of its own session and built in that session.

## M85: One bot ✅ (done)
**Effort:** `high` · **Plan mode:** yes (agreed 2026-11-11; design approved 2026-11-11)

> Reduce the bots to one, Normal. Tag the current commit `bots-three-levels` and push the tag, so the old bots stay
> recoverable from git. Delete Easy (`baseline_bot`), Hard (`src/ai/hard/`), the hidden levels `easy-m55`,
> `normal-m55`, `normal-m77` and `hard-m77`, `src/ai/generation.hpp` and every `Generation::` / `pre_teamwork` branch;
> Normal keeps its current (post-Teamwork) behaviour. The client loses the level choice on the start screen and in
> settings (a settings file that still names a level loads and ignores it). The arena's `--a` / `--b` drop the levels
> and `--weights` tunes Normal. The Easy/Hard replays (m45, m47, m58, m70, m75) and their tests go (kept in git under
> the tag). A new decision records the move to one bot (amends D-054). CLAUDE.md, `docs/dev/bot_arena.md`,
> `docs/dev/replays.md` and README follow.

**Acceptance**
- Normal's replays m46, m68, m73 and m77 are byte-identical and their `TheSavedFileMatchesTheGame` checks pass; core
  replays are untouched.
- `--per-game` for Normal vs Normal on V1, both bot scenarios, Strike, the rosters and random armies is identical
  before and after.
- Every deleted test is listed in the Results, by name, with the code it tested. No test of code that stays is
  deleted or weakened.
- `package.ps1`'s bot smoke test passes. Build and full `ctest` green.

**Results** (2026-11-11, D-085)
- **Tag:** `bots-three-levels` on b24c689, pushed. Easy, Hard, `generation.hpp`, their tests and replays are there.
- **Design as agreed in plan mode, with three findings:**
  - Normal sent every decision it had no answer for to Easy's policy. That code moved verbatim, its reasons too, to
    `src/ai/normal/defaults.*` (`normal::default_answer`); only the M55 branches went.
  - The client already ignores unknown flags (`launch_options.hpp`). So `--bot-level`, removed, is ignored like any
    other unknown flag; it does not exit 2 (the plan's question assumed it would).
  - The army plan's no-op answer is exposed as `normal::no_plan`, since it is what `Generation::M77` read. M80, M81
    and M83 keep their "with and without partners" comparisons through it, instead of losing them.
- **Arena:** `--a` / `--b` are gone (exit 2). `--weights` sets both sides' `normal::MoveWeights`, and `--weights-a` /
  `--weights-b` set one side's. Checked: explicit default weights reproduce the default games, and another weight for
  one side changes play.
- **Identity:** Release `--per-game 1`, 200 games each, old binary (`--a normal --b normal`) against new. V1, both bot
  scenarios, Strike, the rosters, random armies (auto and hand deployment) and the M73 lists all match: 1,600 of 1,600
  `game` lines identical (actions, final state and trace hashes).
- **Replays:** m46, m68, m73 and m77 are byte-identical, and their `TheSavedFileMatchesTheGame` tests pass. Core
  replays are untouched. m45, m47, m58, m70 and m75 are deleted.
- **Removed from shared code** (nothing else called them):
  - `normal::furthest_standing_move`;
  - the shooting's given army plan (`choose_shooting` / `ShootingAim` `plan`);
  - `AttackOutcome::wounds_left_pmf`;
  - `bot_log`'s Hard parsing (`drop_hard_block`, `WinDrawLoss`, `ReasonSummary::odds`);
  - `uses_deploy_heuristic`, `effective_level`, `chain_tables_built`, `share_chain_cache`;
  - `BotLevel`, with the settings, save and CLI level keys.
- **Tests kept, changed or moved:**
  - *Easy's single-decision checks:* now the defaults' answer (`tests/ai/default_choice.hpp`; M46, M47, M56, M60–M63,
    M66, M68).
  - *M74's deployment policy:* called directly (M74).
  - *Games Easy or Hard played:* now the bot on the same seeds (M39–M43, M46, M54, M56, M60–M63, M66–M68, M72–M74,
    M77). M42's grid still compares with the default charge move.
  - *The M68/M80 Desperate Escape position:* only the pre-batch bots reached it, so it is kept as
    `tests/data/states/m68_desperate_escape.json`.
  - *M56's threat test:* keeps M55's end (17.6" from Squad 1) as a number.
  - *M45's bot game:* the whole-game check now plays the bot (no file).
  - *M75:* keeps the builder's scenario checks.
  - *Client:* settings and saves that name a level load and drop it (M38, M39). `--bot-level` is ignored (M61).
- **Renamed** (same checks, minus the removed levels):
  - M39_Arena.FortySeededEasyGames… → FortySeededGames…, EasyGamesUseTheBoard… → GamesUseTheBoard…
  - M41_Arena.…AsWellAsEasyInBothSeats → NormalPlaysCleanGames
  - M43_Arena.NormalMovementWinsSooner… → NormalMovementPlaysCleanGamesThatTheSpaceMarinesWin
  - M46_PileIn / M46_Consolidate NormalAndHard… → Normal…; M46_Easy → M46_Defaults
  - M47_Easy ×2 → M47_Defaults; M56_Easy ×4 → M56_Defaults
  - M60–M63 and M66 EveryLevel… / EasyAndNormal… → TheBotAndItsDefaults… / TheBot…
  - M68_Games → TheBotPlaysTheRostersClean, DISABLED_FortyGamesPlayTheRostersClean
  - M72 / M73 …AtEasy / …AtEveryLevel → …Clean
  - M74_Bots EasySetsUp… → M74sPolicySetsUp…, HandDeployedEasyGames… → GamesHandDeployedByM74sPolicy…
  - M75_Replay.TheBuilderBattleReplaysThroughBattleRoundOne → M75_Builder.TheScenarioFileStartsTheBuildersBattle
  - M77_BotDeploy.HandDeployedGamesAtNormalAndHard… → HandDeployedGamesFinishClean
  - M79_ArmyPlan.TheM77Switch… → BeforeTheBattleThereIsNoPlan (its not-started part)
  - client M39_Settings.BotLevelAndSpeedKeys → BotSpeedKeys
  - New: M38_Settings.AnOldFileWithABotLevelLoadsAndIgnoresIt.
- **Deleted** (96; each tested only removed code):
  - *Hard's evaluator, lookahead, chain cache, budgets and decisions* (`src/ai/hard/`):
    - every M44 test but M44_Scenarios: Evaluator ×6, MonteCarlo, NeverPeeksAtDice, Levels, PlayForTheDraw, Arena,
      Diagnostics ×3;
    - all of M57 (12), M69 (18) and M82 (11: Evaluator ×3, Hard, Regression, Diagnostics ×4, and Rollouts, whose
      given-plan parameter went);
    - M47_Hard.TheDefaultOrderIsAFourthCasualtyCandidate;
    - M61_Rollouts.TheLookaheadAnswersTheDecisionOnItsScratchCopies;
    - M63_Bots.DISABLED_HardGamesOnTheMonsterFixtureAreClean;
    - M66_Bots.HardDecidesLegallyAtTheTyrannofexsDecisions, DISABLED_HardPlaysTheTyrannofexClean;
    - M73_HardGuard ×4;
    - M77_BotDeploy.HardDeploysAsNormalAndBuildsNoTables;
    - M83_Hard.RanksItsMovesWithoutTheLoneApproach;
    - M45_BotLog's Hard parsing ×4 (HardMoveReasonKeepsTheMoveAndItsOdds, HardAdvanceDropsTheScoreAndKeepsTheRoll,
      TheChosenOptionsOddsAreShownForACharge, ARuleNumberBeforeHardsBlockStays). The Advance's score drop is now
      checked in NormalReasonsLoseTheirScoresAndExtraClauses.
  - *The level dispatch:* M39_Levels.EveryLevelIsBuiltAndHardIsItsOwnLevel.
  - *Generation (M55 / M77):*
    - M42_ChargeTargets.ChargesBothUnitsWhenTheTargetAloneCannotBeEngaged (Normal at M55);
    - M56_Regression.TheM55SwitchPlaysAsM55;
    - M78_Regression.TheM77LevelsMatchThePreBatchHashes;
    - M80_Regression.TheCurrentLevelsDifferFromM77OnTheRosters;
    - the pre-batch replay checks M58_Replay.ThePreBatchGameIsAWipeOutInBattleRoundThree,
      M68_Replay.ThePreBatchGameShootsSuperchargedAndFightsOnBothSides,
      M70_Replay.ThePreBatchGameFightsAndLosesAUnitOnBothSides and
      M73_Replay.ThePreBatchGameShootsAndFightsOnBothSides.
  - *Easy as a player:* M47_Arena.EasyTermagantsKeepCoherency.
  - *The deleted replays:*
    - M45_Replay.DISABLED_RegenerateBotGameReplay;
    - M47_Replay ×4 (TheDefaultCasualtiesGame…, TheSavedFileMatchesTheGame, DISABLED_Regenerate…, DISABLED_PrintSeeds);
    - M58_Replay ×4 besides the pre-batch one;
    - M70_Replay ×6 besides the pre-batch one (BotsWithChainCachesOfTheirOwnPlayTheSameActions was Hard's shared
      cache);
    - M75_Replay.TheSavedFileMatchesTheGame and DISABLED_SearchBuilderBattleSeed.
  - The exact list: the TEST names at `bots-three-levels` and not at M85's commit, less the renames above.
- **Debug time:**
  - Games Easy played in a second or two now take Normal's time: the ai tests took about 20 minutes in Debug.
  - M68_Games was cut from 8 roster games to 4 (105 s), and M67_Arena from 5 to 3 (82 s).
  - The other converted game tests keep their seeds (M60 86 s, M66 83 s, M62 73 s, M61 67 s).
- **Package and client:**
  - `package.ps1`'s smoke tests pass (bot-steps 5 of 5).
  - The start screen's Bot section shows only Speed.
  - `ctest` (all suites) is green.
- **Docs:**
  - D-085 is added, with "Amended / Superseded by D-085" notes on D-054, D-066, D-067, D-078, D-080 and D-083.
  - CLAUDE.md, README, `docs/dev/bot_arena.md` (rewritten, with the identity set), `docs/dev/replays.md`,
    `docs/dev/client_flags.md` and `package.ps1`'s README are updated.
  - The Hard and `normal-m77` open items are removed from Later.

---

## M86: Objectives and a mission ✅ (done)
**Effort:** `high` · **Plan mode:** yes (agreed 2026-11-11; design approved 2026-11-12)

> Objective areas and control (14.02 Level of Control: the OC of models within range, compared at the end of each
> phase and turn, ties uncontrolled; 14.03 Secured Objectives as state only, nothing secures yet). An objective area is
> a terrain objective with no terrain effects (no terrain yet); read 13 and 14 first, use D-009 for "within", and ask
> if anything is ambiguous. Control is part of `GameState`. The mission is D-084, as a scenario option
> `"result": "mission"` with every number in the scenario file, so balancing never needs code. New scenario
> `data/scenarios/incursion_objectives_44x60.json` (the rosters armies plus the three areas); custom battles get the
> same mission and areas. Client: areas drawn with their controller's colour, VP per side in the sidebar, the result
> line names VP, and the objective scenario is the start screen's default. The bot ignores objectives here.

**Acceptance**
- Rule-named tests for 14.02 and 14.03 and for D-084's scoring and result (ScriptedDice); state round-trip tests for
  every new field.
- A replay of a short objective game under `tests/replays/`; every old replay byte-identical; old scenarios unchanged.
- The bot plays the objective scenario and custom battles to the end with no rejected action (arena, 200 games).
- Build and full `ctest` green.

**Results** (2026-11-12, D-084's "Implemented" note)
- **Design as agreed in plan mode.** The user chose two points there:
  - the arena's random and army-list battles play the mission, as the client's custom battles do;
  - the bot scores a mission like the points rule.

  Core's `custom_battle_scenario` keeps the old battle unless it is given the mission block, so the m73, m75 and m77
  fixtures and replays didn't change.
- **Core:**
  - `state/mission.hpp` (`Mission`, `ObjectiveArea`);
  - `state/objectives.*` (within range, level of control, `determine_control`, `controlling_units`, kill VP, the VP
    tally, the data checks);
  - `engine/objectives.cpp` (the checks at every phase end and turn end, and the Command phase scoring);
  - the result in `advance.cpp`;
  - two events: `ObjectiveControlChanged` (14.02) and `VictoryPointsScored` (D-084);
  - `VpTally` on `GameResult` and `GameOver`;
  - the loader's `"mission"` block and `load_mission_block`.
- **Scenario:** `data/scenarios/incursion_objectives_44x60.json`, the rosters plus the three areas, and the client's
  new default. Both homes are controlled from the first check:
  - A's by the Terminators' back rank (their two ranks reach 8.85" from the edge);
  - B's by the held-back Tyrant Guard and Tyrannofex.

  For M87.
- **Tests:** 31 core tests (`R14_02_LevelOfControl` ×9, `R14_03_SecuredObjectives` ×4, `D084_Scoring` ×4,
  `D084_KillVp`, `D084_Result` ×4, `D007_GameEnd.AWipeOutStillWinsAtOnceUnderTheMission`, `D084_Scenario` ×4,
  `D084_CustomBattle`, `D084_Json` ×4), `D084_Replay`, 6 client tests (`M86_*`) and 4 ai tests (`M86_*`).
- **Tests changed, none weakened:**
  - `D076_Loader.MalformedPointsOrResultAreRefused` and `D076_Json.MalformedPointsInAStateAreRefused`: the refusal now
    lists both values.
  - `Serialization.EveryEventKindRoundTrips`: the two new events.
  - `M38_Settings.DefaultsMatchThePlan`: the new default scenario.
- **Replay:** `tests/replays/m86_objectives.json` (scripted, seed 86):
  - the Terminators advance in round 1 and take the Centre in round 2;
  - Side B scores its home (2 VP), then Side A the Centre (5 VP);
  - it stops at Side A's round-3 Movement phase.

  Every other replay came out byte-identical, and the old scenarios are unchanged.
- **Client:**
  - areas on the board in their controller's colour, labelled above the area;
  - the VP line in the Game panel;
  - VP in the victory pop-up and the log;
  - the builder's text names the mission.

  The start screen's subtitle no longer repeats the side names when the scenario's name has them, and wraps when it
  is too wide (the new default's name was clipped).
- **Arena:** Release, 200 games each. Every run: 0 rejected, 0 reverted, 0 fallbacks, 0 unfinished.
  - *Identity:* old binary (M85) against new, with `--per-game`.
    - V1, both bot scenarios, Strike and the rosters: all 1,000 `game` lines identical.
    - `--random-armies 1`, `--random-armies 1 --deploy hand` and the M73 lists now play the mission. In their 600
      games every action and trace hash is identical, and so is every field but the final state (all 600 differ) and
      the winner (23, 21 and 69 games changed result).
    - That makes the M86 binary the reference for those three runs (`docs/dev/bot_arena.md`).
  - *The objective scenario,* A first: B wins 185, A 15, no draws.
    - Decided on VP: B 185, A 12. Decided on points (equal VP): A 3.
    - Mean VP: objectives A 1.84 / B 14.53; kills A 7.36 / B 8.71.
    - B scores mostly by sitting on its own home: 6.08 of its 9 units never come within 12" of an enemy. A's
      Terminators walk off their home.
    - With B first (`--first B`): B wins 171, A 29.
  - *Random armies:* B wins 164 of 200, by hand 147. Mean VP: objectives A 4.17 / B 10.02, kills A 4.39 / B 8.22.
  - *For M87:* objectives already give more VP than kills (14.5 vs 8.7 for B on the objective scenario), but the
    holding side wins. This is the bot ignoring objectives, as M86 intended. M87's balance work starts here.
- **Build and `ctest`:** Linux Debug build clean; full `ctest` 1,324 of 1,324 passed (core 744, client 351, ai 229 when
  run per suite). Screenshots under `xvfb-run`: the replay's last frame (the Centre in A's colour, B's home in B's, the VP
  line) and the start screen with the new default.

---

## M87: The bot plays for objectives
**Effort:** `xhigh` · **Plan mode:** yes (agreed 2026-11-11)

> The army plan assigns groups to objective areas (take, hold, contest), weighing each area's VP, distance and
> threat, as well as to enemy units. A unit without a group heads for an area instead of holding back (replacing
> `make_lone_approach` in `src/ai/normal/move_score.cpp`). Objective terms go into the move score. Tune the bot, and the
> mission's numbers in the scenario file (VP, area sizes, home distance), with the Release arena on the objective
> scenario and random armies, 200 games a seat.

**Acceptance**
- Each seat wins 40–60% in Normal vs Normal on the objective scenario and on random armies.
- Objectives give more VP than kills on average.
- "Units never within 12"" and lone engagements well down from M83; no single blob (gang-up and contact metrics). If
  it still blobs, the fallback is three middle areas (left, centre, right) instead of one.
- Changes to play on the non-objective scenarios are recorded; Normal's replays regenerated on purpose if they change.
- Build and full `ctest` green.

---

## Later (post-V1, rough order)
- **Dice pop-up polish:** highlight critical hits and wounds on the dice (e.g. a gold border). `DieFace::critical` already carries it, and the result line already counts them.
- **Engine-paced dice rolls (Option 2 of M20):** a PendingDecision before each roll point (hit, wound, save, charge, battle-shock, hazard, advance), so the roll is made when the player presses Roll instead of being revealed after the engine has made it. Needs the attack sequence split into resumable steps, new decisions in `legal_actions`/`validate_action`, and every replay regenerated. M20's pop-up would then answer those decisions instead of revealing pre-made rolls.
0. **Deployment step:** done in M74 (engine), M76 (client) and M77 (the bots' heuristic), in the custom-battles batch
   (M71–M77). What is left of it is under item 5, "Open items carried from M71–M77".
1. **Terrain** (13): Exposed/Light/Dense areas, cover, visibility blocking, Hidden. This adds the z axis.
2. **Objectives** (14) and a simple mission: the engine is done in M86 (D-084); the bot is M87. This enables Objective
   Consolidation and Bolter Discipline. Left open from M86: nothing secures an objective yet (14.03 is state only).
   - **Bot personalities** (after M87): Aggressive (values kills and charges, takes risks) and Tactical (values
     objectives, holds home, trades carefully), as weight presets on the one bot.
3. **Unit-specific abilities** for the two V1 units (Bolter Discipline, Tactical Mainstay, Skulking Horrors, Synapse).
4. **Core stratagems** (15): Command Re-roll, Fire Overwatch, Heroic Intervention, etc. These are new reactive PendingDecisions.
5. **More unit types** (M59–M70 added the fetch-tool fixes, Hazardous, Sustained Hits, Extra Attacks, Twin-linked,
   Random Characteristics, 17.01/17.03 and oval bases). Still to do:
   - **Weapon abilities:** Lethal Hits (24.23), Devastating Wounds (24.10), Anti-X (24.03), Melta (24.25), Lance
     (24.21), Precision (24.28). Until then, weapons with them are refused when selected (D-071).
   - **Fetch tool:** "Hunter: Monster/Vehicle" profiles (refused today), wargear limits ("1 in 5", "1 per unit"),
     `modifiers`/`hidden`, and bases with no size ("Hull", "-").
   - **Unit-level core abilities:** Feel No Pain (24.12), Deadly Demise (24.08), Leader and attached units (19),
     FRAME (17.02); unit abilities stay out of scope until item 3.
   - **Candidate units:** Assault/Heavy Intercessors, Infernus, Bladeguard, Hellblasters; Hormagaunts, Neurogaunts,
     Tyranid Warriors.
   - **Open items carried from M59–M70** (each where it was left):
     - *Bots, teamwork:* done in M78–M83 (D-083): an army plan forms groups of two or three units on one target;
       Normal moves its groups together, focuses fire, finishes wounded units and charges jointly; a unit without a
       group closes in unless outreached. Left open (M83's Results): the rosters' Tyranid gang-ups (1.7x, not 2x), lone
       engagements and units never within 12" (not halved: the Tyranid infantry is outreached and still holds back
       40-50" away), Normal's rosters p95 (16 ms) on this machine, and the plan's stability (about 60% of an earlier
       mover's partners are still partners when they move, M80). M87 takes over the gang-up, lone-engagement and
       holding-back items (with objectives). (M85, D-085: the Hard and `normal-m77` items went with them.)
     - *Bots:* they never turn an oval (M66, M68, M69); random damage is planned at the preview's maximum (M62, M68);
       the Desperate Escape guard checks one to three failed hazard rolls (M68, M69); the firer's own hazard loss is
       priced in the choice of profile but not taken off Normal's move score, and D-071's "no hazard roll after the
       last enemy model is destroyed" is ignored there (M68); Normal's two-half-turn horizon keeps far units holding
       (M68: measured right for these armies).
     - *Client:* the per-model editors choose one declaration per model, and the Split popup splits only the main
       weapon (M60); no setting re-rolls [TWIN-LINKED] automatically (M61); at a narrow window the action bar can
       still clip a long casualty hint or the "Damage left to allocate" box (scroll the bar; M70).
     - *Engine:* the oval-against-oval turn check is conservative (M65; the rosters have one oval).
     - *Fetch tool / datasheets:* a mixed ranged/melee weapon's profiles list under the first profile's type, and
       ability-name modifiers (Deadly Demise X, Scouts X) and hidden-ability conditions are not evaluated (M59).
   - **Open items carried from M71–M77** (custom battles; each where it was left):
     - *Bots, random armies:* the rates of `kMaxLoadouts` and `kMaxAdvanceSteps` were measured at M73 and not
       re-instrumented since. (M85, D-085: Hard's items there went with Hard.)
     - *Bots, deployment:* the bot never turns an oval when setting it up, and its candidates are whole-inch
       translations of core's proposal (M77, D-081).
     - *Client, hand deployment:* no snapping or "Fix illegal moves" for a set-up (a refused ghost stays where it was let
       go, D-082 part 2, M76); at 1280x800 one long unit name in the builder ("Chaos Terminator Squad 3") is clipped
       before its number (M75).
     - *Manual checks by mouse on Windows* (for the user, M76, M77): build, save and reload two lists; deploy by hand
       against the bot (dragging, Shift-dragging, turning the Tyrannofex with Q / E and Shift+wheel, Enter, Undo);
       finish a custom battle.
     - *Factions:* custom battles offer Chaos Space Marines and Tyranids only, at Incursion (1000 points); Space Marines
       stay for the old scenarios (agreed scope).
6. **Networking:** a server-authoritative host runs the engine, clients send `GameAction`s and receive events or snapshots, and spectators subscribe to events. The engine API shouldn't need to change. If it does, that's a bug in the earlier design.
7. **Attached units, transports, reserves, aircraft** (17–23).
