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

## M87: The bot plays for objectives ✅ (done; balance and holding back open)
**Effort:** `xhigh` · **Plan mode:** yes (agreed 2026-11-11; design approved 2026-11-12; the mission's numbers chosen by
the user 2026-11-12)

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

Read in plan mode (2026-11-12): "each seat" is each side over 400 games, 200 with each side going first; the mission
stays symmetric, and a random-army gap is recorded open; "well down" is a third fewer units never within 12" and a
quarter fewer lone engagements (both sides together); a blob is a game whose largest connected group of engaged units
averages half the units on the battlefield or more.

**Results** (2026-11-12, D-086; D-084's Tuned note)
- **The bot** (D-086): area sets in the army plan (after the groups, which they leave as they were); the objective terms
  in the move score (the enemy scoring a move denies at the end of our turn, ours it holds through the enemy's turn,
  at the VP rate `vp_share`; the enemy's level a distribution, each enemy unit in one place); a unit with an area and no
  group heads for it (it replaces the lone approach for such a unit); spots on the areas among the candidates; the last
  unit weighed on the VP result. Gated on a mission and the objective weights (`objective` 1, `objective_approach` 0.1,
  `objective_fire` 0): without a mission, or with all three 0, play is M86's.
- **The mission** (the user's choice after the tuning table in D-084's note): Centre 1 VP, own home 4, enemy home 8,
  1 kill VP per 25 points (was Centre 5, homes 2 / 4, 1 per 50). M86's numbers are kept as
  `tests/data/scenarios/m86_objectives_44x60.json` for the D-084 rule tests, the M86 replay and the client's M86 checks.
- **Acceptance** (Release arena, seeds 1–200, confirmed on 201–400):

  | Item | Number | Target | |
  |---|---|---|---|
  | Chaos Space Marines' wins, objective scenario | 30.8% / 33.5% (M86's numbers with the new bot: 1.2%) | 40–60% | open |
  | Chaos Space Marines' wins, random armies | 23.0% / 18.2% (before any mission 24.5%) | 40–60% | open (agreed) |
  | objective VP against kill VP a game | 33.6 vs 31.5 (objective scenario), 32.0 vs 25.2 (random armies) | more from objectives | met |
  | units never within 12", both sides | 7.76 (M83 7.74), random armies 5.86 (5.70) | a third fewer | open |
  | lone engagements, both sides | 1.32 (1.61), random armies 1.27 (1.40) | a quarter fewer | open (down 18% / 9%) |
  | no single blob | the largest melee 0.17 of the units on the battlefield | under half | met |
  | identity: V1, both bot scenarios, Strike, the rosters; the mission runs with the objective weights 0 on M86's block | identical to M86, every `game` line | identical | met |
  | replays | m46, m68, m73, m77 byte-identical; m86 regenerated (its scenario path only); new m87 | | met |
  | clean runs | no rejected action, reverted move, fallback or unfinished game | 0 | met |

- **Why balance is open:** the Tyranids out-control the Chaos Space Marines (67 OC against 56, Termagants OC 20 for 55
  points), so every Centre that counts goes to them; only a Centre worth nothing reached 57.5%. The bot's weights
  moved nothing beyond the noise (D-086).
- **Tests:** `tests/ai/m87_test.cpp` (the VP rate, who scores next, the enemy's level, the terms, area sets,
  candidates, the approach and its reason, the promise, the last unit, a fresh bot against the memo, the arena's new
  lines, the tuned mission, the M86 regression pins, the replay). Changed: `M86_Bot` is pinned to the objective weights
  0 (its premise, the bot ignoring objectives, holds only then); the M86 tests read the fixture.
- **Replay:** `tests/replays/m87_objectives_bot_game.json` (seed 29: each side holds its home, both score the Centre,
  fights on both sides; the Tyranids win on VP).
- **Build and `ctest`:** Linux Debug build clean; full `ctest` 1,348 of 1,348 passed. Identity: all 9 runs (1,800
  games) identical to the M86 binary. Screenshot under `xvfb-run` (seed 29, round 3): both homes held, the Terminators
  contesting the Centre. A full game by mouse on Windows stays a manual check for the user.

---

## Terrain, and the bot playing for cover (M88–M89)

Agreed with the user on 2026-11-12 (both milestones approved together as a batch, in M88's plan mode). Terrain is
2D: areas and features are footprints on the battlefield plane, with no floors, vertical moves or Plunging Fire
(D-087). A new default scenario puts terrain on the objective battlefield: the three objective areas become terrain
areas (14.01), with a dense ruin at the Centre. Without terrain nothing changes: old scenarios, replays, state files
and the bot's identity set stay byte-identical. Each milestone starts in a fresh session.

## M88: Terrain in 2D ✅ (done)
**Effort:** `xhigh` · **Plan mode:** done (design approved 2026-11-12; D-087, D-088, D-089)

> Implement terrain as designed in D-087 (areas and features, Exposed / Light / Dense, the scenario format,
> movement and set-up), D-088 (13.10 obscuring areas, 13.11 Solid as a Dense feature's whole footprint, 13.09 Hidden
> with its 15" detection range and the ranged-attacks record, 06.01 Fully Visible) and D-089 (13.08 Benefit of cover;
> worsening BS by 1, the working assumption). Read 13, 03.01 and 06.01 and those three decisions first.
> - **Core:**
>   - the terrain types in `GameState::terrain`;
>   - shape geometry in `geometry/terrain_shape.*` (a convex polygon support in D-074's GJK);
>   - a terrain-aware `has_line_of_sight` overload and `fully_visible_past`;
>   - the rules queries in `rules/terrain.*`;
>   - shooting's visibility and cover;
>   - the skill worsened in `attacks.cpp`;
>   - path, move-end, `move_reach` and set-up checks;
>   - the loader's `"terrain"` block, `load_terrain_block`, and `custom_battle_scenario`'s terrain argument.
> - **Scenario:** `data/scenarios/incursion_terrain_44x60.json` with the agreed layout (below), as the start screen's
>   default.
> - **Client:** draw areas and features, explain cover, Hidden and obscured targets, shade terrain in the move reach,
>   and give custom battles the terrain block.
> - **Bot:** plays legally around terrain (its candidates never end on features, and monsters' lines don't cross
>   Dense walls over 2"). It does not value terrain yet (M89). Arena `--terrain <scenario>`.

**The layout** (point-symmetric about (22", 30"); south is y = 0; exact coordinates finalised in code, every formation
clear of solid features):

| Area | Shape | Features |
|---|---|---|
| Centre (objective) | circle (22, 30), 12" | two Dense walls 5" high, 0.5" thick, L-shaped (x 17–23 / y 27–27.5 and x 17–17.5 / y 27–31), and the mirror image |
| A home / B home (objectives) | circles (22, 6) / (22, 54), 6" | an Exposed crater, 4" (no solid feature in a deployment zone) |
| SW ruin / NE ruin | rectangles x 4–14, y 18–24 / x 30–40, y 36–42 | two Dense walls 5" high, L-shaped, open towards the near deployment zone |
| SE barricades / NW barricades | rectangles x 30–38, y 18–23 / x 6–14, y 37–42 | two Light low walls each |
| W craters / E craters | circles (3.5, 30) / (40.5, 30), 5" | an Exposed crater, 3" |

**Acceptance**
- **Tests:** rule-named tests (ScriptedDice, inch coordinates) for 13.01 validation, 13.06 paths and ends, 13.08 cover
  (both conditions, BS 3+ to 4+, an unmodified 6 still hits, [TORRENT], separate batches), 13.09 Hidden (14.9" against
  15.1", shooting ends it for this turn and the next), 13.10, 13.11 and 06.01 Fully Visible. Also D-087–D-089 cases and
  state round-trip tests for every new field, including an old state with no terrain key.
- **Replay:** `tests/replays/m88_terrain.json`, path-based on the new scenario, from round 1 of bot play. It includes a
  shot with cover, a monster going round a wall and a hidden unit. It is checked by the engine replay only, so M89's
  bot changes don't invalidate it.
- **Unchanged:** every old replay byte-identical; old scenarios unchanged; the identity set (`docs/dev/bot_arena.md`)
  identical to the M87 binary, every `game` line.
- **Clean arena runs:** the terrain scenario (200 games with each side going first) and random armies with
  `--terrain` (200 + 200), with no rejected action, reverted move, fallback or unfinished game. Record the win rates, the
  share of ranged attacks with cover, the targets refused for Hidden or obscuring, and the p95 decision time (target: at
  most twice M87's).
- **Screenshot** under `xvfb-run` on the terrain scenario.
- **Build** and full `ctest` green.
- **Docs:** D-087–D-089 marked implemented, with what was chosen in code; CLAUDE.md (the overview line; geometry: terrain
  is 2D, z stays 0); README; `docs/dev/bot_arena.md`; `docs/dev/replays.md`.

Read in M88's implementation session (2026-11-12, the user's choices): the replay's monster detour is one scripted
two-leg move inside the bot game (the bot itself detours from M89); "random armies with `--terrain` (200 + 200)" is
`--deploy auto` and `--deploy hand`, 200 games each.

**Results** (2026-11-12; D-087–D-089 "In code")
- **Core.**
  - `geometry/terrain_shape.*`: convex polygons and circles; exact round-base formulas; GJK with a polygon support for
    ovals.
  - `state/terrain.*`: the types and `check_terrain_data`.
  - `rules/terrain.*`: `TerrainView`, `model_visible`, `visibility_problem`, `benefit_of_cover` and `path_features`.
  - The terrain line-of-sight overload and `fully_visible_past` (`visibility_oval.cpp`).
  - Path checks, `move_end_problems`, `move_reach`'s `ReachTerrain`s, and set-up.
  - Shooting's visibility, refusal reasons and cover; the BS worsened in `gather_attack_batches`.
  - The ranged-attacks record; the loader's `"terrain"` block, `load_terrain_block`, and `custom_battle_scenario`'s
    terrain argument.
- **Scenario.** `data/scenarios/incursion_terrain_44x60.json` (the agreed layout; the start screen's default).
- **Client.**
  - The board draws areas and features.
  - Hovering an area explains its rules; the Game panel lists the terrain.
  - A greyed target's tooltip gives the engine's reason (13.09, 13.10 / 13.11).
  - The weapon menu and the hit card show the cover line.
  - The move reach and drop fixing avoid features and walls' shadows.
  - Custom battles take the terrain block.
- **Bot.** Its move candidates use the engine's terrain tests (no end on a feature, no line through an uncrossable
  wall). The state hash takes the terrain and the record behind tags. It does not value terrain (M89).
- **Arena.** `--terrain <scenario>`, and the `terrain (M88)` summary line.
- **Acceptance** (Release arena, seeds 1–200):

  | Item | Number | Target | |
  |---|---|---|---|
  | clean runs: the terrain scenario with A first, and with B first; random armies `--terrain`, auto and hand | 800 games: no rejected action, reverted move, fallback or unfinished game | 0 | met |
  | Chaos Space Marines' wins, terrain scenario | 25.0% (A first) / 38.5% (B first); M87's objective scenario 32.0% (A first) | recorded | |
  | Chaos Space Marines' wins, random armies with terrain | 18.5% auto / 18.5% hand | recorded | |
  | ranged selections with the benefit of cover | terrain scenario A 70.6% / 51.4%, B 21.5% / 19.5%; random armies A 52.0% / 49.9%, B 25.1% / 26.9% | recorded | |
  | enemy units in range refused per game | Hidden: terrain scenario A 0.08 / 0.95, B 0.00 / 0.01; random armies 0.17–0.34. Obscuring or dense terrain: terrain scenario A 13.2 / 15.9, B 1.7 / 1.5; random armies 6.8–13.1 | recorded | |
  | p95 decision time | terrain scenario 16.6 / 16.1 ms against M87's 12.7 ms on the objective scenario (1.3x); random armies with terrain 12.4 ms | at most 2x | met |
  | identity set (`docs/dev/bot_arena.md`) and the objective scenario | identical to the M87 binary, every `game` line (1,800 of 1,800 games) | identical | met |
  | replays | every old replay byte-identical; new `m88_terrain.json` | | met |

  The Hidden counts are low because the bot doesn't value terrain yet: its units seldom stand in a dense area
  without shooting while the enemy has them in range. M89 changes that.
- **Tests.**
  - `tests/m88_terrain_test.cpp`: 13.01, 13.06, 13.08–13.11, 06.01 Fully Visible, D-087–D-089, the geometry, the
    redundancy property, and the replay checked with core only.
  - `tests/client/m88_test.cpp`: the board, the shoot reasons, the cover line, the reach and drop fixing, the builder
    and the default.
  - `tests/ai/m88_test.cpp`: candidates, a clean game, the arena line, the state hash, and the replay's regeneration
    and seed search.
  - Changed: `M86_Settings` and `M38_Settings` repointed at the new default (by design, as M86 repointed M38).
    `M12_NoRulesInTheClient` allows the client logic `core/rules/terrain.hpp`'s read-only queries (D-024).
- **Replay.** `tests/replays/m88_terrain.json` (seed 80), battle rounds 1–3:
  - the Hive Tyrant's scripted detour round the east end of the south-west ruin's north wall, in battle round 3;
  - shots into cover;
  - two targets refused because they were hidden.

  It stops at the first decision of battle round 4 and is checked by the engine replay only.
- **Build and `ctest`.** Linux Debug build clean; full `ctest` 1,402 of 1,402 passed. Screenshots under `xvfb-run`
  of the replay at steps 200 (battle round 2) and 271 (just after the detour). A full game by mouse on Windows stays a manual check
  for the user.

---

## M89: The bot plays for cover ✅ (done; the head-to-head target open)
**Effort:** `high` · **Plan mode:** done (design approved 2026-11-12; D-090; finishing with the 55% target open was the
user's choice, 2026-11-13)

> The bot values terrain (D-090). Bot and arena only: no rule, engine, state or file-format change. Read 13.08–13.11,
> D-087–D-089 and D-090 first.
> - **Gate and weight:** `MoveWeights::cover` (default 1, an arena `--weights` key, 0 to 1; outside exits 2) and
>   `cover_on` (terrain present and `cover` above 0). Each terrain-aware rate is `(1 - cover) x plain + cover x
>   terrain` (a target that can't be seen: terrain rate 0). With the gate off no terrain code runs, so terrain-free
>   play and `cover=0` play are the M88 bot's, bit for bit.
> - **Sight for hypothetical positions:** `src/ai/analysis/terrain_sight.*` (`TerrainSight`), over one
>   `core::TerrainView`: masks for any pose, area cover, could-hide, sees (Hidden beyond 15"), unit sees (up to
>   `kSightSamples` = 3 models a side, with a box pre-test), not fully visible. Model blockers are ignored.
> - **`analysis::ShotContext`:** `target_cover` (BS worsened by 1, as `attacks.cpp`) and `visible` (false: no volley),
>   defaulting to today's values.
> - **The enemy's reply** (`score_impl` → `hold_line`, `enemy_turn_from`, `other_threat`), per plan: area cover holds
>   at every end; the "stays" end uses visibility and cover from the enemy's current models; an end where it moves sees
>   again, except a hidden unit while the end gap is over 15". Hidden only when the plan makes no ranged attack this
>   turn (`shoot_now` 0; holding fire on purpose stays open). `RateKey`, `wipes_` and `Other::ranged` key on cover.
>   `our_next_turn` gets the enemy's area cover where it stands.
> - **Our shots in the cheap score** (`our_shooting`, `our_shooting_at_other`): a model that can't see the target
>   unit adds no volley; the target's cover from its area or from not being fully visible; the volley keys gain cover.
> - **Candidates** (appended after D-086's area spots): cover spots in every reachable area (a unit that takes cover),
>   hidden spots in an area with a Dense feature (offset away from the closest enemy), and a two-leg detour round a
>   blocking polygon's inflated silhouette corners for models that can't cross it.
> - **Hand deployment:** with cover on, M77's candidates sorted (stably) by the models with area cover (a Dense area
>   counts double) for a unit that takes cover; the first unit set up faces the enemy zone's centre.
> - **Arena:** a `cover (M89)` line on terrain battlefields, by target side (the selections into cover, the Hidden
>   refusals). The `--per-game` line is unchanged.
> - **Terrain-blind, recorded open in D-090:** the army plan, threat tables, charges, fights, `ShootingAim::reply`, our
>   next turn's visibility, model blockers, detours round circles.

**Acceptance**
- **Head-to-head** (the user's reading, 2026-11-12): on the terrain scenario the M89 bot (default weights) against the
  M88 bot (`cover=0`): `--weights-a cover=1 --weights-b cover=0` and the reverse, each with `--first A` and `--first B`,
  100 games each (seeds 1–100). The M89 bot wins at least 55% of the 400 games; confirmed on seeds 101–200.
- **Into cover:** against `cover=0`, the share of the enemy's ranged selections into cover goes up, and so do the
  Hidden refusals of the bot's units.
- **Recorded:** the Chaos Space Marines' wins on the terrain scenario (M89 on both sides, A first and B first) and on
  random armies with `--terrain` (auto and hand); the tuning (`cover` 0.5 / 1, `kSightSamples` 2 / 3 / 4, each
  candidate kind on or off) in D-090.
- **Identity:** the nine identity runs at the default weights identical to the M88 binary; the four terrain runs (the
  terrain scenario A first and B first, random armies `--terrain` auto and hand) with `--weights cover=0` identical to
  the M88 binary. Every `game` line.
- **Clean runs:** no rejected action, reverted move, fallback or unfinished game; p95 decision time at most twice
  M88's (16.6 ms).
- **Tests:** `tests/ai/m89_test.cpp`: `M89_Sight`, `M89_Odds`, `M89_MoveScore` (cover off and no terrain bit-identical;
  the blend at 0.5 halfway), `M89_Candidates` (the Hive Tyrant's detour), `M89_Deploy`, `M89_Arena`, `M89_Regression`
  (the terrain scenario with `cover=0` plays as at M88), `M89_Replay`. Tests pinning terrain-scenario play at the
  default weights are pinned to `cover=0`, as M87 pinned `M86_Bot`.
- **Replay:** `tests/replays/m89_cover_bot_game.json`, a bot game on the terrain scenario with a unit taking cover, a
  hidden unit and the bot's own detour; checked with core only and `TheSavedFileMatchesTheGame`. Every older replay
  byte-identical.
- **Docs:** D-090 with the design and the tuning; PLAN.md Results; `docs/dev/bot_arena.md` (the key, the line, the M89
  binary as the reference); README and the CLAUDE.md overview line. Screenshot under `xvfb-run`.
- **Build** and full `ctest` green.

**Results** (2026-11-13, D-090's "In code", Tuning and Final numbers)
- **The bot:**
  - `TerrainSight`: sight for hypothetical positions, lines estimated by three parallel lines (core's exact search took
    70% of the time). It agrees with core on 3,985 of 4,000 pairs.
  - The reply's and our shots' terrain in the move score.
  - Candidates into cover, hiding, behind obscuring areas and round walls.
  - Hand deployment into cover.
  - All gated on terrain and `cover` > 0.
- **Arena:** the `cover` key and the `cover (M89)` line.
- **Acceptance** (Release arena):

  | Item | Number | Target | |
  |---|---|---|---|
  | head-to-head, M89 against M88 (4 x 100) | 214 of 400 (53.5%), confirmed 213 (53.3%) on seeds 101–200; M88 against M88 200 | at least 55% | open (the user's choice) |
  | the enemy's selections into cover, M89 units as targets | Chaos 34.7% / 25.2% (M88 21.5% / 19.5%); Tyranids 58.9% / 47.5% (70.6% / 51.4%), but refused as Hidden 3.74 / 5.35 a game (0.08 / 0.95) | up | met for the Chaos Space Marines; the Tyranids hide instead |
  | Chaos Space Marines' wins, M89 against M89 | terrain scenario 21.0% (A first) / 34.5% (B first) (M88 25.0% / 38.5%); random armies `--terrain` 17.0% auto / 17.0% hand (18.5% / 18.5%) | recorded | |
  | identity: the nine runs at the defaults, the four terrain runs at `cover=0` | identical to the M88 binary, every `game` line (2,600 games) | identical | met |
  | clean runs, p95 | no rejected action, reverted move, fallback or unfinished game; p95 28 ms (Chaos) / 16 ms (Tyranids), random armies 18 ms | 0; at most 33 ms | met |
  | replays | every old replay byte-identical (m88's generator pinned to `cover=0`); new m89 | | met |

- **Why the target is open:**
  - Nearly all the gain is the bot knowing which enemies its models will see. The reply's terrain and the new
    candidates are within the noise, and charge odds round walls were worse (193).
  - The army plan, which picks the groups' targets, is still terrain-blind (D-090's open list).
- **Tests:** `tests/ai/m89_test.cpp`:
  - `M89_Sight`, `M89_Odds`;
  - `M89_MoveScore` (cover 0 and no terrain bit-identical, the blend halfway, Hidden only without shooting);
  - `M89_Candidates` (the Hive Tyrant's detour), `M89_Deploy`;
  - `M89_Regression` (the terrain scenario at `cover=0` as at M88), `M89_Arena`, `M89_Replay`.

  Changed: `M88_Replay`'s generator plays the bot at `cover=0`.
- **Replay:** `tests/replays/m89_cover_bot_game.json` (seed 2, a whole game, the Chaos Space Marines win):
  - 3 two-leg detours by the bot;
  - 43 shots into cover;
  - 8 hidden units refused to a shooter.
- **Build and `ctest`:** see the commit. A full game by mouse on Windows stays a manual check for the user.

---

## Later (post-V1, rough order)
- **Dice pop-up polish:** highlight critical hits and wounds on the dice (e.g. a gold border). `DieFace::critical` already carries it, and the result line already counts them.
- **Engine-paced dice rolls (Option 2 of M20):** a PendingDecision before each roll point (hit, wound, save, charge, battle-shock, hazard, advance), so the roll is made when the player presses Roll instead of being revealed after the engine has made it. Needs the attack sequence split into resumable steps, new decisions in `legal_actions`/`validate_action`, and every replay regenerated. M20's pop-up would then answer those decisions instead of revealing pre-made rolls.
0. **Deployment step:** done in M74 (engine), M76 (client) and M77 (the bots' heuristic), in the custom-battles batch
   (M71–M77). What is left of it is under item 5, "Open items carried from M71–M77".
1. **Terrain** (13): 2D terrain is M88 (D-087–D-089), and the bot's use of it is M89 (D-090). Still to do: the z axis
   (13.06 Moving Vertically and upper surfaces, 13.11 gaps above 3", 22.05 Plunging Fire), Stealth (24.33, left out of
   M88 by the user's choice), and a terrain editor for custom battles. Left open from M89: the head-to-head target
   (53.4% against 55%), the army plan's terrain-blind group rates, holding fire on purpose to stay hidden, and charge
   odds round walls (D-090's open list).
2. **Objectives** (14) and a simple mission: the engine is done in M86 (D-084), the bot in M87 (D-086). This enables
   Objective Consolidation and Bolter Discipline. Left open from M86: nothing secures an objective yet (14.03 is state
   only). Left open from M87: the objective scenario's balance (the Chaos Space Marines win about a third: the Tyranids'
   OC takes the Centre; an asymmetric mission, other armies or terrain are the levers), units holding back (about as at
   M83), and the bot's charges, pile-ins and consolidations still ignore the areas.
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
