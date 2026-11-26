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

V1 (M0–M13) and the post-V1 milestones M14–M96 are done (M96 is below); M97, below it, is proposed and waits for
agreement. Their full text (prompts, acceptance checks, results
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
| M92 | The app-only rules audit |
| M93 | M92's open items |
| M94 | Engine-paced dice rolls (open items) |
| M95 | Reaction windows, Command Points and the first stratagems (open items) |

---

## M96: Fire Overwatch with Snap Shooting, Heroic Intervention and Counteroffensive (D-097) ✅ (done; the manual mouse checks open)

**Effort:** max (core architecture). **Plan mode:** yes (agreed 2026-11-26, after a design pass on M95's code).
**Scope:** the opponent-turn core stratagems, on M95's reaction windows: Fire Overwatch with Snap Shooting (15.08,
15.09), Heroic Intervention (15.11) and Counteroffensive (15.12). The user chose them on 2026-11-25, with M96 in the same
session as M95. The user's rulings:
- [TORRENT] still hits automatically when snap shooting;
- a snap shot's 6 is not a critical hit (the app's 15.09, data v963);
- Fire Overwatch and Heroic Intervention are offered only when they can do something;
- a snap shot never re-targets;
- Counteroffensive: play returns to Fights First at once, a pass stays allowed (the requirement lapses), and the window
  follows any enemy fight.

**Rules:**
- PDF: 15.01, 15.08–15.12, 04.01–04.03, 10.02, 11.02, 11.04, 12.04, 17.03, 24.13, 24.16, 24.37, 01.03, 01.07.
- App: 01.03.02, 07.02.01 Out of Phase Rules, 14.02.01, 15.09 (v963); FAQ Q2, Q45, Q50, Q55, Q56, Q62, Q71.
- Decisions: D-018, D-020, D-084, D-091, D-093, D-095, D-096.

**Design** (D-097 has the full text)
- *Windows:*
  - `WindowTiming::EndOfPhase`, opened on entering the End of Movement and End of Charge phase steps for the player
    whose turn it isn't;
  - `WindowTiming::AfterAttacks`, just after a unit has fought in the Fight step, for the other player;
  - the window's roll and unit become optional;
  - `UseStratagem` gains Heroic Intervention's mode;
  - each opens only when a stratagem there can do something.
- *Fire Overwatch:* a `SnapShoot{unit}` decision, answered by `Shoot{unit, Snap, shots}`.
  - Snap shooting allows one target within 24" that is visible and eligible.
  - Only an unmodified 6 hits, and it is not critical; [TORRENT] still hits automatically.
  - There is no hit re-roll and no re-target, and [HEAVY] and 17.03 are off (07.02.01).
- *Heroic Intervention:*
  - the mode is chosen with the stratagem, then its player makes the charge roll;
  - Into the Fray caps the roll at 6 and costs 2CP;
  - the targets are filtered by the mode, and the charge move gives Fights First until the end of the turn.
- *Counteroffensive:*
  - the unit gets Fights First for the phase and is its player's next selection;
  - from Resolve Remaining Combats, play goes back to Fights First;
  - a pass stays allowed.
- *14.02.01:* objective control is determined on entering each End of Phase step, before any window.
- *Format 4:* the new windows are recorded; format 3 converts by declining them, and every replay is regenerated.
- *Client:*
  - the window asks for the unit and the mode;
  - snap shooting uses the shot editor;
  - the cards and the log say what each stratagem did.
- *Bot:*
  - three keys (0 or 1), with heuristics from the existing odds;
  - the session declines a window that offers only stratagems a side doesn't play;
  - the defaults come from D-092's keep rule.

**Acceptance**
- [x] Each stratagem is offered only at its WHEN, to the right player, for a TARGET it can act on. 15.01's cost and
  restrictions and 01.07 are enforced with rule-cited refusals, and declining changes nothing.
- [x] Snap shooting: one target within 24", visible and eligible; only an unmodified 6 hits (not critical); [TORRENT]
  automatic; no hit re-roll or re-target; [HEAVY] and 17.03 off. `R15_09_*` tests each.
