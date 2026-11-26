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

V1 (M0–M13) and the post-V1 milestones M14–M94 are done; M95 is below, the current one. Their full text (prompts, acceptance checks, results
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

---

## M95: Reaction windows, Command Points and the first stratagems (D-096)

**Effort:** max (core architecture). **Plan mode:** yes (agreed 2026-11-25, after a design review).
**Scope:** roadmap Phase 0 item 3. A window is a decision offered to a side at a trigger, ordered by Rules Sequencing
(01.03.02, app-only). Its first users are spending CP (15.01) and two core stratagems: Command Re-roll (15.02, just
after a roll) and Insane Bravery (15.04, just before a battle-shock roll). The user's choices (2026-11-25):
- one milestone (engine, bot, replays, saves and client together);
- all replays regenerated as format 3 (each window answer a step);
- stratagems offered to a human by default, with an "Offer stratagems" setting to stop the offers;
- the bot uses them behind arena keys, on by default only if they measure better (D-092's keep rule);
- the "Later" item "Dice pop-up polish" (a gold border on critical dice) folded in.

Roadmap question 4 (BSData) was answered on the side.

**Rules:** 15.01, 15.02, 15.04; app 01.03.02, 01.05.01, 01.05.02, 15.01.01-03, the FAQ's Insane Bravery entry; 01.07,
04.03, 05.01-05.04, 06.03, 08.02, 08.03, 09.06, 09.07, 11.02, 24.15, 24.38; D-005, D-011, D-018, D-023, D-069, D-072,
D-095.

**Design** (D-096 has the full text)
- *The window:*
  - `decision::UseStratagems{timing, unit}`, answered by `DeclineStratagems` (listed first) or
    `UseStratagem{stratagem, unit, die}`;
  - it opens only when its side can use a stratagem there; otherwise play is exactly M94's;
  - `FlowState::window` holds the roll's dice while it waits and is checked first by `compute_decision` and `advance`.
- *15.01:*
  - CP spent, never below 0;
  - each stratagem once per phase per player, and one stratagem per unit per phase;
  - 01.07 (no stratagem on one's own battle-shocked unit);
  - the records in `GameState::stratagems`, the per-phase ones cleared at the end of each phase.
- *Command Re-roll:*
  - the window comes just after the roller's Advance, Charge, Hazard, random-A, Hit, Wound, Save and Damage rolls
    (saves are the defender's);
  - one die is re-rolled (a charge roll in full; a die Twin-linked re-rolled can't be picked), then modifiers apply.
  - Each roll point becomes: roll faces, emit its event (unchanged), `after_roll` (open a window or apply at once).
    Applying is M94's consequence, rebuilt from the faces by the same judge functions.
- *Damage per attack:* one `RollDice{Damage}` and window per attack that inflicts damage. `plan_damage` replays
  `inflict_damage` on a copy with the stored rolls; the batch resolves at the end as in M94. `DamageRolled` is the one
  new event without stratagems.
- *Insane Bravery:* a window before each 08.03 roll of a unit that isn't battle-shocked; used, the roll passes with no
  dice; once per battle; never for the 09.07 roll.
- *The closure:*
  - inline `apply_action` answers every roll and declines every window, so the bot's simulator, the tests and
    format-1 play are M94's;
  - paced play stops at windows;
  - the M94 and M95 event dumps are identical but for `DamageRolled`.
- *Format 3:* window answers and per-attack damage recorded. `with_roll_steps` converts format 1 (roll and decline
  everything) and format 2 (step by step; a save stopped at a roll stays there). All 29 replays are regenerated, with
  format-2 copies and two M94 saves kept in `tests/data/legacy/`.
- *Client:*
  - GameSession `auto_decline` per side;
  - "Offer stratagems" (default on);
  - the rolled card's "Command Re-roll (1 CP)" with die picking, its Continue declining;
  - a pop-up when the dice pop-ups are off;
  - Insane Bravery on the battle-shock pre-roll card;
  - Undo never takes a decline back alone;
  - critical dice in gold.
- *Bot:* keys `command_reroll` and `insane_bravery`. A side with its keys off declines through the session, so the
  arena's identity set keeps `state` and `trace`.

**Acceptance**
- [ ] A window is a decision for the side that can use a stratagem there, offered only then; declining changes
  nothing; Rules Sequencing orders the sides. 15.01's cost and restrictions are enforced with rule-cited refusals.
- [ ] Command Re-roll after each of the eight roll kinds re-rolls one die (a charge roll in full) before the roll's
  result is used; `R15_02_*` tests cover each kind, the roller's side, the refusals and the skipped cases.
- [ ] A random D is rolled per attack that inflicts damage, each roll a step with its own window.
- [ ] Insane Bravery passes a battle-shock roll automatically, once per battle, never for a battle-shocked unit.
- [ ] The closure holds: `apply_action` plays exactly as M94, and paced play with every window declined equals it,
  for every replay and for random games. The M94 and M95 event dumps differ only by `DamageRolled`.
- [ ] Formats 1 and 2 load (legacy fixtures); format 3 round-trips; every new state field has a JSON round-trip test
  and is omitted when empty.
- [ ] All 29 replays regenerated as format 3 with byte-identical `expect` blocks; `m95_stratagems.json` shows each
  stratagem.
- [ ] Bots: the identity set identical to M94 in `state` and `trace`; the keys measured and their defaults set by the
  keep rule; decision p95 within budget.
- [ ] Client: the re-roll on the rolled card (Continue declines), the pop-up without dice pop-ups, Insane Bravery on
  the pre-roll card, "Offer stratagems", Undo past declines, critical dice in gold; logic tests and xvfb screenshots.
- [ ] Full `ctest` green, warnings as errors.

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
