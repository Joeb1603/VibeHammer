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

V1 (M0–M13) and the post-V1 milestones M14–M91 are done; M92, M93 and M94 are below. Their full text (prompts, acceptance checks, results
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
| M84 | Docs housekeeping |
| M85 | One bot |
| M86 | Objectives and a mission |
| M87 | The bot plays for objectives (open items) |
| M88 | Terrain in 2D |
| M89 | The bot plays for cover (open items) |
| M90 | Objective Consolidation and [IGNORES COVER] |
| M91 | The bot plays terrain and objectives in every phase (open items) |

---

## M92: The app-only rules audit (D-093)

**Effort:** high. **Scope:** Phase 0, item 1 of `docs/roadmap.md`. Agreed with the user on 2026-11-23: compare the 101
app-only entries in `docs/gdmissions/core_rules/` with D-001, D-009, D-010, D-011, D-012, D-015, D-069 and every other
decision that leans on an app-only rule; **the scraped app text overrides the wording the user supplied** (the user's
ruling), and the differences are built now.

**Rules** (app-only, cited through D-093): 04.03.03 Target No Longer Eligible or Viable; 13.11.01 Gone to Ground;
14.02.01 Controlling an Objective at the End of a Phase; 24.11.01 Only [EXTRA ATTACKS] weapons; 02.02.01 Modifiers (BS
no worse than 6+). PDF rules read: 04.03, 05.04, 13.09, 13.11, 14.01–14.03, 03.03, 24.11, 25 (Rules Appendix).

**Acceptance**
- [x] D-093 records every app-only entry the engine touches: what changed, what matched, what waits for later rules.
- [x] 04.03.03: a weapon whose target is gone can select an eligible new target (`RetargetAttacks`, the attacking
  side's decision); unselected ones are lost; with no eligible new target nothing is asked (D-012's behaviour). Shooting
  checks the new target with the unit's shooting type; fighting, an enemy unit the model is engaged with. Select Targets
  facts are recorded again; new batches are appended. Validation reasons are rule-cited. JSON round trips.
- [x] 13.11.01: a hidden model not fully visible due to intervening Dense features has a 12" detection range (engine
  and the bot's sight estimate).
- [x] 14.02.01: end-of-turn control is determined before Regaining Coherency.
- [x] 24.11.01: [EXTRA ATTACKS] weapons may be selected without another melee weapon.
- [x] 02.02.01: a BS worsened by cover stops at 6+.
- [x] The bot, the defaults and the fallback answer `RetargetAttacks`; the client shows its answers, logs the event and
  keeps the dice cards' batches.
- [x] Every existing replay stays byte-identical; a new replay, `m92_retarget.json`, shows a re-target.
- [x] Full `ctest` green.

**Results.** Every existing replay is byte-identical: no saved game had a weapon left with an eligible new target. The
bot's pinned M88 game on the terrain battlefield (M89's regression) keeps its actions and final state; its trace hash
changed with Gone to Ground (re-pinned). Random play on the Strike, monster and Tyrannofex fixtures reaches the new
decision and stays valid.

**Open items.** All three done in M93 (below).

---

## M93: M92's open items (D-094)

**Effort:** high. **Scope:** the three open items of M92, asked for by the user on 2026-11-23.

**Rules:** 04.03.03 (D-093), 14.01.01 Objectives Not Within a Terrain Area (app-only; also the Rules Appendix), 14.01,
14.02, 12.08, 13.11.01 (D-093).

