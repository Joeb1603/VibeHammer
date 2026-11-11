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

## M85: One bot
**Effort:** `high` · **Plan mode:** yes (agreed 2026-11-11)

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

---

## M86: Objectives and a mission
**Effort:** `high` · **Plan mode:** yes (agreed 2026-11-11)

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
2. **Objectives** (14) and a simple mission: planned as M86 (engine, D-084) and M87 (the bot). This enables Objective
   Consolidation and Bolter Discipline.
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
       Normal moves its groups together, focuses fire, finishes wounded units and charges jointly; Hard's army value
       reads the plan; a unit without a group closes in unless outreached. Left open (M83's Results): the rosters'
       Tyranid gang-ups (1.7x, not 2x), lone engagements and units never within 12" (not halved: the Tyranid infantry
       is outreached and still holds back 40-50" away), the Chaos Space Marine seat against `normal-m77` (49% rosters,
       21.5% random armies against the 55% / 50% targets), Normal's rosters p95 (16 ms) and Hard's first-decision
       maximum (3.5 s) on this machine, Hard's narrower margin over the new Normal (57.5% / 53.3%), and the plan's
       stability (about 60% of an earlier mover's partners are still partners when they move, M80). M87 takes over
       the gang-up, lone-engagement and holding-back items (with objectives); M85 drops the Hard and `normal-m77` ones.
     - *Bots, Hard on the rosters:* Hard's Tyranids (9 units) move as Normal and win no more than Normal's own from
       that seat; Hard's own move search lost there because the army value is optimistic for the bigger army early
       (M69, D-078 part 11; M70's sweep found no clearly better setting, and its search lost even with the plans valued by
       the engine-played rollouts alone). Calibrating the army value and the move horizon for big armies is left.
     - *Bots, Hard's chain:* no state for a third unit, so an engaged target's 17.03 -1 and a MONSTER's close-quarters
       shot at another unit appear only in the half-turn in progress (M69, D-078 part 10).
     - *Bots:* they never turn an oval (M66, M68, M69); random damage is planned at the preview's maximum (M62, M68);
       the Desperate Escape guard checks one to three failed hazard rolls (M68, M69); the firer's own hazard loss is
       priced in the choice of profile but not taken off Normal's move score, and D-071's "no hazard roll after the
       last enemy model is destroyed" is ignored there (M68); Normal's two-half-turn horizon keeps far units holding
       (M68: measured right for these armies).
     - *Bots, speed:* Strike's Hard vs Hard decision p95 rose 5.7% at M69 at the same play (not profiled).
     - *Client:* the per-model editors choose one declaration per model, and the Split popup splits only the main
       weapon (M60); no setting re-rolls [TWIN-LINKED] automatically (M61); at a narrow window the action bar can
       still clip a long casualty hint or the "Damage left to allocate" box (scroll the bar; M70).
     - *Engine:* the oval-against-oval turn check is conservative (M65; the rosters have one oval).
     - *Fetch tool / datasheets:* a mixed ranged/melee weapon's profiles list under the first profile's type, and
       ability-name modifiers (Deadly Demise X, Scouts X) and hidden-ability conditions are not evaluated (M59).
   - **Open items carried from M71–M77** (custom battles; each where it was left):
     - *Bots, Hard on random armies:* its decision maximum is over 3 s (3.16 s at M73, two searches with 10–14 units on
       the table; no budget binds there, D-080 part 3), and its strength there is no better than Normal's from the Chaos
       Space Marine seat (M73: 47 of 200 against Normal, Normal 37; as the Tyranids 149, Normal 163). The rates of
       `kMaxLoadouts`, `kMaxAdvanceSteps` and `move_search_units` were measured at M73 and not re-instrumented since.
     - *Bots, deployment:* a search-based Hard deployment (Hard uses Normal's heuristic, M77); the bots never turn an
       oval when setting it up, and their candidates are whole-inch translations of core's proposal (M77, D-081).
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
