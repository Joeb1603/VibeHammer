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

V1 (M0–M13) and the post-V1 milestones M14–M89 are done. Their full text (prompts, acceptance checks, results
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

---

## Rules gaps, then the bot on terrain and objectives (M90–M91)

Agreed with the user on 2026-11-13, after M89. The user asked for more work on terrain and objectives, the rules
gaps and the bot, with M89's 55% bar for the bot.
- **M90** closes two rules gaps that objectives and terrain left open:
  - 12.08 Objective Consolidation. Missions have existed since M86, but `consolidation_mode` still says "V1 has none
    (D-022)". In m87's replay at step 550, a unit in the Centre after its fight gets no Consolidate decision.
  - 24.18 [IGNORES COVER]. It is the only reason the Flamers and the Heavy flamer are refused.
- **M91** takes the open bot items of D-090 and D-086.
- Each milestone is planned in detail in plan mode at the start of its own session.
- **Not in the batch:**
  - Stealth (24.33): the Genestealers list it only because the fetch tool ignores BSData's Chameleonic condition
    (D-070).
  - Terrain layouts or an editor for custom battles.
  - The scenario's balance.
  - Charge odds round walls (tried in M89, and worse).

## M90: Objective Consolidation and [IGNORES COVER]
**Effort:** `high` · **Plan mode:** yes

> Read 12.07–12.08, 14.01–14.02, 24.18, 13.08, D-009, D-022, D-071, D-084 and D-089 first. Ask if anything is
> ambiguous, and record the readings in a new D-091.
> - **12.08 Objective Consolidation:**
>   - *The mode:* `consolidation_mode` (`rules/fight.cpp`) returns Objective when the unit is unengaged, no enemy unit
>     is within 3", and one or more objective areas are within 3". Measure to the area's closest part (14.01; D-009).
>   - *The choice:* the player selects one of those areas. `StartConsolidation`, `ConsolidationStarted` and
>     `MoveInProgress` gain an optional objective field, omitted when empty, so old files stay byte-identical.
>     `state_hash` takes it.
>   - *While moving:* each moved model ends within range of the area, or closer to it. "If possible" is left to the
>     player (`TODO(if-possible)`).
>   - *After moving:* the unit is within range.
>   - `move_reach` and `move_end_problems` check both.
> - **24.18 [IGNORES COVER]:**
>   - `ability::IgnoresCover`, which does not split attack batches.
>   - `make_weapon_selections` clears `benefit_of_cover` for such a profile.
>   - The Flamer (Legionaries, Havocs) and the Heavy flamer (Chaos Terminators) become pickable in the builder and in
>     random armies. They are [TORRENT], so their odds don't change.
> - **Client:**
>   - The consolidation UI offers the areas.
>   - The Auto move (M34) aims at the area.
>   - The "too small to matter" skip (M31) measures the gap to the area.
>   - The action, event and log text name the area.
>   - The ability gets its help text.
> - **Bot:**
>   - It answers the new option legally, in Normal and in the defaults: an Auto move onto the area, taken when it adds
>     models within range. Weighing it with the objective terms comes in M91.
>   - `unit_ranged_volley` ignores `ShotContext::target_cover` for an [IGNORES COVER] profile.

**Acceptance**
- **Rule tests** (ScriptedDice, inch coordinates):
  - `R12_08_*`:
    - the modes' precedence;
    - an area at 2.9" and at 3.1";
    - only an area within 3" may be chosen;
    - a model that ends further from the area is refused;
    - the unit must end within range;
    - without a mission, play is as before.
  - D-022's test updated (a mission now changes the outcome).
  - `R24_18_*`: a fixture weapon without [TORRENT] keeps its BS against a target in cover.
  - JSON round-trip tests for every new field. An old action, event or state without them loads and saves byte for
    byte.
- **Client tests** `M90_*`: the offer, the Auto move, the skip, the text, and the builder offering the flamers.
- **AI tests** `M90_*`: objective consolidation answered legally, and a game with a flamer that plays clean.
- **Replays:**
  - Core replays stay byte-identical.
  - `m87_objectives_bot_game.json` is regenerated on purpose, since it now meets a Consolidate decision at step 550.
    Re-search its seed if its checks need it.
  - Every other replay stays byte-identical.
  - New: `tests/replays/m90_objective_consolidation.json`, scripted on the objective scenario.
- **Arena** (Release, 200 games each):
  - V1, both bot scenarios, Strike and the rosters are identical to the M89 binary.
  - The mission and terrain runs change on purpose. Record the objective consolidations a game and the win rates. The
    M90 binary becomes their reference.
  - Every run is clean: no rejected action, reverted move, fallback or unfinished game.
- **Docs:**
  - D-091;
  - notes on D-022, D-069 and D-071;
  - CLAUDE.md's list of implemented abilities;
  - `docs/dev/bot_arena.md`.
- **Build** and full `ctest` green.

---

## M91: The bot plays terrain and objectives in every phase
**Effort:** `xhigh` · **Plan mode:** yes

> Bot and arena only: no rule, engine, state or file-format change. Read 13.08–13.11, 12.05–12.08, 14.02, D-083,
> D-086, D-090 and D-091 first.
>
> Each feature sits behind a `MoveWeights` key (arena `--weights`), gated like `objectives_on` and `cover_on`, with
> nothing computed when it is off. A feature is kept only if it measures better; the tuning goes in a new D-092.
> - **The army plan with terrain:**
>   - `Planner::ranged` (`army_plan.cpp`) gives a target that stands in a terrain area the benefit of cover
>     (`ShotContext::target_cover`), both ways: our volleys and its reply.
>   - A shoot track that doesn't move uses whether its shooters see the target now.
>   - The cache keys carry the cover bit.
>   - One `TerrainSight` per decision is shared by the plan and the move score, threaded through `army_plan`'s five
>     callers.
> - **Holding fire to stay hidden (13.09):** a hidden unit passes when its best volley is worth less than the reply it
>   avoids by staying hidden. The move score's Hidden then counts for a plan that holds fire on purpose.
> - **Objectives in charges:**
>   - `charged_value` and `hold_value` (`charge.cpp`) add the objective terms at the charge's end positions. That
>     includes engaging an enemy unit that holds an area.
>   - `decide_charge*` take the weights.
> - **Objectives in pile-ins and consolidations:**
>   - The pile-in gain and `consolidation_score` (`normal/fight.cpp`) add the objective terms.
>   - The outlook covers the enemy's turn too: control at the end of its turn is what we score next.
> - **`ShootingAim::reply`** gets cover and visibility.

**Acceptance**
- **Head-to-head on the terrain scenario:** the M91 bot (defaults) against the M90 bot (the new keys at 0).
  - Run `--weights-a ...` and the reverse, each with `--first A` and `--first B`, 100 games each (seeds 1–100).
  - The M91 bot wins at least 55% of the 400 games, confirmed on seeds 101–200.
- **Recorded:**
  - the same head-to-head on the objective scenario and on random armies with `--terrain`;
  - the Chaos Space Marines' wins with M91 on both sides;
  - objective VP against kill VP;
  - units never within 12" and lone engagements;
  - Hidden refusals, holds of fire and objective consolidations a game.
- **Identity:**
  - V1, both bot scenarios, Strike and the rosters at the defaults are identical to the M90 binary.
  - The mission and terrain runs with the new keys at 0 are identical to the M90 binary.
- **Clean runs:** no rejected action, reverted move, fallback or unfinished game. The p95 decision time is at most
  twice M90's.
- **Tests** in `tests/ai/m91_test.cpp`:
  - `M91_ArmyPlan`, `M91_HoldFire`, `M91_Charge`, `M91_FightMoves`;
  - `M91_Regression` (the keys at 0 play as M90), `M91_Arena`, `M91_Replay`.

  Tests that pin mission or terrain play at the defaults are pinned to the keys at 0, as M87 and M89 did.
- **Replay:** `tests/replays/m91_bot_game.json` on the terrain scenario.
  - It contains a charge onto an area, an objective consolidation, and a unit holding fire while hidden.
  - It is checked with core only and with `TheSavedFileMatchesTheGame`.
  - Older replays stay byte-identical (the m87 and m89 generators are pinned to the keys at 0 if needed).
- **Docs:**
  - D-092 (the design, the tuning, the final numbers);
  - the D-086 and D-090 open lists;
  - PLAN.md Results;
  - `docs/dev/bot_arena.md` (the keys, the reference binary);
  - the README and the CLAUDE.md overview line;
  - a screenshot under `xvfb-run`.
- **Build** and full `ctest` green.

---

## Later (post-V1, rough order)
- **Dice pop-up polish:** highlight critical hits and wounds on the dice (e.g. a gold border). `DieFace::critical` already carries it, and the result line already counts them.
- **Engine-paced dice rolls (Option 2 of M20):** a PendingDecision before each roll point (hit, wound, save, charge, battle-shock, hazard, advance), so the roll is made when the player presses Roll instead of being revealed after the engine has made it. Needs the attack sequence split into resumable steps, new decisions in `legal_actions`/`validate_action`, and every replay regenerated. M20's pop-up would then answer those decisions instead of revealing pre-made rolls.
0. **Deployment step:** done in M74 (engine), M76 (client) and M77 (the bots' heuristic), in the custom-battles batch
   (M71–M77). What is left of it is under item 5, "Open items carried from M71–M77".
1. **Terrain** (13): 2D terrain is M88 (D-087–D-089), and the bot's use of it is M89 (D-090). Still to do: the z axis
   (13.06 Moving Vertically and upper surfaces, 13.11 gaps above 3", 22.05 Plunging Fire), Stealth (24.33, left out of
   M88 by the user's choice; the Genestealers list it only because the fetch tool ignores BSData's Chameleonic
   condition, D-070), and a terrain editor for custom battles. Left open from M89: charge odds round walls (D-090's
   open list). M91 takes the rest (the head-to-head target, the army plan's terrain-blind group rates, holding fire
   to stay hidden).
2. **Objectives** (14) and a simple mission: the engine is done in M86 (D-084), the bot in M87 (D-086). This enables
   Bolter Discipline. Left open from M86: nothing secures an objective yet (14.03 is state only). Left open from
   M87: the objective scenario's balance (the Chaos Space Marines win about a third: the Tyranids' OC takes the
   Centre; an asymmetric mission, other armies or terrain are the levers) and units holding back (about as at M83).
   M90 adds Objective Consolidation (12.08), and M91 has the bot's charges, pile-ins and consolidations weigh the
   areas.
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