**Acceptance**
- [x] Client: one row per weapon waiting on a gone target, each picking an eligible unit or "lost"
  (`RetargetPicks`, core's read-only `retarget_choices`); the engine's buttons stay.
- [x] Objective markers: `ObjectiveArea::marker` (40 mm, within range at 3", held as its range circle); the scenario
  format (`"marker": true`, no diameter), JSON, a terrain area can't link to one; 12.08's "within 3"" measured to the
  marker; the board draws the marker. The objective scenario's objectives are markers; custom battles and the arena over
  terrain read the terrain scenario's mission.
- [x] Bot: 13.11.01 in `moved_visible` behind `gone_to_ground`, measured by M91's head-to-head; on by default only if
  better (D-092's keep rule).
- [x] Replays regenerated on purpose only where the rules moved them; every other one byte-identical. Full `ctest` green.

**Results.**
- *Bot:* `gone_to_ground` on won 208 of 400 against it off on the terrain scenario (+8, under one standard deviation),
  so it defaults to 0 and the bot plays as at M92 (the terrain bot games and pins are unchanged).
- *Objective scenario with markers* (200 games, default weights): Chaos Space Marines win 67 (33.5%; 32.5% at M87), 9 of
  them on points destroyed at equal VP. Mean objective VP A 15.6 / B 17.9; the Centre is still the Tyranids' (B 1.95 VP a
  game, A 0.08). The balance item of M87 stays open.
- *Replays:* `m87_objectives_bot_game.json` and `m90_objective_consolidation.json` regenerated (the markers); the M91
  objective-scenario pin re-pinned. `ctest`: 1485 of 1485.

**Open items.** None new. The objective scenario's balance (Chaos about a third) is M87's, unchanged.

---

## M94: Engine-paced dice rolls (D-095)

**Effort:** max (core architecture). **Plan mode:** yes (agreed 2026-11-25; the design below is the agreed plan, revised
the same day after a design review: decisions derived from flow state, `apply_action` keeps rolling inline, auto-rolls
in `GameSession`, random damage once per batch).
**Scope:** roadmap Phase 0 item 2 (Option 2 of M20). The engine stops at every roll point with a `RollDice` decision for
the roller, and the roll is made when the roller submits it. Groundwork for Command Re-roll (15.02 "just after you
make" the roll), re-roll abilities and Feel No Pain. The user's choices (2026-11-25): one milestone (engine, bot,
replays, saves and client together); a human's rolls follow "Show dice pop-ups" (on: press Roll; off: automatic), and
bot rolls are always automatic; every roll is recorded as a step (format-2 saves and replays; format 1 migrates on
load; all 28 test replays regenerated); a random D is rolled once per batch (the per-attack split waits for Feel No
Pain / Command Re-roll).

**Rules:** 01.05, 01.05.02 (app), 04.03, 05.01–05.04, 06.03, 08.03, 09.06, 09.07, 11.02, 24.15; D-033, D-069, D-072.

**Design**
- *The roll closure (the invariant):* the old `apply_action(X)` equals the paced `apply_action_paced(X)` followed by
  `RollDice` answers until no roll is pending: the same dice in the same order, the same events, the same state. If a
  roll point can't keep it, stop and ask.
- *One machine, two ways to apply:* the engine always stops at a `RollDice`. `apply_action` / `start_game` (every
  existing overload) answer each pending `RollDice` for its roller with the same dice, inside the same rollback scope:
  the M93 behaviour, so the bot's `Simulator`, the M39 guard, the existing tests and format-1 replays are unchanged.
  `apply_action_paced` / `start_game_paced` stop at the next decision, a `RollDice` included (used by `Match` in
  `RollMode::Paced`, so by `GameSession`, by format-2 replays and by the D095 tests). Also `roll_pending(state)` and a
  read-only `roll_dice_info(state)` (who rolls what, how many dice) for the client's pre-roll card (D-024).
- *Types* (appended to their variants): `enum class RollKind { Advance, Charge, BattleShock, Hazard, RandomAttacks,
  Hit, Wound, Save, Damage }`; `decision::RollDice{roll, unit}` and `action::RollDice{roll, unit}` (one legal action;
  validated against the pending decision). The roller is the unit's owner: for a Save the unit is the target (05.03),
  for a Damage roll the attacker (D-069). New state, each omitted from JSON when empty and hashed behind a tag when set:
  `AttackFlow::rolling` (a selected batch until its wound roll, with its hit rolls), `BatchInProgress::priority` (the
  casualty order while a random D waits), `FlowState::battle_shock_tests` (the battle-shock rolls owed). None is ever
  set at a non-Roll decision, so states at the bots' decisions are as at M93.
- *Roll points* (each decision derived from the flow state; one Roll each, 05: "make all of those rolls
  simultaneously"):

| Point | Today | After M94 |
|---|---|---|
| 09.06 Advance | `begin_move` (`movement.cpp`) | `StartMove{Advance}`, then a Roll, then `MoveModels`. A reused roll (D-005) asks nothing. |
| 09.07 Desperate Escape hazards | `begin_move` | a Hazard roll before `OrderCasualties` |
| 09.07 Desperate Escape battle-shock | at `CommitUnitMove` | the commit owes a BattleShock roll (the battle-shock queue) |
| 08.03 Battle-shock step | a loop in `battle_shock_step` (`command.cpp`) | the queue, filled on entering the step: one roll per unit, in unit order |
| 11.02 Charge | `DeclareCharge` apply | a Charge roll, then `SelectChargeTargets` or failure |
| D-069 random A | `start_batch` (`attack_flow.cpp`) | a RandomAttacks roll if the batch has random A |
| 05.01 Hit | `start_batch` | a Hit roll (none for [TORRENT]); results stored |
| 05.02 Wound | `start_batch` | a Wound roll (none with no hits); Twin-linked's `RerollWoundRolls` follows as now |
| 05.03 Save | `attack_progress` | a Save roll by the defender after the allocation order |
| 05.04 random D | inside `resolve_casualties` | one Damage roll per batch, by the attacker, after the casualty order |
| 24.15 Hazardous | `make_attack_hazard_rolls` | a Hazard roll by the attacker after the last batch |

- *Replays and saves:* format 2 records the Roll steps; format 1 is played inline (replays) or converted with
  `with_roll_steps` (saves, the generators). All 28 `tests/replays/` files are regenerated with byte-identical `expect`
  blocks; old copies (m8, m62) and an M93 save are kept in `tests/data/legacy/` as migration fixtures.
- *Bot:* in live games `GameSession` rolls for bot sides, so `Bot::decide` never gets a `RollDice` (its fallback answers
  one anyway). Arena `steps`/`actions` change by construction; `state` and `trace` must not.
- *Client:* `GameSession` has a per-side `auto_roll` (bot sides; human sides when `show_dice` is off): a pending
  `RollDice` for such a side is submitted at once, each Roll a step. Undo never takes a Roll back on its own. With
  pop-ups on, a human's pending `RollDice` shows a face-down pre-roll card (`logic/roll_prompt`); its Roll submits
  `RollDice` (joining the previous input's Undo group), and the event cards then open already rolling. Bot cards keep
  the cosmetic Roll (`DicePace`). `--steps` counts Roll clicks; `--bot-steps` still counts bot decisions.
- *Docs:* D-095; amend D-033, D-069, D-072, D-014, D-025, D-039; CLAUDE.md's engine API; the roadmap's Phase 0 item 2.

**Acceptance**
- [x] Each roll point is a `RollDice` decision for the roller, and the Roll answer makes the same dice in the same
  order. `D095_RollPoints` tests cover each point, the roller's side, the skipped cases (Torrent, no hits, fixed D,
  reused Advance, no Hazardous roll with no enemy left) and a refused Roll (wrong side, kind or unit).
- [x] The closure holds: the existing tests pass unchanged through the inline `apply_action`; every regenerated
  replay's `expect` is byte-identical; its steps without the Roll steps are the old ones; the paced and inline runs of
  every replay give the same events and state; the M93 and M94 event dumps of all 28 replays are identical.
- [x] Format-1 saves and replays load (fixtures in `tests/data/legacy/`); format 2 round-trips. Every new state field
  has a JSON round-trip test.
- [x] Bots: arena `--per-game` `state` and `trace` hashes identical to M93 on the identity set; decision p95 within
  budget.
- [x] Client: with pop-ups on, a human's roll waits for Roll and the faces match the events (logic test); with them off,
  or for a bot, rolls are automatic; Undo goes back past Rolls; no deadlock against the bot (logic test); xvfb
  screenshots of a pre-roll card for a hit roll and a save.
- [x] Full `ctest` green, warnings as errors.

**Results.**
- *The closure:* the core suite passed unchanged through the inline `apply_action` after each roll point moved. The
  M93 and M94 dumps of all 28 replays (every event and the final state) are byte-identical, the M94 one both inline
  (format 1) and paced (format 2). `D095_Closure` checks paced against inline for every replay, for random games on V1,
  the rosters and Strike, and for each roll point with scripted dice.
- *Replays:* all 28 regenerated as format 2: the same description, initial state and `expect`, and the same steps once
  the 1009 roll steps are taken out (scripted against `git show HEAD:`). New `m94_paced_rolls.json`: a Desperate Escape
  (its hazard and battle-shock rolls), the 08.03 step, and a volley and a charge with every other kind of roll, 11 of
  its 28 steps rolls.
- *Bots:* the identity set (13 runs, 2,600 games) is identical to the M93 binary in every per-game field but `steps` and
  `actions`. Run one after the other on an idle machine, the decision p95 is 1.6-2.9% higher (terrain 12.7 -> 13.1 ms,
  objectives 9.0 -> 9.2 ms, random armies 6.6 -> 6.7 ms; wall time +2-4%): the inline roll loop's extra passes in the
  bot's simulations. Far inside the 500 ms budget.
- *Tests changed, each checked:* the client's history counts now include the rolls (`M13_Undo`, `M61_Undo`,
  `M16_BoardInteraction`); the per-game pins re-pinned `actions` only (M87, M89, M91; `M78_Arena`'s line its steps too:
  every `state` and `trace` pin unchanged); the "file is what the builder plays now" checks and every
  `TheSavedFileMatchesTheGame` compare as format 2; `M88_Replay` applies the file's format; the serialization lists gain
  `RollDice`. `ctest`: 1520 of 1520 (35 new: 24 `D095_*`, 8 `M94_*` client, 3 `M94_Bots`).
- *Client:* the pre-roll card (`roll_prompt`, the same title, purpose and need as the rolled card) and its Roll button;
  xvfb screenshots of a human's hit card before and after Roll and of the defender's save card; the package's
  `--bot-steps 5` smoke test still takes 5 of 5.

**Open items.**
- A random D is rolled once per batch; the per-attack split, and a decision between each roll and its result (15.02
  Command Re-roll, Feel No Pain), are for their milestone (D-095 part 4 lists what resolves inside its Roll today).
- The pre-roll card's damage dice are the casualty preview's count (each D at its maximum), so a lower D can roll more.
- Manual checks by mouse on Windows: pressing Roll in hotseat and against the bot, Undo just after a roll, saving while a
  roll waits, and turning the dice pop-ups off while a roll waits.

---

## Later (post-V1, rough order)
The long-range map from here to a full game with any army on any mission (phases, order, open questions) is
`docs/roadmap.md`. The items below are the nearer, smaller ones; each roadmap item still becomes a milestone here first.

- **Dice pop-up polish:** highlight critical hits and wounds on the dice (e.g. a gold border). `DieFace::critical` already carries it, and the result line already counts them.
- **Engine-paced dice rolls (Option 2 of M20):** done in M94 (D-095).
0. **Deployment step:** done in M74 (engine), M76 (client) and M77 (the bots' heuristic), in the custom-battles batch
   (M71–M77). What is left of it is under item 5, "Open items carried from M71–M77".
1. **Terrain** (13): 2D terrain is M88 (D-087–D-089), and the bot's use of it is M89 (D-090). Still to do: the z axis
   (13.06 Moving Vertically and upper surfaces, 13.11 gaps above 3", 22.05 Plunging Fire), Stealth (24.33, left out of
   M88 by the user's choice; the Genestealers list it only because the fetch tool ignores BSData's Chameleonic
   condition, D-070), and a terrain editor for custom battles. Left open from M89: charge odds round walls (D-090's
   open list). M91 built the army plan's terrain rates and holding fire to stay hidden, both
   off by default (no measured gain, D-092); the 55% head-to-head target is open.
2. **Objectives** (14) and a simple mission: the engine is done in M86 (D-084), the bot in M87 (D-086). This enables
   Bolter Discipline. Left open from M86: nothing secures an objective yet (14.03 is state only). Left open from
   M87: the objective scenario's balance (the Chaos Space Marines win about a third: the Tyranids' OC takes the
   Centre; an asymmetric mission, other armies or terrain are the levers) and units holding back (about as at M83).
   M90 adds Objective Consolidation (12.08). M91 built the objective terms into charges, pile-ins and consolidations
   (off by default: no measured gain, D-092).
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
