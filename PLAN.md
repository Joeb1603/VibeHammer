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

V1 (M0–M13) and the post-V1 milestones M14–M97 are done (M97 is below). Their full text (prompts, acceptance checks,
results
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
| M96 | Fire Overwatch with Snap Shooting, Heroic Intervention and Counteroffensive (open items) |

---

## M97: Explosives and Crushing Impact (D-099) ✅ (done; the manual mouse checks open)

**Effort:** xhigh (two rule areas on M95's and M96's windows). **Plan mode:** done. The design was agreed on 2026-11-26
in M96's session, where the user chose to design it. Implement it in a fresh session.
**Scope:** the last two core stratagems the engine can take without a new rule system. Both are used in their player's
own turn and end in mortal wounds (06.02): Explosives (15.05) and Crushing Impact (15.06). The other core stratagems
wait for what they need: Epic Challenge (15.03) for [PRECISION] and attached units (19), Rapid Ingress (15.07) for
strategic reserves (20), and Smokescreen (15.10) for a SMOKE unit. In the data:
- EXPLOSIVES: the Intercessors;
- GRENADES: the Chaos Lord and the Legionaries;
- MONSTER: the Hive Tyrant (T10), the Screamer-killer (T9) and the Tyrannofex (T12);
- VEHICLE: none.

**The user's rulings** (2026-11-26, plan mode):
- Explosives is offered at the Shoot step, as an answer beside the Shoot templates (no window).
- The EFFECT's model and enemy unit are picked in a step after the stratagem (`SelectModelAndEnemy`), once its CP are
  paid.
- Crushing Impact's "maximum of 6 mortal wounds per unit" caps each unit: the enemy unit and the charging unit.
- Crushing Impact inflicts the enemy's mortal wounds first. If that leaves the enemy with no models, the game ends at
  once (D-007) and the 1s are never inflicted.

**Rules:**
- PDF: 15.01, 15.05, 15.06, 06.01, 06.02, 09.07, 10.02, 11.04, 12 (its example of Fights First units), 01.07.
- App: 01.03.02 Rules Sequencing, and 02.05.01 Using Keywords (slashed keywords; singular and plural alike).
  - The app's headings swap the two numbers. The PDF's are used (D-098).
  - No FAQ entry covers either stratagem.
- Decisions: D-007, D-013, D-051, D-071, D-076, D-095, D-096, D-097, D-098, D-099.

**Design** (D-099 has the full text)
- *Explosives:*
  - Offered as `UseStratagem{Explosives, unit}` among the Shoot step's answers. The unit must be unengaged,
    EXPLOSIVES/GRENADES, eligible to shoot (10.02, 09.07) and must not have advanced this turn.
  - One of its models must have an unengaged enemy unit within 8" and visible.
  - Then `SelectModelAndEnemy`, then six D6 (`RollDice{Explosives}`): a mortal wound per 4+.
  - The unit can still shoot afterwards.
- *Crushing Impact:*
  - A `WindowTiming::AfterChargeMove` window opens just after a charge move. It is usable only in its player's own
    Charge phase, by a MONSTER/VEHICLE unit; never after a heroic charge.
  - Then `SelectModelAndEnemy`: an engaged enemy unit, and a model engaged with it.
  - Then T D6 (`RollDice{CrushingImpact}`): each 5+ a mortal wound on the enemy unit, then each 1 one on the unit, each
    at most 6.
- *The effect:*
  - `FlowState::effect` (`StratagemEffect`) runs the selection, the roll and the mortal wounds. Casualties use 06.02,
    with the controlling player's `OrderCasualties` (D-013, D-051).
  - No Command Re-roll follows either roll (15.02 lists neither).
- *Format 5:* it records the AfterChargeMove windows. Format 4 converts by declining them (8 replays have one). Every
  replay is regenerated, and a new `m97_stratagems.json` is added.
- *Client:* the Explosives buttons at the Shoot step, a selection bar, the window, the cards and the log.
  `GameSession::legal_offered()` hides the answers a side declines.
- *Bot:* keys `explosives` and `crushing_impact` (0 or 1), valued with a mortal-wound expectation; the defaults are set
  by D-092's keep rule.

**Steps** (D-099 sections 3–10 give the detail)
1. Done in plan mode: this section and D-099.
2. Baselines:
   - copy pre-M97 format-4 files with a MONSTER charge (a replay such as m60, and a session save) to
     `tests/data/legacy/`;
   - record the M96 event dumps of the 31 replays;
   - keep the M96 Release arena for the identity runs.
3. Plumbing:
   - the enums, the decision, the action, the events and `StratagemEffect`;
   - JSON (omitted when absent) and the hash;
   - every visitor and switch (D-099 section 10).
4. Explosives in the engine, with the `R15_05_*` tests.
5. Crushing Impact in the engine and the wipe-out order, with the `R15_06_*` tests.
6. Format 5 and its conversions. Regenerate the replays, check them against their format-4 files, and record the m97
   replay.
7. Client, with `M97_*` client tests and xvfb screenshots.
8. Bot:
   - the keys, the heuristics and the `M97_*` bot tests;
   - the identity set against the M96 binary;
   - the head-to-heads; the defaults by the keep rule.
9. Docs:
   - D-099's results, and the amendments it lists;
   - CLAUDE.md, README, the roadmap, `docs/dev/replays.md` and `docs/dev/bot_arena.md`;
   - this section marked done with its results, and M96 archived.
   Then the full `ctest`, then commit, push, the PR and the rebase merge.

**Acceptance**
- [x] **Offers:**
  - each stratagem only at its WHEN and for a TARGET with a legal pair;
  - 15.01's cost and records, and 01.07, enforced, with rule-cited refusals;
  - declining or not using one changes nothing, and the closure holds.
- [x] **Explosives:**
  - at the Shoot step, with one model and one unengaged, visible enemy unit within 8" of it;
  - six D6, with a mortal wound per 4+;
  - the unit can still shoot. `R15_05_*`.
- [x] **Crushing Impact:**
  - the window only after the unit's own charge move;
  - an engaged enemy unit and a model engaged with it, then T D6;
  - the 1s and the 5+s, each capped at 6;
  - the enemy's mortal wounds first, with D-007 first. `R15_06_*`.
- [x] **Format 5:**
  - it round-trips, and formats 1–4 load and convert;
  - the 31 replays are regenerated with only declines added, and the event dumps are byte-identical;
  - `m97_stratagems.json` shows both stratagems.
- [x] **Bots:**
  - the identity set at the defaults has every `state` equal to M96's;
  - the two keys are measured by D-092's keep rule, and the defaults are set by it;
  - p95 is within budget.
- [x] **Client:** the Explosives button, the selection bar, the window, the cards and the log; logic tests and xvfb
  screenshots.
- [x] **Full `ctest`** green, with warnings as errors.

**Results** (D-099 part 11 has them in full): the M96 and M97 event dumps of the 31 older replays are byte-identical with
no filter; the 31 replays regenerated as format 5 are each their format-4 file converted (11 declines added, in the 8
replays named); `m97_stratagems.json` shows both stratagems (Explosives: 3 mortal wounds on the Hive Tyrant, then the
Legionaries shoot; Crushing Impact: 2 on the Legionaries, then 1 on the Tyrant). The bots' identity set at the defaults
is the M96 binary's in every field but `steps` and `actions` (2,600 games; 1,170 declined windows added). Head-to-heads (400
games each, all clean): Explosives +6, Crushing Impact -10, both -4, so both keys stay off by default. p95 unchanged
(8.9 to 9.0 ms on the rosters, 13.8 to 13.4 ms on terrain, 9.0 to 8.9 ms on the objective scenario). The full
`ctest` passes: 1,678 of 1,678.

**Open items.**
- Manual checks by mouse on Windows: Explosives at the Shoot step and its selection, Crushing Impact's window after a
  monster's charge, both in hotseat and against the bot (keys on), and Undo after each.
- The bot's charge planning doesn't price Crushing Impact (it is judged only at the window), and Explosives' valuation
  doesn't weigh the CP against a later Command Re-roll. The thresholds were not tuned (one setting measured).
- The selection is made with buttons, not by clicking on the board.
- Explosives isn't offered in the Start or End of Shooting phase steps, which have no decision in the engine.

The alternative next step was the first line of the roadmap's Phase 1: the weapon abilities still refused when selected
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