- [x] Heroic Intervention: Leap to Defend and Into the Fray (the cap, 2CP, the 6" targets). The charge is the unit's
  player's, and the move gives Fights First. `R15_11_*`.
- [x] Counteroffensive: Fights First for the phase; the unit is its player's next selection; the return to Fights
  First, the pass and the lapse; it fights between two enemy Fights First units. `R15_12_*`.
- [x] 14.02.01: control is determined before the window; every event dump is unchanged.
- [x] The closure holds (inline declines every window). Format 4 round-trips. Formats 1 to 3 load, and the legacy
  fixtures convert. Every new field has a JSON round-trip test and is omitted when absent.
- [x] Every replay is regenerated as format 4 with only declines added, and the M95/M96 event dumps are byte-identical.
  `m96_reactions.json` shows each stratagem.
- [x] Bots:
  - the identity set at `command_reroll=0` matches M95 in every field but `steps` and `actions`;
  - at the defaults, every `state` is the same;
  - the keys are measured, with their defaults set by the keep rule;
  - p95 is within budget.
- [x] Client: each window asks for its unit and mode; snap shooting in the shot editor; the texts; logic tests and xvfb
  screenshots.
- [x] Full `ctest` green, warnings as errors.

**Results** (D-097 part 10 has them in full): the M95 and M96 event dumps of the 30 older replays are byte-identical; the
30 replays regenerated as format 4 are each their format-3 file converted (129 declines added); `m96_reactions.json`
shows each stratagem; the bots' identity set at `command_reroll=0` is the M95 binary's in every field but `steps` and
`actions` (2,600 games), and at the defaults every `state` is the same. Head-to-heads (400 games each): Fire Overwatch
+26 (on by default), Heroic Intervention +2 and Counteroffensive +0 (off), all three +24; p95 unchanged.

**Open items.**
- Manual checks by mouse on Windows: each new stratagem in hotseat and against the bot (Fire Overwatch and the snap
  shot's editor, Heroic Intervention in both modes with its charge move, Counteroffensive), and Undo after each.
- The window's units are picked from the decision panel's buttons (the board outlines them in gold); no board mode
  with range rings (24", 12", 6") or highlighted chargers.
- The bot: the thresholds were not tuned (one setting measured); Counteroffensive is judged from the sequence's state,
  not a scratch simulation; Heroic Intervention's values use M42's horizon from the enemy's turn.
- 15.09's "not eligible to start an action" waits for actions (16).

---

## M97: Explosives and Crushing Impact (proposed, not yet agreed)

**Effort:** xhigh (two rule areas on M95's and M96's windows). **Plan mode:** yes.
**Scope:** the last two core stratagems the engine can take without a new rule system, both used in their player's own
turn and both ending in mortal wounds (06.02): Explosives (15.05) and Crushing Impact (15.06). The other core
stratagems wait for what they need: Epic Challenge (15.03) for [PRECISION] and attached units (19), Rapid Ingress
(15.07) for strategic reserves (20), Smokescreen (15.10) for a SMOKE unit. The Intercessors have the EXPLOSIVES
keyword; the Hive Tyrant, Screamer-Killer and Tyrannofex are MONSTERs.

**Rules:**
- PDF: 15.01, 15.05, 15.06, 06.02, 10.02, 11.04, 04.02 (visible, unengaged targets), 17.01, 01.07.
- App: 01.03.02 Rules Sequencing. The app's headings swap the two numbers (15.05 Crushing Impact, 15.06 Explosives);
  the PDF's numbers are used (D-098: "Crushing Impact stratagem (15.06)" in 12's example, in both sources). The
  core-rules changelog's errata (the Grenade and Tank Shock stratagems are Explosives and Crushing Impact) only rename
  older references.
- Decisions: D-095, D-096, D-097, D-098.

**Questions to settle in plan mode:**
- Explosives' WHEN is "Your Shooting phase". Is it offered at the Shoot step as an answer beside the Shoot templates
  (`UseStratagem{Explosives, unit}` among the step's legal actions), or as a window at the step's start? Does the unit
  stay eligible to shoot afterwards (15.05 doesn't say it can't, and a stratagem doesn't select the unit to shoot,
  10.02)?
- Explosives' model and target: one EXPLOSIVES/GRENADES model, then an unengaged enemy unit within 8" of it and visible
  to it. Are they picked in a decision after the stratagem, or named with it?
- Crushing Impact's WHEN, "just after a friendly MONSTER/VEHICLE unit ends a charge move": a new `AfterChargeMove` timing
  for the charging player (their own rule, before the opponent's, 01.03.02). Is a Heroic Intervention charge move
  (M96) one too? It is in the opponent's Charge phase, so not "your Charge phase".
- "To a maximum of 6 mortal wounds per unit": each side's unit capped at 6.

**Design (to agree in plan mode)**
- *Windows and answers:* Explosives at the Shoot step, Crushing Impact at a new `AfterChargeMove` timing.
- *Explosives:* the model and the target, then six D6 as one roll of its player's (`RollDice`): each 4+ a mortal wound
  to the target (06.02). Command Re-roll can re-roll one of the dice (15.02 lists no such roll yet; to be settled).
- *Crushing Impact:* the engaged enemy unit and the model engaged with it, then T D6 as one roll: each 1 a mortal
  wound to our unit, each 5+ one to theirs, each side capped at 6.
- *Replays:* format 5 only if a format-4 file can't hold the new answers (to be checked in the design); every older
  replay either byte-identical or regenerated with only declines added.
- *Client:* "Explosives (1 CP)" offered at the Shoot step for an EXPLOSIVES/GRENADES unit; Crushing Impact's window
  after the charge move; the cards for the new rolls; the log.
- *Bot:* two keys (0 or 1), measured by D-092's keep rule: Explosives when six D6 at 4+ are worth the CP (and the unit's
  shooting isn't lost); Crushing Impact when the mortal wounds expected on the enemy outweigh those expected on us.

**Acceptance (draft)**
- [ ] Each stratagem only at its WHEN, for a TARGET it can act on, with 15.01's cost and restrictions and 01.07;
  rule-cited refusals; declining changes nothing; the closure holds.
- [ ] Explosives: one model, one unengaged visible enemy unit within 8" of it, six D6, a mortal wound per 4+. `R15_05_*`.
- [ ] Crushing Impact: one engaged enemy unit, one model engaged with it, T D6, the 1s and the 5+s, the caps. `R15_06_*`.
- [ ] Replays and saves: the new answers recorded; older formats convert; a new `m97_*.json` shows both.
- [ ] Bots: the identity proofs with the keys at 0 and at the defaults; the keys measured; p95 within budget.
- [ ] Client: the offers, the cards and the log; logic tests and xvfb screenshots.
- [ ] Full `ctest` green, warnings as errors.

The alternative next step is the first line of the roadmap's Phase 1: the weapon abilities still refused when selected
(Lethal Hits, Devastating Wounds, Anti-X, Melta, Lance, Precision), which unlock the most datasheet profiles.

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
   M96 (D-097) Fire Overwatch with Snap Shooting, Heroic Intervention and Counteroffensive. Still to do: Explosives and
   Crushing Impact (no new rule system needed), then Epic Challenge ([PRECISION], attached units), Rapid Ingress
   (reserves) and Smokescreen (a SMOKE unit).
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
