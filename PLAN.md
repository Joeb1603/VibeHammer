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

V1 (M0–M13) and the post-V1 milestones M14–M97 are done. Their full text (prompts, acceptance checks, results and open
items) is in `docs/plan_archive.md`; read a section there only when the current milestone needs it. M97's open items
not taken by M98: the manual mouse checks on Windows (M94–M98), Explosives' CP against a later Command Re-roll, the
thresholds, and Explosives in the Start and End of Shooting phase steps.
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
| M92 | The app-only rules audit |
| M93 | M92's open items |
| M94 | Engine-paced dice rolls (open items) |
| M95 | Reaction windows, Command Points and the first stratagems (open items) |
| M96 | Fire Overwatch with Snap Shooting, Heroic Intervention and Counteroffensive (open items) |
| M97 | Explosives and Crushing Impact (open items) |

---

## M98: M97's open items: the selection on the board, Crushing Impact in the charge planning (D-100) ✅ (done; the manual mouse checks open)

**Effort:** high (client and bot work on M97's rules; one read-only core query). **Plan mode:** no. The user asked for
M97's open items on 2026-11-26 ("Do the still open tasks").
**Scope:** two of M97's open items.
1. *The selection on the board:* 15.05's and 15.06's model-and-enemy step (`SelectModelAndEnemy`) is answered by
   clicking. A click on an enemy unit submits its pair, with the model chosen or the first legal one (as the bar's
   buttons). A click on a model of the stratagem's unit chooses it first. The bar's buttons stay.
2. *The charge planning prices Crushing Impact:* with the `crushing_impact` key on, a planned charge move's value gains
   what Crushing Impact is expected to gain at its end. That is its window's valuation less the CP's worth
   (`kCrushMinGain`), counted only where the engine would offer it there. The key's default is measured again by D-092's
   keep rule.

Left out:
- the manual mouse checks on Windows: they need the user's own computer, and none was attached to the cloud session;
- weighing Explosives' CP against a later Command Re-roll, and tuning the thresholds;
- Explosives in the Start and End of Shooting phase steps.

**Rules:** 15.01, 15.05, 15.06 and 01.07; no new rule text. Decisions: D-024, D-029, D-042, D-051, D-092, D-097, D-099,
D-100.

**Design** (D-100 has the full text)
- *Core:* `forecast_stratagem_problem` in `engine.hpp` gives the refusal a window would give, were it opened in the state
  as it stands, or nullopt. It runs the window's own checks (15.01, 01.07, the WHEN and the TARGET), for the windows that
  follow no roll. It is read-only, for the bots' forecasts (D-024).
- *Client:* `BoardInteraction` gains `Mode::PickEffect` at the `SelectModelAndEnemy` decision:
  - the enemy units in some legal pair are clickable, and so is the stratagem's unit (to choose its model); every other
    unit is grey;
  - a model in no legal pair is dimmed and can't be chosen;
  - once a model is chosen (a gold ring), only the enemy units legal with it can be clicked;
  - hovering a unit that can't be clicked gives the engine's reason;
  - Esc, an empty click or a second click on the model drops the model.
  The bar keeps one button per enemy unit, for the chosen model's pairs once one is chosen.
- *Bot:* `crushing_impact_value(after, unit)` in `normal/stratagems`: the best pair's gain (as at the window) less
  `kCrushMinGain`, when positive and the forecast allows it. It is added to:
  - each roll's value in `charge_prospect` (the charge declaration and the move score's charge term);
  - each target set's value in `decide_charge_targets`.
  All of this happens only with the key on. At 0 nothing is computed, so the default bot plays exactly as at M97.

**Steps**
1. This section and D-100.
2. The core query, with `D100_*` tests.
3. The board mode, with `M98_*` client tests and xvfb screenshots.
4. The bot's pricing, with `M98_*` bot tests:
   - the identity set at the defaults against the M97 binary: every field of every game the same;
   - the head-to-heads for `crushing_impact` and for both keys, with the defaults set by the keep rule.
5. Docs: D-100's results and D-099's amendments, `docs/dev/bot_arena.md` and `client_flags.md`, this section marked
   done, and M97 archived. Then the full `ctest`, commit, push, the PR and the rebase merge.

**Acceptance**
- [x] **The board:**
  - a click on an enemy unit submits its pair (the chosen model's, else the first legal model's);
  - a click on a model of the stratagem's unit chooses it, and a model in no pair is dimmed and ignored;
  - the units that can't be picked are grey, with the engine's reason on hover;
  - Esc drops the model, and the bar's buttons stay;
  - `M98_*` client tests and xvfb screenshots.
- [x] **The forecast:** the window's refusal, or nullopt where it would offer the stratagem. `D100_*`.
- [x] **The charge planning:**
  - with `crushing_impact` on, it adds Crushing Impact's expected net gain wherever the engine would offer it;
  - with the key off, every value is as before: the identity set against the M97 binary is identical in every field.
- [x] **The keys:** head-to-heads for `crushing_impact` and for both, with the defaults set by D-092's keep rule.
- [x] **Full `ctest`** green.

**Results** (D-100 part 6 has them in full):
- *The board:* the selection is picked by clicking. The tests cover each kind of click (an enemy unit, a model, the chosen
  model again, a model in no answer, Esc), and every answer submitted is legal, with the session going on to the roll.
  The xvfb screenshots show Explosives' selection with two enemy units in reach of different ends of the Legionaries'
  rank, the same with model 8 chosen, and Crushing Impact's selection.
- *The forecast* agrees with the window after the Hive Tyrant's charge, and gives each of its refusals.
- *The bots:*
  - at the defaults, the identity set (2,600 games) is the M97 binary's in every field;
  - the head-to-heads: `crushing_impact` -8 (196-204; M97 -10) and both keys -2 (199-201; M97 -4), every run clean.
    Both keys stay off.
- *Tests:* the full `ctest` passes, 1,693 of 1,693. M97's check that the board left the selection to the bar now checks
  the board's mode.

**Open items.**
- The manual checks by mouse on Windows for M94-M98. They need the user's computer: the cloud session had none attached.
  - M98: the selection by clicking (a model first or not, Esc, the grey units' reasons on hover).
  - M97: Explosives and Crushing Impact in hotseat and against the bot (keys on), and Undo after each.
- The bot: Explosives' CP against a later Command Re-roll; the thresholds (one setting measured each).
- Explosives in the Start and End of Shooting phase steps.

---

## Later (post-V1, rough order)
The long-range map from here to a full game with any army on any mission (phases, order, open questions) is
`docs/roadmap.md`. The items below are the nearer, smaller ones; each roadmap item still becomes a milestone here first.

- **Dice pop-up polish:** done in M95 (a shown critical die has a gold border).
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
4. **Core stratagems** (15): M95 (D-096) built reaction windows, CP spending (15.01), Command Re-roll and Insane Bravery;
   M96 (D-097) Fire Overwatch with Snap Shooting, Heroic Intervention and Counteroffensive; M97 (D-099) Explosives and
   Crushing Impact. Still to do: Epic Challenge ([PRECISION], attached units), Rapid Ingress (reserves) and Smokescreen
   (a SMOKE unit).
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
