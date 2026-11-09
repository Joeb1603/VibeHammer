# Rules and design decisions

Decisions on ambiguous rules, app-only rules (not in `docs/rules/`), and scope. Cite the ID in code
comments where a decision is applied, e.g. `// D-001 (04.01): profile chosen per model`.

## D-001: Multiple Weapon Profiles are chosen per model (04.01, app-only rule)
**Decided:** 2026-11-02
"Multiple Weapon Profiles" is an app-only rule; its wording is not in `docs/rules/`. 04.01 selects
weapons "for each model in the attacking unit" and 04.02 selects a target "for each weapon selected".

- **Core (`wh40k_core`):** profile selection is per model. Two models in one unit may fire different
  profiles of the same weapon (e.g. one Bolt Rifle on Focused Fire, another on Saturation), and the
  engine validates each model's choice independently.
- **Client (`wh40k_client_2d`):** the shooting UI applies one profile and one target to the whole unit
  by default, expanding it into per-model selections in the `GameAction`. The player overrides
  individual models only by explicitly selecting a single model. This is UI convenience only; the
  action sent to the engine is always per model.

If the app wording is obtained later and contradicts this, update this entry and the tests.

## D-002: Random Characteristics deferred (app-only rule)
**Decided:** 2026-11-02
"Random Characteristics" is an app-only rule. No V1 loadout weapon has a random characteristic.
Loaders parse dice expressions (`D3`, `D6+1`, ...) into a `DiceExpr` value and store it, but the engine
does not roll random characteristics. Attempting to resolve an attack with an unrolled random
characteristic is a programmer error (an exception) until the rule wording is supplied and implemented.
- **Replaced by D-069 (M62)** for A and D, the only characteristics that can be random: a random A is rolled per
  model and weapon when its batch is resolved, and a random D after the model is selected.

## D-003: No deployment step in V1
**Decided:** 2026-11-02
V1 starts at battle round 1, with both units in the formation that the scenario file defines inside
their deployment zones. An interactive deployment step (alternating unit placement within zones, done
through `PendingDecision`) is post-V1; see "Later" in `PLAN.md`.

## D-004: `legal_actions` for continuous decisions (engine API)
**Decided:** 2026-11-02
Positions are continuous, so `legal_actions` cannot list every move.

- For discrete decisions (select unit, move type, target, allocation order, model to remove, pass ...),
  `legal_actions` returns every legal action, fully specified.
- For decisions with continuous parameters (propose a model move, pile-in, consolidate, charge move),
  it returns **action templates**: the action kind plus fixed fields (e.g. which model may be moved),
  with the path left open. Commit/reset actions are listed only when they are currently legal.
- Property test (M5): every fully specified action from `legal_actions` is accepted by
  `validate_action`. For templates, the test fills the open parameters with seeded random values and
  checks only that `validate_action` gives a deterministic, rule-cited result (accept or reject).
  It never requires that an arbitrary path be accepted.

## D-005: Dice rolled for a reverted move stand (03.01, 09.06, 09.07)
**Decided:** 2026-11-02
The 03 sidebar "If you cannot set up a unit" says a unit whose models are returned to their start
positions "has not been selected to make that move" and may be selected again. The rules don't say what
happens to dice already rolled for that move.

- **Advance roll (09.06):** once made, the result is kept for the rest of the phase. If the unit selects
  Advance again it uses the same roll; it may select a different move type (including Remain Stationary).
- **Desperate Escape hazard rolls (09.07, 06.03):** mortal wounds already inflicted stand. Destroyed
  models are not returned. If the unit selects Desperate Escape again, no new hazard rolls are made.
- Rationale: stops a player re-rolling by deliberately failing a move. Applies equally to an explicit
  Reset by the player and to an end-of-move validation failure.

## D-006: Identical attacks compare attack-time abilities only (04.03)
**Decided:** 2026-11-02
04.03 defines identical attacks as "the same BS/WS, S, AP and D characteristics, and ... affected by the
same applicable abilities and rules".

- Compare BS/WS (including Torrent's auto-hit), S, AP, D, and only the abilities and rules that still
  affect the attack from the hit roll onward: e.g. the Heavy +1 to hit **when it applies**, Twin-linked,
  Lethal Hits, Sustained Hits, Devastating Wounds, Anti-X, Lance, Melta, Precision, Hazardous.
- Ignore abilities that are fully used up when weapons, targets or attack dice are selected or gathered:
  Assault, Close-quarters/Pistol, Rapid Fire, Blast, Cleave, Extra Attacks, One Shot, Indirect Fire
  (except its hit-roll clause, which does count).
- Example: a Bolt pistol (12", A1, BS3+, S5, AP-1, D1, Close-quarters) and a Bolt Rifle on Saturation
  (BS3+, S5, AP-1, D1, Assault, Rapid Fire 1) fired at the same unit by different models **are**
  identical and are batched.

## D-007: V1 game end and result (07.03; mission rules not in V1)
**Decided:** 2026-11-02
V1 has no mission or objectives, so the end of the game and its result are house rules:

- If at any point a side has no models on the battlefield, the game ends immediately (after the current
  attack sequence or move resolves), and the other side wins.
- Otherwise the game ends after the End of Battle Round step of battle round 5 (scenario
  `battle_rounds`), and the result is a draw.
- The result is emitted as a `GameOver` event carrying the winner (or draw) and the reason.
- *Amended by D-076 (M67):* a scenario with `"result": "points_destroyed"` is decided after the last battle round by
  the points each side destroyed instead of being a draw; a wipe-out still wins at once.

## D-008: V1 starting formation (D-003)
**Decided:** 2026-11-02
The scenario defines a `formation` for each unit, which the loader turns into model positions:

- Models are placed in the order listed in the scenario, filling each rank left to right (low x to
  high x), front rank first.
- `ranks` ranks, each centred on `centre_x_in`, with `gap_in` edge-to-edge between neighbouring bases
  both along a rank and between ranks.
- `anchor: "zone_front"`: the front rank's bases touch the front line of the deployment zone (the line
  furthest from the owning side's battlefield edge); further ranks are behind it.
- V1 values: Intercessors 1 rank of 5; Termagants 2 ranks of 5; gap 0" (bases in contact along and
  between ranks, M14; it was 1" until 2026-11-05); centred at x = 12".
  The two front lines are exactly 12" apart, so a unit is within 12" of the enemy (charge-eligible,
  11.02) from turn 1. Both formations satisfy 03.03. (At 1" gaps a Termagant line of 10 would not: it
  is about 20" wide and breaks the 9" clause. In base contact it is 8.98" end to end, so it would.)
- The loader must check the result is in coherency (03.03) and wholly within the deployment zone, and
  fail with an exception (corrupt data) if not.
- Accepted consequence (2026-11-02, re-measured 2026-11-05 for gap 0): at these positions only the
  centre Intercessor is within 12" (half Bolt Rifle range) of the Termagants. The two beside it are
  0.018 mm further and the two at the ends 0.073 mm further, all beyond `EPS_MM` (0.01 mm), so on turn 1
  they don't get [RAPID FIRE] (24.30). This is intentional: it exercises per-model range checks. A
  larger `EPS_MM` would change it; see `R24_30_RapidFire.HalfRangeIsMeasuredToTheClosestTargetModel`.

## D-009: Within / Wholly Within (01.04 "See also", app-only rule)
**Decided:** 2026-11-02 (wording supplied by the user, AI-sourced; **not verified against the app**)
"Within/Wholly Within" is an app-only rule; its wording is not in `docs/rules/`. Wording as supplied:

> **Within:** A model is within a specified distance or area if any part of its base (or hull, for
> model types without a base) is within that distance or area. A unit is within if one or more of its
> models are within it.
>
> **Wholly Within:** A model is wholly within a specified distance or area if every part of its base
> (or hull) is within that distance or area. A unit is wholly within only if every model in that unit
> is wholly within it.

- Distances between models are base edge to base edge (01.04), so "model A within X of model B" is
  `base_distance(A, B) <= X + EPS_MM` (`geometry/distance.hpp`: `within`).
- A unit is within X of another unit if any pair of their models is (`geometry/closest.hpp`:
  `closest_distance`).
- Wholly within an area: the whole base circle lies inside it, with `EPS_MM` tolerance
  (`wholly_within`). Used by the D-008 deployment-zone check.

## D-010: Closest Model / Unit (01.04 "See also", app-only rule)
**Decided:** 2026-11-02 (wording supplied by the user, AI-sourced; **not verified against the app**)
"Closest Model/Unit" is an app-only rule. Wording as supplied:

> **Closest Model/Unit:** When a rule refers to the closest model/unit, this is measured relative to the
> model or unit using that specific rule.
>
> **Tie-breaker:** If two or more models or units are equally close, the controlling player of the
> model/unit using the rule selects which one is treated as the closest for the purposes of that rule.

- Geometry (`geometry/closest.hpp`) returns **every** candidate tied for closest (distances equal within
  `EPS_MM`), never picks one. Measurement is from the model/unit using the rule, edge to edge (01.04).
- When more than one candidate is tied, the engine must ask the controlling player of the model/unit
  using the rule to choose, through `PendingDecision` (M5 onwards). It never breaks ties itself.

## D-011: Modifying Dice Rolls and Re-rolls (05 "See also", app-only rules)
**Decided:** 2026-11-02 (wording supplied by the user, AI-sourced; **not verified against the app**, so
revisit if the official text differs)
"Modifying Dice Rolls" and "Re-rolls" are app-only rules. Wording as supplied:

> **Order:** Modifiers are applied after any re-rolls of that dice roll.
> **Maximums:** A dice roll can be modified above its maximum possible value (for example, a D6 roll
> can be modified above 6).
> **Minimums:** A dice roll can never be modified below 1. If, after all modifiers have been applied, a
> dice roll would be less than 1, count that result as a 1.
> **The Cap (Hit/Wound Rolls):** Hit rolls and wound rolls can never be modified by a net total of more
> than +1 or -1. All individual modifiers are cumulative, but after all cumulative modifiers are
> calculated, if the total modifier would be +2 or better, it is changed to +1; if it is -2 or worse,
> it is changed to -1.
> **Re-rolls:** If a rule allows you to re-roll a dice roll, you roll the dice again. Re-rolls must
> always be made before any modifiers are applied to the result. You cannot re-roll a dice more than
> once, and re-rolls do not stack. If multiple rules trigger a re-roll on the same die, it is only
> rolled a second time.

Applied as (`dice/modifiers.hpp`):
- Hit and wound rolls: sum all modifiers, clamp the net to -1..+1, add it to the unmodified roll.
- Save rolls: AP (and any other save modifier) is **not** capped. In V1 the cap applies to hit and wound
  rolls only.
- Every modified result is floored at 1 and has no maximum: a 5 with +1 is a 6 (passes a 6+ test) and a
  6 with +1 is a 7. Critical hits/wounds still need an **unmodified** 6, and an unmodified 1 still fails
  (05.01, 05.02, 05.04 tables).
- Re-rolls: not used in V1 (no V1 weapon has [TWIN-LINKED], no stratagems). When implemented: re-roll
  the raw die once, then apply modifiers. **Since M61 (D-072):** [TWIN-LINKED] wound rolls are re-rolled this way,
  at the attacker's choice.

## D-012: Target No Longer Eligible or Viable (04 "See also", app-only rule)
**Decided:** 2026-11-02 (wording supplied by the user, AI-sourced; **not verified against the app**)
"Target No Longer Eligible or Viable" is an app-only rule. The supplied text (it was labelled
"04.03.03", but the rulebook has no such number) says, in summary:

> If a unit that was an eligible target for a rule or attack when it was selected stops being an
> eligible target (e.g. it is moved out of range by a reactive rule, or destroyed by an earlier attack
> during the resolution sequence), any remaining attacks or effects allocated to it can no longer be
> resolved against it, and those attacks are lost. Remaining attacks are never re-allocated to a
> different enemy unit: targets are locked in at the Select Targets step.

Applied as:
- When resolving attacks (04.03), a batch whose target has been destroyed (or is otherwise no longer a
  valid target) is not resolved: its attack dice are lost, with an event recording that. No dice are
  rolled for it and the attacks are never moved to another target.
- Within a batch, 05.04 already loses excess attacks once every target model is destroyed.
- Implemented in M5 when attack resolution moves into the engine; until then
  `resolve_attack_batch` treats a destroyed target as a precondition violation.

## D-013: One casualty-order decision per batch (05.04, 06.02; extends D-004)
**Decided:** 2026-11-02 (M5 plan)
05.04 asks the opposing player to Select Model once per save roll that inflicts damage, which can be
many prompts per volley. All save rolls are made before Inflict Damage starts and each attack's damage
is fixed, so no new information appears while 05.04 resolves. A priority order over the target unit's
models is therefore exactly as expressive as answering each Select Model step separately.

- When an attack batch reaches 05.04, the opposing player answers once with
  `OrderCasualties{target, priority}`: every model in the target unit, exactly once. At each Select
  Model step the engine selects the first model in that order that is legal (05.04 still forces a
  wounded model when there is one).
- The decision is **skipped** when no Select Model step has more than one legal model (the engine
  checks by simulating `inflict_damage` on a copy of the unit). **Amended by D-051 (M35):** it is now
  skipped only when no save roll inflicts damage; a forced casualty is still asked for.
- The same approach applies to mortal wounds (06.02) when they are wired into the engine.
- D-004 extension: a permutation is too large to enumerate, so `legal_actions` returns
  `OrderCasualties` as a **template** with an empty `priority`; any permutation of the target unit's
  models fills it.
- Revisit if a rule adds dice during 05.04 (e.g. Feel No Pain, 24.12; not in V1).
- **Amended by D-069 (M62):** a random D is rolled during 05.04, so new information does appear. By the user's choice
  the decision stays one order per batch, and each damage roll lands on the model the order picks at that step.

## D-014: Event log and replay files (engine design)
**Decided:** 2026-11-02 (M5 plan)
- `apply_action` returns the events it caused. They are not stored in `GameState` (saves stay small);
  the `Match` class keeps the running event log, the initial state and the action list.
- A replay file (`tests/replays/*.json`, format 1) holds `initial` (either `{scenario, seed}` or a
  full `{state}`), the `(side, action)` steps, and an `expect` block (result, stage, battle round, and
  every model's wounds and position). Rebuilding the initial state from scenario + seed keeps replay
  files valid when later milestones add state fields; `expect` compares only what the replay is
  about.
- Replay regression test: every file must run with every step valid and match its `expect` block.

## D-015: Multiple Battle-shock Rolls (01.07 / 08.03 "See also", app-only rule)
**Decided:** 2026-11-02 (wording supplied by the user, AI-sourced; **not verified against the app**)

> If a rule requires a unit to make a battle-shock roll at a time when it is already under the effects
> of a previous battle-shock roll (or when multiple rules require a battle-shock roll to be made
> simultaneously for the same unit), the unit only makes one cumulative battle-shock roll for that
> instance. However, if any rule or ability imposes a modifier to a battle-shock roll, apply all
> applicable modifiers to that single roll. If the roll fails, the unit remains battle-shocked (or
> becomes battle-shocked if it wasn't already), and any ongoing penalties do not stack beyond the
> standard battle-shock effects.

Applied as:
- One battle-shock roll per instance, never several at once for the same unit; battle-shock effects
  don't stack (the unit is simply battle-shocked).
- V1 has no battle-shock modifiers. Desperate Escape (09.07) only requires a roll when the unit is
  **not** battle-shocked, so in V1 this rule never changes an outcome; M11 (08.03) is the other user.

## Note on app-only rule wording (2026-11-02)
D-009, D-010, D-011, D-012 and D-015 use wording the user obtained from an AI assistant, not copied from the
Warhammer 40,000 app. Treat them as working assumptions: if the official app text differs, update the
decision, the code that cites it, and its tests.

## D-016: The Shoot action (10.02, 04.01, 04.02; extends D-001 and D-004)
**Decided:** 2026-11-02 (M8). **Confirmed by the user** (2026-11-02).
- **One action per unit.** `Shoot{unit, type, shots}` answers the Shoot decision: it selects the unit
  (10.02 Select Unit), its shooting type (Select Shooting Type), and every model's weapons, profiles and
  targets (04.01, 04.02) in one go, and the attacks are resolved through the attack flow (04.03). The
  engine validates the whole declaration before any dice are rolled.
- **One profile and one target per weapon.** Each `ShotDeclaration` is one model firing one weapon with
  one profile at one target. A model may declare several weapons (04.01 "one or more ranged weapons"),
  but the same weapon only once. This reads D-001 (profiles chosen per model) as "one profile per
  weapon"; revisit if the app's Multiple Weapon Profiles wording says otherwise.
- **Template.** The declarations are combinatorial, so `legal_actions` lists a `Shoot` template with an
  empty `shots` list for each unit and shooting type it is eligible for and that has at least one valid
  shot (D-004). An empty declaration is never valid: selecting a unit to shoot with no weapons is the
  same as not selecting it. `shot_options()` (rules/shooting.hpp) lists every valid single declaration
  for clients.
- **Not tracked:** "After Shooting: not eligible to start an action" (10.04-10.06). Actions (16) are not
  in V1.
- **10.06 MONSTER/VEHICLE models** are implemented (eligibility, the -1 to hit, [BLAST] not targeting an
  engaged unit) though no V1 unit has the keywords; tests use a stand-in MONSTER unit.

## D-017: Line of sight geometry (06.01)
**Decided:** 2026-11-02 (M8)
06.01: a straight line 1 mm wide from any part of the observing model's base to any part of the
observed model's base. Models in either of those two units are ignored; every other model blocks.
- The line's centre runs between a point of each base. It is blocked if it passes within
  `r + 0.5 mm` of another model's centre (that base overlaps the 1 mm strip), with the usual `EPS_MM`
  tolerance: an overlap of up to 0.01 mm does not block. So a gap of exactly 1 mm between two bases lets
  a line through, and touching bases do not.
- Only the stretch of the line between the two bases is checked (any part of a base may be the end).
- Candidate lines (`geometry/visibility.cpp`): centre to centre; every pair of 33 sample points on the
  two bases (centre plus 32 on the edge); and every common tangent of two of the circles (the two bases
  and the blocking bases widened by 0.5 mm). Where a free line exists, one tangent to two of these
  circles usually exists too, so the tangent candidates find narrow gaps that sampling misses (tested
  at a 1.28 mm gap crossed at a slant). This is not a proof of exactness: it could miss a sightline that
  only exists in a configuration bounded by other constraints. V1 has only two units, so in V1 play no
  model can ever block line of sight; only tests with extra units exercise this.
- V1 has no terrain (13.07). "Fully visible" is not needed in V1 and is not implemented.

## D-018: Charge flow and a reverted charge move (11.02, 11.04, 03.01; extends D-005)
**Decided:** 2026-11-02 (M9). **Confirmed by the user** (2026-11-02).
- **Flow.** `DeclareCharge{unit}` (11.02 Declare Charge) makes the charge roll at once (Make Charge Roll).
  If no enemy unit is within 12" and within the roll (11.04 Before Moving), no charge move is possible:
  the charge is resolved and fails, and the models don't move (11.02 Attempt Charge). Otherwise the
  player answers `SelectChargeTargets` with `StartChargeMove{targets}` (one or more eligible units;
  `legal_actions` lists every non-empty set) or `SkipChargeMove` ("if you still want to"). The charge
  move then runs model by model like other moves (03.01), with the charge roll as the maximum distance.
- **Reverted charge move.** If the move fails its end conditions (03.01 Ending a Move, 11.04 After
  Moving), or the player resets it, the models return to their start positions and the unit "has not
  been selected to make that move" (03 sidebar). It has still declared a charge, so it goes back to
  selecting charge targets (it may select different ones) or skipping. The charge roll stands: it
  belongs to Declare Charge, not to the move, and is never re-rolled (as D-005 does for the advance
  roll). The charge is resolved only by a committed charge move or `SkipChargeMove`.
- **"End closer" (11.04 While Moving)** applies to each model that is moved (03.01: "each time you move
  a model ... all stated While Moving conditions must be met"); a model that is not moved is not
  checked. Closer means the edge-to-edge distance to the closest model of one or more charge targets
  goes down by more than `EPS_MM`. A path that ends back where it started is not closer.
- **If possible:** "within 1" if it can" and "engaged if it can" (11.04 While Moving) are left to the
  player in V1 (`TODO(if-possible)` in `engine/movement.cpp`).
- A successful charge sets the unit's turn flags `made_charge_move` and `fights_first` (11.04 After
  Moving, 24.13); both end with the turn.

## D-019: Fight phase placeholder until M10
**Decided:** 2026-11-02 (M9). **Superseded by M10 (D-020 to D-022):** the Fight phase is implemented.
Before M9 no unit could become engaged in play, and the Fight-phase steps threw if one was. A successful
charge now leaves units engaged, so until M10 the Pile In, Fight and Consolidate steps (12.02-12.08) pass
with no fights. Engaged units stay engaged into the next turn (they can then fall back, make
close-quarters shooting, and so on). This is scaffolding, not a rule: M10 replaces it. Replays that
reach a Fight phase with engaged units will need regenerating then; `m9_charge.json` stops at the end
of the charge for that reason.

## D-020: Fight step alternation and passing (12.04, Appendix: Eligible to Fight, but Unable to Fight)
**Decided:** 2026-11-02 (M10 plan; the two choices below were made by the user)
- **Returning to Resolve Fights First Combats** (12.04: "after resolving a fight in the Resolve Remaining
  Combats step, if one or more Fights First units are now eligible to fight, return to the Resolve Fights
  First Combats step"): the alternation continues. The opponent of the player who just fought selects
  next. The step's "starting with the player whose turn it is" applies only when the Fight step begins.
  This fits the Overrun Fights sidebar ("may even be able to fight next if they are Fights First units").
- **A pass can't stall the Fights First step.** If a player passes and the next player cannot select a
  Fights First unit while Fights First units are still eligible (the passer's), the sequence moves to
  Resolve Remaining Combats with that next player selecting. The passer's units stay eligible and can be
  selected later. Literally, the sequence would bounce between "the other player selects" and another
  pass forever, and "both players pass in succession" would never happen.
- **How the rest is applied** (`engine/fight.cpp`):
  - A player is asked to select only when they can (a Fights First unit in that step, any eligible unit
    in Remaining Combats). Otherwise 12.04's "If this is not possible" branches run automatically.
  - Fights First step: if no Fights First units are eligible, go to Remaining Combats with the same
    player selecting. Otherwise the other player selects (or, after a pass, as above).
  - Remaining Combats: if no units are eligible, the Fight step ends. If the other player just passed,
    the step ends (Appendix: a pass "when their opponent has no remaining units that are eligible to
    fight"). Otherwise the other player selects.
  - **Passing** is offered when the selecting player has eligible units and all of them are more than 5"
    from every enemy unit. Two passes in succession end the step. So does a pass when the opponent has
    no eligible units.
  - After each fight the opponent of the player who fought selects next.
- Fights forced by an Engaging Consolidation (12.08) happen in the Consolidate step and don't touch the
  sequence.
- Supersedes D-019.

## D-021: Melee declarations (04.01, 04.02 While Fighting)
**Decided:** 2026-11-02 (M10). **Confirmed by the user** (2026-11-02).
- **Who must declare.** Every model that has a melee weapon and is engaged with at least one enemy unit
  must select exactly one melee weapon and one profile (D-001). The rules: 04.01 While Fighting says
  "you must select one melee weapon", and the 12 sidebar says "you have to fight with all units that
  can". The 04 sidebar's "choose not to select a target" covers only ranged weapons.
- **Targets.** A model's targets must be enemy units engaged with that model (04.02).
- **Splitting.** More than one target means at most A targets, at least one attack each, and the attacks
  summing to A (04.02 Splitting Melee Attacks). A single target takes all A attacks.
- **No possible attacks.** A model with no possible target declares nothing. If no model of the selected
  unit can attack, its fight resolves with no attacks and the player isn't asked.
- **Template.** The `Fight` action carries every declaration. An empty list is the template that
  `legal_actions` returns (as D-016 does for shooting); `melee_options()` lists the single-target choices.
- **Amended by D-071 (M60):** a model with [EXTRA ATTACKS] weapons selects all of them as well as one other melee
  weapon if it has one it can use (24.11).

## D-022: Pile-in and consolidation movement details (12.03, 12.08)
**Decided:** 2026-11-02 (M10). **Confirmed by the user** (2026-11-02).
- **Closest target.** "Closer to the closest pile-in target / selected enemy unit" is measured from the
  moved model's start position, edge to edge. If units tie for closest, any of them counts (D-010: the
  controlling player chooses). The check applies only to models that are moved (03.01, as D-018 for
  charges).
- **Base contact.** A model is in base contact with an enemy model if it is within `EPS_MM` of it at the
  start of the move. Such a model cannot be moved in a pile-in move or an Ongoing Consolidation.
  (D-048: the client's step skipping treats 0.05" as base contact; the rules don't.) **Open question
  (D-048):** "closer to the closest unit" lets a model slide round to a different enemy model; review later.
- **If possible.** "And engaged with it if possible" is left to the player in V1 (`TODO(if-possible)`).
- **Reverted moves.** A pile-in or consolidation move that fails its end conditions (03.01), or is
  reset, returns the models and goes back to the same decision: the Pile In step, the Consolidate step,
  or the overrun pile-in. The unit may try again (03 sidebar). A unit is marked as having piled in or
  consolidated only when the move is committed.
- **Turn order within a step.** In the Pile In and Consolidate steps the player whose turn it is moves
  first. Their part is over once they end it, or once the opponent acts.
- **Objectives.** V1 has no objectives, so Objective Consolidation never applies. A unit that is
  unengaged with no enemy unit within 3" cannot consolidate (09 sidebar "Selecting modes").

## D-023: Command points, battle-shock order and Regaining Coherency (08.02, 08.03, 03.03)
**Decided:** 2026-11-02 (M11)
- **Command points.** Each side's CP is tracked in `GameState::command_points`. Both sides start at 0:
  starting CP comes from the mission, and V1 has none. Nothing spends CP until stratagems are added
  (post-V1).
- **Battle-shock step (08.03).** The active player's units are processed in unit order, one roll each
  (D-015). The rolls are automatic, so no decision is needed. A unit already battle-shocked at the start
  of the step that passes its roll is no longer battle-shocked (`BattleShockEnded` event). A unit at or
  below half-strength that fails becomes battle-shocked. One that passes stays as it was.
- **Effects (01.07).** While battle-shocked, a unit's models have OC '-' (`objective_control()` returns
  nullopt), and the unit cannot select Ordered Retreat (09.07, M7). Stratagems and actions aren't in V1.
- **Regaining Coherency (03.03).** In the End of Turn step, each unit on the battlefield that is not in
  coherency is fixed by its controlling player, one model at a time, through the `RestoreCoherency`
  decision. Any model of the unit may be removed. Units are taken the turn player's first, then the
  opponent's, each in unit order; the order can't change the outcome.
- **Removed models.** They are destroyed, but they trigger no rules that apply on destruction. The
  engine emits `ModelRemovedForCoherency`, not `ModelDestroyed`. A one-model unit is always coherent,
  so removal never destroys a unit and can't trigger D-007.
- **Stage entry and dice.** Entering a stage can now roll dice: `enter_stage`, `next_stage` and
  `on_enter_stage` take the `DiceSource`.

## D-024: Client architecture (M12)
**Decided:** 2026-11-02 (M12 plan; both choices made by the user)
- **Core queries in the client.** The client may call core's read-only queries for display and editor
  choices: `shot_options`, `melee_options`, `max_move_distance_mm`, `move_start_position`,
  `move_end_problems`, the geometry constants (coherency, engagement) and the game data. Every action
  is still checked with `validate_action` before `apply_action`.
- **Only `GameSession` changes the game.** `GameSession::submit` (`src/client_2d/logic/session.cpp`)
  validates first, and on a failure nothing changes. `Timeline` (replays) is the other caller of
  `apply_action`. The client has no rules logic of its own.
- **Client tests.** The client's non-graphics code is the library `wh40k_client_logic`
  (`src/client_2d/logic/`). It links `wh40k_core` only, and on every build the same include guard as
  core (`cmake/CheckCoreIncludes.cmake`) forbids raylib, ImGui and OS headers in it. A separate
  GoogleTest executable, `wh40k_client_tests`, tests it. `wh40k_tests` still links only `wh40k_core`.
- **Structural check.** A test (`M12_NoRulesInTheClient`) checks every client source:
  - `apply_action(` appears only in `session.cpp` and `timeline.cpp`.
  - Only the logic library includes core rule headers, limited to the query headers
    (`core/rules/shooting.hpp`, `core/rules/fight.hpp`) and the plain type headers.
- **UI conventions.**
  - **Selection.** A click selects a unit; a click on a model of the selected unit selects that model.
  - **Moves.** A left-drag on a model of the unit being moved adds a straight line, and releasing
    proposes the whole path. The diamond handle at the unit's centre moves every model by the same
    offset. A rejected proposal shows its reasons and changes nothing.
  - **Weapons.** The shooting and melee editors set one weapon profile and target for the whole unit,
    and each model's row can override it (D-001). The client expands this into a per-model
    `GameAction`.
  - **Casualties.** The casualty editor starts in unit order (D-013). **Changed by D-058 (M47):** it starts in
    the default order, furthest from the action first, keeping coherency.
  - **Decision panel.** It is generated from `legal_actions`: one button per fully specified action,
    and one editor per template.

## D-025: Save files, undo and the seed (M13)
**Decided:** 2026-11-02 (M13)
- **Save format.** A save is JSON:
  `{"format": 1, "kind": "vibehammer-save", "initial": GameState, "steps": [(side, action)], "state": GameState}`.
  - It stores the initial state and the accepted steps, so a loaded game still has its event log
    (events are not stored, D-014), undo and replay export.
  - Loading rebuilds the game by validating and applying every step from "initial", then requires the
    result to equal "state". A mismatch, or an invalid step, is a corrupt file (`DataError`).
  - A bare `GameState` JSON also loads, as a game with no history.
  - The client saves to `saves/game.json` by default; `/saves/` is gitignored.
  - M39 (D-054) adds an optional `"players"` block (which sides the bot played, and its level). Old saves
    have none and load as hotseat.
- **Replay export.** `GameSession::to_replay` writes the initial-state form of a replay (D-014), with
  the current state as its `expect` block. The client exports to `replays/exported_replay.json` by
  default (gitignored); the file can be copied into `tests/replays/` to make it a regression test.
- **Undo** (hotseat). Undo rebuilds the game from the initial state with every step but the last.
  - The dice state is part of `GameState`, so the rebuilt game is exactly the earlier state, dice
    position included: undoing an action and repeating it gives the same rolls.
  - In V1 both players share the screen, so undo has no permission model.
- **Seed.** A new game takes the seed from the panel (or `--seed`). It defaults to the scenario's own
  seed, and the dice position starts at 0. The panel shows the current game's seed.

## D-026: Unimplemented attack-time abilities are rejected when selected (24.10, 24.38)
**Decided:** 2026-11-05 (code review)
- **What is rejected.** V1 does not implement [TWIN-LINKED] (24.38; it needs Re-rolls, D-011) or
  [DEVASTATING WOUNDS] (24.10).
  - Before this decision, validation accepted weapons with them. The engine then threw when the dice
    were rolled, which left the game with an attack half-started and no pending decision.
  - Such weapon profiles are now rejected when they are selected, with a rule-cited reason, as D-002
    does for random characteristics.
  - `shot_options` and `melee_options` don't offer them, and `legal_actions` lists no `Shoot` template
    that needs one.
- **Melee.** A model whose only melee weapons are unsupported (D-002 or D-026) counts as unable to make
  melee attacks, so D-021 doesn't require it to declare one.
- **Defence in depth.** `apply_action` and `start_game` keep a backup of the state and restore it if
  the engine throws (a programmer error), so the caller's state is never left half-applied. They still
  change the state in place: callers' references into it behave as before. A copy-and-commit version
  was tried and dropped, because replacing the state's vectors left callers' references dangling.
  `GameSession::submit` reports such an exception as a failure instead of letting it crash the client.
- When either ability is implemented, remove it from `unimplemented_ability_problem`
  (`rules/attacks.cpp`) and update this entry.
- **Extended by D-071 (M60):** every ability the engine does not recognise (`ability::Unknown`) is refused the same
  way. [HAZARDOUS], [SUSTAINED HITS X] and [EXTRA ATTACKS] are implemented.
- **Amended by D-072 (M61):** [TWIN-LINKED] is implemented (the attacker chooses which wound rolls to re-roll) and
  no longer refused. [DEVASTATING WOUNDS] (24.10) and the D-071 unknown abilities still are.

## D-027: A proposed model move may not end on another model (03.01, M14)
**Decided:** 2026-11-05 (agreed with the user)
- 03.01 lets a model move through friendly models, and checks "no models in that unit are on another
  model" only under Ending a Move, once the whole unit has moved. Before this decision the engine did
  the same: a model could be dropped on a squad-mate, and the whole move was undone at the commit.
- Now each `ProposeModelMove` is also rejected if its path ends on any other model at its current
  position: a friendly model, a model of the moving unit (already-moved models at their proposed
  positions) or an enemy model. The reason is `"03.01: model X cannot end its move on model Y"`. Moving
  through friendly models and ending in base contact are still allowed.
- This is stricter than 03.01. Accepted consequence: two models can't swap places in one step. The
  player moves one aside first; proposing a model again replaces its whole path (measured from its
  start), so the swap still works in three proposals (`D027_ProposalOverlap` test).
- The commit still checks 03.01 Ending a Move for the whole unit (`move_end_problems`).
- The client's "Move unit" handle proposes the models furthest along the move first, so translating a
  unit whose bases touch never ends a model on one that has yet to move.

## D-028: Client layout, scaling and overlays (M15)
**Decided:** 2026-11-05 (agreed with the user). Display only; no rules logic in the client (D-024).
- **Scale.** The UI scale is the monitor's DPI scale (`GetWindowScaleDPI`) times a user factor from the
  "UI scale" slider (75-250%), clamped to 0.75-3. It scales ImGui's fonts and sizes and every raylib
  pixel size (sidebar width, phase bar, text, line widths, the unit handle). The window opens maximized.
  On Windows the UI font is Segoe UI when `C:/Windows/Fonts/segoeui.ttf` exists, else ImGui's default.
- **Layout.** Fixed panels that cannot be moved or resized: a phase bar across the top of the board and
  a sidebar on the right with a toolbar (New game, seed, Undo), Decision (or the replay controls), Game
  (units, CP, overlays and legend, UI scale), Log, and Files (closed by default: save, load, export and
  replays).
- **Phase bar** (`phase_track`, client logic): battle round, whose turn it is, the phase track Command,
  Movement, Shooting, Charge, Fight with the current phase highlighted, the current step with its rule,
  and the side being waited for when it is not the turn side (reactive decisions, 01.03).
- **Rings** (`plan_overlays`, client logic), each only when relevant and each switchable off:
  - movement range, around each model's start position at the move's maximum distance
    (`max_move_distance_mm`) out from the edge of its base, while a `MoveModels` decision is pending for
    any move type except a charge (which has the charge distance ring). It stays at the start while the
    model is dragged: 03.01 measures from the same point on the base, and a new proposal replaces the
    whole path;
  - engagement range (2", 03.04), around the enemy models of the unit being moved, while a
    `MoveModels` decision is pending (any move type);
  - coherency (2", 03.03), around the unit being moved, during `MoveModels`. The 9" clause gets no
    ring: one around every model was clutter;
  - weapon ranges, around the selected model of the deciding side, during the `Shoot` decision;
  - charge distance, the charge roll out from each model's base, while that unit's charge is being
    made (selecting targets and the charge move). It is centred on the model's start position, so it
    stays put while a model is dragged, and it goes once the charge is resolved.
- **Wounds.** A model at full wounds shows nothing; a damaged model shows a pip per wound, filled for
  wounds remaining. The tooltip gives the numbers.
- **Clipping.** Rings are drawn only inside the battlefield.
- **Merged outlines.** Overlapping rings of one kind are drawn as a single border, the outer edge of the
  union of their circles (`outline_arcs`, client logic). Different kinds are never merged.
- **Dragging.** While a model or the whole unit is dragged, its rings are drawn at the preview
  positions (`ring_circles` with overrides), and each dragged model shows its path from its start
  position to the preview.
- **Explanations.** Hovering the board inside a ring (not over a model) shows what each ring there
  means; the sidebar legend uses the same names (`ring_name`) with a short line each (`ring_summary`) and
  shows the full `ring_meaning` on hover (D-061, M50). Engagement range stays
  around the enemy only: it is the zone a moving model must not end in.

## D-029: Click-on-the-board interaction (M16)
**Decided:** 2026-11-05 (agreed with the user). Client only; it amends D-024's "one button per legal action":
the board offers exactly the legal actions, grouped by the unit they are for (`BoardInteraction`, client
logic).
- **Picking a unit.** For Move Units, Shoot, Charge, Pile In, Consolidate, Select Unit to Fight and the
  forced fights of 12.08, the deciding side clicks one of its units. If that unit has one legal action it
  is submitted; if it has several (Normal / Advance / Fall back, or Normal / Overrun fight) a menu at the
  cursor offers them. Remain stationary is not in the menu.
- **End phase.** A green button ends what is left of the phase: "End Movement phase (the rest remain
  stationary)" submits Remain stationary for every unit not yet selected to move; "End Shooting phase",
  "End Charge phase", "End pile-in moves", "End consolidation moves" and "Pass" submit the step's end
  action. When no unit is left to click, the hint says so ("No units left to shoot with.") and this button
  is all the panel offers.
- **Shooting.** Clicking a shooter selects it; its eligible targets become clickable. Clicking a target
  offers each weapon choice (weapon profile) that can reach it, with the number of models that would fire;
  picking one fires the unit-wide choice (D-001), keeping any per-model overrides made in the sidebar.
  Clicking the shooter again, Esc or a click on empty board drops the selection. While a target is being
  picked (shooting, charge targets, pile-in and consolidation targets) the panel's headline says so ("Pick
  a target for the Intercessor Squad") and a line runs from the acting unit to the cursor.
- **Targets.** For charge targets, pile-ins and consolidations, clicking enemy units toggles them; when the
  chosen set equals one legal option's targets and no bigger option contains it, it is submitted at once,
  otherwise a confirm button appears in the sidebar. An engaged unit, whose targets are fixed, starts its
  pile-in or consolidation as soon as it is clicked.
- **Grey.** Only while the board is waiting for a click (a unit to pick, or a target), every unit that
  can't be clicked is drawn dark grey (80% towards {55, 55, 60}). When no unit is left to act (only the
  end-phase button), every unit is grey. Nothing is grey while models are being dragged or in the
  sidebar's decisions. The unit picked first
  has a yellow outline and chosen targets a red one.
- **Sidebar.** Unit buttons are not listed while the board handles the decision; the sidebar shows the
  decision, a hint, the step-level buttons (End shooting, End charges, Pass, Commit / Reset move, Make no
  charge move, ...), a confirm button for a chosen target set, and the selected shooter's per-model
  editor. Other decisions (melee declarations, allocation order, casualties, attack batches, coherency)
  stay in the sidebar as before, with nothing grey.
- Esc cancels a board selection; it no longer closes the window.
- **Hover.** A clickable unit under the cursor gets a bold white outline round each of its models.
- **Menus.** While a click menu is open (e.g. the weapon menu on a target), the target line stays on the
  clicked point and the "Pick a target" label is hidden. Closing the menu by clicking off it keeps the
  selection (ImGui takes that click), and the line follows the cursor again.
- **Ring tooltips** (D-028) appear only after the cursor has rested for 0.6 s, and never while picking a
  target or with a menu open, so they don't cover the target prompt.

## D-030: Action bar under the phase bar (M17)
**Decided:** 2026-11-05 (agreed with the user). Client layout only; amends D-028's sidebar and D-029's "Sidebar".
- **Action bar.** The decision moves from the sidebar into a bar across the board's width, directly under the
  phase bar, three rows high (scaled with the UI). The left column has the headline (larger, in yellow), the
  hint and the decision's editor or move status. The right column has the step buttons (Commit / Reset move,
  Confirm, Make no charge move, ...) and, under them, the End-phase button at full column width and taller
  than the rest. A decision with one button and no editor shows that button the same way (not green).
- **Whose input.** While a decision is pending in play, the bar has a 3px outline in the colour of the side
  that must act (`PendingDecision::side`, 01.03: blue for A, red for B), its background is tinted 22% towards
  that colour, and the headline starts with a tag in that colour ("Side A to act: Space Marines").
- **Fixed height.** Whatever could be long goes into a popup or a tooltip, so the board never refits while a
  decision is made: each model's shooting choice (D-001) is behind "Per-model weapons..." (the model selected on
  the board also gets its own combo inline), the melee per-model choices and attack splits behind
  "Per-model / split attacks...", the casualty order (D-013) behind "Edit order..." with a one-line summary,
  and the full list of move problems in the move-status line's tooltip.
- **Rejections** show in a red strip over the top of the board, under the bar, until dismissed.
- **Replay mode** puts the replay controls (step buttons, frame slider, step label, source) in the bar.
- **Sidebar** keeps the toolbar, Game, Log (now taller) and Files.
- **Amended (M70):** the bar is **four rows high** (three and a half before), still fixed. Measured at UI scale 1 (15 px
  font, 25 px rows with spacing): the casualty decision (D-051, D-069) needs the 1.25x headline row (29 px), a three-line
  hint at the default 1400 x 860 window (49 px) and the "Damage left to allocate" box (21 px), 99 px, against the 82.5 px
  the bar held, so the box was only reachable by scrolling. Four rows hold 100 px. The headline drops describe()'s
  leading "Side A: " (the tag in the side's colour already says it; the decision's words and rule number stay), and
  when tag and text are still wider than the column at 1.25x they are drawn at the body size, wrapping rather than
  running under the buttons. At a narrow window (about 1024 px wide, the sidebar keeping its width) a long hint can
  still push the box below the bar's bottom; the bar scrolls, as before. Layout only: no rules or client-logic change.

## D-031: Custom shots after picking the target (M18)
**Decided:** 2026-11-05 (agreed with the user). Client only; amends D-029 "Shooting" and D-030.
- Weapons are only chosen once a target has been clicked. With a shooter picked, the bar shows the prompt,
  the hint and End Shooting phase, and no weapon controls.
- The target menu lists each weapon choice that can reach the target (the whole unit fires it at once), then
  a separator and **"Custom..."**.
- "Custom..." opens the custom-shot editor in the action bar: "Custom shot: <shooter> at <target>", one combo
  per model in a grid across the bar, each offering "Doesn't fire" and every weapon profile and target that
  model can use (so fire can be split, D-001). Every model starts on the first weapon choice at the clicked
  target. On the right, Back and a large green Shoot button; the End-phase button is hidden.
- While editing nothing is grey, there is no target line, and a click on empty board doesn't drop the edit.
  Esc or Back returns to picking a target for the same shooter, with the edit discarded. A rejected Shoot
  keeps the editor open with the reasons shown.
- For units with many models the bar can grow to 45% of the window height, overlaying the top of the board.
  The board doesn't refit.

## D-032: Fight weapons and casualty order follow the same flow (M19)
**Decided:** 2026-11-05 (agreed with the user). Client only; extends D-031 and amends D-029/D-030.
- **Fight weapons** (`DeclareMeleeAttacks`, 04.01/04.02, D-021) are picked on the board. The fighting unit
  is fixed by the decision. The bar says "Pick a target for the <unit>'s attacks", a target line follows the
  cursor, and only engaged enemy units can be clicked (the rest are grey).
- Clicking a target offers each melee weapon choice at it, with the number of models that fight it there.
  Models that can't reach that target still fight with their first option (every model that can fight
  must), and the label counts them: "(+n at another target)". Then a separator and "Custom...". A weapon
  submits the Fight at once.
- "Custom..." opens "Custom fight: <unit> at <target>": one combo per model that can fight, offering each of
  its melee weapons and targets (no "doesn't fight"). A **Split...** button opens the per-target attack
  counts (04.02) when the model's weapon reaches more than one engaged unit. Back and a large green Fight
  sit on the right. Esc, Back and empty-board clicks behave as in D-031.
- **Casualty order** (`OrderCasualties`, D-013) stays a sidebar-mode decision. **Replaced by D-051 (M35):**
  casualties are picked on the board; the Custom order grid is gone, "Use default order" stays. The bar shows the default
  order (unit order: "As the <unit> lose models, they are taken off in this order: models 4, 5, ...") and a
  large "Use this order" button, plus **Custom order...**. That opens a grid of "n. model m" with
  earlier/later arrows, Back (restores the default order) and a large "Use custom order".
- Attack batch, allocation order and coherency removal are plain button choices and stay as they were.

## D-033: Dice roll pop-ups, presentation only (M20)
**Decided:** 2026-11-05 (agreed with the user). Client only; no engine, replay or determinism change.
- **Option 1, chosen:** the engine makes every roll when the action is applied, as before. The client turns
  the roll events of each applied action into dice cards (`dice_cards`, client logic) and shows them one at a
  time. "Roll" reveals results that already exist; for a hotseat game this can't be told apart from rolling
  then. **Option 2** (the engine pausing before each roll) is listed under Later in `PLAN.md`.
- **Cards:** hit (05.01), wound (05.02) and save (05.03) rolls, advance (09.06), charge (11.02), hazard (06.03)
  and battle-shock (01.07) rolls, in event order. Each card shows the side rolling (attacker for hit and wound,
  defender for saves, the unit's side otherwise), what the roll is for (unit, target and weapon profile),
  what it needs, the dice and the result.
- **Pass/fail:** hit and wound dice use the outcomes in the events. Save dice use core's read-only 05.04
  `check_save_roll` against the first allocation group in order with the batch's AP. Each V1 unit has one
  group; with several groups the colours follow the first group. Charge and advance dice are neutral.
  Hazard dice fail on 1-2, and battle-shock dice share the roll's pass/fail.
- **Pop-up:** centred over the board and outlined in the roller's colour. The dice are face-down until the
  big green Roll (or Enter/Space), then tumble for about 0.45 s (faces cycled by time, display only). After
  that, passes are green and fails grey, with the result and Continue (Done on the last card). The pop-up
  has nothing else: no skip, no setting. "Show dice pop-ups" in the sidebar's Game section turns them off.
  **Amended by D-053** (no Roll on a card with nothing to roll) **and D-060 (M49):** a bot's card that rolls by
  itself shows neither Roll nor Continue / Done; every card that waits for a click (a human's always) does.
  Criticals are not highlighted yet (PLAN.md Later).
- **The screen stays at the roll:** while cards show, the board, rings, phase bar and log show the game as it
  was just before the action that made the rolls (no casualties removed, no results in the log yet). The
  action bar shows who is rolling and what for ("Side A rolls: Space Marines", "Hit rolls (05.01)"), outlined
  in the roller's colour, so it always matches the pop-up. When the last card closes, everything moves on to
  the current state and the next decision. The board takes no clicks or drags meanwhile (zoom and pan still
  work), and offers nothing from the next decision: no target line or "Pick a target" label, no grey, no
  selection outline, no move handle, no menu, and Esc does nothing. The board's click state is rebuilt for the
  real decision when the last card closes (e.g. after a charge roll, target picking only starts after Done).
- No cards for replays, loads, undo or new games (the log size at that point is taken as already seen), or
  for `--steps` runs unless `--dice 1` (keep) or `--dice 2` (keep and reveal the first card).

## D-034: Datasheet windows (M21)
**Decided:** 2026-11-05 (agreed with the user). Client only; display, no engine change.
- **Opening:** hovering a model shows its stat line and "Right-click: datasheet". A right-click on a model
  opens that unit's datasheet in a floating, closable window; several can be open at once. Right-drag still
  pans the board: the press only counts as a click if the cursor moves less than the drag threshold (the
  same one left-clicks use) before release. It works while a dice pop-up is open (it changes nothing).
- **Data:** the live `core::Unit` in the shown `GameState`, not the JSON datasheet. The sheet shows the
  models on the table (alive / starting strength), one stat row per distinct model type and characteristics,
  and the weapons the models actually carry (each profile once, with how many models carry it), not every
  wargear option.
- **Text:** unit ability text is shown with U+00A0 as a space, the bullet glyphs (U+25AA, U+25A0) as "-",
  and the BSData markup `**` and `^^` removed; the default font has no glyphs for them. Abilities are marked
  "not applied in V1" (CLAUDE.md: unit-specific abilities are out of scope).

## D-035: Free measuring ruler (M22)
**Decided:** 2026-11-05 (agreed with the user). Client only; display, no engine change.
- **Start:** a left-press on nothing clickable (no model under the cursor, not the "Move unit" handle, not
  an ImGui window). It works in play and replay, while picking a target, and while a dice pop-up is open
  (it changes nothing).
- **Measure:** a straight line from the world point of the press to the cursor, labelled with its length in
  inches (two decimals). Point to point, not base edge to base edge: it is a free tape measure; base-to-base
  ranges are already shown by the rings (D-028).
- **Release:** the ruler disappears. If the cursor moved less than the drag threshold, the press was a click
  on empty board and does what it did before (drops the selection, cancels a board pick unless a custom
  editor is open); a measuring drag leaves the selection alone. A release over a window ends the ruler too.

## D-036: Skip steps with nothing to do (M23)
**Decided:** 2026-11-05 (agreed with the user). Client only; no engine, rules or replay change.
- **Setting:** a sidebar checkbox "Skip steps with nothing to do", on by default (it was off in M23; the
  user asked for it on in M24).
- **When:** the deciding side's `legal_actions` is exactly one action and it is a skip: `EndShootStep`,
  `EndChargeStep`, `EndPileIns`, `EndConsolidations`, `PassFight` or `SkipPileIn`. The engine already
  offers a unit only when it has a valid option (e.g. a `Shoot` template needs at least one valid shot), so
  this means nothing else can be done. Single forced choices that aren't skips (one allocation order, one
  forced fight, one model to remove) are still the player's to click.
- **How:** after each accepted player action (and at a new game, and when the box is ticked) the client
  submits the lone skip through `GameSession::submit`, repeating while the next decision is skip-only. They
  are ordinary steps in the log, saves and exported replays. Not run after a load or an undo. The status line
  names what was skipped.
- **Undo:** the client remembers which history steps were skipped automatically; one Undo removes those that
  follow the player's last action and then that action, so it does not land on a skip-only decision.
  (Since M26 this is part of the Undo groups, D-039.)
- **Extended by D-047 (M31):** pile-ins and consolidations too small to matter are skipped too.

## D-037: Explaining weapon abilities and keywords (M24)
**Decided:** 2026-11-05 (agreed with the user). Display only; no state, event or replay change.
- **Help text** (`rules_help`): every weapon ability, keyword and shooting type has a short plain-English
  summary paraphrased from `docs/rules` with its rule number, and says whether this game applies it.
  Keywords only get what the core rules say about them: INFANTRY (terrain, 13; actions, 16), EXPLOSIVES /
  GRENADES (the Explosives Stratagem, 15.05). Faction keywords say they're for army building (02.05). Any
  other keyword (BATTLELINE, TACTICUS, GREAT DEVOURER, ...) says that no rule in this game uses it. IMPERIUM
  appears in the PDF only as a flavour quote, so it gets the generic text too. Nothing is invented.
- **What applies** comes from core's read-only `ability_effects` (24.30, 24.05, 24.16, 24.37) and
  `batch_context` (for past batches: [HEAVY] applied iff the batch's hit modifier is more than the
  selection's own). The client only words it (D-024). [ASSAULT] and [CLOSE-QUARTERS] act when the shooting
  type is chosen: they show as applying (green) when the unit is using Assault / Close-quarters shooting,
  and as information otherwise.
- **Where:**
  - The weapon menu (after picking a target) shows each choice's stats and one line per ability: green
    when it applies to this volley, red when it doesn't. Hovering shows the abilities' help.
  - Shooting-type menu entries have their 10.04-10.06 help.
  - On a datasheet, each weapon ability and keyword can be hovered.
  - A hit roll's dice card lists the dice gathered and the abilities that changed them.
- Tooltips appear only on hover, so there is no setting to turn them off.

## D-038: Quality-of-life fixes (M25)
**Decided:** 2026-11-05 (agreed with the user). Client only; no engine, state or replay change.
- **UI scale slider:** the value applies on release (`IsItemDeactivatedAfterEdit`). Applying it while
  dragging rescaled the slider under the mouse, so the value ran away.
- **Move-type menu:** clicking a unit to move always opens its menu, even with one move type, and the menu
  ends with **Cancel**, which picks nothing and drops the unit. Before, an engaged unit's only option, Fall
  Back, was chosen by the click itself, which can make a Desperate Escape roll (09.07). Other menus are
  unchanged.
- **Warn before ending shooting** (setting, on by default): if the end-phase actions include
  `EndShootStep` while a `Shoot` template is still legal, a modal lists the units and asks to end the
  phase or keep shooting. The engine only offers a template for a unit with at least one valid shot
  (D-016), so "can still shoot" means a real target. A lone skip (D-036) never warns, because then no unit
  can shoot.
- **Allocate damage automatically** (setting, on by default; **off by default since D-051**, so casualties
  are picked on the board): the defender's `DeclareAllocationOrder`
  takes the first legal order, and `OrderCasualties` takes `CasualtyPlan`'s default order (since M47 D-058's order,
  not unit order). These are the
  defaults the panel offers. They go through `GameSession::submit` like the D-036 skips (`run_auto_actions`)
  and count with them for Undo. The attacker's choice of batch (04.03) is not damage allocation and is
  still asked.
- **Victory pop-up:** when a submitted action ends the game, a modal (after the last dice pop-up) names the
  winner and army ("Side A (Space Marines) wins!") or "Draw", with the engine's reason, and offers New game
  and Close. It isn't shown for loads, replays or undo.
- **Random seed:** each New game draws a seed with `std::random_device` in the client, unless "Fixed seed" is
  ticked (then the seed box is used). Core only ever sees the seed in `GameState`, so it stays
  deterministic. `--seed n` ticks "Fixed seed", so scripted screenshots are reproducible.

## D-039: Undo by the player's inputs (M26)
**Decided:** 2026-11-05 (agreed with the user). Client only; each undo is still `GameSession::undo` (M13).
- **Groups:** `UndoGroups` marks the history size where each player input starts. An input is one model
  drag, one whole-unit drag (a `ProposeModelMove` per model), one button or menu choice, or one end-phase
  button that submits several actions. The actions the client takes by itself afterwards (D-036, D-038) join
  that input's group. An input that added no step (everything rejected) is not a group. Steps from before
  any group (e.g. after a load) undo one at a time.
- **Undo** (sidebar) removes the last group. This replaces M23's per-step list of automatic steps.
- **Undo last step:** while the decision is `MoveModels` and every step in the last group moves a model of
  the moving unit, the action bar offers "Undo last step" next to Commit / Reset. It removes that group, so a
  unit drag undoes as a group and a model drag as that one model. It is not offered right after the unit is
  selected to move (that is a `StartMove` group); Reset abandons the whole move as before.

## D-040: Where a moving model can end, shaded (M27)
**Decided:** 2026-11-05 (agreed with the user).
- **The rule stays as written.** A Normal or Advance move only has to *end* unengaged (09.05, 09.06 After
  Moving); 03.01 forbids moving through enemy *models* and off the battlefield, not through engagement
  range. The user asked whether paths should be blocked from crossing engagement range and chose the
  official rule, so validation is unchanged.
- **`move_reach` (core, read-only, `engine/movement.hpp`):** for a model in the move in progress, where its
  centre can end:
  - a circle round its start position of the move's maximum distance (`max_move_distance_mm`);
  - inside the battlefield shrunk by its base radius;
  - outside a circle round every other model, of radius (both base radii) for "not on another model"
    (03.01 Ending a Move), or (both base radii + 2" + EPS) round enemy models for a move that must end
    unengaged (Normal, Advance, Fall Back; 03.04, the tolerance policy).
  - None for a charge move, which keeps its charge-distance ring (11.04 needs the end engaged), or when
    the model isn't moving.
- **What the area doesn't promise:** the path must still not cross enemy bases (checked when the drag is
  dropped), and the unit must still end in coherency (checked on commit). The area is per model, with the
  other models where they are now.
- **Drawing:** the client turns it into horizontal spans, one per screen pixel row (`reach_spans`, pure
  interval maths), and fills them faintly in the movement-ring colour for the dragged model or, when not
  dragging, the selected model of the moving unit. Not for a whole-unit drag. It is shown with the
  "Movement range" ring toggle.
- **Extended by D-041 (M28):** the area follows each step, covers pile-ins and consolidations, and is shown
  for whole-unit drags too.
- **Extended by D-046 (M30):** charge moves are shaded too.
- **Used by D-052 (M36):** "Fix illegal moves" fixes a refused drop into this area, without its Engagement
  Range parts.

## D-041: The movement area follows each step, covers pile-ins and whole-unit drags (M28)
**Decided:** 2026-11-05 (agreed with the user). Read-only core query and client only; movement validation
is unchanged.
- **Each step:** the client always adds a straight line to a model's path (`extend_path`), so the next drop
  is a circle round where the model is *now* (`MoveReach::from`) of the distance it has *left*
  (`remaining_mm` = maximum less its path so far, 03.01). The movement ring still shows the whole move from
  the start.
- **Pile-in (12.03) and consolidation (12.08 Ongoing / Engaging):** per-model rules are shaded as
  `required_any` groups (inside at least one circle of each group). Engagement range is not forbidden,
  because these moves end engaged.
  - **Closer** to the closest target, measured from the start, edge to edge, "more than" EPS closer, any
    unit tied for closest counting (D-022, D-010, `ends_closer_to_closest_target`).
  - **Still engaged** with each enemy unit the model started engaged with (12.03 / 12.08 Ongoing After
    Moving, `still_engaged_problems`).
  - **Immobile:** a model in base contact with an enemy model at the start (12.03 / 12.08 Ongoing While
    Moving) shows no area.
  - Unit-wide conditions (12.03 "your unit must be engaged", 12.08 Engaging "engaged with all selected")
    and "engaged with it if possible" (TODO(if-possible)) are not shaded; the commit checks the former.
- **Whole-unit drag:** `translate_unit` moves every model by the mouse's offset from the grab point, so the
  mouse can be let go where every movable model's area, shifted by (grab − its position), overlaps. The
  unit's own models don't block each other (they move together). Models that can't move (immobile, or no
  distance left) are left out, since the engine refuses only their part.
- **Cancel:** a move menu's Cancel, the new Cancel button while picking a shooting target, and Esc also
  clear the clicked unit and model, so the board looks as it did before the click (the selection rings
  were left on a unit that was no longer acting).
  (Superseded by D-043's single rule, M29.)
- **Extended by D-044 (M29):** spots behind enemy models are left out.

## D-042: Removing a model for coherency by clicking it (M28)
**Decided:** 2026-11-05 (agreed with the user). Client only.
- `RestoreCoherency` (03.03 Regaining Coherency) is handled on the board: the unit's models are clickable,
  a click chooses one (a red ring and cross; another click changes it), and the action bar's
  **Confirm: Remove model N** submits `RemoveModelForCoherency`. There is no longer a button per model.
- The engine asks again while the unit is still out of coherency, so several models are removed one at a
  time, each clicked and confirmed.

## D-043: The selection follows the board (M29)
**Decided:** 2026-11-05 (agreed with the user). Client only.
- The clicked unit's selection rings stay only while that unit is the board's actor (the picked shooter,
  charger or fighter, the unit removing a model), the unit moving, or the unit whose click menu is open
  (`keep_selection`). Otherwise the selection is cleared.
- The rule is applied after the events that can change it, not every frame (so `--select` still works for
  screenshots):
  - every board press (a click on a unit that starts nothing, enemy or friendly, leaves no rings);
  - the click menu closing, whether by a pick, a click outside it or Esc;
  - each new decision or undo (Reset move, Commit, any submit or automatic action);
  - an empty-board click, Esc and the Cancel buttons.
- It replaces M28's clearing on the explicit Cancels only, which missed the other ways a unit is dropped.

## D-044: The shaded area leaves out spots behind enemy models (M29)
**Decided:** 2026-11-05 (agreed with the user). Read-only core field and client only.
- 03.01: a model's base cannot be moved through enemy models. The engine checks each straight line
  (`check_model_path`: the line passes "less than" base radii − EPS from an enemy's centre). `move_reach`
  now lists those enemy bases as `blocking`. The list is empty for a Desperate Escape (09.07 While Moving:
  it may move through enemy models).
- The client leaves out each blocking base's shadow from where the model is now: the disc, and the wedge
  between the tangents beyond the tangent chord. Every straight line to a point there crosses the chord,
  which lies inside the disc. Per row that is one interval.
- For a whole-unit drag, each model's shadows are shifted with its area. A spot reachable only by a path
  that bends round the enemy needs two drags; the area shows the next straight line, as the client always
  adds one line per drag.

## D-045: Cancel on every click menu; a kept advance roll is not shown as a new roll (M30)
**Decided:** 2026-11-05 (agreed with the user). Client only.
- **Cancel** ends every board click menu: move types (D-038), shooting types, a target's weapons
  (after Custom...), melee weapons, fight types.
  - On a target's weapon menu it only closes the menu, so the same shooter or fighter goes on picking a
    target; the action bar's Cancel or Esc drops the shooter.
  - On a unit's menu it drops the unit.
- **Declaring a charge** asks first (Declare charge / Cancel), as Fall Back does: the click would otherwise
  make the 2D6 charge roll at once (11.02).
- **A kept Advance roll** (D-005: once made it stands for the phase): the engine's `AdvanceRolled` with
  `reused` makes no dice pop-up, since nothing was rolled. The Advance menu entry says "(advance roll kept:
  +N")" once the unit has rolled this phase. The log line still names it as kept.

## D-046: The shaded area for charge moves (M30)
**Decided:** 2026-11-05 (agreed with the user). Read-only core query; validation unchanged.
- `move_reach` now covers a charge move (11.04):
  - **Distance:** up to the charge roll, from where the model is now (D-041).
  - **Closer:** it must end closer to one or more charge targets (While Moving, as
    `ends_closer_to_a_charge_target`): one `required_any` group with, for each target, a circle round each
    of its models.
  - **Non-targets:** it must not end within 2" of a model of an enemy unit that isn't a charge target
    (After Moving: "cannot be engaged with … enemy units that are not charge targets", which holds only if
    no model is): `forbidden` engagement circles.
  - **Paths:** not through enemy models (D-044).
- **Not shaded:**
  - "engaged with all of the charge targets" (unit-wide; checked when the move ends);
  - "within 1" / engaged if it can" (TODO(if-possible), left to the player).
- The charge-distance ring is still drawn.


## D-047: Skipping pile-ins and consolidations too small to matter (M31)
**Decided:** 2026-11-06 (agreed with the user). Client only, plus a read-only core query; no state, event or
replay-format change.
- **Extends D-036.** Under "Skip steps with nothing to do", in a Pile In or Consolidate step (12.02, 12.07)
  the client submits `EndPileIns` / `EndConsolidations` when every unit's offered move (every target
  choice) is not worth making; in an overrun pile-in (12.06) it submits `SkipPileIn`. Moving is the
  player's choice ("units they choose to move"), so this changes no rule.
- **Not worth making:** for every model of the unit, `move_reach` (D-041) on a preview of the move's start
  (`preview_fight_move`) says it is immobile (base contact, 12.03 / 12.08 Ongoing While Moving), or no point
  of its area is more than **2 mm** (fixed) from its start (sampled every 5° and 0.5 mm), **and** no model's
  move could reach the engagement range of a model of an enemy unit it isn't engaged with at its start
  (centre distance − both base radii − 2" − EPS ≤ the distance it may move; deliberately generous, other
  limits ignored). A small move that makes new contact can let the unit fight, or force an enemy unit to
  fight (12.08 Engaging Consolidation), so it is always offered.
- **Errs towards offering the move.** The area is per model and ignores coherency (03.03) and the unit-wide
  After Moving conditions, so it can only show more room than a real move has: a move is never skipped
  because of those, only kept.
- The skip joins the player's last input for Undo (D-039) and is named on the status line, like D-036
  ("Skipped (nothing worth doing): ...").
- **Not tested end to end:** the overrun pile-in case (12.06) uses the same check but has no logic test of
  its own.
- **Extended by D-048 and D-049 (M32, M33):** near base contact, and a model that can only get up to 0.1"
  closer, also count as not worth making.

## D-048: Near base contact counts for step skipping only (M32) — house rule, review later
**Decided:** 2026-11-06, chosen by the user. **Not rules text: review it** (e.g. if the client ever snaps
dragged models into base contact, the leeway may no longer be needed).
- **The rules engine is unchanged:** base contact is `dist <= EPS_MM` (01.04, D-022). A model a fraction
  of a millimetre from an enemy may still pile in (12.03) or make an Ongoing Consolidation (12.08), as long
  as it ends closer. Skipping is a convenience, so playing with "Skip steps with nothing to do" off keeps the
  literal rules.
- **The leeway (client only):** for "Skip steps with nothing to do" (D-036, D-047), in a pile-in or an
  Ongoing Consolidation a model whose base is **0.05" (1.27 mm) or less** from an enemy model is treated as
  in base contact, so as not moving (`kNearBaseContactMm`, `fight_move_skip`). If every model of every
  offered move is like that, or otherwise can't move more than 2 mm, the step is skipped.
- **Why:** on a table you push models into tactile base contact. A mouse drag can't be that exact, so a model
  dropped "touching" often ends a fraction of a millimetre short, and the step was then offered for
  pointless moves. Seen in play: a Termagant about 1.2 mm from an Intercessor made an Ongoing Consolidation
  0.57" sideways that ended only ~0.7 mm closer.
- **History:** M32 first put the leeway in the engine's `in_base_contact`; the user then limited it to
  skipping the same day (commit c92bc57 was replaced).
- **Extends D-047.**
- **Open question, not changed (user, 2026-11-06), look at later:** "closer to the closest pile-in target /
  selected enemy unit" is measured to the nearest model of that *unit* (D-022). So a model almost touching
  one Termagant may slide round, away from it, into contact with a different Termagant of the same unit: its
  gap to the unit shrinks, so the move is legal. Seen in play (pile-in): the end Intercessor of a line,
  ~1.3 mm below one Termagant, could swing down and left round the Termagant beside it. In person you'd
  usually leave that model where it is. The rule text allows it; whether to restrict it (e.g. "closer to
  the model it was closest to") needs a decision.
- **See D-052 (M36):** with "Fix illegal moves" on, a drag onto an enemy model now ends in exact base
  contact, so this leeway may no longer be needed. Not changed yet.

## D-049: Skipping judges how much closer a model can get, not how far it can move (M33)
**Decided:** 2026-11-06 (agreed with the user). Client only, like D-047; no rule changes.
- **Extends D-047.** A model's move is also not worth making when its starting gap to the closest target
  unit of the move (edge to edge, D-022) is **0.1" (2.54 mm) or less** (`kWorthwhileGainMm`). It can't end
  more than that gap closer, however far it travels. Otherwise the D-047 test still applies (it must be able
  to end more than 2 mm from its start). A model that could reach a new enemy unit's engagement range is
  always offered, as before.
- **Why:** seen in play (pile-in.png). After a charge every Intercessor was engaged and a millimetre or two
  from the Termagants, but the end model could slide a long way round a Termagant to touch another one,
  getting ~2 mm "closer to the unit". D-047 measured the distance moved, so the pile-in was offered.
- **Errs towards offering the move:** the gap is an exact upper bound on the gain, so no move that could
  really gain more than 0.1" is skipped. A model with a larger gap but only sideways room is still offered.
- Subsumes D-048 for this test (0.05" < 0.1"); D-048 still keeps near-contact models out of the new-contact check.

## D-050: "Auto move" for pile-ins and consolidations (M34)
**Decided:** 2026-11-06 (agreed with the user). Client only (`auto_fight_move`); no rule, state or replay
change.
- **What it does:** during a pile-in (12.03) or consolidation (12.08), before any model has been dragged, an
  **Auto move** button next to Commit / Reset places the unit's models. It submits ordinary
  `ProposeModelMove`s (one straight line per moved model) as one Undo group. The player reviews the result,
  adjusts or undoes it, and commits. Each placement is planned in a scratch `GameSession`, so the engine
  validates every one, and the arrangement is only offered if `move_end_problems` is empty. Otherwise the
  panel says "Auto move: no legal arrangement found (<the engine's first reason>)".
- **Greedy placement**, closest model to the target first (ties by model id). For each model, the spot it can
  reach in one straight line (inside `move_reach`'s area, D-040/D-041), ranked by:
  1. gap to the closest target unit (D-022), in **1 mm bands**;
  2. **friendly models within 2"** (03.03), up to 2;
  3. base contact;
  4. gap (to 0.1 mm);
  5. the shorter move.

  The candidates are the direct line into contact with the nearest target model, contact points every 5°
  round each target model in reach, and a 1 mm × 5° sample of the area.
- **Coherency (03.03) is kept after every placement:** a spot is only used if the whole unit is in
  coherency with the model there (unless the unit started out of coherency). So no model runs ahead and
  cuts a friend off.
- **Formation balance (user's concern):** a unit only just in coherency can be split when a model dies.
  So spots within the same 1 mm band count as equally close, and the one with more friends within 2" wins.
  It gives up at most a millimetre, never a real gain.
- **D-049:** a model 0.1" or less from the target only moves straight into contact (no sliding round to
  another enemy model, the D-048 open question). Models in base contact (12.03 / 12.08 Ongoing) stay put.
- **Both orders:** the pass is run closest-first and furthest-first. Of the results the engine accepts,
  the one with more models in base contact, then less total gap, is kept. (A rear model moving first can
  let the front one reach contact without breaking coherency.)
- **Not attempted:** a global optimum; re-planning a model after others have moved; "engaged with it if
  possible" for every model (`TODO(if-possible)` stays with the player); auto move after a manual drag
  (Undo or Reset first).

## D-051: Casualties are picked on the board, one model at a time (M35)
**Decided:** 2026-11-06 (agreed with the user). Amends D-013 (engine) and D-032 (client).
- **Engine (amends D-013):** the casualty decision (`OrderCasualties`, 05.04 / 06.02) is raised whenever a
  save roll inflicts damage (or a mortal wound is to be inflicted), even when every selection is forced.
  So no model takes damage, and no model is removed, without the player picking it. It is skipped only
  when nothing would be damaged. With "Allocate damage automatically" on, the client still answers it at
  once (D-038), so play is unchanged. The answer is still one priority over every model (D-013), and the
  resolver is unchanged. No saved replay had a forced-only casualty: the regenerated replays came out
  byte-identical.
- **Preview (core, read-only):** `preview_casualties(state, picks)` replays the pending decision's steps
  with the models picked so far. A damaging step goes to the first model already picked that is a legal
  choice for it. That is how a picked model keeps taking the damage forced onto it (05.04 "a model that
  has lost one or more wounds if possible"; 06.02 wounded first). Otherwise the step goes to the next
  pick, which must be legal **even when it is the only choice**. The preview stops at the first step
  that needs a pick not yet made, and gives its options and damage. `inflict_damage` /
  `inflict_mortal_wounds` gained an opt-in `ask_when_forced` so the preview can follow every step;
  their default behaviour is unchanged. Applying `OrderCasualties(picks + the rest in unit order)` gives
  exactly the preview's result, and that is tested.
- **Client:** with automatic allocation off, the board handles the decision (`Mode::PickCasualties`).
  - Only the unit losing models can be clicked. Its models that can't be picked next are grey, and the hint
    says how much damage the next pick takes.
  - Each click shows its result on the board: a red ring, cross and "-2" for a model that dies, or an
    orange ring and "-1" for one that only loses wounds.
  - **Removed, to revisit (user, 2026-11-06):** a "So far: model n -d (destroyed), ..." text line in the action
    bar. It clipped the fixed-height bar under the hint, then ran off the screen beside the damage box, and
    the board's marks already show the same. If it comes back, it needs a bar that grows (as the custom
    shot editor's does) or a tooltip on the damage box. `CasualtyPicks::marks()` still provides the data.
  - The action bar shows **"Damage left to allocate: N"** in a coloured box under the hint (`CasualtyPreview::damage_left`): the damage of
    the next step and every damaging step after it. Damage that would be lost because the unit is wiped
    out isn't counted. At 0 it reads "All damage allocated: confirm the casualties".
  - "Undo pick" takes back the last pick (board-local, not a game action). "Confirm casualties" appears
    once nothing is left to pick, and submits the one `OrderCasualties`. "Use default order" (unit order)
    skips the picking. **Changed by D-058 (M47):** the default order is no longer unit order, and after one or
    more picks the button reads "Default order for the rest" and keeps them (the picks, then the default order
    for the rest); it goes once every casualty is picked (Confirm covers it). Its tooltip says what the order
    does.
- **Default (user, 2026-11-06):** "Allocate damage automatically" is now **off** by default, so casualties
  are picked by hand unless the player ticks it. (The allocation-group order, 05.03, is then asked too, but
  only when a unit has more than one legal order.) A destroyed model's red cross and its "-N" have a black
  outline, so they show on red bases.
- **Replaces D-032's casualty bar:** the "Custom order..." grid is gone. Without the board (no board
  interaction), the sidebar offers only "Use default order".
- **Amended by D-069 (M62):** with a random D, the decision check and the preview use the D's maximum as a stand-in
  and roll no dice. The preview marks its damage as a maximum and the steps after the first random roll as
  provisional; the picks then cover every model the real rolls can reach.

## D-052: "Fix illegal moves": a refused drop goes to the nearest spot it can go (M36)
**Decided:** 2026-11-06 (agreed with the user). Client only (`drop_fix`); no rule, state or replay change.
- **The setting:** "Fix illegal moves", on by default. When a drag of a model, or of the Move unit handle
  (D-041), ends where the engine would refuse the `ProposeModelMove`, the model goes to the nearest spot it
  can go instead. The ghost, path line, rings and ruler follow the fixed spot while dragging, so the player
  sees where the model will land before letting go. The fixed drop is an ordinary `ProposeModelMove`,
  checked by `validate_action` before it is submitted, and is one Undo step. If no spot is found, the raw
  drop is submitted as before and the engine's reasons show.
- **What is fixed:** only what the engine refuses for a proposal:
  - too far: the move's maximum, less the path so far (03.01, 09.05-09.07, 11.04, 12.03, 12.08);
  - off the battlefield (03.01);
  - ending on another model (03.01, D-027);
  - the line crossing an enemy model's base (03.01, D-044);
  - a charge not ending closer to a charge target (11.04);
  - a pile-in or consolidation not ending closer to the closest target (12.03, 12.08).

  The region fixed into is `move_reach`'s area (D-040, D-041, D-044, D-046) without its Engagement Range
  parts. Each enemy's engagement circle becomes a plain "not on it" circle.
- **Engagement Range is never fixed (user, 2026-11-06):** a model may move through it, and ending in it is
  checked only at Commit (D-040). So a drop within 2" of an enemy (Normal, Advance, Fall Back), a charge
  ending within 2" of a non-target, and a pile-in or consolidation model leaving Engagement Range of a unit
  it started engaged with are all left where they were dropped. Commit flags them as before. This also
  keeps waypoints inside 2" possible, for going round an enemy model in two drags.
- **How the spot is chosen:**
  - **A line into or through an enemy model** (the drop is on its base or in its D-044 shadow): the model
    goes **along the drag**, to the furthest point on the line it may end at. That is usually base
    contact with the first enemy model the line meets. It is never routed round the model: through a gap
    too narrow for its base, it stops before the gap.
  - **Anything else:** the **nearest spot** to the drop. Too far gives the point along the drag at the
    maximum. Off the edge, it stays at the edge. On a model, it ends touching it beside the drop.
  - **At most 1" away:** if the nearest spot is further (e.g. a drop on a friendly model's centre), the
    model goes along the drag instead, so it never jumps to the far side of something.
  - The search is exact and deterministic: projections onto the area's circles and lines and their
    intersections, or the line's crossings with them. It works on the area moved 0.005 mm inwards (below
    EPS_MM). So a point against a model is in base contact (01.04) but not on it, as with D-050's contact
    nudge.
- **Not fixed: rules about the whole unit.** Coherency (03.03), "engaged with all charge targets" (11.04)
  and "the unit must be engaged" (12.03) are checked at Commit, as before, with the existing warning.
  Pulling a model towards its friends would put it somewhere the player didn't drag it.
- **With the setting off,** drops are submitted exactly as they were made, as before M36.

## D-053: Engagement Range in the shaded area; no Roll click when there is nothing to roll (M37)
**Decided:** 2026-11-06 (asked for by the user). Client only; no rule, state or replay change.
- **Engagement Range part of the area:** since D-052 a drop within Engagement Range is accepted and left
  for Commit to check. So the shaded area (D-040) also shows that part, faintly in the Engagement Range
  ring's red: "you may go here, but can't end the move here". It is `fix_region(region)` less `region`
  (`engaged_spans`):
  - within 2" of an enemy (Normal, Advance, Fall Back);
  - within 2" of a non-target (charge);
  - out of range of a unit the model started engaged with (pile-in, consolidation).

  It is drawn with the blue fill under the "Movement range" toggle, whether or not "Fix illegal moves" is
  on. The movement ring's tooltip says what the red part means.
- **Dice pop-ups (D-033) with no dice:** a card with nothing to roll, such as a wound roll after no hits
  ("No hits to roll for"), or a save roll with no wounds ("No wounds to roll saves for"), is shown with its
  result straight away. Its button is OK, or Continue if more cards follow. There is no face-down tray and
  no Roll click (`needs_roll`).

## D-054: The bot opponent: a player, not the rules (M38–M45)
**Decided:** 2026-11-06 (asked for by the user; the milestones are pre-approved as one batch). Client and the new
`wh40k_ai` library only; no rule, state or replay change.
- **Where it lives:** `src/ai/` → `wh40k_ai` (namespace `wh40k::ai`), headless, linking `wh40k_client_logic`
  (ai → logic → core). Include-checked like core and logic (no raylib, ImGui or OS headers). Tests in
  `tests/ai/` → `wh40k_ai_tests`.
- **A player, not the rules (extends D-024):** the bot reads `const GameState&` and, in a live game, acts only
  through `GameSession::submit`, like a human. Legality comes only from `legal_actions`, `validate_action` and
  `move_end_problems`. Probabilities come from core's own functions (`make_hit_rolls` / `make_wound_rolls` driven by
  `ScriptedDice` faces, `check_save_roll`, `required_wound_roll`, `make_attack_profile`, `gather_attack_dice`,
  `resolve_attack_batch` on a scratch `Unit`); thresholds are never re-typed in ai code. The bot may hold strategic
  knowledge ("an Advance means no charge this turn"), but legality is always confirmed by the engine.
- **Simulation:** `simulator.cpp` is the only ai file that calls `apply_action(`, always with the 4-argument
  overload, a bot-owned `DiceSource`, and a bare `GameState` copy (never `GameSession`, for speed). Guard-tested.
- **Never peeks at dice:** the bot's RNG is seeded from an FNV-1a hash of the state that excludes `state.dice`.
  Changing only `state.dice` never changes a decision (tested). The bot never resets a move to dodge a roll it has
  already seen (e.g. starting an Advance, seeing the roll, then switching to a Normal move), although D-005 lets a
  player do that.
- **Deterministic:** work budgets count candidates or simulated actions, never wall-clock time. Candidate directions
  come from a constant unit-vector table, not `sin`/`cos`, so results don't drift with the maths library. A plan is
  a pure function of the state when planning starts, so undo, save/load and the arena reproduce it.
- **One action per decision:** `Bot::decide(state, side)` returns one `GameAction` and a reason. A multi-step plan
  (a unit's model moves) is memoized and replayed only while the state hash matches what the plan expected;
  otherwise it replans. A plan never runs past an action that rolls dice (Advance, Desperate Escape,
  DeclareCharge, Shoot, Fight).
- **No silent reverts:** before `CommitUnitMove` the bot checks the full proposal with `move_end_problems`.
  `fix_drop` (D-052) handles neither Engagement Range nor coherency, so the bot checks both. Arena acceptance is zero
  rejected actions and zero `UnitMoveReverted` events.
- **Fallback and loop guard:** every decision kind has an always-legal fallback (lone skip / End step, Remain
  Stationary, SkipChargeMove, PassFight, the `MeleePlan` default, the `CasualtyPlan` default; since M47 D-058's
  order, and for RestoreCoherency D-058's pick instead of the first model offered). The same state hash
  and decision three times means the fallback is used. If the fallback is rejected, the bot stops and shows the
  reasons instead of looping.
- **Client:** which sides are bots is client state (`std::array<bool,2>`), plus an optional `"players"` block in the
  save JSON; there is still no seat or player abstraction in core. Human input is ignored while a bot owns the
  pending decision. Auto-allocate (D-038) never answers a bot's decision. Undo pops the bot's trailing input groups
  and the human's last one. Bot pacing (thinking pauses, auto-rolled bot dice) is presentation only.
- **Levels:** Easy = the M12 test policy (baseline), Normal = the M41–M43 decisions, Hard = the M44 evaluator and
  lookahead. Tuned weights and arena results are recorded here as the milestones land. **Changed by D-066 (M56)**
  for several units per side: Easy aims at the closest enemy unit and walks its full Move while it is far away;
  Normal moves its units closest first, weighs every enemy unit in its moves, shares the charger's strength between
  charge targets and measures casualty worth against the attacker. One unit per side plays exactly as before.
  **Changed by D-067 (M57)** for several units per side: Hard values the army (per-unit-pair duel chains combined
  into P(win) − P(loss) over every unit, D-007), checks its move plans with rollouts in which every unit acts, and
  uses that value for its moves, charges, casualties, coherency removals and consolidations. One unit per side plays
  exactly as before.
- **M39 notes:**
  - **Files:** `src/ai/` holds `bot` (the `Bot` class, `BotConfig`, `BotChoice`, the level dispatch), `state_hash`,
    `simulator`, `baseline_bot` (level Easy), `fallback` (the fallback and `LoopGuard`), `bot_driver` (pacing) and
    `arena` (+ `arena_main.cpp`, exe `wh40k_bot_arena`). `BotLevel` and `BotSpeed` are the client's enums
    (`settings.hpp`); `ai::speed_factor` is the one place the speed multipliers live (the bot's dice pop-ups use it
    too): Slow ×2 (**×3 since M48, D-059**), Normal ×1, Fast ×0.4, Instant 0. Normal and Hard play as Easy until M41/M44 (`effective_level`); the start screen and the game status say so.
  - **How a level plugs in:** a policy is `(state, side, legal, seed) -> PolicyResult {action, reason, rest}`.
    `Bot::decide` hashes the state, runs the loop guard, replays the memo while its next step's `expect_hash` equals
    the state's hash, otherwise calls the level's policy and keeps its `rest` as the new memo; then it validates the
    answer (`validate_action`, no template, `move_end_problems` for a commit) and falls back if it isn't legal. A
    policy must give the same answer from any state along its own plan (Easy plans a move from the move's start
    positions, so a replan after a load or undo comes out identical; tested against a fresh bot at every decision).
  - **The hash** covers units (ids, side, flags, rolls already made such as charge and advance rolls), models (id,
    position bits, wounds), command points, the flow (stage, round, activation, attack batches and the batch in
    progress, the move in progress with its starts and paths, charge, step_done, fight sequence and fight, forced
    fights), the pending decision and the result. It leaves out `state.dice` and static data (names, weapons).
  - **Easy (the M12 policy, kept as it was):** Normal move 2" towards the enemy while it is more than 8" away, else
    Remain Stationary; shooting, melee and casualties with the editors' defaults; charge whenever it can; a charge move
    translates the unit to 1" short of the enemy; an Overrun's pile-in goes 2" towards it; other pile-ins and
    consolidations are ended. The one change: every move is simulated in full first (a proposal the engine refuses
    leaves that model where it is; the commit must pass `move_end_problems` and not be reverted), and a move that
    wouldn't stand is never started (Remain Stationary, SkipChargeMove, SkipPileIn instead). So Easy never resets a
    move, and the arena sees no `UnitMoveReverted` at all. **Changed by D-057 (M46):** Easy's pile-ins (an Overrun's
    too) and consolidations are now made by Auto move whenever one is worth making. **Changed by D-058 (M47):** the
    casualty editor's default is D-058's order (furthest from the action first, keeping coherency), and Easy answers
    RestoreCoherency with D-058's pick (`default_coherency_removal`) instead of the first model offered.
  - **Loop guard:** `LoopGuard` counts visits per state hash; the third visit uses the fallback. `Bot::forget()` (an
    undo, a new game, a load) clears it and the memo, so replaying a position after an undo is not a "loop".
  - **Driver:** `BotDriver::tick(now, inputs)` keys each decision on (state hash, history size, generation). A new key
    starts a job (on a `std::async` worker, or synchronously at Instant speed and in tests); a stale worker is waited
    for, its result dropped. The pause (0.8 s activation, 0.12 s placement, 0.4 s commit and step buttons, 0.6 s
    allocation and casualties, times the speed factor) starts when the decision appears and restarts while a pop-up
    is open or the bot is paused; Step hands over the next action at once. A refused or stopped bot stops the driver
    (the panel shows the reasons) until the next invalidate.
  - **Client:** the action bar shows "Bot (<army>) is thinking..." (or "is paused" / "has stopped") with Pause /
    Resume, Step, the bot speed and the next or last reason (**since M48, D-059:** Step and the speed only; Pause is
    the sidebar's "Pause the bots (P)"). While a bot owns the decision the board is inactive (no
    clicks, drags, move handle or menus) and the decision panel isn't drawn; `--steps` also stops there. Each bot
    action is its own Undo group, tagged `InputOwner::Bot`; against a bot, Undo (`undo_human`) removes the bot's
    groups after the human's last input and that input, and is disabled until the human has an input to undo (so it
    is always off in bot vs bot). "Undo last step" is never offered for the bot's placements. A bot's dice pop-ups
    roll after 0.35 s and continue after 1.2 s (times the speed factor; at Instant, at once, with no tumble) when "Bot
    rolls its own dice automatically" is on (a sidebar toggle in a game with a bot); a click or Enter skips the wait
    (**since M48, D-059:** only a click on the pop-up or the bare board, not on another window). **Since M49
    (D-060):** such a card has no Roll and no Continue / Done button, only a dim line; paused, or with the setting
    off, it waits with Roll and Continue as a human's does.
  - **Save players block (extends D-025):** `saved_game.{hpp,cpp}` writes
    `"players": {"A": "human"|"bot", "B": ..., "bot_level": "easy"|"normal"|"hard"}`. Loading restores the bot sides
    and that game's level (kept apart from the settings' level, so loading doesn't change the user's setting). A save
    without the block, a bare GameState, or a block that can't be read loads as hotseat; an unknown level keeps Easy.
  - **CLI:** `--bot A|B|both|none`, `--bot-level`, `--bot-speed`, `--bot-steps n` (bad values exit 2). `--bot-steps`
    runs after `--steps` and stops at a human's decision, so `--seed 1 --bot B --bot-steps 20` shows side A's first
    decision (A moves first); `--seed 1 --bot B --steps 6 --bot-steps 4 --bot-speed slow` shows the bot mid-move.
  - **Build:** `src/client_2d/logic` is now added from the root CMakeLists (before `src/ai`, which links it, and
    `src/client_2d`, whose exe links `wh40k_ai`).
- **M40 notes (odds and threat analysis, `src/ai/analysis/`, namespace `wh40k::ai::analysis`):**
  - **Per-die chances from the engine:** `OddsCache` derives P(hit), P(critical hit), P(wound | hit) and P(critical
    wound) by calling `make_hit_rolls` / `make_wound_rolls` with a `ScriptedDice` face 1-6 and one die, and P(the save
    roll inflicts damage) from `check_save_roll` per face. No threshold or S-vs-T row is typed in ai code (tests
    check them against the rulebook and `required_wound_roll` for S, T 1-12). Profiles come from
    `make_attack_profile` (so [HEAVY]'s +1 is in `hit_modifier`) and dice from `gather_attack_dice` ([RAPID FIRE],
    [BLAST] per five target models, rounded down). `profile_supported` = fixed A and D (D-002) and no
    `unimplemented_ability_problem` (D-026): the Thunder Hammer, spinefist, Shardlauncher and Strangleweb are out.
  - **Cache keys:** hit/wound by (BS/WS or -1, torrent, S, hit modifier, wound modifier, T); saves by (AP, Sv or -1,
    InSv or -1), in `std::map`s. Damage and the attack-time abilities don't change per-die chances ([HAZARDOUS] hurts
    the attacker, not the odds). A cache belongs to its caller, not a global; a value never depends on what else is
    cached (tested).
  - **Criticals:** reported, but in V1 they add nothing beyond succeeding (an unmodified 6 always hits and wounds;
    [DEVASTATING WOUNDS] and [TWIN-LINKED], the abilities that would use them, are rejected by D-026).
  - **The exact outcome:** a volley (batches in resolution order) against a `TargetSnapshot` (T, allocation groups in
    the first legal order, models in damage order: within a group a wounded model first). A dynamic programme over
    (models destroyed, wounds left on the front model), one attack die at a time, with P(inflicts damage) =
    hit x wound x unsaved for the group of the model next in line. Excess damage is lost with the model, and attacks
    after the unit is wiped are lost (05.04, Appendix: Destroyed). It is exact for a single allocation group (every
    V1 unit); with several groups it ignores 05.04's lowest-save-roll-first order across groups.
  - **Value unit:** `model_equivalents` = models destroyed plus the change in the front model's lost-wound fraction,
    so it adds up across volleys and a W2 model left on 1 wound counts half. `damage` is wounds lost (D2 into W1
    counts 1). Every outcome carries the pmf of models destroyed (for M44's chain).
  - **Sampling:** `sample_outcome` runs `resolve_attack_batch` on a stripped copy of the target (keywords reduced to
    CHARACTER, no weapons or names) with the bot's own `SeededDice` (its own `DiceState`, D-054), the first legal
    allocation order and model. Acceptance: all 66 cases (22 profile / [HEAVY] / half-range variants x Intercessors,
    Termagants and a wounded Intercessor squad; attack dice from `gather_attack_dice` x enough models to strip about
    60% of the target's wounds) are within 2% for expected damage and models destroyed, and within 0.015 per pmf
    entry, of 20,000 engine batches each: `M40_ExpectedDamage.DISABLED_FullGrid...` (about 6 min in Debug, 18 s in
    Release; passed in both). ctest runs an 8-case subset at 20,000 batches each (about 23 s in Debug).
  - **From a state:** `shooting_batches` / `melee_batches` are exactly the engine's batches
    (`make_weapon_selections` / `make_melee_selections` + `gather_attack_batches` with `shooting_context` / the
    Fighting context), empty when `shot_problems` / `melee_problems` object. `best_shooting` picks, for each shooting
    type the unit is eligible for, each model's best profile per weapon from `shot_options` and the better of its
    [CLOSE-QUARTERS] and other sets (24.07), each valued alone against the fresh target (no overkill: M41's job).
    `PositionProbe` is M43's seam: one `GameState` copy whose unit positions and turn flags are rewritten per
    candidate (it also clears `selected_to_shoot`, so the shooting queries answer for "after this move").
  - **Charges:** `charge_odds` scripts each 2D6 total (a face pair found through `roll_2d6`) into `DeclareCharge` on a
    `Simulator` copy (new `Simulator::step(side, action, DiceSource&)`; `apply_action(` stays in simulator.cpp) and
    counts the totals where `legal_actions` offers `StartChargeMove` with exactly the targets: 11 copies at most.
    A model within the roll of a target can reach its engagement range, so target eligibility is the quick bound;
    M42 plugs its placement check in through the `PlacementCheck` hook. `p_charge_roll_covers(gap)` is the same bound
    without a state (as `eligible_charge_targets`: within the roll and within 12"); both equal a brute force over the
    36 pairs at 3", 6", 9", 11.5" and 13" (35/36, 26/36, 10/36, 1/36, none). `advance_odds` does the same with each D6
    face through `StartMove{Advance}` and `max_move_distance_mm` (a reused roll, D-005, gives one distance).
    `distance_after_casualties(from, enemy, kills)` is the gap to the (floor(kills) + 1)-th closest enemy model: the
    defender is assumed to remove the front models (the worst case for a charger).
  - **Strategic knowledge (hypothetical turns):** `allowance_after(MovePlan)`: Remain Stationary or a Normal move up
    to 3" keeps [HEAVY]; a Normal move over 3" loses it; an Advance allows Assault shooting only and no charge; a Fall
    Back allows neither. Tested against `eligible_shooting_types`, `shooting_context` and `declare_charge_problem` on
    states with those turn flags. The live state always uses `current_options` (the engine's queries). Threat ranges:
    M (the slowest model), M + D6 (09.06), the longest usable ranged / [ASSAULT] / [CLOSE-QUARTERS] range, and
    P(charge after a Normal move) = the charge bound on max(gap - M, engagement range), since the move must end
    unengaged. `within_half_range` is core's own test (`within(gap, range * 0.5)`), checked against
    `select_weapon_target`.
  - **Roles:** ranged = the best shooting (unengaged, [HEAVY], at half the longest range), melee = every model with
    its best melee weapon, both in model equivalents against the actual enemy. `ranged_share` = ranged / (ranged +
    melee); the lean is Ranged above 0.55 and Melee below 0.45; strong = at least 25% of the enemy unit's models.
    Termagants vs Intercessors: ranged 0.417, melee 0.278 (share 0.60, Ranged, neither strong). Intercessors vs
    Termagants: ranged 5.79, melee 4.44 (strong at both).
  - **Matchup table per range band** (`direction_table`, both directions in `matchup`): Engaged (close-quarters
    shooting, then the fight; `turn` resolves both in sequence), HalfRange (at half the attacker's longest range),
    InRange (at its longest range), OutOfRange (nothing). Each unengaged band has stationary, moved and advanced
    shooting. Intercessors -> Termagants: engaged 6.26 (pistols 1.85, melee 4.44), half range 5.79 (moved 5.55), 24"
    3.70 (Saturation). Termagants -> Intercessors: engaged 0.28, in range 0.42 (0.83 wounds).
- **M41 notes (level Normal: shooting, defense, fight; `src/ai/normal/`, namespace `wh40k::ai::normal`):**
  - **Levels:** `effective_level(Normal) = Normal`, `effective_level(Hard) = Normal` until M44. `normal_policy`
    answers Shoot, SelectAttackBatch, DeclareAllocationOrder, OrderCasualties, RestoreCoherency, SelectUnitToFight,
    ForcedFight, DeclareMeleeAttacks, PileIn, OverrunPileIn, Consolidate and the MoveModels of its own pile-ins and
    consolidations. **Everything else is the baseline's (`easy_policy`)**: MoveUnits and the moves it starts, Charge
    (declare or end), SelectChargeTargets and charge moves. That dispatch (`normal_answer` in `normal_bot.cpp`) is
    the hook M42 (charges) and M43 (movement) replace. A decision Normal has no answer for also goes to the baseline.
  - **Value currency:** a share of the target unit's starting strength (`unit_share` = model equivalents / starting
    strength). It adds up across volleys and makes the two sides' losses comparable (1 of 5 Intercessors = 0.2, 1 of
    10 Termagants = 0.1); V1 is won by wiping the enemy out (D-007), so progress towards that is the measure.
  - **Shooting:** for each offered Shoot (unit, type), each model's loadouts from `shot_options` (one profile and
    target per weapon, D-016; within one 24.07 weapon set; at most 64 per model), its best one valued alone, then
    coordinate descent (3 passes, one model at a time) on the whole declaration's value, each target's batches in
    their best order. **Overkill rule:** nothing extra is typed: the exact outcome (M40) loses excess damage and
    attacks after a wipe, so a model moves its shots off a unit likely to be destroyed when another target is worth
    more; with one target unit it is just the best profile mix. The unit and type with the most value shoots; the
    step ends when no offered Shoot is worth more than 0. Focused Fire vs Saturation comes out of the numbers: within
    12" and stationary Focused Fire (1.157 per Intercessor), otherwise Saturation (1.111 after moving over 3"; beyond
    12" 0.741 vs 0.579).
  - **Batch order (04.03):** for each offered batch, the best order of its target's remaining batches starting with
    it (every permutation up to 6 batches, else by damage per attack); the batch with the most value goes first,
    ties keep the engine's order. So D2 goes before D1 against W2 models (the D1 attacks then finish wounded models),
    and against W1 models the order is left alone.
  - **Casualty heuristics (05.04, 06.02):** first the number of models the batch destroys (from `preview_casualties`
    with the first legal pick each time); then which models to lose: wounded ones first (the engine forces them),
    then greedily the one whose removal leaves the fewest coherency losses, then the lowest score; then swaps while
    one leaves fewer coherency losses (03.03). Picking one model at a time was myopic: in one Release game it
    isolated a Termagant the End of Turn step then removed; choosing the whole set fixed it. The picks then go
    through the preview step by step (a chosen model while one is a legal choice, else the legal choice with the
    lowest score), so the answer is always legal. **Score** (lower goes first): 10 per coherency loss
    (`coherency_losses`: greedy, each time the model whose removal leaves the fewest coherency problems), plus the
    position term by **stance**, plus 2 × (worth / mean worth - 1) (`model_worth`: the model's best ranged weapons at
    0" plus its best melee weapon against the closest enemy, so a special weapon is kept), plus 0.05 × the fraction
    of wounds left. Stances: unengaged and the enemy gains more from a combat than we do (`melee_share` both ways)
    -> AvoidContact, the models closest to it first (its charge gets longer; position weight 1 while it may charge
    after a move, 0.3 otherwise); unengaged and we gain more -> SeekContact, the furthest first; engaged and still to
    fight this phase -> HoldCombat (keep the engaged models, they fight back); engaged, already fought and losing ->
    BreakCombat (the engaged models first); falling back (Desperate Escape hazards, 09.07) -> BreakCombat (the
    engaged models are the ones that must get away). Allocation order (05.03, never more than one legal order in
    V1): the groups from the lowest worth per wound first.
  - **RestoreCoherency (03.03):** each model's cost is 1 + the coherency losses left after removing it; the lowest
    cost, then the lowest score as above.
  - **Fight:** the measure is the **melee exchange**: `plan_melee` values (each model's weapon, profile and targets,
    attacks split between two engaged units when that is worth more, by coordinate descent on the exact outcome) of
    our units that will still fight this phase, minus those of the enemy units that will still fight and are engaged
    with ours. **Pile-ins (12.02):** each offered pile-in that `fight_move_worthwhile` keeps is simulated in full
    (`auto_fight_move`, then `move_end_problems` and the commit, no revert) and made only if it raises the exchange;
    otherwise EndPileIns. The Termagants pile in only to add attackers against Intercessors already engaged; the
    Intercessors pile in to bring more of theirs in. **Selecting a unit to fight (12.04, 12.08 ForcedFight):** each
    offered unit and type is simulated to its attacks; an Overrun's additional pile-in (12.06) is planned as above,
    and the enemy units it newly engages (they fight back, "OVERRUN FIGHTS" sidebar) count against it: the score is
    our fight's value minus the enemy's new attacks. **PassFight** (Appendix) only when it is offered and no unit
    could make an attack. **OverrunPileIn:** the pile-in only if it beats skipping by the same score.
  - **Consolidation weighing (12.07, 12.08):** each offered consolidation is simulated; the position score after it
    is, for each of our units, engaged: our next fight (`plan_melee`) and close-quarters shooting minus the engaged
    enemy's; unengaged: our shooting at the gap minus theirs ([HEAVY] applying both ways); minus, for every enemy
    unit an Engaging Consolidation forces to fight now (`flow.forced_fights`), its attacks on us (a free fight that
    hasn't happened yet, so it is counted on top of the next one). It is made only if it beats not moving; so a
    consolidation that changes nothing, or engages more enemy attackers than ours, is skipped. **Changed by D-057
    (M46):** a pile-in, Overrun pile-in or consolidation that ties with not moving is now made if it closes in.
  - **Plans and determinism:** a pile-in or consolidation is one plan (start, the `auto_fight_move` placements,
    commit) memoized as usual. From a mid-move state (a fresh bot, a load, an undo) `continue_fight_move` replans
    from a copy with the models back at the move's starts and no paths (the state just after the start) and
    continues if the moves so far are the plan's; otherwise it commits a move that stands or resets. Tested: a fresh
    Normal bot gives the played answer at every decision of 4 Normal-vs-Normal games, also with only the dice changed.
  - **Arena metrics:** "better" for M41 is defined in PLAN.md (M41). The arena gained mean wounds lost per side (the
    wounds every model lost, destroyed models counting all of theirs), models removed for coherency per side,
    fallbacks, and `--trace <seed>` (one game's bot decisions with their reasons). Results are in PLAN.md: as Space
    Marines Normal ends games earlier (1.92 vs 1.94); as Tyranids it plays exactly as Easy (open item: their result
    is decided by the baseline movement and charges, M42/M43).
- **M42 notes (charges; `src/ai/normal/charge.*`, the placement in `src/client_2d/logic/auto_charge_move.*`, D-056):**
  - **Placement priorities (D-056):** each model aims for base contact with a charge target model, else within 1",
    else Engagement Range, else as close as it can get, never within Engagement Range of a unit that isn't a target
    (11.04), always in one straight line inside `move_reach`'s area. Arrangements are ranked by **models engaged**,
    then within 1", then in base contact, then the least total gap. Engaged comes first (not base contact, as first
    proposed) because only engaged models fight and the acceptance measure is "engages at least as many models as
    the baseline"; base contact still wins every tie, so most charges end with most models touching.
  - **The charge move in the bot:** `plan_charge_move` simulates StartChargeMove, the auto move's placements,
    `move_end_problems` and the commit on a scratch state (no `UnitMoveReverted`), so a roll with no such plan is a
    failed charge and the bot answers `SkipChargeMove`. It is memoized as one plan; from a mid-move state
    `continue_charge_move` replans from the move's start (as `continue_fight_move`) and continues only while the
    moves so far are the plan's.
  - **Odds:** `charge_odds` (M40) with that placement as its `PlacementCheck`, one scratch state per 2D6 total with a
    scripted roll. A legal arrangement for a roll is legal for any larger one, so once a roll's plan engages every
    model the larger rolls reuse its value (fewer placements to plan). The real roll is never read before the
    declaration; after it (SelectChargeTargets) the actual roll is used.
  - **Value model** (unit shares, as M41) over this Fight phase, the enemy's next turn and our next turn, cut at the
    game's end (D-007: one half-turn left in the last round when the enemy still plays, none in the game's last
    turn). Volleys are M40 expected outcomes of a whole unit (melee with its best weapon, [CLOSE-QUARTERS] shooting
    while engaged, shooting across the gap with [HEAVY]), scaled by the attacker's surviving fraction and capped at
    what is left of the target (`Exchange`).
    - Holding (= a failed charge: the unit doesn't move, 11.02): the enemy shoots us, then charges us with
      `p_charge_after_move(gap)` if, in its own view, charging beats not charging (it strikes first, we reply, then
      our turn engaged), else our turn's shooting. An enemy already engaged neither shoots us nor charges, and the
      engine (`shot_options`) says whether we may shoot it (04.02: not while it is engaged).
    - A successful charge: the fight now with Fights First (our models engaged or within pile-in reach, 3" + 2",
      strike first; theirs within the same reach reply), then the enemy's turn engaged (its close-quarters shooting,
      it fights first, we reply) and ours (our close-quarters shooting, we fight first). So engaged Termagants
      can't shoot and engaged Intercessors still fire their Bolt pistols.
    - Declaring costs nothing (no move on a failure, and after the roll the bot can still make no charge move), so
      the gain of declaring is E[max(charge value, hold)] - hold over the roll; a unit declares when the gain is
      positive, and of several units the one with the largest gain goes first (the others are weighed again at the
      next Charge decision). After the roll, the target set with the best planned value is charged if it beats
      holding, else `SkipChargeMove`.
    - Outcomes (V1): Intercessors always want to charge Termagants (at 4": 92% odds, +0.94 against +0.45 for
      holding). Termagants against free Intercessors are doomed either way: the Intercessors would shoot and charge
      them next turn, so a charge first is very slightly better (−0.944 against −0.962 at 10") and they declare up
      to about 10"; at 11.5" holding keeps a chance of not being caught and they hold. Termagants hold when the
      Intercessors are locked in another combat (holding is worth 0, a charge only joins a losing fight). The
      "Termagants 10" away hold" example first suggested for the fixture is that near tie, so the fixtures use 11.5"
      and the locked-in-combat position instead (PLAN.md, M42 results).
  - **Reasons:** "Intercessor Squad: declare a charge (92% charge odds against Termagants; a charge move +0.94 on
    average, holding +0.45)", "Termagants: no charge (3% charge odds; the Intercessor Squad would win the fight: a
    charge move −0.94, holding −0.87)", "<unit>: charge move into <targets> (roll N): E of M models engaged, C in
    base contact (<value> against <hold> for holding)", "<unit>: no charge move stands for a roll of N (<the
    engine's reason>)".
  - **M43's seam:** `charge_prospect(cache, state, unit, seed, enemy_losses)` gives `p_success`, `hold`, `expected`,
    `gain` and the value and engaged models per roll. From a Shoot or MoveUnits decision of the same turn it first
    takes a scratch copy to the Charge step (no more shooting; units not yet moved remain stationary), and
    `enemy_losses` takes our shooting's expected kills off the enemy's front (the worst case for a charge, as
    `distance_after_casualties`). It plans a charge move per roll, so M43 should rank candidates with the quick
    bound (`ThreatRanges::p_charge_now`) and call it for the few best.
  - **Still TODO(if-possible): 11.04** "each model that can end within 1" / engaged must": not enforced by the
    engine; the ranking satisfies as many models as the arrangements tried allow, not provably every one.
  - **Arena:** zero rejections, reverts and fallbacks in all four pairings (PLAN.md, M42). The Tyranid open item is
    unchanged over 1000 seeds: their charges come after the first Bolt Rifle volley, with 4-6 Termagants in one
    rank that the baseline translation also engages in full; movement (M43) has to keep them out of Rapid Fire range.
- **M43 notes (movement; `src/ai/normal/movement.*`, the score in `src/ai/normal/move_score.*`):**
  - **Dispatch:** `normal_answer` now answers MoveUnits (`decide_move_units`) and the MoveModels of Normal, Advance
    and Fall-back moves (`continue_unit_move`); level Normal no longer uses the baseline for anything it has an answer
    for. `Exchange` moved from charge.cpp to value.hpp (no behaviour change).
  - **Move types (MoveUnits):** for the first unit offered, each offered type is valued: Remain Stationary (scored
    from the state after it, with its charge prospect); a Normal move and an Ordered Retreat (the whole planned move,
    memoized from StartMove to the commit; a Normal move whose best plan is to stay put is left to Remain
    Stationary); an **Advance before its roll**: the six results on scratch states with `ScriptedDice` faces 1-6 (as
    `advance_odds`), the candidates made once for the largest roll and filtered by each roll's maximum, one refined
    move per roll, the mean of the six; a **Desperate Escape** only when it is the only mode: planned with no failed
    hazard roll (scripted sixes) less the expected hazard losses (`make_hazard_rolls` per face). The best value is
    chosen (ties: the order above). An Advance or Desperate Escape plan stops at the StartMove (dice); after the
    roll (and any casualties) `plan_unit_move` plans with the actual roll. The bot never resets a move to change its
    type after a roll.
  - **The plan of a move:** `plan_unit_move(origin)`, a pure function of the state just after StartMove (seed
    `decision_seed(origin)`). `continue_unit_move` replans from `move_origin` (positions back at the starts, no
    paths) and continues from the step whose expected hash is the state's; otherwise it commits a move that stands or
    resets (only from a state the bot didn't make).
  - **Candidates (`move_candidates`):** staying put (when that is legal for the type); the unit translated along the
    16 fixed directions (`kDirections16`, most in line with the enemy first, so a tie goes to the straighter move) by
    0.5", 1" ... 3", then every inch up to 24" and the move's maximum, plus, where a step first fails, the furthest
    point found by 8 bisections ("as far as it can go": up to the board edge or just outside engagement range);
    line-abreast and two-rank formations facing the enemy, centred 0", +-3", +-6", +-9", +-12" along the line to it and
    shifted inside the battlefield. Formation slots: the models nearest the enemy take the front rank, each rank in
    order along the line (no crossings); proposals furthest along the move first, and a model whose slot is on a
    model still to move waits; a cycle is broken by parking one model beside its start (a 1" step past touching) and
    proposing it again later with a direct line (a proposal replaces the earlier path, so no two-segment path is
    needed). **Cheap checks** prune them: bases inside the battlefield, straight lines clear of enemy bases (not for a
    Desperate Escape), ends more than engagement range + EPS + 0.05 mm from every enemy model, no base on a base, and
    `check_coherency`. About 150-250 candidates per plan.
  - **Pruning budgets (counts, never time):** every candidate gets the cheap score; in that order, up to 12 are
    simulated in full on a Simulator (each ProposeModelMove validated, `move_end_problems` empty, the commit not
    reverted) until 3 stand; those 3 are refined from the simulated state (`best_shooting`), the first 2 that can
    charge also with `charge_prospect` (exact odds and the models each roll engages). The best refined one is the
    plan. Release: a plan takes 3-6 ms, a whole MoveUnits decision 12-16 ms.
  - **The score (`score_move_plan`, `refine_move_score`; M44's seam):** components in unit shares (our side
    positive) along a `ScoreLine` (an Exchange whose losses are weighted per half-turn) over this turn, the enemy's
    next turn and our next turn, cut by `turns_left` (D-007): `shoot_now` (each model at its own gap: range and half
    range per profile; [HEAVY] after Remain Stationary or a move of no more than 3"; [ASSAULT] only after an Advance;
    none after a Fall Back; close quarters while engaged), `charge` (E[max(charge, hold)] - hold; quick bound
    `p_charge_roll_covers` on the gap to the enemy model left closest after our expected kills, or charge_prospect's
    exact rolls), `hold` (the enemy's reply worst for us of: stay, close to its half range, or close by its M, each
    with shooting and a charge if its roll covers the gap; Advance and [ASSAULT] shooting; keep away by its M as far
    as the board allows; then our next turn's best of stay / close to half range / close by M, with a charge), `edge`,
    `coherency`, `tie_break`. Engaged and staying: our close-quarters shooting and fight, then the enemy fights on or
    falls back. Rates are cached by the range / half-range band of every profile, [HEAVY] and the shooting type.
  - **Game length (D-007) and the weights (`MoveWeights`, M44 tunes them):** the exchange's lean (each side's best
    shooting at half range plus melee, (ours - theirs) / total: Intercessors +0.76 against Termagants) sets a time
    preference, `delay` 0.5: the winning side counts the enemy's losses one half-turn later x (1 - 0.5 x lean) (it
    prefers to wipe the enemy out sooner), the losing side its own later losses the same way; and `survival` 1.0:
    the losing side adds 1.0 x |lean| x (battle round / rounds) x P(it survives the enemy's reply) (the exact
    distribution of models destroyed by that reply's shooting and, if it charges, its melee; the enemy's reply is
    chosen with it). `edge` 0.05 x (1 - room / M) x the enemy's shooting rate on us (only when we move again within
    the horizon); `coherency` 0.05 x the share of our models in groups of two or fewer linked by 2" (03.03 doesn't
    need one group; such a pair loses a model whenever one of them is a casualty: one Release game lost a Termagant
    that way with the first, weaker slack term); `tie_break` 1e-5 x lean x the gap in inches (the winner closes, the
    loser keeps away, when nothing else differs). Arena sweeps (Easy vs Normal, 1000 seeds): survival 0 gave 2.20
    rounds / 0.38 wounds dealt, 0.3 gave 2.31 / 0.40, 1.0 gave 2.38 / 0.40 (Easy: 1.91 / 0.36); without the losing
    side's discount games were shorter (2.12 rounds over 300 seeds).
  - **Reasons:** "Termagants: Advance (-0.19 on average over the advance roll) - 15.4\" from Intercessor Squad,
    outside its half range, its charge odds next turn 17% (-0.19; Remain Stationary -0.21, Normal move -0.19)";
    after the roll "Termagants: Advance (roll 5) 9.8\" obliquely back - ...".
  - **Outcomes:** Intercessors move 6" at the Termagants (half range, then a charge); Termagants kite to 15-17"
    (outside half range and most charges, still within 18"), Advance and shoot, fall back from a losing combat, and
    charge when caught anyway. As Tyranids they still never draw (PLAN.md, M43: open item for M44).
  - **Diagnostics (disabled tests, run on purpose):** `M43_Diagnostics.DISABLED_PrintTheRankedCandidates`,
    `DISABLED_PrintDecisions` and `DISABLED_TimeThePlanner` print a position's ranked candidates with every score
    component, the bot's choices across fixture layouts, and the planner's timings.
- **M44 notes (level Hard; `src/ai/hard/`, namespace `wh40k::ai::hard`):**
  - **Files:** `weights.hpp` (`BotWeights`: every weight Hard plays with), `evaluator.*` (the chain, `Evaluator`,
    `ChainCache`), `lookahead.*` (the rollout policy and `monte_carlo`), `hard_bot.*` (`hard_policy`, the move plan
    and the decisions). `Bot` owns a `HardContext` (the weights and the chain cache; a copied `Bot` gets an empty
    cache) and `BotConfig` carries `hard::BotWeights` (the arena's `--weights` overrides them for tuning).
    `effective_level(Hard) = Hard`. Two behaviour-neutral additions to shared code: `AttackOutcome::wounds_left_pmf`
    (M40's exact outcome also reports the distribution of the target's wounds left) and `normal::standing_moves`
    (the M43 candidates that stand when simulated, in the cheap score's order; Normal doesn't call it). Normal and
    Easy play exactly as before: Normal vs Normal reproduces, on every scenario, the numbers measured before any M44
    code (and M43's V1 row).
  - **When Hard applies:** a duel, exactly one unit with models on each side (every V1 scenario); otherwise Hard
    plays as Normal. Hard answers MoveUnits and the MoveModels of its Normal, Advance and Fall-back moves, Charge,
    SelectChargeTargets, OrderCasualties, RestoreCoherency and Consolidate; everything else (shooting, attack batch
    order, allocation order, fights, pile-ins, the MoveModels of charge moves, pile-ins and consolidations) is
    Normal's.
  - **The evaluator (a Markov chain, expectimax):** the state is each unit's wounds left (with the wounded model
    first, 05.04, that fixes what is left of a unit whose models share one W; Intercessors 0-10, Termagants 0-10 or
    0-20), the half-turn (battle round and turn: whose turn, how many are left, D-007) and the range band: the gap
    in 2" bands (0 = engaged) on a 1D line as long as the gap plus both units' rooms to back away (measured along the
    best of the 16 fixed directions within 60 degrees of straight away, as M43's `room_away`; 10 bands = 20" on the
    V1 board), so each unit's room behind it is part of the state. In its half-turn the side to move picks what is
    best for it: unengaged, a Normal move to any band in reach (towards, staying, or back as far as its room
    allows; [HEAVY] after no more than 3", strategic knowledge as `allowance_after`) or an Advance (each D6 result,
    then the best band; [ASSAULT] only, no charge); its shooting; then (unless it advanced) a charge if it wants
    one (P = `p_charge_roll_covers` of the band's gap), and the fight (it strikes first, 11.04 / 12.04, then the
    defender replies); engaged, stay (close-quarters shooting, then the fight) or Fall Back (its M away, no
    shooting or charge). Every volley's outcome is M40's exact distribution of the target's wounds left
    (`expected_outcome`) for the attacker's models alive (copies of the unit's most common model, profile picks
    from `unit_ranged_volley` / `unit_melee_volley` in that band against the target at starting strength) against
    the target's wounds left; no threshold is typed. Payoffs: win +1 + tempo x (H - h) / H, loss -1 - tempo x
    (H - h) / H (h the half-turn it happens in, H = 10), draw 0, plus material x (our share of wounds left - theirs).
    V = P(win) - P(loss) under the chain's play is reported apart from that score. For the half-turn in progress
    the evaluator uses the real position: the shooting's exact distribution from the engine's `best_shooting`, and
    after it the gap to the enemy model left closest (the defender removes its front models, M40's
    `distance_after_casualties`). Approximations: every model fights in melee, no battle-shock or Desperate Escape
    hazards, a unit's formation (the gap after casualties) only in the half-turn in progress, [BLAST] counted at the
    target's starting strength (no V1 weapon has it).
  - **Tables and the cache:** one table holds every state from a half-turn on, up to the wounds left then. Its key
    is the two representative models (their JSON without ids, positions and names), the keywords, starting wounds,
    game length, who goes first, the line length and the chain weights; a later decision whose state it covers
    reuses it (at most 8 kept). A state's value is the same whichever table answers (the same terms in the same
    order: tested), so the cache never changes a decision. Distributions are shared between attack kinds with the
    same volley (the Fleshborer's Heavy and Moved volleys are one). A table costs 14 ms (V1) or 27 ms (20
    Termagants) in Release, 100-200 ms in Debug.
  - **The lookahead:** `monte_carlo` plays `rollouts` rollouts from the position a plan leaves to the start of our
    next turn (or the game's end) on a `Simulator` with the bot's own dice, seeded from `rollout_seed` (a dice-free
    hash of the round and turn and every model's id, position (for a move in progress, its start) and wounds, so
    every option of one decision meets the same rollout dice) and the rollout's index, and values each end with the
    evaluator. The rollout policy: the chain's own choices for the move (`turn_choice`: a Normal move, an Advance or
    a Fall Back by the distance the chain likes best, made as a straight translation along the line between the
    units' centres, or the closest of the 16 fixed directions within 60 degrees that stands, as far as it can go by
    bisection, after cheap checks: battlefield edges, enemy bases on the path, engagement range, bases on bases) and
    for declaring a charge (`wants_charge`); a declared charge is Normal's `plan_charge_move` (the D-056 Auto move,
    simulated in full); everything else is the baseline's (`easy_policy`). A refused action takes the fallback, and
    a unit whose move can't be made stays put for the rest of the phase. Budgets count simulated actions, never
    time: at most `rollout_actions` per rollout and `budget_actions` per Monte Carlo; a rollout cut short is valued
    where it stopped.
  - **Hard's move plan (`hard_plan_unit_move`, a pure function of the state just after StartMove):** Normal's own
    plan (`plan_unit_move`) and the best `static_plans` M43 candidates that stand when simulated, each valued with
    the evaluator at the position after it; Normal's plan and the `lookahead.plans` best others are checked by the
    Monte Carlo; value = (1 - share) x the chain's value + share x the rollouts' mean. A MoveModels decision replans
    from `move_origin` and continues from the step whose expected hash is the state's, as M43 does. **MoveUnits:**
    Remain Stationary (valued after it, with the Monte Carlo), a Normal move and an Ordered Retreat (their plans), an
    Advance before its roll (each D6 result's best plan by the chain on a scratch state with that roll scripted; the
    Monte Carlo rolls it with the bot's dice and then makes that result's plan), and a Desperate Escape only when it
    is the only fall-back mode and every likely hazard outcome (one to three failures, our casualties chosen as Hard
    chooses them) leaves a fall-back move that stands (`escape_stands`: in an early run the hazard losses broke the
    Termagants' coherency, no move could restore it, and the move had to be reset, which the arena counts as a
    UnitMoveReverted).
  - **The other decisions:** Charge: the chain's fight (we strike first) against holding, with P from the 2D6
    roll covering the gap (declaring costs nothing). SelectChargeTargets: Normal's planned charge move against
    `SkipChargeMove`, both valued after the fact. OrderCasualties: Normal's order, the front models first, the rear
    models first and (M47, D-058) the default order, each previewed with the engine's `preview_casualties` (who
    dies, wounds lost), the coherency losses of 03.03 estimated greedily, and valued; a candidate with the same
    preview as an earlier one takes its value (the same position, so never chosen over it). RestoreCoherency: each
    model's removal (and the further removals it needs), valued. Consolidate: each worthwhile consolidation
    simulated (Normal's `simulate_fight_move`) and not moving, valued.
  - **Normal as the default (`BotWeights::margin`):** every Hard decision starts from Normal's answer (its move
    type, its plan, its charge and charge move, its casualty order, its coherency pick, its consolidation) and
    plays something else only when the evaluator (with the lookahead, for moves) values it more than `margin`
    higher. Without it (margin < 0, or 0) Hard deviated on near-ties the chain can't tell apart, and lost as often
    as it gained: as Space Marines it consolidated less into a unit about to be wiped out (the chain expects the
    Termagants to fall back from the combat; Normal's stay and die, so Normal's Marines end the game a round
    earlier: seeds 33 and 59 of V1), and as Tyranids in the Termagants-first scenario 12 of 100 seeds changed, 6
    for the better and 6 for the worse.
  - **Final weights (`BotWeights{}`):** `chain.tempo` 0.2, `chain.material` 0.05, `lookahead.share` 0.5,
    `lookahead.plans` 3, `lookahead.rollouts` 6, `lookahead.rollout_actions` 400, `lookahead.budget_actions` 6000,
    `static_plans` 4, `margin` 0.01 (half a half-turn of tempo), `move` = M43's `MoveWeights{}`.
  - **Sweeps** (Release, seeds 1-200, Hard against Normal in both seats on the three scenarios; "beats" as defined
    in PLAN.md, M44; Marines / Tyranids seat):

    | Weights changed | V1 | Termagants first | 20 Termagants |
    |---|---|---|---|
    | none (final) | tie / beats (1.67 rounds) | tie / beats (2.06) | tie / beats (-0.960) |
    | margin -1 (no default) | worse (1.52) / beats (1.75) | worse (2.06) / worse (2.04) | tie / beats (-0.965) + 1 revert (before `escape_stands`) |
    | margin 0 | worse (1.52) / beats (1.75) | worse (2.06) / worse (2.04) | tie (194 W, 6 D) / beats (-0.965) |
    | margin 0.005 | tie / beats (1.72) | worse (2.06) / worse (dealt 1.24) | tie / beats (-0.960) |
    | margin 0.03 | tie / tie | tie / tie | tie / beats (-0.960) |
    | tempo 0.4 | tie / beats (1.73) | worse (2.06) / worse (2.04) | tie / beats (-0.960) |
    | share 0 (no lookahead) | tie / beats (1.66) | tie / worse (dealt 1.24) | tie / beats (-0.960) |
    | share 1 | tie / beats (1.67) | worse (2.06) / beats (2.06) | tie / beats (-0.965) |
    | rollouts 12 | tie / beats (1.67) | tie / tie (2.05) | tie / beats (-0.960) |

    Normal vs Normal: V1 1.51 rounds, Termagants first 2.05 rounds (both 200 Marine wins), 20 Termagants 195 Marine
    wins, 1 loss, 4 draws (+0.970 for the Marines). "Tie": the same mean result (and, where both always win or always
    lose, the same end round and wounds).
  - **Results:** in PLAN.md (M44). As Tyranids Hard beats Normal on every scenario (most clearly in V1, where it
    falls back from losing combats and keeps away: 1.67 rounds against 1.51); as Space Marines it plays as Normal
    and ties on every scenario (open item: with the margin lowered its own choices do slightly worse).
  - **Reasons** (`BotChoice::reason`; M45 puts them in the combat log): every Hard reason ends in "(Hard: ...)":
    the chain's odds ("win 100%, draw 0%, loss 0%") and the score ("value +1.22", V plus tempo and material, so a
    winning side reads above +1 and a losing one below -1), "lookahead +1.22 over 6 rollouts" when the Monte Carlo
    ran, ", as Normal" / "(Normal's plan)" when Hard kept Normal's answer, and the other options' values. E.g.
    "Intercessor Squad: Normal move 6\" towards (Normal's plan) - 6.0\" from Termagants (Hard: win 100%, draw 0%,
    loss 0%, value +1.22; lookahead +1.22 over 6 rollouts, as Normal; Remain Stationary +1.19, Advance +1.21)",
    "Intercessor Squad: declare a charge (Hard: 42% charge odds against Termagants; the fight +1.21 (win 100%, draw
    0%, loss 0%), holding +1.17 (win 100%, draw 0%, loss 0%))", "Intercessor Squad: charge move (roll 9): 4 of 5
    models engaged (Hard: +1.21, win 100%, draw 0%, loss 0%; holding +1.17)". A casualty order keeps Normal's reason
    with Hard's value appended ("Termagants: casualties front models first (Intercessor Squad may charge), keeping
    coherency (Hard: -1.20, win 0%, draw 0%, loss 100%)"). The reasons are full but long; the "value" (above +1 for
    a winner) and the tie-break numbers mean little to a player, so the combat log may want only the action and the
    win / draw / loss odds.
  - **Diagnostics (disabled tests, run on purpose):** `M44_Diagnostics.DISABLED_TimeHardDecisions`,
    `DISABLED_PrintChainValues` and `DISABLED_TimeTheChainTable`. The arena's `--trace` now prints each decision's
    time, and its report the mean result per game and the seeds with a UnitMoveReverted.
- **M45 notes (watching the bot, packaging, the bot-game replay):**
  - **Reasons in the log** (`src/ai/bot_log.*`, ai tests `M45_BotLog`): when the bot's action is accepted in the
    live client, a `BotLogNote` is recorded against the session log entry it became, and the Events tab shows it
    under that entry's header in the side's colour: `Bot (<army>): <short reason>`, with
    ` | win W% / draw D% / loss L%` for Hard. Hovering it shows the full reason (and how many actions it covers).
    Notes are client state only (`ai::BotLog` in the App): never in GameState, saves or replays. An undo drops
    the undone entries' notes; a new game, a load or the start screen clears them. The thinking bar's
    "Next:" / "Last:" use the same short form, with the full reason as a tooltip. Long lines are wrapped to the
    panel's width (`wrap_words`).
  - **The short form** (`summarize_reason`): Hard's "(Hard: ...)" block is dropped and its first win / draw /
    loss kept (a rule number before it stays, "(03.03)"); "(Normal's plan)" and a trailing score block
    ("(+0.82; ...)", "(-1.20 on average ...)") are dropped; inside other parentheses only the first clause is
    kept; after " - " only the first clause. E.g. "Termagants: Advance (roll 5) 9" obliquely back - 11.1" from
    Intercessor Squad | win 0% / draw 0% / loss 100%". For this, every Hard reason lists the chosen option's
    odds first: Hard's "no charge" reason now lists holding before the fight (text only, no decision changes).
  - **No spam:** a plan's later steps (`ProposeModelMove`, `CommitUnitMove`, `ResetUnitMove` with the same
    side and reason as the last note) are grouped under the note of the plan's first action, so a move logs one
    line (an Advance two: its start, then the move after the roll, whose reason differs).
  - **Watch controls** (`src/client_2d/logic/watch_controls.*`, client tests `M45_Watch`, ai test
    `M45_Watch.SpeedChangesLiveAndStepHandsOverOneAction`): `WatchControls` holds Pause and a Step request and
    maps the commands (Pause / Resume, Step, Faster, Slower) for the buttons and the keys P, N, + and -. In Watch
    bot vs bot a watch bar at the top of the action bar always shows them with the level and "Paused" /
    "Playing at <speed> speed", also while dice pop-ups show; against one bot they stay in the thinking bar.
    **Changed by D-059 (M48):** Pause is the sidebar's "Pause the bots (P)" checkbox (`set_paused`), not a bar
    button; the bar has Step first, then the speed, and against one bot it also shows while a bot's card is open.
    The speed is the settings' bot speed, which the driver reads every frame, so a change applies to the pause
    already running. Paused, the bot's own dice pop-ups wait for a click like a human's. Step with a pop-up
    showing reveals its dice, or closes it once they show; otherwise it hands over the bot's next action at
    once (paused or not) and is used up. (**D-059:** only a bot's card; a Step on a human's card is dropped.)
  - **Bot-game replay:** `tests/replays/m45_bot_game.json`, scenario form (`data/scenarios/v1_skirmish.json`,
    dice seed 9), Hard vs Hard, played through `GameSession` by `play_bot_session` (the arena's loop, now on a
    session the caller owns): 33 steps, ending in battle round 2 with a Space Marine win (the Termagants advance
    away and shoot back first). Generated by the ai test `M45_Replay.DISABLED_RegenerateBotGameReplay`; two runs
    give byte-identical files. The enabled test `M45_Replay.TheBotGameIsDeterministicAndReplaysWithCoreOnly`
    generates the game twice in memory (identical JSON) and replays it with `run_replay`; nothing compares
    against the committed file, so the bot's play can still change (then regenerate the file on purpose).
  - **Packaging:** `package.ps1`'s README covers the start screen, the bot levels and speed, the watch controls
    and `vibehammer_settings.json`. A second smoke test runs `--seed 1 --bot B --steps 40 --bot-steps 5
    --screenshot`; `--bot-steps` prints "bot-steps: <taken> of <n> bot actions taken", and the script throws
    unless all 5 were taken.

## D-055: Start screen and saved settings (M38)
**Decided:** 2026-11-06 (M38, pre-approved batch). Client only; no rule, state or replay change.
- **Settings file:** `AppSettings` (`src/client_2d/logic/settings.*`) is saved as JSON to `vibehammer_settings.json`
  next to the exe (under `build/` in a source build, so never in the repo). It is written whenever a setting
  changes (checked once a frame) and on exit. Each key is read on its own: a missing, wrong-typed or unknown value
  keeps that key's default; unknown keys are ignored; a missing or corrupt file gives all the defaults.
- **One source of truth:** the sidebar toggles, the seed box, "Fixed seed" and the UI scale edit the same
  `AppSettings` the start screen does.
- **Scripted runs:** any CLI flag skips the start screen (`--menu 1` shows it anyway) and runs with the default
  settings plus the flags' overrides (`--seed` fixes the seed, `--scenario` the scenario). Such a run never reads or
  writes the settings file, so screenshots and `package.ps1`'s smoke test are reproducible and don't change a
  player's settings.
- **Main menu** (sidebar toolbar, victory pop-up) discards the current game and opens the start screen; the Files
  section can save it first. "Continue saved game" is offered only when the Files section's save path exists, and
  loads it as hotseat until M39 adds the save's players block.
- **Bot sides** are client state (`BotSides`, from `bot_sides_for(mode, human side)`), shown as "(bot)" in the
  sidebar and in the new-game status. Until M39 a bot side is played by hand.

## D-056: "Auto move" for charge moves (M42)
**Decided:** 2026-11-06 (M42, pre-approved batch). Client only (`auto_charge_move`); no rule, state or replay
change.
- **What it does:** during a charge move (11.04), before any model has been moved, the **Auto move** button next
  to Commit / Reset (D-050) also places the charging unit's models: ordinary `ProposeModelMove`s, one straight line
  per moved model, submitted as one Undo group. The player reviews the result, adjusts or undoes it, and commits.
  Every proposal is checked on a scratch `GameSession` and the arrangement is offered only if `move_end_problems` is
  empty; otherwise the panel says "Auto move: no legal arrangement found (<the first reason>)". The bot's charge
  moves (D-054, M42 notes) are the same arrangement.
- **Each model's aim:** base contact with a charge target model, else within 1", else within Engagement Range,
  else as close as it can get (11.04 While Moving, its "if possible" clauses: TODO(if-possible), not enforced by
  the engine). Its spots are the ones `move_reach` allows (D-046: up to the charge roll, closer to a charge target,
  not on a model or through an enemy one, not within Engagement Range of a unit that isn't a target), sampled with a
  fixed table of directions (`directions.hpp`, every 5°, no sin/cos): rings round each target model in contact,
  just inside 1" and just inside 2", the straight line to each target model, and rings round the model's start.
- **The arrangement:** the unit is first moved as a block towards the targets (towards their centre, along the
  closest pair of models, towards the closest target model), as far as every model's spot is allowed, and also the
  M39 baseline (1" short). Each start is then refined one model at a time: a model moves to its best spot as long as
  the whole unit stays in coherency (03.03, core's check), every charge target stays engaged by some model (11.04
  After Moving) and the models can still be proposed one at a time without one ending on a model still to move
  (D-027; a cycle is not attempted). Closest-first and furthest-first orders, and a "least movement" aim (engage
  with the shortest move, keeping the block together) followed by the closest aim, are tried.
- **Ranking:** the most models engaged, then within 1", then in base contact, then the least total gap (D-054 M42
  notes say why engaged comes first). The best arrangement the engine accepts is used.
- **Not attempted:** a global optimum; two-segment paths; models swapping places; an arrangement for a unit that
  starts out of coherency (it is refined like any other, but nothing tries to repair it); Auto move after a manual
  drag (Undo or Reset first, as D-050).

## D-057: Bot fight moves: ties go to the tidier move (M46)
**Decided:** 2026-11-06 (agreed with the user). Bot only, plus a read-only client query; no rule, core, state or
replay-format change. Extends D-054 (pile-in and consolidation weighing, Easy) and uses D-047/D-049/D-050.
- **Why:** in play the bots hardly ever piled in or consolidated, which looked passive. After a charge the models
  are usually already engaged (within 2"), so moving them into base contact changes nothing in the melee exchange:
  a tie, and Normal and Hard kept the step's end on a tie.
- **The shape** (`client::fight_move_shape`, `auto_fight_move.hpp`): for the moving unit against the move's target
  units, the models engaged (03.04), in base contact (01.04), the sum of their gaps to the closest target unit, and
  the friendly models within 2" of each (03.03, capped at 2, summed). A move's change is the shape after the
  committed move minus the shape before it. `tidier` orders changes: more engaged, then more in base contact, then
  a smaller total gap (by more than 1 mm, `kAutoMoveGapBandMm`), then more links: closeness first, then
  coherency, the order Auto move places each model in.
- **Normal (12.02, 12.03, 12.06, 12.07, 12.08):** the candidates are as before (each offered start that
  `fight_move_worthwhile` keeps, simulated in full by Auto move). The highest gain wins. Gains within **1e-6** are
  a tie, which goes to the tidier change. Not moving has a gain of 0 and a zero change, so a move that costs
  nothing and closes in is made, and a move with a negative gain (e.g. one that brings more enemy models into the
  fight) still loses. The same rule applies to the Overrun pile-in (against skipping it).
- **Hard:** pile-ins are Normal's (unchanged). Consolidations keep Normal's answer as the default and leave it only
  when another candidate's evaluation beats it by `margin` (as before). Without a Normal default (a negative
  margin, for tuning) an equal score goes to the tidier move.
- **Easy:** the first offered pile-in or consolidation (an Overrun's pile-in too) that `fight_move_worthwhile`
  keeps, by Auto move, if the simulated move stands; otherwise the step's end (or SkipPileIn). No weighing. Its
  MoveModels during a pile-in or consolidation are replanned with `continue_fight_move`, as Normal's.
- **Reasons:** "Intercessor Squad: pile in towards Termagants - 5 more in base contact, 5.0" closer in total
  (+0.000)". The combat log's short form (M45) keeps the head and the first clause after " - ".
- **Seen in the arena (PLAN.md M46):** results unchanged within noise, and the bots now make nearly every
  pile-in and consolidation that reaches them. Easy's Termagants, unweighted, now lose models to coherency after
  casualties (Auto move strings them round the enemy): left as found and reported, not fixed here. **Addressed by
  D-058 (M47):** with the default order they keep coherency (20 Termagants Easy/Easy 37 → 2, Normal/Easy 128 → 5;
  Termagants-first Normal/Easy 10 → 2).

## D-058: The default casualty order: furthest from the action first, keeping coherency (M47)
**Decided:** 2026-11-07 (asked for by the user; M47 is pre-approved with M48-M50). Client logic and bots only; no
rule, core, state or replay-format change. Amends D-024, D-038, D-051, D-054 and closes D-057's open item.
- **Why (the user):** "when choosing which units to kill, the auto mode should prefer models furthest away from the
  'action' ... so that could be models at the back, or models at risk of being split off and losing coherency. so
  the least coherent ones I guess. this is important". The default was plain unit order, and V1 lists the front rank
  first (D-008), so it removed the Sergeant and the front rank first, and broke coherency where a link went.
- **Where:** `src/client_2d/logic/casualty_order.{hpp,cpp}`, read-only core queries and geometry only (D-024): the
  engine still decides 05.04, 06.02 and 03.03; this only ranks the models.
  - `default_casualty_order(state, target, picks = {})`: every model of the unit once, most-preferred casualty first
    (an `OrderCasualties` priority, D-013), the picks first, unchanged.
  - `default_coherency_removal(state, unit)`: the 03.03 Regaining Coherency pick (the default order's first model).
  - `coherency_losses(models)`: the 03.03 estimate, moved unchanged from the Normal bot (M41), on a pairwise table
    built with exactly `check_coherency`'s `within` tests (tested equal to the old function on every subset of
    several layouts).
- **Keys**, for each candidate m of the models S not yet ordered; the first difference decides:
  1. Fewest extra 03.03 losses after removing m (`coherency_losses` of S without m). So a model already out of
     coherency goes first, and a model whose removal would split the unit goes late.
  2. The odd one out last: the only model of its `type_name` in a unit with more than one type (the Sergeant),
     fixed at the decision. Key 1 comes first, so it goes earlier only when that is what keeps coherency (an
     isolated Sergeant goes first). Two such models (a Sergeant and one Intercessor left): the later one first.
  3. At risk first: not engaged (03.04) and within 2" of at most one other model of S: a leaf of the unit's chain, a
     straggler, one of a pair, or already isolated. Engaged models are the action and are never at risk.
  4. Furthest from the closest enemy model first (01.04 edge to edge, any enemy unit; `more_than`, so a difference
     within `EPS_MM` is a tie). Skipped when there is no enemy model.
  5. Later in unit order first (the back rank, D-008), so the first-listed model is kept longest.
  Candidates are scanned in unit order and the best replaced only by a strictly better one (no unordered
  containers).
- **Two phases.** A: only while `OrderCasualties` for the unit is pending, step through `preview_casualties(state,
  order so far)` and take the best of the models it offers next, so a wounded model the engine forces (05.04,
  06.02), a picked W2 model that keeps taking the damage, the allocation groups (05.03) and the 06.02 tiers are
  followed. B: the best of the rest, repeatedly, so every prefix is a good set of models to lose.
- **Examples (tests `M47_CasualtyOrder`):** V1 Termagants: 9, 5, 8, 6, 7, 4, 0, 3, 1, 2 (rear rank first; every
  prefix in coherency). A bent line A..E with its tip furthest from the enemy: E, D, C, B, A (by distance alone C and
  B would go first and leave A isolated). A side straggler within 2" of one model only goes first; an isolated
  model goes first even at the front. In melee (Intercessors 3 and 4 engaged): 1, 3, 4, 2, 0. The Sergeant last
  (also behind the line), 1 then 0 with two left.
- **Where it is used:**
  - `CasualtyPlan` starts in it (`move_up` / `move_down` stay): "Allocate damage automatically" (D-038), the panel's
    "Use default order", level Easy's casualties and every level's fallback.
  - `CasualtyPicks` fills the rest after the board picks with it (`default_casualty_order(state, target, picks)`,
    kept with the picks). On the board, "Use default order" finishes from the picks so far ("Default order for the
    rest" once a model is picked) and goes once every casualty is picked (Confirm covers it). The hint and a tooltip
    on the button (and the auto-allocate tooltips) say what the order does.
  - Easy and the fallback answer RestoreCoherency with `default_coherency_removal` (before: the first model
    offered, usually the Sergeant). A human's pick stays a click (D-042).
  - Normal: unchanged (it uses the moved `coherency_losses`); `m46_bot_fight_moves.json` (Normal vs Normal)
    regenerates byte-identical.
  - Hard: the default order is a fourth casualty candidate ("the default order, furthest from the enemy first"),
    after Normal's order, front-first and rear-first, so ties change nothing; Normal's order is still kept unless
    beaten by the margin. A candidate whose preview equals an earlier one's takes that value without being
    evaluated again. Hard's lookahead rollouts use Easy's answers (D-054), so they see the new order too. In the
    arena Hard never chose the new candidate (0 in 240 games checked, PLAN.md M47).
    `hard::hard_casualties` exposes the candidates for tests. `m45_bot_game.json` (Hard vs Hard, seed 9)
    regenerates byte-identical: only Hard's reported values move, by a few hundredths.
- **Accepted consequences:**
  - The default has no stance: it never removes the front models to lengthen an enemy charge, or the engaged models
    to break a combat, as Normal's stances do (D-054). M40's `distance_after_casualties` and Hard's evaluator still
    assume the defender removes its front models: now a worst-case bound for a charger, not the default.
  - It is greedy, one model at a time. A thin line wrapped round the enemy can end as two pairs, and the next
    casualty must then isolate one: 1-5 coherency removals per 200 Release games remain for Easy's Termagants.
  - With Easy's Termagants keeping their engaged models, they fight back more (20 Termagants, Normal vs Easy: 557 →
    608 Termagant fights over 200 games), and Normal's Intercessors, whose casualty order is unchanged, lose more
    models to coherency there (1 → 15): Normal wounds a model without weighing that 05.04 forces that model to take
    the next damage, and when it is the link of a line strung out by the charge its death isolates a neighbour
    (casualty decisions that can't keep coherency: 4 → 18 of about 230). Open: fixing it means changing Normal,
    which this batch keeps unchanged (PLAN.md M47).

## D-059: Bot pacing and Pause in the sidebar (M48)
User feedback (2026-11-07): "slow bots should be slower. When the bots are moving quick you can't click pause.
maybe have somewhere else to pause the bots, like maybe have it as a config option. Tooltip for pausing bots
covers some of the ui saying what the bot is doing, that would also fix this. still need to have the step button
where it is though." Agreed: Slow = 3× Normal; Pause becomes a sidebar checkbox, not saved between games; Step
and the bot speed stay in the bar above the board. Presentation only: no bot decision changes, replays unchanged.
- **Speeds** (`ai::speed_factor`, still the one place): Slow ×3 (was ×2), Normal ×1, Fast ×0.4, Instant 0. At
  Slow: 2.4 s before a unit's activation, 0.36 s per model placement, 1.2 s before a commit, 1.8 s for reactive
  allocation and casualty answers; a bot dice card waits 1.05 s, tumbles 0.45 s (fixed) and shows its result
  3.6 s, about 5 s in all.
- **Where Pause is:** in a game with a bot, the sidebar's "Game" section has a bot block right under the unit
  list (so it is near the top and never moves with the bar): "Pause the bots (P)" (`WatchControls::set_paused`;
  key P still toggles it) and "Bot rolls its own dice automatically" (moved up from the toggles). Pause is client
  state: never in `AppSettings` / `vibehammer_settings.json`, reset by a new game, a load and the start screen,
  as before. "Paused" still shows in the watch bar's status and as "is paused" in the thinking bar.
- **The bar:** no Pause / Resume button (its label width shifted Step). Step comes first at the row's start,
  then the bot speed; the watch bar (bot vs bot) then has the "Watching bot vs bot (<level>)" chip and the
  status. Against one bot, the Step / speed row is also drawn in the rolling bar while a bot's dice card is open
  (on the same row as in the thinking bar), so it no longer vanishes whenever dice show.
- **Why Pause couldn't be clicked, and the fix:** `DicePopup::draw` called `ImGui::SetNextWindowFocus()` every
  frame. Focusing a window clears the active item of every other window, and a button fires on release, so a
  press on any sidebar or action-bar widget was cancelled while a dice card showed (at Fast, nearly always). The
  pop-up now takes the focus once per card, when it first appears, and waits while another item is active
  (`!ImGui::IsAnyItemActive()`), so it still comes up on top (it is drawn last) without stealing a press.
  Enter / Space still work (they are read globally).
- **What skips a paced bot card's wait:** only a left click on the pop-up itself or on the bare board. A click on
  the pop-up's Roll / Continue button is left to the button (it acts on release; a skip on the press as well
  would also close the next card), and a click on the sidebar, the action bar, a datasheet or any other ImGui
  window (`io.WantCaptureMouse`) doesn't skip, so Step doesn't both step and skip. The decision is
  `click_skips_bot_dice_wait(clicked, DiceClickTarget)` in `watch_controls` (tested, `M48_Watch`).
- **Step only on a bot's card:** `step_closes_popup(ShownCard)` (None / Bots / Humans). Step or key N while a
  human's own card shows is dropped and leaves the card alone (the human rolls their own dice); with no card it
  still waits for the bot's next action, and on a bot's card it reveals or closes it, as before.
- **Tooltips:** the bar's tooltips (Step, the bot speed, the reason line's full reason) appear after a longer
  rest (`ImGuiHoveredFlags_Stationary | DelayNormal | NoSharedDelay`, 0.4 s instead of 0.15 s) and open at the
  item's x just below the action bar's bottom edge (`SetNextWindowPos` before `BeginTooltip`), so the bot's
  status and reason lines stay readable. The bar's fixed height (D-030) is unchanged.
- **Tests:** client `M48_Watch.*` (set_paused and the toggle, Step on a human's card, the dice run, the click
  targets); ai `M48_Pacing.*` (the factors and a Slow delivery at 2.4 s); M39 / M45 expectations updated to ×3.
  Mouse input can't be scripted: the clicks while dice show are a manual check on Windows (PLAN.md M48).

## D-060: The Roll button only when the bot's dice wait for a click (M49)
User feedback (2026-11-07): "Only show the button to roll bot dice rolls if the bot is paused". Presentation only:
no engine, bot or replay change. Amends D-033 and D-054.
- **The rule:** a dice card is *paced* (rolls and continues by itself, `DicePopup::draw` gets a `DicePace`) when it
  is a bot's card (its roller is a bot side), "Bot rolls its own dice automatically" is on and the bots aren't
  paused: `bot_dice_paced(bot_card, auto_dice_setting, paused)` in `watch_controls` (also
  `WatchControls::card_paced(bot_card, setting)` with the current pause). A paced card shows **no Roll and no
  Continue / Done / OK button**. `dice_buttons_shown(...)` is exactly `!bot_dice_paced(...)`; main.cpp's
  `bot_dice_pace()` uses `card_paced` and the pop-up draws the buttons exactly when it gets no pace, so the
  condition lives in one place and the two always agree.
- **The placeholder:** in the buttons' place, a dim (TextDisabled) line in a row of the big button's height (the
  pop-up auto-resizes, so it doesn't jump when the bots are paused or resumed mid-card): "The bot rolls its own
  dice. Pause the bots to roll them yourself." before the reveal, "Continuing..." after it.
- **Skipping the wait:** Enter / Space still roll at once (before the reveal) or continue (after it), and a left
  click on the pop-up or the bare board still skips (D-059). With no button on the card, `DiceClickTarget` is
  `PopUp` anywhere on the pop-up, its dim line included (`PopUpButton` only arises when a button is drawn, and a
  button is only drawn on an unpaced card, where clicks don't skip anyway). Step and key N on a bot's card reveal
  or close it as before.
- **Pausing mid-card** (the sidebar's "Pause the bots (P)" or key P): the next frame the card is no longer paced,
  so Roll (or Continue, if its faces already show) appears at once and the card waits for it; un-pausing hides
  them again and the pacing carries on from the card's own timers. Turning the setting off mid-card is the same.
  A human's card always has both buttons.
- **The action bar:** while a paced bot card shows, the rolling bar's hint ends "The bot rolls its own dice."
  instead of "Roll the dice in the pop-up." (which is kept for every card that waits for a click).
- **Tests:** client `M49_DiceButtons.*` (all eight combinations of bot card / setting / paused; a human's card
  always shows the buttons; pausing and resuming mid-card; a click on a paced card's pop-up still skips). The
  paused card's buttons and clicks on it are a manual check on Windows (no flag pauses the bots).

## D-061: Short ring legend, full text on hover (M50)
User feedback (2026-11-07): "the movement range description paragraph is too big, if you want that much info have
a tooltip when you hover over it." Presentation only: no engine, bot or replay change. Amends D-028.
- **The legend:** under each ring's checkbox the sidebar shows `ring_summary(kind)` (overlay_plan), one short
  sentence (at most 100 characters), in the same dim colour and indent as before:
  - Movement range: "how far the moving unit's models can go; the shaded area is where the dragged model can end"
  - Engagement range: "2\" around the enemy models: a model inside it is engaged (03.04)"
  - Coherency: "2\" around each model of the moving unit, for unit coherency (03.03)"
  - Weapon ranges: "the selected model's weapon ranges, while choosing shots" (the old meaning, already short)
  - Charge distance: "the charge roll: how far a unit that rolled to charge can move"
- **Hover:** each legend row (swatch, checkbox and summary) is one ImGui group; hovering any part of it shows a
  tooltip with the ring's name in its colour and the full `ring_meaning`, wrapped at 24 em, like the board's ring
  tooltip (ImGui's usual tooltip delay for items applies, not D-029's 0.6 s board rest). The caption reads
  "Rings, shown when relevant (hover for details):" (shortened so it fits the sidebar's width).
- **The board:** the ring tooltip on the board (D-028) keeps the full `ring_meaning`, unchanged.
- **Tests:** client `M50_RingText.*` (every summary non-empty, at most 100 characters, one sentence; the full
  meanings unchanged; no summary longer than its meaning). The hover itself is a manual check on Windows (mouse
  hover can't be scripted).

## D-062: Bot dice follow Pause; no auto-dice setting (M51)
User feedback (2026-11-07): "I don't think there needs to be an option for bot to roll its own dice. Either they do
when they are unpaused or they don't when they are paused." Presentation only: no engine, bot or replay change.
Amends D-054, D-059 and D-060.
- **The rule:** a dice card is paced (rolls and continues by itself, no Roll / Continue button) exactly when it is
  a bot's card and the bots aren't paused: `bot_dice_paced(bot_card, paused)`, `dice_buttons_shown` its negation,
  `WatchControls::card_paced(bot_card)` and `bot_dice_run()` with the current pause. Paused, a bot's card waits for
  Roll and Continue like a human's; a human's card always does.
- **The setting is gone:** "Bot rolls its own dice automatically" (`AppSettings::bot_auto_dice`) is removed from the
  start screen, the sidebar and `vibehammer_settings.json`. An older file with `bot_auto_dice` still loads; the key
  is ignored and not written back. The Pause checkbox's tooltip says what unpaused means for the dice.
- **Tests:** client `M51_BotDice.*`; `M49_DiceButtons.*`, `M48_Watch` and `M45_Watch` use the new signatures.

## D-063: Releases are built and published by GitHub Actions (M52)
User request (2026-11-07): "Could you make a release on GitHub for me?" The game only packages on Windows, so
`.github/workflows/release.yml` does it on a `windows-latest` runner.
- **Version and tag:** a release tag is `vX.Y.Z` and must equal `project(VibeHammer VERSION X.Y.Z)` in
  `CMakeLists.txt` and the string in `src/core/version.cpp`; the workflow refuses a mismatch. The release notes are
  `docs/releases/vX.Y.Z.md` (written for players), required.
- **Starting it:** push the tag, or run the workflow by hand with the tag (it builds the commit it runs on and
  `gh release create --target` makes the tag there, so a failed attempt never leaves a tag behind; a manual run
  refuses a tag that already exists at another commit). A re-run for an existing release replaces the zip.
- **What it checks:** a Release build of every target and all of ctest. The GUI smoke tests in `package.ps1`
  are skipped (`-SkipSmokeTests`): CI runners have no OpenGL 3.3, so raylib can't open a window. `-Generator`
  replaces the VS 2026 preset when the runner has VS 2022. Without arguments `package.ps1` is unchanged.
- **The asset:** `VibeHammer-vX.Y.Z-windows-x64.zip`, the same contents as `VibeHammer.zip`. The workflow fails if
  it holds anything under `docs/`, a PDF or `rules-summary`, or no `VibeHammer.exe` (Games Workshop copyright).

## D-064: Several units per side; formation depth offset, set-up overlap check, the 44"x60" scenario (M53)
**Decided:** 2026-11-07 (board size, zones and armies agreed with the user; the details below are the
conservative choices made for M53). Amends D-008.
- **Board size is not a core rule.** The core rules give no battlefield size. 60" wide by 44" deep, with 12"-deep
  deployment zones along the long south and north edges (20" between the zones), is the usual Strike Force table
  and was agreed with the user on 2026-11-07. Winning stays D-007; no terrain, no objectives; deployment stays
  fixed by the scenario file (D-003).
- **Depth offset (amends D-008).** A `formation` may give `depth_in` (inches, 0 or more, default 0): the front
  rank's bases stand that far behind the zone's front line (towards the owning side's edge) instead of touching
  it; further ranks follow as in D-008. A negative value, or one that pushes a model out of its zone (D-009
  wholly within), is corrupt data (`DataError`). A file without `depth_in` places every model bit-for-bit as
  before: the offset is added as an exact 0 (`D064_Formation.AbsentDepthIsTheSameStateAsZero`).
- **Overlap check at set-up (03.02, D-027).** 03.02 asks that "all other requirements and restrictions are met"
  when a unit is set up; as D-027 does for moves (03.01 "on another model"), no model may be set up on another.
  The loader refuses any two bases, of the same or different units and sides, that intersect by more than
  `EPS_MM` (`bases_overlap`). Bases in contact are fine, as the D-008 formations are. The exception names both
  models and their units. The 03.02 unengaged check runs after it.
- **`data/scenarios/strike_44x60.json`** ("Strike - 3 Intercessor Squads vs 4 Termagant units (44x60)", 5
  battle rounds, Side A first). Side A (south): 3 Intercessor Squads (Sergeant + 4, the V1 wargear), each one rank
  of 5 in base contact on the zone front, centred at x = 15", 30" and 45" (units 1-3). Side B (north): 4 units of
  10 Termagants, each two ranks of 5 in base contact, centred at x = 10", 23", 37" and 50" (units 4-7); the third
  (x = 37", unit 6) has `depth_in` 3. Every unit passes 03.03 (a Termagant block is 3.5" corner to corner, well
  inside 9"), is wholly within its zone, and no bases overlap. The closest enemy front lines are 20" apart (23"
  for unit 6), so no unit is charge-eligible on turn 1.

## D-065: Per-unit shade, board number and numbered names (M55)
**Decided:** 2026-11-07 (M55 is pre-approved; the details below are the conservative choices made for it).
Client only (`wh40k_client_logic` `unit_style.{hpp,cpp}`, drawn by `board_view.cpp` and `main.cpp`); no rule,
state, replay or JSON change. Amends D-028 (the client layout) and D-029 (greyed units) only by adding to them.
- **Per-side index.** A unit's number is its 1-based position among its own side's units in `GameState::units`
  order (the scenario file's order). Destroyed units stay in `units`, so numbers never shift during a game.
- **Shades (RGB).** Each side has six shades, handed out by index (a seventh unit wraps round to shade 1).
  Side A: 1 (52,110,200) side blue, 2 (130,180,245) light blue, 3 (60,175,200) teal, 4 (28,62,140) navy,
  5 (120,110,225) periwinkle, 6 (175,215,250) pale blue. Side B: 1 (190,60,60) side red, 2 (245,130,105) salmon,
  3 (225,120,40) orange, 4 (125,28,40) maroon, 5 (205,60,140) crimson pink, 6 (245,180,170) pale pink. Shade 1
  is the pre-M55 side colour, so a side with one unit (V1, the bot scenarios) looks exactly as before (the V1
  start screenshot is pixel-identical). `M55_UnitTint` checks, in CIE76 delta E: at least 20 between any two
  shades of a side, at least 40 between any shade of A and any of B, at least 45 from the board fill
  (46,58,48), at least 35 from the shade's own greyed "can't act" look (D-029: 80% towards (55,55,60)) and at
  least 25 from the white outline / selection ring. Greyed units stay grey as before; their number tells them
  apart. Deployment-zone tints and other side-coloured chrome (turn chip, panel border, dice cards, datasheet
  title) keep the side colour.
- **Board number.** Drawn in white with a black outline (8 offsets) at the centroid of the unit's remaining
  models, on top of all bases; light grey on a greyed unit. Size: base radius on screen x 1.6, clamped to
  16..30 px x UI scale, rounded to a multiple of 10 px (raylib's default font is a 10 px bitmap, so these stay
  crisp); 20 px at the default fitted zoom of the 44"x60" board. Hidden when the largest base radius is under
  4 px on screen, when the unit has no models left, and whenever the side has only one unit.
- **Names.** When a side has several units, its units are shown as the datasheet name plus the number:
  "Intercessor Squad 1".."Intercessor Squad 3" and "Termagants 1".."Termagants 4" in the Strike scenario. The
  number is the per-side index even when the side's datasheets differ, so it always matches the board label.
  A side with one unit keeps the plain datasheet name. Used by the action text (which keeps its id: "Declare
  charge: Termagants 2 (5)"), the decision panel, board prompts, dice cards, the datasheet window title, the
  sidebar unit list and the hover tooltip (both keep "(unit N)" and show a swatch of the shade with the number,
  drawn only when the side has several units), and the bots' decision reasons (`src/ai`, through
  `client::unit_display_name`). The event log keeps naming units "unit N" (it never used names).
- **Tests.** No existing expectation changed: every one uses a scenario with one unit per side. The bots'
  reasons for multi-unit scenarios now read "Termagants 2", so a decision-trace hash of the Strike scenario
  (D-068's `--per-game` report) differs from M54's; the actions, rolls and states do not.
- **Extended in M67 (2026-11-08): ten shades per side.** The roster scenario (D-076) has nine Tyranid units, so
  `kUnitShades` is 10 and an eleventh unit wraps round to shade 1. Shades 1-6 keep their values, so the single-unit and
  Strike looks are unchanged (`M67_UnitShades` checks the values and the Strike tints). The new ones, A (cool):
  7 (85,45,175) indigo, 8 (160,240,240) pale cyan, 9 (155,125,205) lavender, 10 (200,130,255) light violet; B (warm):
  7 (250,225,145) cream, 8 (185,105,110) dusty rose, 9 (110,55,0) brown, 10 (245,165,10) amber.
  - *How they were chosen:* a search over the sRGB cube in steps of 5 (scratch script, not in the repo) picked each
    new shade in turn as the one furthest from the shades before it under simulated colour blindness, subject to every
    M55 bound (at least 20 from the side's other shades, 40 from the other side's, 45 from the board, 35 from its own
    greyed look, 25 from the white outline; all CIE76 delta E) and to a hue family (A: CIE hue 180-320 degrees, B:
    330-95), lightness 28-90 and chroma 15-80.
  - *How they were checked* (`M67_UnitShades`, CIE76 delta E on the colours as a protanope, a deuteranope and a
    tritanope see them: Machado, Oliveira and Fernandes 2009, severity 1, applied to linear sRGB): every pair of a side's
    shades that includes a new one is at least 11 apart under each of the three (lowest: A5 / A10 11.6 protanopia, A2 /
    A10 11.5 deuteranopia; Side B's lowest 16.2) and at least 20 in normal vision; every shade of A is at least 20 from
    every shade of B under each simulation (lowest 21.1, protanopia). The M55 pairs are unchanged: their closest under
    simulation are A1 / A5 (7.3, deuteranopia) and B2 / B3 (7.1, tritanopia), told apart by their numbers.
  - *Legibility:* the board number is white outlined in black and the sidebar badge white with a black shadow, so on
    every shade one of the two contrasts by at least WCAG's 4.5 (lowest 4.62, B5). On a greyed unit the number is light
    grey outlined in black: at least WCAG's 3 for large text (lowest 3.57, A8 and B7; M55's A6 3.74).

## D-066: Easy and Normal across several units (M56)
**Decided:** 2026-11-07 (M56 is pre-approved with M53–M58; the details below are the conservative choices made
for it). Bot only (`src/ai`); no rule, core, state, replay or JSON change. Amends D-054 (levels Easy and Normal).
**Amended by D-077 (M68):** on a scenario with points and sides of several unit types every share below is weighted by
points; under the points tiebreak (D-076) no side plays for the draw (marginal exposure for every side), and engaged
MONSTER/VEHICLE units may be shot and still shoot (10.06, 17.03). Nothing changes on V1, the bot scenarios or Strike.
- **The rule for 1v1 (one unit per side).** Every change below adds a term, a candidate or an ordering that only
  exists when a side has more than one unit, or picks "the closest" where M55 picked "the first" (the same unit
  when there is one). With one unit per side the old expressions run unchanged, in the same order, so V1 and the
  two bot scenarios play exactly as before (proved below by per-game hashes) and `m45_bot_game.json` /
  `m46_bot_fight_moves.json` / `m47_default_casualties.json` regenerate byte-identical.
- **Easy (`baseline_bot.cpp`):**
  - Aims at the closest enemy unit (01.04 edge to edge, D-010: a tie goes to the first in unit order, the
    controlling player's choice) instead of the first one, for its Normal moves and its 8" stop.
  - Walks its full Move while that unit is more than 12" + M away (after the move it is still more than 12" away,
    outside any charge), then the old 2" steps. A full move is used only if the whole of it, made from the move's
    start positions, stands (a pure function of the move's start, so a replan from any state along the move picks
    the same; tested with a fresh bot); otherwise the 2" step. V1's units start 12" apart and Easy only ever
    closes, so the 24" boards never reach it: the arena's 1v1 games (below) are identical, including every pairing
    with Easy (easy/easy, normal/easy, easy/normal, hard/easy, easy/hard). Reason: "Normal move 6" towards
    Termagants 1 (more than 12" + its Move away)"; a 2" step keeps the M55 text.
  - Charge targets: the closest eligible target alone when that set is offered (else the first set, as before),
    and the charge move translates towards the closest of the move's own targets (M55 translated towards the first
    enemy unit, so a charge at any other target failed `move_end_problems` and ended as SkipChargeMove).
  - Easy's activation order stays the engine's (unit) order.
- **Normal, the move score (`move_score.*`):** the ScoreLine still follows the closest enemy unit ("the enemy", as
  M43), and each other enemy unit (in unit order) adds:
  - **A target for our shooting this turn.** Cheap score: our shooting from the end positions at that unit (each
    model's own gap, band-cached, 0 for an engaged unit, 04.02); refined: the engine's `best_shooting` at every
    enemy unit. The unit our shooting is worth most against is the one shot (the closest one keeps a tie, 1e-9);
    shooting another unit leaves the line's enemy untouched, its share is added to `shoot_now`, the shot unit's
    replies are scaled by what is left of it, and the charge prospect takes the expected kills off the shot unit's
    front (`charge_prospect(..., losses_from)`).
  - **Its reply in its next turn** (`other_threat`): the worst for us of staying and shooting ([HEAVY]), closing to
    its half range, or closing by its M; then a charge if the 2D6 covers the gap (it strikes first). It is a direct
    loss to our unit in the enemy's half-turn (`ScoreLine::others_hit`, capped at what is left) and a factor on
    P(we survive) (each reply's exact wipe chance from M40's outcome, a charge's melee wipe scaled by its odds).
    While we are engaged (after our charge, or staying in a combat) it can't shoot us (04.02), it may charge us after
    a Normal move (`p_charge_after_move`); if it is engaged with us it shoots at close quarters and fights. A unit
    engaged with another of our units does neither (04.02, 11.02).
  - **Which replies count.** An enemy unit engaged with us always counts (it can only fight us). Otherwise, by the
    exchange's lean (M43: our best attacks against theirs):
    - **Winning (playing for tempo): marginal exposure.** An enemy unit counts only when it can reach no other unit
      of ours (its gap to our other units more than its M plus its longest usable range,
      `ThreatRanges::shooting_reach_mm`). Otherwise it shoots or charges one of our units whatever this move does,
      and drawing it onto this unit only moves the loss between our units; the side's total is what costs tempo.
    - **Losing (playing for the draw, D-007): closest target.** An enemy unit counts whenever this unit would be its
      closest target after the move (D-010, a tie counts): a unit's own survival keeps the draw alive.
    - How it was found (Release arena, Strike, M55 Normal as the opponent; PLAN.md M56 has the tables): counting every
      enemy unit's reply against every unit of ours (the first try) made each unit hide behind the others: Normal's
      Marines won 199 of 200 (mean end round 4.29 against M55's 3.60) and its Termagants died sooner (3.48); the
      closest-target rule on both sides still slowed the Marines (197 wins, 3.94); marginal exposure on both sides
      gave the Marines 200 of 200 and an earlier end, but its Termagants, crediting shots at a Marine unit whose reply
      it then ignored, kept fewer models against Easy (8.91 against 9.43 over 1000 seeds). The split by lean keeps
      both.
  - The edge term, the time preference (lean), the tie-break and the candidates' directions and formations stay
    those of the closest enemy unit (M43).
- **Normal, activation order (`unit_to_move`, MoveUnits):** of the units offered a move, the one closest to an
  enemy unit (edge to edge; so an engaged unit, which chooses between holding and falling back, goes first); a tie
  keeps the engine's order. Its move matters most, and the units further back then plan round where it ended
  (the planner sees friendly models as obstacles). M55 moved the first offered unit. Shooting (the most valuable
  declaration first, M41) and charges (the largest expected gain first, M42) were already chosen by value, not unit
  order, and stay so. Measured: the order barely moves the results (within noise in both seats); it is kept as the
  stated rule.
- **Normal, the charge planner (`charge.*`):**
  - **No double counting.** With several targets, the charger's models are split between them (each fights the
    target closest to it, a tie to the first target) and its losses add up in one exchange (`MultiExchange`: our
    strength `us` shared, each target its own). M55 valued each target from the charger's full strength in a fresh
    Exchange and summed them. One target is M42's value, term by term.
  - **Holding value and losses from the units involved:** `hold_value` adds the shooting and charge threat of the
    other enemy units that have no other target (marginal exposure; not engaged); `charged_value` adds their charge
    threat (they can't shoot an engaged unit, 04.02). In the arena these terms changed no result measurably (the
    charge decisions are dominated by the closest unit). `remove_front_models` takes our shooting's
    expected kills from the unit actually shot (`losses_from`), not the closest.
  - Kept: the target sets are drawn from the closest three eligible units (a fixed budget, D-054); `at_charge_step`
    still assumes the units not yet moved remain stationary and no more shooting (an approximation for a prospect
    from earlier in the turn).
- **Normal, casualties and consolidation (`defense.*`, `fight.cpp`):**
  - Model worth (`model_worth`) is measured against the attacker of the attack in progress (`attacking_enemy`),
    else the closest enemy unit (M55: always the closest).
  - "Front" (`StanceInfo::front`): engaged or falling back, the units it is engaged with; unengaged, the enemy units
    that may charge it next turn (not engaged, `p_charge_after_move` > 0), else the closest one. Each model's gap is to
    the closest of them. The stance avoids contact if any of those units gains more from a combat than we do.
  - Consolidation (12.07, 12.08): an unengaged unit of ours counts its best shot at any enemy unit it may shoot
    (unengaged, 04.02) less the shooting of every enemy unit free to shoot it (not engaged); with one enemy unit
    left, M41's expression.
- **The M55 regression switch.** `BotConfig::generation` (`generation.hpp`: `Current`, `M55`), passed by `Bot::decide`
  to `easy_policy` / `normal_policy` and from there to every function that changed (a trailing default argument, so
  Hard's calls and the tests' stay as they were). `M55` takes the M55 path at every branch above. The arena's hidden
  level names `easy-m55` and `normal-m55` set it (`--a` / `--b` only; not in the client, the settings or the save
  file). Level Hard ignores it: where it plays as Normal (any game with several units a side, until M57) it plays the
  current Normal.
- **A test expectation pinned to M55.** `M42_ChargeTargets.ChargesBothUnitsWhenTheTargetAloneCannotBeEngaged`
  (a lone Termagant ringed by six of another unit) expected the two-target charge; that came from M42's summed value
  (+0.68 for both, the Squad's strength counted twice). With the strength shared the ring alone is worth more
  (+0.58; both less), so the test now runs with `Generation::M55` (its assertions unchanged) and the new
  `M56_Charge.TheRingedTermagantIsChargedForTheMostValue` checks the current choice, that the two-target move
  still stands, and that its value is below M55's.
- **Proof that one unit per side plays the same:** the arena's `--per-game 1` lines (D-068) of a Release build of
  `d76e51b` (M55) and of this build are identical on V1, Termagants first and 20 Termagants for easy/easy,
  normal/normal, hard/hard, hard/normal, normal/easy, easy/normal, easy/hard and hard/easy, seeds 1–200 (4800 games;
  result, counts and the hashes of the actions, the final state and the decision trace with every reason, so the
  1v1 reason texts are unchanged too). Hard 1v1 plays as before (its own code is untouched; the Normal and Easy
  functions it calls reduce exactly). The four replay regenerations leave `tests/replays` unchanged.
- **Proof that the switch is M55:** on the Strike scenario `normal-m55`/`normal-m55`, `normal-m55`/`easy-m55`,
  `easy-m55`/`easy-m55` and `easy-m55`/`normal-m55` (seeds 1–200) give the M55 build's per-game lines for
  normal/normal, normal/easy, easy/easy and easy/normal, trace hashes included (800 games);
  `M56_Regression.TheM55SwitchPlaysAsM55` keeps four of those lines in ctest.
- **Measured** (Release arena, Strike, seeds 1–200; the tables are in PLAN.md M56): Easy vs Easy now ends in contact
  (188 Marine wins, 12 draws; M55's Easy: 93 and 107). As Marines Normal beats Easy 200 of 200 (end round 2.56) and
  the M55 Normal 200 of 200, sooner and with fewer losses than M55 against itself (3.58 rounds and 2.19 wounds lost
  against 3.60 and 2.67). As Tyranids it wins nothing (no Tyranid side ever has): it draws 197 of 200 against Easy
  (mean result −0.015, against Easy-vs-Easy's −0.940) and plays level with the M55 Normal against Normal's Marines
  (1000 seeds: 3 draws against 4, the Marines' end round 3.58 against 3.60); against Easy over 1000 seeds it keeps
  slightly fewer models than the M55 Normal (9.33 against 9.43, 989 draws against 995). Decision times: Normal
  p95 5.2 ms, max 68 ms (M55 4.1 / 27; M54's targets are 100 ms / 1 s). The Tyranid-seat targets (75% against
  Easy, 55% against the M55 Normal) are open items, not lowered.
- **Approximations kept (for M57/M58):** a unit's own reply model is still the closest enemy unit's line; the other
  units' replies ignore our reply to them and our next turn against them; only the moving unit is scored (friendly
  units are obstacles); a prospect from earlier in the turn assumes the units not yet moved remain stationary.

## D-067: Hard across several units: the army value, the lookahead and the decisions (M57)
**Decided:** 2026-11-07 (M57 is pre-approved with M53–M58; PLAN.md's outline was agreed with the user, the details
below are the conservative choices made for it). Bot only (`src/ai/hard`, and one helper in `src/ai/normal/movement.*`
that only Hard calls); no rule, core, state, replay or JSON change. Amends D-054 (level Hard).
- **When each path applies.** Hard's own answers apply while both sides have a unit with models and the game isn't
  over. With one unit with models per side (every V1 scenario, and a bigger game reduced to one unit each) Hard is
  M44's duel, unchanged: the same code, the same expressions in the same order (the duel's evaluation, rollouts and
  decisions take the army path only behind `Evaluator::army()`), so V1 and the two bot scenarios play exactly as at
  3afc2f0 (proved below by per-game hashes) and `m45_bot_game.json` regenerates byte-identical. With several units on
  either side, the army path below (before M57 Hard played as Normal there).
- **The army value** (`Evaluator` in army mode, `evaluator.*`):
  - **Per-unit-pair exchange values:** M44's duel chain, one table per pair of unit types (a type is the
    representative model, the keywords and the starting strength: on the Strike scenario one table, Intercessor
    Squad against Termagants), built for the whole unit (every wound, from the half-turn of the first decision on) and
    cached as M44's tables are, so one build serves the game.
  - **Each unit's main enemy** (`Evaluator::main_enemy`, the real target choice): an enemy unit it is engaged with
    (the closest of them), else the closest enemy unit (01.04, D-010; a tie goes to the first in unit order).
  - **A pair's duel:** the chain of the pair (an attacker, a defender) at the pair's real position: both units' wounds
    left, the half-turn, the gap in 2" bands and the rooms to back away, with the half-turn in progress read as M44
    reads it (the engine's best_shooting for the pair's unit that still shoots, the charge, the fight). Two things
    differ from the duel's reading: the stage tests look at the pair's own units (a move or an attack in progress may
    be another unit's), and "engaged" means engaged with each other.
  - **Attention:** a unit can't fight everyone at once. Each unit deals with the enemy units closest to it first (its
    queue, by distance; engaged ones are the closest), so its duel with a further one starts only when it is free of
    the ones before. When that is comes from a timeline of each side's turns: every attacker works on its closest
    target still standing, once that target is in reach (its M plus its longest usable range, or a charge, in each of
    its turns; a turn spent closing takes nothing), at its pace (`volley_wounds`: one volley per turn, the better of
    its shooting at close range after a move and its fight, the volley's expected wounds against a whole unit),
    within a turn in unit order; what a volley has left when its target falls goes on to the next one, which it
    starts that turn if at least half the volley is left (else the next turn). So attackers share the work: a unit
    that three squads stand in front of is reached once they have wiped out together what is in front of it. A
    delayed duel starts from the chain at that half-turn, with the attacker having closed on its first target in the
    meantime (its M in each of its turns, never past it; the gap to this defender follows from where that leaves it),
    and models the approach to this defender itself.
  - **How it was found** (Release arena, Strike, seeds 1–200 per seat against Normal; the full tables are in PLAN.md
    M57). No attention (each unit's fate its duel with its main enemy alone): every Termagant unit looked surely
    wiped out (a squad alone kills 10 in time), so there was no point in keeping one back. Each attacker's queue
    on its own, the delay the sum of the time it would need alone for every unit ahead: too hopeful for a unit at
    the back (a 94% draw for the Tyranids in battle round 1 of some games, which they lose). The shared timeline
    without reach, then with it (the final one): the arena results of these last three are level within the noise
    (see below), and the last is the one whose reasoning holds.
  - **A unit's fate:** it is wiped out unless it survives every enemy unit's duel with it: P(wiped) = 1 − the
    product over the enemy units of (1 − P(that duel wipes it out)).
  - **Army-level P(win) − P(loss), still D-007:** a side loses only when every one of its units is wiped out. With the
    units' fates taken as independent, P(A wins) = the product over side B's units of P(wiped), P(B wins) the same over
    side A's units, and the draw is the rest (if the two add up to more than 1 they are scaled down to 1). V =
    P(win) − P(loss).
  - **Tempo and material (ChainWeights unchanged):** a side's win or loss comes when its last unit falls, so the score
    adds the tempo and material terms of each side's longest-lasting unit (the one whose duel with its main enemy is
    best for its own side), the mean of the two sides'. The first try took the mean over all units: a unit kept back
    a round longer then moved the score by about 0.006, below Hard's margin (0.01), so no plan ever changed for it.
  - **The end is exact:** the game over, a side without models (it lost, now) or the last half-turn played (a draw),
    scored as a duel's with material over the whole army's wounds.
  - **It reduces to the duel:** with one unit per side every unit's queue holds one enemy unit (no delay) and both
    units' longest-lasting duels are the duel, so the combination is the duel's own value, term by term
    (`M57_Evaluator.TheArmyValueOfOneUnitEachIsTheDuel`, through `force_army`), though that case keeps the duel path.
  - **The line (`kMaxLine`):** 24 → 38 buckets (76"). The army's tables use one fixed line, the battlefield's
    diagonal in 2" bands rounded up (the 60"x44" board: 74.4" → 38 buckets), so one table serves every pair. A pair's
    position on it: its real gap and the smaller of its two rooms exactly, the rest of the line to the larger room
    (the side with room to spare; M44's line was the gap plus both rooms, so it never had a rest). The duel keeps its
    own line, the gap plus both rooms; on the 24"x24" boards that is never more than their 34" diagonal (17
    buckets), so the new limit changes nothing there.
  - **Approximations kept:** the fates are independent; the timeline's paces ignore the attackers' own losses and
    the defenders' kiting before their duel starts (a delayed duel's defender stands where it is, and kites from then
    on, as the chain lets it); a unit's queue follows distance, not value; in the half-turn in progress a pair's unit
    shoots the pair's other unit (its real target may differ).
- **The lookahead** (`lookahead.*`):
  - **Plans:** the unit's top move plans are Normal's plan, the best `static_plans` M43 candidates that stand (as
    M44), and the move that ends furthest from every enemy model (`normal::furthest_standing_move`: with several
    enemy units M43's cheap score rarely ranks the keep-away move among its best few, and a probe that only kept away
    lasted longer against Normal's Marines than Normal's Termagants did); each valued with the army value, Normal's
    plan and the `plans` best others checked by the Monte Carlo, the blend and the margin as M44.
  - **Rollouts:** each plays the rest of the Movement phase (every other unit of ours), the rest of our turn and the
    enemy's whole turn with every enemy unit, on the bot's own dice (`rollout_seed`, as M44), to the start of our next
    turn, valued with the army value. The budget is M44's, counted in simulated actions (`rollout_actions` 400 per
    rollout, `budget_actions` 6000 per Monte Carlo), never time.
  - **The real target choice in the rollouts** (it replaces M44's first-enemy `enemy_of`): a unit's move is made
    relative to its rollout target, an enemy unit it is engaged with, else of the enemy units within its shooting
    reach next turn (its M plus its longest usable range) the one with the most models (a tie to the closest), else
    the closest; the chain's choice for that pair (`Evaluator::turn_choice(state, unit, target)`) says how far
    towards or away. The units move closest to an enemy first (Normal's order, D-066). Shooting is Normal's (M41:
    the most valuable unit and declaration, overkill avoided across targets). The first try, each unit at its closest
    target with best_shooting, fired full volleys at nearly-wiped units and walked every squad at a 1-model unit:
    the rollouts' Marines killed 3 Termagants in a turn where Normal's killed 9, so the lookahead was far too hopeful
    for the Tyranids. Charges: the closest eligible target (D-010), declared when that pair's chain wants it; the
    charge move is M44's (Normal's planned D-056 Auto move), with the closest target alone when that set is offered.
    Everything else is the baseline's, as M44.
  - A duel's rollouts are M44's, unchanged.
- **The decisions** (`hard_bot.cpp`): as M44, Normal's answer is the default, kept unless the army value (with the
  lookahead, for moves) is more than `BotWeights::margin` higher.
  - MoveUnits: the unit Normal would move (D-066's `unit_to_move`), so Normal's answer is for the same unit; its
    Remain Stationary, Normal move, Advance (before its roll) and Fall Back as M44.
  - Charge: every unit offered a DeclareCharge, at its closest eligible target (within 12", D-010): P from the 2D6
    covering the gap (11.02), worth P x the army value with the two units engaged and the charger striking first
    (11.04) + (1 − P) x the army value with it holding; ending the step, the army value after EndChargeStep.
  - SelectChargeTargets, OrderCasualties, RestoreCoherency and Consolidate: as M44 with the army value; the "front
    models first" and "rear models first" casualty orders are measured against the attacker (D-066's
    attacking_enemy) rather than the closest enemy unit.
- **No generation switch.** On several units Hard at 3afc2f0 was Normal (apart from an endgame reduced to one unit
  each), so the comparison is with `normal` itself; no `hard-m56` level was added.
- **Weights: `BotWeights{}` unchanged** (so the duel's are too). Release arena, Strike, seeds 1–200 per seat against
  Normal, with the army value as it stood then (v5 below), `--weights` one at a time: rollouts 10, share 0.7, margin
  0.005 and 0.02, static_plans 6, plans 5 all came out level with the defaults within the noise (draws as the
  Tyranids 0–4 of 200 against 2; mean end round as the Tyranids 3.63–3.73 against 3.695, as the Space Marines
  3.54–3.61 against 3.555), so none was taken. M58 tunes for several units.
- **Speed (D-054: budgets in counts, never time).** M44's budgets unchanged: 6 rollouts per plan, at most 400
  simulated actions per rollout and 6000 per Monte Carlo. A Strike rollout is 100–200 actions; the army's table (38
  buckets, Intercessor Squad against Termagants) costs about 80 ms in Release once per game and bot. Measured on the
  final build (Release, Strike, seeds 1–200, every bot decision): Hard against Normal p50 0.005 ms, p95 77 ms, max
  1.16 s; Normal against Hard p95 65 ms, max 0.48 s; Hard against Hard p95 116 ms, max 1.14 s (M57's targets: p95
  500 ms, max 3 s); over 1000 seeds of Normal against Hard the slowest was 1.15 s. The slowest decisions are a
  Tyranid unit's MoveUnits (10 models: more candidates, six advance results). `ChainTable::build_half_turn` was not
  needed and is unchanged (D-068: its sums' order fixes the duel's values). One speed-up was tried and dropped: the
  rollouts shooting one unit at a time (the first offered unit with a declaration worth anything) instead of Normal's
  most valuable first cut a decision by a fifth but made the lookahead's shooting unlike the real one (Hard's Marines
  finished later: mean end round 3.645 against 3.555). Debug ctest is kept quick by the tests' choice of games.
- **Measured** (Release arena, Strike 44x60; PLAN.md M57 has the tables). Seeds 1–200: Hard as the Space Marines wins
  all 200 against Normal, and sooner (mean end round 3.575, against 3.625 for Normal against Normal; wounds lost
  2.29 against 2.21); as the Tyranids it draws 3 (Normal against Normal: 0), mean end round 3.67 against 3.625.
  Seeds 1–1000, the Tyranid seat: Hard draws 11 against Normal's 2, mean result −0.989 against −0.998, mean end
  round 3.692 against 3.610 (standard error 0.019 each), 78 games reach battle round 5 against 54, 0.023 Termagants
  left on average against 0.003, Marine wounds lost 2.14 against 2.18 (level). How the army value grew (seeds 1–200
  per seat, Hard as the Tyranids / as the Space Marines, draws and mean end round): no attention, first rollouts (50
  seeds): 0 draws, 3.54 / 3.68; the rollouts' shooting and targets as Normal's: 1 of 50, 3.58 / 3.64; the keep-away
  candidate: 2 of 50, 3.62 / 3.64; attention per attacker, with its move: 3, 3.625 / 3.63; the last unit's tempo: 2,
  3.695 / 3.555; the shared timeline: 3, 3.675 / 3.560; with reach (final): 3, 3.67 / 3.575.
- **Proof that one unit per side plays the same:** the arena's `--per-game 1` lines (D-068) of a Release build of
  3afc2f0 and of the final build are identical on V1, Termagants first and 20 Termagants for hard/hard, hard/normal,
  normal/hard, hard/easy, easy/hard, normal/normal and easy/easy, seeds 1–200 (21 runs, 4200 games: result, counts and
  the hashes of the actions, the final state and the decision trace with every reason), every game finished and
  clean. On the Strike scenario normal/normal and easy/easy are identical too (Normal and Easy are untouched). The
  four replay regenerations leave `tests/replays` unchanged.
- **Open (M58):** the Tyranid seat's target (Hard winning 60% against Normal) is not reached: no Tyranid side wins a
  game on the Strike scenario at any level (their losses are almost all to shooting: about 14.5 Termagants in battle
  round 1, before they can act, then 13 and 10; they can deal about 2 wounds a game), so the measure there is the
  draw (D-007). Hard's Tyranids lose a model to coherency in 1 of 200 games (Hard against Hard: 3), Normal's in none:
  a casualty order the army value chose over Normal's in a unit about to be wiped out anyway.
- **Tuning (M58).** **Outcome: `BotWeights{}` unchanged**, and no army-only weight added, since no value was clearly
  better on Strike. The rule set for a change: clearly better on Strike beyond the noise, checked over 1000 seeds, and
  V1 and the two bot scenarios no worse. A default that changes 1v1 play would change `m45_bot_game.json`, so any
  change had to be scoped to the army path. With no change there was nothing to re-measure on V1, Termagants first or
  20 Termagants: their code paths are untouched, and the four replay regenerations leave `tests/replays` unchanged.
  - **What was swept.** M57 had already swept rollouts 10, share 0.7, margin 0.005 / 0.02, static_plans 6 and plans 5.
    M58 swept the weights it had left alone, the chain's tempo and material and M43's move weights, plus a larger
    lookahead budget, one at a time with `--weights`.
  - **The Tyranid seat** (Release arena, Strike 44x60, Normal as the Space Marines against Hard, seeds 1–200; the main
    target, M57's open item). Each row gives draws, then mean end round, then Marine wounds lost:
    - defaults: 3, 3.67, 2.12.
    - tempo 0.1: 1, 3.62, 2.15. tempo 0.4: 4, 3.65, 2.11.
    - material 0.02: 4, 3.71, 2.16. material 0.15: 3, 3.67, 2.27.
    - share 0.3: 1, 3.62, 2.08.
    - survival 2.0: 3, 3.65, 2.10. survival 0.5: 2, 3.66, 2.10.
    - delay 0.25: 3, 3.65, 2.15. edge 0.15: 2, 3.64, 2.08.
    - tempo 0.4 with material 0.02: 3, 3.65, 2.15.
    - rollout_actions 800 with budget_actions 12000: identical to the defaults in every count, because the budget
      never binds on Strike.
    - No Tyranid win anywhere.
  - **The Space Marine seat** (Hard against Normal, seeds 1–200). Hard won all 200 games in every run. Each row gives
    mean end round, then wounds lost:
    - defaults: 3.58, 2.29.
    - tempo 0.1: 3.62, 2.23. tempo 0.4: 3.50, 2.36.
    - material 0.02: 3.58, 2.25. share 0.3: 3.56, 2.28.
    - survival 2.0: 3.58, 2.29. edge 0.15: 3.58, 2.29.
    - delay 0.25: 3.62, 2.23.
  - **Over 1000 seeds** (the two values that looked best).
    - The Tyranid seat: material 0.02 gave 13 draws, mean end round 3.698 and 2.12 Marine wounds lost; tempo 0.4 gave
      13 draws, 3.663 and 2.17, with 13 Termagants removed for coherency. The defaults (M57, the same code) gave 11
      draws, 3.692 and 2.14. All are within the noise: one standard error is about 0.019 rounds and about 3 draws.
    - The Space Marine seat: tempo 0.4 gave 1000 wins, mean end round 3.533 and 2.19 wounds lost. The defaults gave
      999 wins with 1 draw, 3.550 and 2.31. The difference is under one standard error, and tempo 0.4 is worse in the
      Tyranid seat.
    - Every run of the sweep was clean: 0 rejected, 0 `UnitMoveReverted`, 0 fallbacks and 0 unfinished. Hard's p95
      stayed at most 83 ms and its slowest decision at 2.0 s (seeds 501–750 of Hard against Normal; under M57's 3 s).
  - **The bot-game replay** `tests/replays/m58_strike_bot_game.json` is Hard against Hard on Strike with dice seed 11.
    - Choosing the seed: the arena's `--per-game 1` run over seeds 1–200. Side A won all 200 games, clean. Games ended
      in battle round 2 (1 game), 3 (86), 4 (106) or 5 (7), with 183 steps on average and the longest at 294.
    - Seed 11 is 159 steps and a wipe-out in battle round 3. Both sides shoot and take casualties (the Space Marines
      lose 4 wounds), and there is a charge, pile-ins on both sides and a fight.
    - The shortest game, seed 12 (89 steps), has no charge and no Space Marine losses.
    - In a Debug build the whole game takes about 30 s, so `M58_Replay.TheSavedFileMatchesTheGame` checks the bots'
      actions through battle round 1 (64 of the 159 steps, about 14 s). The whole file is covered by the replay test
      and by `DISABLED_TheWholeSavedFileMatchesTheGame`.

## D-068: Speed-ups that keep every decision and roll the same (M54)
**Decided:** 2026-11-07 (M54 is pre-approved; numbered after D-065–D-067, which PLAN.md reserves for M55–M57).
Pure optimisation of core's read-only queries, the client logic the bots call and `src/ai`. No rule, state,
replay or JSON change; the engine API gains one overload. The test of "the same" is the arena's per-game report
(below): before and after builds give the same lines for every game compared.
- **What "the same" means here.** Every change keeps each function's answer bit for bit, not just "close": the
  same floats from the same expressions in the same order, the same reasons in the same order. Where a shortcut
  decides a comparison without the slow computation, it only decides cases a margin far larger than the rounding
  makes certain, and computes the original expression otherwise.
- **Geometry (core).** `centre_distance`, `base_radius`, `base_distance` and `bases_overlap` are inline (the same
  arithmetic; the non-round base error is thrown from an out-of-line function). `engaged_enemy_units`,
  `is_engaged` (stops at the first engaged enemy unit), `unit_distance` and `model_distance_to_unit` loop over the
  models instead of building `PlacedModel` lists (a minimum of the same `base_distance` values).
  `CoherencyWithOneMoved` answers 03.03 for one model tried at many spots from a table of the other models'
  distances: each spot's answer is `check_coherency`'s, from the same `within` tests on the same `base_distance`
  calls (both orders of each pair, as the full check measures them).
- **Shooting (core, 04.02, 06.01, 10.04–10.06).** One `shot_problems` call, and one `shot_options` walk, share their
  facts (`TargetFacts`): the enemy units the shooting unit is engaged with, whether a target is engaged, whether a
  target is visible to a model (06.01's blockers built once per observing and target unit). The checks and the
  reasons are unchanged and in the same order. `shot_options` (only a yes/no per declaration) checks range before
  line of sight and the target-free checks once per profile. New `has_shot_option` = `!shot_options().empty()`,
  stopping at the first; `legal_actions` at a Shoot decision uses it (D-004/D-016 templates unchanged).
- **Engine step (core, D-026).** New overload `apply_action(state, side, action, dice, backup)`: the same
  validation and rollback, with the rollback copy made by copy assignment into the caller's `backup`, so its
  strings and vectors are reused (about 3 µs instead of 15 µs per step on the strike state). The bot's
  `Simulator` keeps one; `Match::submit` keeps one as scratch (not copied with the match) and passes the
  `SeededDice(state.dice)` the 3-argument overload uses. `Simulator::reset(state, seed)` = a new simulator,
  reusing the copies (charge and advance odds, Normal's move candidates).
- **The auto moves (client logic, D-047, D-050, D-057).** `trimmed_region` drops the circles and shadows no point of
  the region can be in (further than its smallest `inside_all` circle reaches, with a 1 mm margin); `in_region`
  gives the same answer everywhere. `hypot_less(dx, dy, r)` is `std::hypot(dx, dy) < r`: false when
  `|dx|` or `|dy|` ≥ r (a faithful hypot is never below either), decided from the squares in double when they are
  more than 1e-6 apart relatively (hypot is accurate to well under that), and `std::hypot` itself otherwise (near
  the edge, r outside 1e-10..1e10, or not finite). `RayRefusals` marks the sample spots on rays from the start
  that `in_region` certainly refuses (inside a forbidden circle, or outside every circle of an `inside_any` group,
  by more than 0.05 mm against about 1e-4 mm of rounding in a spot's coordinates); the others still get
  `in_region`. `auto_fight_move` uses all three and the coherency table, works each angle's sine and cosine out
  once (the same expressions), finds the gap to the closest target from each base group's least squared distance
  (`base_distance` only grows with it) and rounds each candidate's gap once for the sort. `fight_move_skip` uses
  `trimmed_region` and `RayRefusals` too, `auto_charge_move` `trimmed_region`.
- **Instant bot speed (client, D-054).** Decisions now always run on the worker thread, so a slow one never
  freezes the window; at Instant speed a frame still takes up to 20 actions, waiting for the worker at most
  15 ms a frame (`BotDriver::wait_for_decision`). `--bot-steps` still decides on the calling thread.
- **Arena `--per-game 1`.** One line per game: result, end round, steps, survivors, wounds, coherency removals,
  fight-move counts, rejections, reverts, fallbacks, and FNV-1a hashes of the accepted actions (JSON), the final
  state (JSON) and the bot decisions' trace lines (reasons, which carry Hard's printed values; no times); then the
  decision times and the run's wall time. The same patch was applied to a Release build of `7536893` (M53) for the
  "before" lines.
- **Guard test.** `M39_NoRulesInTheBot` checks that `simulator.cpp` calls `apply_action(state_, side, action, dice,
  backup_)` (the bot's own dice, and now its reused backup) instead of the 4-argument form.
- **Not changed (open for M57):** the Hard chain table build (`ChainTable::build_half_turn`, about a quarter of a V1
  Hard game; its sums' order fixes its values), `has_line_of_sight` itself, Normal's move scoring.

## D-069: Random characteristics: A rolled per model and weapon when its batch is resolved, D after the model is selected (M62; replaces D-002, amends D-013 and D-051)
**Decided:** 2026-11-08 (M62 of the pre-approved M59–M70 batch). The wording and "one casualty order per batch" were
agreed with the user in planning; other ambiguities take the conservative choice. Written before any code (design
first), and placed before D-070 to keep the numbers in order.
- **Wording** ("Random Characteristics" is an app-only rule; supplied by the user, AI-sourced and **not verified against
  the app**, so it is a working assumption, as D-011 is):
  > **Attacks:** each time a model attacks with a weapon whose A is random, roll it for that model and weapon (Blast
  > bonus added after).
  > **Damage:** each time an attack with a random D is allocated to a model after a failed save, roll the damage for
  > that attack.
  > **Random damage keeps one casualty order per batch:** the defender still answers `OrderCasualties` once per batch,
  > and each damage roll lands on the next model the order (and 05.04's wounded-model-first rule) picks.
- **Scope: A and D only.** They are the only characteristics the unit loader reads as dice expressions (`DiceExpr`).
  Every other characteristic must be a whole number (a dice string there is a data error), and an ability with a
  random X (e.g. [SUSTAINED HITS D3]) stays an unknown ability, refused under D-071. So D-002's refusal is gone for A
  and D, and nothing else can be random.
- **Out of scope:** a random A on a melee weapon (refused, below; none in the data), and any other random
  characteristic (none can be loaded).
- **Rolling (01.05).** `roll_characteristic(dice, expr)` rolls one D6 per die, in order, halves each for a D3 (rounding
  up), and adds the modifier, giving `CharacteristicRoll{dice, value}` (the D6 faces and the result). A fixed value is
  never rolled, so a weapon with fixed A and D consumes exactly the dice it did before: every old replay and bot game
  is unchanged.
- **Random A (04.03 Gather Attack Dice).**
  - *When (agreed in planning):* when the batch is gathered for resolution. That is after the attacker selects it
    (`SelectAttackBatch`, or the only batch left) and before its hit rolls, not when the unit's attacks start.
    - `gather_attack_batches` (Select Targets facts, D-006 batching) leaves each random-A selection unrolled in
      `AttackBatch::random_attacks` (`{selection, model, A, bonus}`).
    - `attack_dice` counts only the dice already known: the fixed-A selections with their bonuses.
  - *Per model and weapon:* one roll per selection, in selection order. A model selects each weapon once, and a
    random-A melee weapon is refused (below), so a selection is one (model, weapon).
    - Two weapons with the same expression (e.g. two D6+3) are still identical attacks: A is not one of 04.03's
      compared characteristics (D-006). They share a batch, and each model rolls its own A.
  - *Bonus after the roll:* [BLAST] dice (24.05, per five models in the target at Select Targets) and [RAPID FIRE X]
    dice (24.30, target within half range at Select Targets) are added to that selection's roll. Neither depends on A,
    so the order only shows in the display, which follows the wording.
  - *Events:* `AttackDiceRolled{batch, rolls, attack_dice}` (04.03; each roll `{selection, model, A, dice, value,
    bonus}`), then `AttackBatchSelected` with the full count, then the hit rolls (05.01).
    - The batch's `random_attacks` is then emptied and its `attack_dice` holds the total. From there on the batch looks
      like a fixed one (allocation, saves, [TWIN-LINKED] re-rolls, D-072, as before).
  - *D-012:* a batch whose target is gone before it is gathered is lost unrolled. `AttacksLost` lists its unrolled
    random A (`random_attacks`) next to the known `attack_dice`; no dice are rolled for it.
  - *[TORRENT] with a random A* (flamers; the Tyrannofex's acid spray, M65) hits automatically with the rolled number of
    attacks.
  - *Melee: out of scope, refused (reviewed at the design stage).* 04.02 splits "that weapon's attacks" by declaring
    how many go to each target, which needs a known A, and no melee profile in the data has a random A. So a melee
    profile with a random A is refused as D-026 refuses an unimplemented ability: "D-069: '<profile>' has a random A
    (D3), which the engine does not implement for melee weapons (none in the data); it can't be selected".
    - `melee_options`, the Fight validation and the bots' `profile_supported` leave it out. A model whose only melee
      weapons are refused counts as unable to make melee attacks (D-026), and 24.11 treats it as absent (D-071).
    - The attack sequence's safety net still throws on one (a programmer error).
    - The design first proposed one target with `MeleeTarget::attacks` 0 for "all of them"; it was dropped at review
      (a sentinel value, CLAUDE.md, for a case the data doesn't have). A stand-in tests the refusal.
- **Random D (05.04 Resolve Damage).**
  - `inflict_damage` takes a `DamageRoller` (called with the profile's D). At each save roll that inflicts damage, in
    05.04's order (lowest first):
    1. the model is selected (05.04 Select Model: a wounded model if possible);
    2. the D is rolled for that attack: from the engine's `DiceSource` when `OrderCasualties` is applied;
    3. the model loses that many wounds.
  - A saved attack rolls nothing. When the model is destroyed, the excess damage is lost (05.04, Appendix: Destroyed).
    Attacks left after the unit is destroyed are lost without a roll.
  - `SaveStep::damage_roll` records the roll (`{dice, value}`); `DamageResult::damage` is the rolled value.
  - *Dice order:* the save rolls (05.03) as before; then, as the casualty order is applied, one roll per damaging step,
    lowest save roll first.
  - *One order per batch (amends D-013, the user's answer):* the defender still answers one `OrderCasualties` per
    batch. Each roll's damage goes to the model the priority picks at that step: the first legal model in the order,
    a wounded one forced. D-013's argument (no new information appears while 05.04 resolves) no longer holds for a
    random D; the user chose to keep one order anyway.
  - *Who rolls:* the dice cards name the attacking side as the roller, as for its hit and wound rolls (the D is its
    weapon's). This is presentation only.
- **The stand-in (amends D-051).** `casualty_decision_needed` and `preview_casualties` run 05.04 on a copy of the
  target with `damage_stand_in`: the expression's maximum (D6+1: 7; D3: 3). It never touches the dice.
  - *Why the maximum:* whatever the rolls, the models take damage in the same order, so the picks cover every model any
    roll could reach.
    - The order is: the picks, in pick order, a wounded model forced until it is destroyed (05.04 Select Model; at most
      one model per group is wounded at a time).
    - A lower roll only reaches the next model later, or not at all.
    - So with the maximum, D-051's "no model takes damage without being picked" still holds. The expected value would
      leave some reachable models to the default order.
  - The decision check gives the same answer with any stand-in: the first damaging save roll always selects a model.
  - *What the preview says:*
    - `CasualtyPreview::random_damage` (the D expression, when it is random): the preview's damage, `next_damage` and
      `damage_left` are maxima ("up to").
    - `provisional_from`: the results from that index on, and the next pick, are provisional. It is 1 once a random
      roll is in the results: if that roll is lower, the model it hit survives and takes the next attack instead.
  - *D-051's exactness:* "picks + the rest gives exactly the preview's results" still holds for a fixed D. For a random
    D, the models the resolution damages are always among the picks, in pick order (tested).
  - *Bots:* Normal's `casualty_order` and Hard's `decide_casualties` read the same preview, so they plan for the
    maximum. That is a documented approximation, and it only matters on the fixtures.
- **Weapons this makes usable** (no other refused ability):
  - *In the rosters:* the Havocs' Havoc lascannon (D D6+1) and the Screamer-killers' bio-plasmic scream (A D6+3,
    [ASSAULT], [BLAST]).
  - *Datasheet options and fixture variants:*
    - the Hive Tyrant's heavy venom cannon (A D3, D3, [BLAST]; the m60 replay's Tyrant carries one) and stranglethorn
      cannon (A D6+1, [BLAST]);
    - the Havoc missile launcher's frag (A D6, [BLAST]) and krak (D D6) profiles;
    - the Legionaries' lascannon (D D6+1, [HEAVY]) and missile launcher frag and krak ([HEAVY]);
    - the Termagants' shardlauncher (A D3, [BLAST], [HEAVY]);
    - the Tyrannofex's rupture cannon (D D6+6, [HEAVY]) and acid spray (A D6+6, [TORRENT]), from M65.
  - *Still refused for another ability:* meltaguns ([MELTA 2], D-071), flamers and the heavy flamer ([IGNORES COVER],
    D-071), and the strangleweb ([DEVASTATING WOUNDS], D-026).
  - None of these is equipped in V1, the two bot scenarios or Strike (a test checks), so their play is unchanged.
- **The refusal is removed** (for ranged weapons, and for a random D in melee).
  - The D-002 checks are gone from `shot_problems` / `profile_selectable` and the bots' `profile_supported`. The
    fight's `declaration_problem` / `usable_melee_profile` refuse only a random A (above), with the D-069 reason.
  - `DiceExpr::fixed_value` still throws (a programmer error). Each caller checks `is_fixed()` first or works on known
    fixed data:
    - `gather_attack_dice` requires a fixed A (a random A goes through `selection_dice` and the batch);
    - `inflict_damage` with a random D and no roller is a programmer error.
- **`resolve_attack_batch`** (the pure 05 function the bots sample with) follows the engine's dice order: a batch's
  random A, the hit and wound rolls, [TWIN-LINKED] re-rolls, the save rolls, then the damage rolls. With the same dice it
  gives the engine's result (tested).
- **JSON.** New fields are omitted when they hold their default, and the new event is appended to its variant, so every
  old file and the bots' state hash are unchanged.
  - `AttackBatch`: `"random_attacks": [{"selection", "model", "A", "bonus"}]`.
  - Event `{"kind": "AttackDiceRolled", "rule": "04.03", "batch", "rolls": [{"selection", "model", "A", "dice", "value",
    "bonus"}], "attack_dice"}`.
  - `AttacksLost`: `"random_attacks"`.
  - `SaveStep`: `"damage_roll": {"dice", "value"}`.
  - A save can stop between the start of the attacks and an A roll, or at a casualty decision before its damage rolls.
- **Client.**
  - *Dice cards:*
    - "Attack dice" (04.03), before the hit rolls, one line per model: "Bio-plasmic scream: D6+3 -> 4+3 = 7, +2
      [BLAST] = 9".
    - "Damage rolls" (05.04, D-069), after the casualties are confirmed: per attack that inflicts damage, its dice,
      damage and model ("4+1 = 5 -> model 23", "(destroyed, 2 lost)").
    - The hit card's "N attack dice" counts the rolled dice.
  - *Text:*
    - Log text for `AttackDiceRolled`, the damage rolls in `SavesResolved` and an unrolled `AttacksLost`.
    - `is_roll_event` includes `AttackDiceRolled` and a `SavesResolved` with a damage roll.
  - *Expressions where the client counts attacks:* a random A shows its expression: the log's start-of-attacks line
    ("4 + D6+3 attack dice"), lost attacks, and an attack-dice line in the weapon menus' effects.
  - *Help:* "Random A (D6+3)" and "Random D (D6+1)" entries citing D-069, in the weapon menus' tooltips and the
    datasheet window (hovering A or D).
  - *Casualty picking:*
    - The hint names the expression ("the next D6+1 damage (up to 7)") and, once the next pick is provisional, says
      "Thin rings are provisional."
    - The box reads "Damage left to allocate: up to N (rolled when you confirm)".
    - A mark from a stand-in reads "-7?", and the provisional marks have a thin ring.
    - The board still offers only the preview's options.
    - *Screenshots:* `--pick-models <id,...>` picks those models in the first casualty decision, as clicks would, and
      submits nothing (like M61's `--pick-dice`).
  - *Undo:* the damage rolls are part of the `OrderCasualties` answer, so Undo restores the dice position (D-025) and
    the same order gives the same rolls. A different order can put the same rolls on other models (as D-072 notes for
    re-rolls).
- **Bots.**
  - `profile_supported` lets random A and D through.
  - *Odds (`analysis/odds.cpp`):*
    - A `VolleyBatch` carries its random A expressions (`random_attacks`); their bonus dice are in `attack_dice`.
    - `expected_outcome` mixes exactly over A: the fixed dice first, then one die at a time up to the most the random A
      can give, each count weighted by its probability (the convolution of the expressions).
    - It mixes exactly over D too: each damaging transition branches over the D's values.
    - Both distributions come from core's `roll_characteristic` on every face of one die, then convolution.
    - Fixed A and D take the old code path, with the same arithmetic.
  - `sample_outcome` resolves through `resolve_attack_batch`, which rolls the random A and D with the bot's own dice
    (D-054), never the game's.
  - *Other places that assumed a fixed A or D:*
    - Hard's chain tables multiply the random expressions with the models (n copies) as they do the fixed dice.
    - Normal's batch order, when there are too many batches to permute, sorts by mean damage (a fixed D sorts as
      before).
    - Threat, volley and melee values come from `expected_outcome`.
    - `representative` compares loadouts, not values, so it needs no change.
  - *Cost:* only batches with a random value pay. A random A adds one die step per possible extra die (D6+3: up to 9
    instead of a fixed count). A D6-based D makes each damaging transition six branches.
  - *Easy's default plans (D-071)* now fire the Havocs' lascannons. That changes Easy on the fixtures only.
- **This batch's replays.**
  - *`m59`:* its steps, dice and expectation are unchanged. Its generator's check that nothing can shoot in Side A's
    first Shooting phase now excludes the Havoc lascannons, which reach the Tyranids (48") and don't fire. Its
    description said "nothing is in range to shoot", written under D-002; at review it was corrected to say that only
    the lascannons are in range and Side A ends its Shooting phase without firing. That one field is the only change
    to the file.
  - *`m60`:* the Tyrant's heavy venom cannon is now usable, but the replay answers its Shoot step with `EndShootStep`.
    The file is byte-identical.
  - *`m61`:* the Tyrant has no ranged weapon. The file is byte-identical.
  - *New `m62_random_characteristics.json`* (seed 10, embedded state): the Havocs vs Hive Tyrant fixture with the m60
    heavy venom cannon.
    - The four lascannons fire at the Tyrant (W10). Two saves fail, so Side B orders the casualties once, then the two
      D6+1 rolls (2, then 6 on the wounded Tyrant) are made; it survives on 2 wounds.
    - The cannon's D3 is rolled (a 1), +1 [BLAST] for the five Havocs: 2 attack dice.
    - It stops at Side B's first Charge decision.

## D-070: Fetch tool fixes, the nine roster datasheets and the roster fixtures (M59)
**Decided:** 2026-11-08 (M59 of the pre-approved M59–M70 batch; ambiguities take the conservative choice).
`tools/fetch_bsdata.py` now reads the nine roster units (Hive Tyrant, Genestealers, Tyrant Guard, Screamer-killer,
Tyrannofex, Chaos Lord, Legionaries, Chaos Terminator Squad, Havocs) correctly from BSData `0f90355`, and never
crashes on a datasheet it can't read. The engine's unit loader reads the two new things the files can hold.
- **Fetch rules (02.02, 02.04, 02.06, 02.07).**
  - *Composition* comes from the model types and the groups that hold them. A group's own min/max bounds the sum of
    what it holds; a model outside the group (a champion) adds to it. Legionaries are 1 + (4–9) = 5–10, Chaos
    Terminators 5–10, Havocs 1 + 4 = 5–5, Genestealers 5–10 and Tyrant Guard 3–6 (their constraints sit on the model).
    Intercessors (5–10) and Termagants (10–20) come out as before. A unit that is a single model is 1–1, and so is
    its model type. A composition the tool can't find is written as null with a warning (the loader refuses it).
  - *Wargear* is read at any depth: groups and entries are walked, also through entryLinks to groups. The Crusade,
    Weapon Modifications, Enhancements and Crusade Relic Upgrades subtrees are skipped (Crusade holds real weapon
    profiles such as Vortex Grenade). Weapon profiles reached through `infoLinks` of type "profile" are read
    (Boltgun, Bolt pistol, Astartes chainsword, Accursed weapon, Combi-bolter, Plasma gun, Heavy melee weapon, ...).
  - *Default weapons:* a node is in the default loadout when its parent is and it is a min-1 entry, or it is the
    default entry (`defaultSelectionEntryId`, else the first entry) of a required (min-1) group. Groups without a
    min are plain containers. This gives the Hive Tyrant, Chaos Lord and every champion a loadout (the Terminator
    Champion's is three levels deep). The datasheet default is not the roster's loadout in three places (Chaos
    Lord: daemon hammer, not chainblade; Aspiring Champion: boltgun, not chainsword; Havoc Champion: flamer, not
    plasma gun). That is a wargear choice (02.07), so the fixture scenario lists the roster loadouts.
  - *Weapon type per profile:* each profile's own Ranged/Melee type. When a weapon mixes the two (none of the
    roster's do), every profile gets a `"type"` field; the weapon's `"type"` stays the first profile's, as before.
    Files without mixed weapons are unchanged.
  - *Characteristic modifiers:* BSData changes some characteristics with modifiers on shared profiles. The tool
    applies a `set` modifier when all its conditions are `instanceOf` / `notInstanceOf` an ancestor that is the unit,
    its model or one of their categories. The Astartes chainsword is A5 in a HAVOCS unit (the roster agrees) and A4
    for Legionaries. Any other modifier of a characteristic (e.g. `append`, roster-wide conditions) is reported as a
    warning and not applied. None of the roster units' profiles has one.
  - *Abilities from an Abilities profile* behind an infoLink ("Damaged: 1-5 Wounds Remaining", "Will of the Hive
    Mind", "Lord of Chaos") now get their Description text; it was empty before. Everything else about keywords,
    InSv and abilities is as before (hidden-ability modifiers are not evaluated, see the roster differences).
- **Bases.** "32mm" / "28.5mm" are round. "AxBmm" is an oval: `{"shape": "oval", "diameter_mm": B, "length_mm": A}`
  with B the width (the smaller number) and A the length (the larger); "120x92mm" is `diameter_mm` 92, `length_mm`
  120. `Base` keeps that meaning (`diameter_mm` = width across the short axis). Any other text ("Hull", "~40mm",
  "32mm Flying", "Unique", "-") is written as `"base": null` with a warning. The loader refuses a null base
  ("D-070: base is null") and an oval whose `length_mm` is missing or smaller than its `diameter_mm`.
  `scenario_loader.cpp` still refuses non-round bases at placement until M65.
- **Conditional profiles.** A profile named "... - Hunter: X" (only the Space Marine Chainfist at this commit) is
  written with `"condition": "Hunter: X"`. The loader leaves it out of the weapon's profiles, so it can never be
  selected, and lists it in `Datasheet::conditional_profiles`. A weapon with only conditional profiles is left out
  too, so a scenario that equips it is refused (02.04). This is the conservative reading: the condition (attacks
  against a MONSTER or VEHICLE) is not implemented.
- **Unreadable data never crashes the run.** Other base strings, a missing model max or profile, or a missing
  composition produce a warning and a file the loader refuses. Checked on about twenty other units from the three
  catalogues (Land Raider, Gargoyles, Chaos Rhino, Cultist Mob, ...).
- **The V1 files.** Re-running the tool on `intercessor_squad.json` and `termagants.json` gives byte-identical
  content apart from the `fetched` date. So the files were left as they are, and every replay and bot game stays
  the same.
- **Roster differences (New Recruit export vs BSData; BSData wins).** Every stats row (M, T, Sv, W, Ld, OC, InSv,
  base) and every weapon profile with its abilities in the two rosters matches the generated files, including the
  Havoc Champion's A5 chainsword (through the modifier above). The differences are all outside the engine's scope:
  - *Keywords:* BSData gives the Chaos Lord and the Hive Tyrant a LEADER keyword, which the rosters' categories don't
    list.
  - *Abilities:* "Leader" appears twice for the Chaos Lord and the Hive Tyrant (an ability profile and the core rule).
    Deadly Demise and Scouts lose their value ("Deadly Demise D3", "Scouts 8\""), which BSData appends through a
    modifier the tool doesn't apply. The Genestealers list Stealth, which BSData hides unless Chameleonic is taken;
    the roster doesn't show it. Ability text is display-only (unit abilities are out of scope for the batch).
  - *Composition* is not in the roster exports. The BSData values above match the datasheets (Legionaries 5–10,
    Havocs 5, Terminators 5–10, Genestealers 5–10, Tyrant Guard 3–6).
- **Fixture scenarios** (`tests/data/scenarios/`, not under `data/scenarios`, so the client and the package don't
  list them):
  - `rosters_no_tyrannofex_44x60.json`: both rosters except the Tyrannofex, with the roster loadouts, on the 44"
    wide by 60" deep board. The zones are 12" deep on the short south (A, Chaos Space Marines, first turn) and north
    (B) edges, 36" apart. It has 13 units and 72 models. Formations (D-008, D-064) put the 10-model units in two
    ranks; the second Genestealers unit has `depth_in` 3.
  - `havocs_vs_hive_tyrant_24x24.json`: the Havocs 8" from the Hive Tyrant, for focused tests of their weapons.
- **Weapons the engine can't resolve yet.** On the fixture these are refused by validation with a rule-cited reason,
  never by the engine throwing into the D-026 rollback:
  - random A or D (D-002): Havoc lascannon, Bio-plasmic scream, Heavy venom and Stranglethorn cannons. *Since M62
    (D-069)* these are rolled, so they can be used;
  - [TWIN-LINKED] (D-026): Monstrous bonesword and lash whip, Crushing claws.
  - *Accepted silently until M60:* Unknown abilities and [HAZARDOUS] are loaded and ignored. In the fixture's
    loadouts these are the Monstrous scything talons (Extra Attacks) and the plasma pistol / plasma gun supercharge
    profiles. Random play fires and fights with both.
  - Outside the fixture, the Tyrannofex's Fleshborer hive (Sustained Hits 1) is also an Unknown ability.
- **Replay** `m59_rosters_fixture_round1.json` embeds its initial state (`initial.state`) rather than naming the
  fixture, so the client's replay list and the package load it without `tests/data`. It is larger than the others
  (about 210 KB).

## D-071: Unknown abilities refused; [HAZARDOUS], [SUSTAINED HITS], [EXTRA ATTACKS]; mixed-loadout default plans (M60; extends D-026)
**Decided:** 2026-11-08 (M60 of the pre-approved M59–M70 batch; ambiguities take the conservative choice).
- **Unknown abilities are refused (extends D-026).**
  - Any weapon profile with an ability the engine does not recognise (`ability::Unknown`: [LETHAL HITS], [ANTI-X Y+],
    [MELTA X], [LANCE], [PRECISION], [IGNORES COVER], [PSYCHIC], [INDIRECT FIRE], a keyword-conditioned or random-X
    form such as [SUSTAINED HITS D3], ...) is refused when it is selected, like [TWIN-LINKED] and [DEVASTATING
    WOUNDS]. The reason is `"D-071: '<profile>' has [<ABILITY TEXT>], which the engine does not implement yet;
    weapons with it can't be selected"`.
  - `unimplemented_ability_problem` gives it, so `shot_options`, `has_shot_option` (the Shoot templates),
    `melee_options` and the bots' `profile_supported` all leave such profiles out. The attack sequence's second
    safety net throws on one.
  - A model whose only melee weapons are refused counts as unable to make melee attacks (as D-026). [IGNORES COVER]
    is refused too, although V1 has no cover: Stealth-like rules aren't implemented either, so it is not "known to do
    nothing".
  - No weapon equipped in V1, the two bot scenarios or Strike has such an ability (checked by a test), so their play
    is unchanged.
  - *Old files:* JSON written before M60 holds "Extra Attacks" and "Sustained Hits 1" as `{"kind": "Unknown", "text":
    ...}`. Reading an Unknown ability parses its text again, so such a file gets the ability the text now names.
    Text the engine still doesn't know stays Unknown.
  - *Replay m59:* its embedded initial state is written again by the regeneration with `{"kind": "ExtraAttacks"}`
    for the scything talons (the only byte change; its steps and expectation are identical).
- **[HAZARDOUS] (24.15, 06.03, 06.02).**
  - *Count:* the number of [HAZARDOUS] weapons selected in the Select Weapons step. That is the distinct (model,
    weapon) pairs whose selected profile has the ability, so a melee weapon splitting its attacks counts once. It is
    recorded when the attacks start (`AttackFlow::hazard_rolls`).
  - *When:* once every batch is resolved or lost (D-012), the rolls are made together (06.03 "simultaneously"),
    event `HazardRollsMade`. A unit whose target was destroyed early still rolls. This applies to shooting and
    fighting alike.
  - *Mortal wounds:* 1 per failed roll (1-2), or 3 if every model in the unit is a MONSTER/VEHICLE. They go in
    `AttackFlow::pending_mortal_wounds` (JSON omitted when 0, as is `hazard_rolls`). The attacking unit's player
    answers `OrderCasualties{attacker}` (06.02 Select Model, D-013), always asked even when forced (D-051). The
    D-051 preview follows them.
  - *Telling the casualties apart:* `OrderCasualties` during an attack with pending mortal wounds is 06.02 for the
    attacker; otherwise during an attack it is 05.04 for the batch's target; outside an attack it is Desperate
    Escape's (09.07).
  - *Self-wipe:* a unit destroyed by its own hazards ends its activation normally. Shooting goes back to the Shoot
    decision; a fight goes on to the next unit (12.04). If that leaves a side with no models, D-007 ends the game.
  - *D-007 first:* if no enemy model is left when the attacks are resolved, no hazard roll is made. The game is over
    "after the current attack sequence"; this also keeps both sides from being wiped out at once.
  - The bots' odds do not price the hazard (M68 adds its cost), so Normal and Hard may pick supercharge.
- **[SUSTAINED HITS X] (24.36, 05.01).**
  - Each critical hit (an unmodified 6; D-011) scores X additional hits, which are not critical and have no roll.
    They are kept in `HitRolls::sustained_hits` (JSON omitted when 0); `hits()` includes them and `rolled_hits()`
    doesn't. One wound roll is made for every hit (05.02).
  - [TORRENT]'s automatic hits are not critical. A modified 6 is not critical.
  - With two instances on one profile (24.02, the player selects one) the highest X is used: it is always the
    controlling player's best choice.
  - It is an attack-time ability (D-006), so it splits identical attacks.
  - *Bots:* the exact odds mix over 0, 1 or 1 + X hits per die and a one-wound step per wound. A profile without it
    takes the old code path unchanged, so old play's numbers are bit-identical.
- **[EXTRA ATTACKS] (24.11, 04.01; amends D-021).**
  - *What must be selected:* a model with [EXTRA ATTACKS] weapons must select all of them and exactly one other melee
    weapon if it has one. A weapon is an [EXTRA ATTACKS] weapon if one of its melee profiles has the ability. Each
    weapon is selected once; to attack several units it splits its attacks (04.02).
  - *Refusals:* missing an [EXTRA ATTACKS] weapon, missing the other weapon, two other weapons, or one weapon twice.
    A model without [EXTRA ATTACKS] weapons keeps 04.01's "one melee weapon, not more" message.
  - *"If possible" (conservative):* a weapon the engine refuses (D-002 random A/D, D-026, the Unknown abilities above)
    counts as absent. So while the Hive Tyrant's bonesword is refused ([TWIN-LINKED], until M61), the talons alone
    are its legal selection, and with the bonesword the declaration is refused for the bonesword. Once M61 makes the
    bonesword selectable, the Tyrant must take both. A model with only [EXTRA ATTACKS] weapons selects those alone.
    *Since M61 (D-072)* the bonesword is selectable, so the Tyrant takes both; the tests of the "refused weapon
    counts as absent" case use a stand-in bonesword with an ability the engine doesn't know.
  - The ability is used up at selection, so it is excluded from D-006's attack-time abilities, like [ASSAULT].
  - `is_extra_attacks_weapon` (rules/attacks.hpp) lets clients and bots build the selection from `melee_options`.
- **Default plans (client, D-001 / D-016 UI rule; used by the Shoot and Fight buttons, the board menus, `--steps`,
  level Easy and the bots' fallback).**
  - *Shooting, single loadout:* a unit whose models all carry the same ranged weapons (every V1, bot-scenario and
    Strike unit) keeps the M12 default exactly. That is the first option as a unit-wide choice, and a model that
    can't make it doesn't fire. Old plans and the Easy hashes are unchanged.
  - *Shooting, mixed loadout:* there is no one choice that suits a mixed loadout (Havocs: lascannons and a champion's
    plasma gun; boltgun Legionaries: a champion with only a bolt pistol), so by default each model fires its own
    weapons.
    - It aims at the unit default's target, or at the first target it can reach if none of its weapons reaches that.
    - Each weapon's first non-[HAZARDOUS] profile is used, at that target if the weapon reaches it, else at its own
      first target.
    - 24.07 applies to a non-MONSTER/VEHICLE model outside close-quarters shooting: it keeps the set (its
      [CLOSE-QUARTERS] / [PISTOL] weapons or its other ranged weapons) with more weapons reaching the target; a tie
      goes to the other ranged weapons. So a Legionary fires its boltgun, not its pistol.
    - A unit-wide choice the player picks on the board still goes to every model that has it, and the other models
      of a mixed loadout fire their own weapons at its target.
  - *Never [HAZARDOUS] by default:* the unit default is the first option. If that is [HAZARDOUS], it becomes the
    same weapon's first other profile at the same target, else the first option that isn't. With none, there is no
    default and Easy doesn't shoot with the unit. BSData lists "Plasma pistol - supercharge" first for the
    Intercessors; the order is not relied on. In melee, where D-021 makes every able model fight, a [HAZARDOUS]
    profile is used only if the model has nothing else.
  - *Melee:* each model's main weapon is its override, else the unit default if it is one of its options, else its
    first option at the unit default's target, else its first option (non-[HAZARDOUS] first). For a model with
    [EXTRA ATTACKS] weapons, the main weapon is picked from its other weapons (or from the [EXTRA ATTACKS] ones if
    it has no other it can use). The rest of its [EXTRA ATTACKS] weapons are added at the main weapon's target where
    they reach it. For single-loadout units this is the M12 plan exactly.
  - *Editors:* the per-model editors still pick one weapon per model; the other declarations a model makes are listed
    under it.
- **Bots.**
  - Easy uses the client plans above.
  - Normal's melee plan, `best_melee` and `unit_melee_volley` declare (or count) every [EXTRA ATTACKS] weapon with
    the model's best other weapon. Hard plays Normal's melee.
  - A model without [EXTRA ATTACKS] weapons goes through the old code with the same arithmetic, so old play is
    identical.
  - *Fixed in passing:* Normal's melee candidates (`normal/fight.cpp`, `melee_candidates`) kept references into a
    vector they were growing while adding attack splits. Once a split reallocated it, the next split was copied from
    freed memory: undefined behaviour. In Debug it crashed a Normal game on the roster fixture; in the Release
    arena it silently changed which splits Normal considered.
    - The candidates are copies now, so every split is offered as intended.
    - This changes 3 of the 200 Strike Normal-vs-Easy arena games (seeds 23, 65 and 150), where an Intercessor
      engaged with two Termagant units now weighs a split. With the two lines reverted, every M60 change leaves all
      24 arena pairings identical to the pre-batch baseline.
    - The fix was kept because the old code can crash. It is the only intended change to old bot play.

## D-072: [TWIN-LINKED]: the attacker picks the wound rolls to re-roll (M61; amends D-026, applies D-011)
**Decided:** 2026-11-08 (M61 of the pre-approved M59–M70 batch). The player's choice, asked only after a failed wound
roll, and the "re-roll all failed" shortcut were agreed with the user in planning. Other ambiguities take the
conservative choice.
- **Rule.** 24.38: "Each time an attack is made with a [TWIN-LINKED] weapon, you can re-roll the wound roll." Re-rolls
  follow D-011:
  - the raw die is rolled again, then the batch's capped wound modifier is applied;
  - a die is re-rolled at most once (the decision comes once per batch, and an index can't be listed twice);
  - a critical wound still needs an unmodified 6 (05.02), which is now the re-rolled result;
  - an unmodified 1 still fails.
  The re-rolled result replaces the first. Hit rolls are not re-rolled. Two instances of [TWIN-LINKED] on one profile
  are one ability (24.02).
- **When it is asked (agreed with the user).** After a batch's wound rolls (05.02), when its profile has
  [TWIN-LINKED] and at least one of them failed.
  - "Failed" is the 05.02 table's result for the die: an unmodified 1, or a modified result below the required roll.
    An unmodified 6 never fails.
  - When every wound roll succeeded, or there were no hits, nothing is asked and the batch goes straight on, with the
    same dice as before M61.
- **Who answers.** `decision::RerollWoundRolls{attacker}` belongs to the attacking unit's player. That player is the
  active player while the unit's attacks are resolved (01.03). In the Fight phase it can be the player whose turn it
  is not.
- **One answer per batch, any subset.** A batch is one weapon profile's identical attacks at one target (04.03,
  D-006). [TWIN-LINKED] is an attack-time ability, so a batch is all Twin-linked or not at all. Its wound rolls are
  made together (05: "make all of those rolls simultaneously"), so the player sees them all, then answers once with
  `action::RerollWoundRolls{indices}`.
  - `indices` is any set of distinct indices into the batch's wound rolls, in any order. That includes successful
    rolls, since 24.38 lets the wound roll of every attack be re-rolled, and no index at all ("Keep").
  - *D-004 extension (as D-013 did for casualties):* `legal_actions` lists two fully specified answers: every failed
    wound roll first, then none. Every other subset is valid too, but the 2^n subsets are not listed. The D-004 random
    play also tries random subsets.
  - *Refused, with rule-cited reasons:* an index out of range ("24.38: ... made N wound rolls") and an index listed
    twice ("D-011: ... can't be re-rolled more than once").
- **Dice order.** First the hit rolls and wound rolls, as before. Then, only when the decision was asked and answered,
  one D6 per chosen die, in ascending index order. Then 05.03 (allocation order, save rolls). The batch goes on with
  the final wound rolls; with no wound left it is done. Weapons without the ability keep exactly the old dice order.
- **Engine.**
  - `start_batch` (`engine/attack_flow.cpp`) is split after the wound rolls. When the decision is needed, the rolls
    wait in `AttackFlow::wound_rerolls` (`{batch, rolls}`), and `attack_decision` asks first. Otherwise, and after the
    answer, `after_wound_rolls` goes on (allocation groups, or the batch is done).
  - The answer emits `WoundRollsRerolled{batch, rerolls, rolls}`, even for "Keep" (an empty list, so the log shows
    the choice). `rerolls` holds each re-rolled die's index and its roll before and after. `rolls` holds every wound
    roll of the batch afterwards. The original `WoundRollsMade` event is unchanged.
  - `reroll_wound_rolls` (`rules/attack_sequence`) re-rolls the chosen dice. It is shared with `make_wound_rolls`'s
    per-die result, so a re-rolled die is judged exactly as a first roll. `failed_wound_rolls` gives the failed
    indices.
  - `resolve_attack_batch` (the pure 05 function the bots sample with) takes the attacker's choice as an input
    (`WoundRerollChooser`). It is called exactly when the engine would ask. Without one, a [TWIN-LINKED] batch with a
    failed wound roll is a programmer error.
- **[SUSTAINED HITS] with [TWIN-LINKED]** (the Tyrannofex's fleshborer hive, M65): every wound roll of the batch can
  be re-rolled, the extra hits' included.
  - 24.36 says the critical hit "results in a number of additional hits on the target". They are hits of the same
    attack, made with the Twin-linked weapon, and 05.02 makes one wound roll for each hit.
  - Reading "the wound roll" as only one per attack would leave a choice of which of that attack's rolls. No rule
    describes that choice, and neither the rules nor the engine ties a wound roll to one attack die.
- **D-012.** The re-roll comes before allocation, and nothing between the wound rolls and the answer can remove the
  target. No change.
- **JSON** (new fields omitted when default, so old files and replays are byte-identical):
  - `AttackFlow`'s `"wound_rerolls": {"batch", "rolls"}` is written only while the decision waits;
  - the decision is `{"kind": "RerollWoundRolls", "attacker": n}`;
  - the action is `{"kind": "RerollWoundRolls", "indices": [...]}`;
  - the event is `{"kind": "WoundRollsRerolled", "rule": "24.38", "batch", "rerolls": [{"index", "before", "after"}],
    "rolls"}`.
  - The new decision, action and event are appended to their variants, so the bots' state hash (which uses the
    decision's index) and every old file are unchanged.
  - A save or replay can stop at the decision and loads back to it.
- **Rule citations** (with M60's carry-over).
  - The decision and the action cite 24.38.
  - `decision_rule(state, decision)` and `describe(state, decision)` tell `OrderCasualties` apart: 06.02 when the
    attack flow has pending mortal wounds ([HAZARDOUS], 24.15) or there is no attack flow (Desperate Escape, 09.07);
    otherwise 05.04.
  - `validate_action`'s messages, the client's headline and its "Waiting for" line use them.
  - The one-argument forms are unchanged.
- **Client.**
  - *Action bar:* the decision panel's headline, a line saying how many wound rolls failed and what they needed, and
    the two legal buttons: "Re-roll all failed (n)" first, so `--steps` and the bots' fallback pick it, then "Keep".
    "Re-roll selected (k)" appears once the picked dice are other than every failed one. The bar's headline is a short
    "Re-roll wound rolls? ([TWIN-LINKED], 24.38)": `describe`'s full text doesn't fit beside the buttons.
  - *Pop-up:* once the wound card has closed, the attacker's wound dice show over the board (`RerollPicks` in
    `wh40k_client_logic`, drawn in `src/client_2d`). Failed dice are outlined, and a click toggles a die (any die, a
    success too). Its buttons are "Re-roll all failed", "Re-roll selected" and "Keep". It is shown only for a human's
    decision in play, never in the replay viewer.
  - *Text:*
    - The wound card notes that a Twin-linked attacker may re-roll any of the dice.
    - The hit card's ability lines include "Twin-linked: the wound rolls can be re-rolled".
    - A dice card "Wound re-rolls" (24.38). Before the roll it names the re-rolled dice and what they were ("Re-rolling
      die 2 (a 1)"); after it, their new faces, old -> new per die and the new totals. "Keep" has no card.
    - Log text and action text cover every answer.
    - The help text says 24.38 is in the game.
  - *Bots' dice:* a bot's re-roll card follows the M48/M49/M51 pacing like its other cards.
  - *Undo:* the answer is one player input, so it is its own Undo group (D-039), like an allocation order. Undo goes
    back to the decision with the dice position as it was (D-025), so the same answer gives the same dice.
    - After an undo, a different answer can see the same next dice land on other dice. Hotseat Undo has no permission
      model (D-025), as for every other roll.
    - Against a bot, Undo goes back to the human's last input (D-054).
  - *Settings:* "Allocate damage automatically" and "Skip steps with nothing to do" never answer the decision. It is
    the attacker's choice, like the batch order (D-036, D-038).
  - *Screenshots (agreed at review):* `--steps` can't reach a human's re-roll decision (the Shoot and Fight
    declarations are templates). `--load <save.json>` starts the client on a saved game (the sidebar's Save / Load
    format). It can't be combined with `--scenario`, `--replay` or `--seed`; the save's players apply unless `--bot` /
    `--bot-level` are given; a missing or unreadable file, or one that fails to load, exits with code 2.
    `--pick-dice <i,j,...>` picks those wound dice (0-based) in the first re-roll pop-up, as clicks would, and submits
    nothing. The flags are parsed in `wh40k_client_logic` (`launch_options`, moved there from `main.cpp`).
- **Bots.**
  - *Every level re-rolls every failed wound roll and never a success.* No critical-wound effect is in the game, so a
    re-rolled success can only fail and a re-rolled failure can only succeed.
    - Easy answers it (`easy_policy`). Normal and Hard fall through to Easy, so the lookahead's rollouts (which use
      `easy_policy`) answer it on their scratch copies too.
    - `fallback.cpp` prefers the same answer, and the driver gives it the 0.4 s step pause.
  - *Odds* (`analysis/odds.cpp`): for a Twin-linked profile, `derive_hit_wound` enumerates the first roll and the
    re-roll of a failed one through core's own roll functions. That gives P(wound) = p + (1−p)·p and P(critical
    wound) = c + (1−p)·c exactly. Twin-linked is part of the cache key, and a profile without it takes the old
    arithmetic unchanged.
  - *Exact outcomes:* `expected_outcome` treats each wound roll independently, so the re-rolled chance is exact, also
    with [SUSTAINED HITS]. Hard's chain tables and threat estimates get their per-attack wound odds from it, so they
    see the re-roll with no change of their own.
  - `profile_supported` lets [TWIN-LINKED] through.
  - No weapon equipped in V1, the two bot scenarios or Strike has [TWIN-LINKED] (checked by a test), so their play is
    unchanged.
- **Amends D-026.** [TWIN-LINKED] is no longer refused. [DEVASTATING WOUNDS] (24.10) and the D-071 unknown abilities
  still are. With the Hive Tyrant's bonesword selectable, 24.11 / D-071 make the Tyrant select its bonesword and its
  scything talons together.

## D-073: Monsters and Vehicles: moving through enemy models, shooting at engaged ones, the 10.06 penalty added (M63; applies D-011)
**Decided:** 2026-11-08 (M63 of the pre-approved M59–M70 batch). The readings marked *agreed* were agreed with the user in
planning; the others take the conservative choice.
- **"MONSTER/VEHICLE".** The engine's `is_monster_or_vehicle`: the unit has the MONSTER or VEHICLE keyword (02.05, matched
  without case). It is a unit keyword, so every model of such a unit counts, and "a MONSTER/VEHICLE unit" (10.06, 17.03)
  is the same test. In the data the Hive Tyrant, the Screamer-killers and the Tyrannofex are MONSTERS; no unit is a
  VEHICLE, and no unit of V1, the two bot scenarios or Strike is either (checked by a test), so none of this touches
  their play.
- **17.01 Moving Monsters and Vehicles.** "Each time you make a normal or advance move with a unit, MONSTER/VEHICLE
  models in that unit can be moved through friendly and enemy models (excluding other MONSTER/VEHICLE models)."
  - *Normal (09.05) and Advance (09.06) moves only* (agreed). Not Fall Back (a Desperate Escape already passes every
    enemy model, 09.07), charge, pile-in, consolidation or any other move.
  - *What it lets through:* in those moves, a MONSTER/VEHICLE model's path may cross enemy models that are not
    MONSTER/VEHICLE models. Enemy MONSTER/VEHICLE models still block it, as does the battlefield edge (03.01). The move
    still can't end on any model (03.01 Ending a Move, D-027), and 09.05 / 09.06 After Moving still need the unit to end
    unengaged. Crossing an enemy's Engagement Range on the way is no problem: no rule forbids it.
  - *Friendly MONSTERS (agreed conservative reading):* 03.01 already lets every model be moved through friendly models,
    and 17.01 only adds a permission. So a MONSTER may still pass friendly MONSTERS; the "excluding" clause takes nothing
    away from 03.01.
  - *Engine:* `moves_through_non_monster_enemies(type, unit, model)` (`engine/movement`) says whether 17.01 applies.
    `PathContext` gets a `passable_enemy` list: enemy models the path may cross but not end on. The engine's
    ProposeModelMove check splits the enemy models into blocking ones (`enemy`) and passable ones when 17.01 applies;
    otherwise every enemy model blocks, as before.
  - *Movement reach (D-040, D-044):* `move_reach` leaves out the `blocking` circle of a passable enemy model, so the
    client's shaded area, the fix-illegal-move helper (D-052), the ring outline and every caller of `move_reach` follow.
    Its `forbidden` circles (not on a model; not within an enemy's Engagement Range for a move that must end
    unengaged) are unchanged. Charge, pile-in and consolidation moves are not 17.01 moves, so the auto moves (D-050,
    D-056) are unaffected.
  - *Bots:* Normal's and Hard's own movement planner keeps every enemy model as an obstacle (`segment_clear`). That is
    stricter than the rules and never illegal, so it stays (a MONSTER of theirs may walk round enemy infantry it could
    pass). Easy's straight-line moves are checked by the engine (`validate_action`), so its MONSTERS now pass enemy
    infantry where the engine allows it; the client helpers use `move_reach`.
- **17.03 Shooting at engaged Monsters and Vehicles.** "In your Shooting phase, enemy MONSTER/VEHICLE units that are
  engaged can be selected as targets of ranged attacks. Each time a model makes a ranged attack that targets such a
  unit, subtract 1 from the hit roll (excluding attacks made with [CLOSE-QUARTERS] weapons by models in a unit engaged
  with the target)."
  - *Targets:* 04.02's "Unengaged" no longer refuses an engaged enemy MONSTER/VEHICLE unit. This applies to every
    shooting type: normal and assault shooting by unengaged units, and close-quarters shooting, where 10.06 already lets
    a unit target what it is engaged with and lets MONSTER/VEHICLE models target other units too. An engaged enemy unit
    that is not a MONSTER/VEHICLE stays refused (04.02).
  - *[BLAST] is refused against an engaged MONSTER/VEHICLE unit* (agreed; the Rules Appendix FAQ: "When my unit shoots at
    an engaged MONSTER/VEHICLE unit, can models in my unit target that unit with [BLAST] weapons? A: No.", and the 17.03
    diagram). It is refused for every shooter and shooting type, with the reason "17.03: '...' is a [BLAST] weapon and
    cannot target unit N, an engaged MONSTER/VEHICLE unit (Rules Appendix FAQ)". 10.06's own [BLAST] refusal (a
    MONSTER/VEHICLE model at a unit its unit is engaged with) keeps its message and is checked first.
  - *The -1:* every ranged attack at an engaged enemy MONSTER/VEHICLE unit, except one made with a [CLOSE-QUARTERS]
    weapon ([PISTOL] is the same, 24.27) by a model whose unit is engaged with that target. A unit not engaged with the
    target gets the -1 with any weapon, e.g. the Chaos Lord's plasma pistol in normal shooting.
  - *When it is judged:* when the targets are selected (04.02 Select Targets, `make_weapon_selections`), as 10.06's -1
    and the [BLAST] / [RAPID FIRE] facts already are. A target that becomes unengaged while the unit's attacks are being
    resolved (its engaged models destroyed by earlier batches) stays a valid target and keeps the -1 recorded for it.
    It was a valid target when selected, and 04.02 checks targets only when they are selected (conservative: no change
    in the middle of a unit's attacks).
  - *Your Shooting phase only:* there is no shooting outside the Shooting phase in this game (no Overwatch or other
    stratagems), so every ranged attack the engine makes is in its player's Shooting phase.
- **10.06 fix.** `make_weapon_selections` set the MONSTER/VEHICLE close-quarters penalty with `= -1`; it now adds it
  (`-= 1`), and 17.03's -1 is added the same way. A MONSTER/VEHICLE model in close-quarters shooting firing a
  non-[CLOSE-QUARTERS] weapon at an engaged enemy MONSTER/VEHICLE gets both: -2 on the selection. D-011's cap applies
  where the hit roll is modified (`make_hit_rolls`: `cap_hit_wound_modifier(profile.hit_modifier)`), so the roll is
  made at -1. The selection and the batch's `AttackProfile` keep the uncapped sum, as D-006 compares profiles and
  [HEAVY]'s `batch_context` reads it. (Two batches whose sums differ but cap to the same value stay separate batches;
  they resolve exactly as one would.)
- **Recorded for the client.** `WeaponSelection::hit_modifier_rules` lists the rules behind its `hit_modifier`
  (`HitModifierRule::CloseQuartersMonster` for 10.06, `EngagedMonsterTarget` for 17.03; JSON `"hit_modifier_rules":
  ["10.06", "17.03"]`, omitted when empty, so every old file and replay is byte-identical). The hit card and the weapon
  menus explain the -1 from it ("Engaged MONSTER/VEHICLE target: -1 to hit (17.03)") and note the cap when it bites
  ("Net -2 to hit, capped at -1 (D-011)"; the card's need reads "(modifier -1, capped from -2, D-011)").
- **Bot odds.** The live choices (`shooting_batches`, `evaluate_shots`, `best_shooting`: Normal, Hard and the
  lookahead's shooting) take the -1 from the engine's selections, and the odds use core's `make_hit_rolls`, so the cap
  is the engine's. The hypothetical volleys of the threat and movement scores (`unit_ranged_volley`) apply neither
  10.06's nor 17.03's -1, as they applied no 10.06 penalty before (an approximation for M68 / M69). Normal's charge
  planner asks the engine (`shot_options`) whether an engaged enemy can be shot, so it sees 17.03.
- **Out of scope.** 17.02 FRAME (no unit in the data has the keyword); Deadly Demise and damaged profiles (the batch's
  out-of-scope list); oval bases (M64–M66: until then the Tyrannofex's oval base is refused by the scenario loader).

## D-074: Oval bases with a facing (M64 part 1: the geometry kernel; M65 part 2: the engine; M66 part 3: client and bots)
**Decided:** 2026-11-08 (M64 of the pre-approved M59–M70 batch). The conventions below are the M64 brief's recommended
ones, unchanged. Conservative choices are marked *conservative*.

### Part 1 (M64): the geometry kernel
Pure functions in `src/core/geometry/` (`pose.hpp`, `oval.hpp/.cpp`, `visibility_oval.cpp`, and pose-aware overloads
added to `engagement`, `coherency`, `closest`, `path` and `visibility`). No state, caller or behaviour changes: the
engine, client and bots still call the round-only functions, and `base_radius()` (so `base_distance(Position, Base,
...)`, `bases_overlap(...)` and `check_model_path(... PathContext)`) still throws for an oval until M65 converts them.
- **Oval base.** `Base{shape = Oval, diameter_mm = width (minor axis), length_mm = length (major axis)}` with
  `length_mm >= diameter_mm` (the loader's D-070 check). The Tyrannofex is 92 x 120.
- **Facing.** `facing_deg`, a float in degrees, normalised to (-180, 180] (`normalise_facing_deg`; a non-finite value
  gives 0, and never -0).
  - At 0 the major axis lies along the battlefield's y axis (south-north): a 0-degree Tyrannofex is 120 mm along y and
    92 mm along x, so an oval facing the enemy across the portrait board needs no facing field.
  - Positive angles turn counter-clockwise seen from above with y pointing north (from +y towards -x): at facing f the
    major axis points along (-sin f, cos f). At 90 the length lies along x.
  - A round base ignores facing. (M65 will refuse a non-zero facing on a round base.)
  - `rotation_delta_deg(from, to)`: the turn the shorter way, in (-180, 180]; a half turn is +180 (counter-clockwise).
  - *Open for M65:* an oval's footprint is the same at f and f + 180. The kernel turns from the given facing to the
    given facing the shorter way, so a turn from 0 to 170 sweeps 170 degrees, although the footprint at 170 is also
    reached by 10 degrees the other way. Whether facings 180 apart are the same facing is M65's call.
- **Types.** `BasePose{position, base, facing_deg}`; `PosedModel{id, position, base, facing_deg}` (PlacedModel plus a
  facing, with `pose()`). `PlacedModel` and every existing signature are unchanged; the new overloads take
  `BasePose` / `PosedModel` / `PosedPathContext`, so existing calls keep resolving to the round-only functions.
- **01.04 distance.** `base_distance(BasePose, BasePose)`: between the closest parts of the bases, clamped at 0.
  `signed_base_distance`: the gap, or minus the penetration depth (how far one must move to separate them).
  - *Round to round:* the existing inline float code (`base_distance(Position, Base, Position, Base)`), called first,
    so the result is bit-identical.
  - *Round to oval:* the signed distance from the circle's centre to the ellipse, minus r. Eberly's robust bisection
    ("Distance from a Point to an Ellipse"), in double, at most 1100 halvings, bisected in w = s + 1 rather than s: next
    to the major axis inside the evolute the root is s = -1 + 1e-13, and bisecting s left the closest point off by up to
    0.13 mm (found by the M64 tests). Exact to double rounding.
  - *Oval to oval* (and the swept and rectangle shapes below): sd(A, B) is the signed distance from c_B - c_A to
    K = A0 - B0, the Minkowski difference of the centred shapes, given by its support function. When separated, GJK on
    the support functions until its bracket (the support-plane lower bound, the simplex upper bound) is below 1e-9 mm
    (at most 128 iterations). Otherwise (overlapping, touching, or GJK not certain) a bracket:
    - an inner polygon of support points (inside K, so it bounds sd from above) and an outer polygon of support lines
      (round K, so it bounds sd from below);
    - for two plain ellipses also a curvature bound: with S(n) = h_K(n) - n . x, sd = -min S, and S'' + S = rho_K (K's
      radius of curvature, the sum of the two ellipses' a^2 b^2 / s(n)^3), so from each end of an interval of normals
      S(d) >= (S0 - rho_lo) cos d + S0' sin d + rho_lo. This is tight where the inner polygon converges slowly (deep,
      nearly concentric overlaps);
    - refined at the features that bind either bound until the bracket is below 1e-9 mm, at most 96 refinements. The
      value returned always lies in the certified bracket (inside: the best separation found along a sampled normal).
    - Measured over 100,000 random oval pairs (10% concentric): within 1e-9 mm of the support-function brute force,
      the widest bracket 1e-9 mm, at most 31 iterations (GJK or bracket; 16 on average).
- **Overlap (D-027, 03.01 "on another model").** `bases_overlap(BasePose, BasePose)`: the bases still intersect after
  each is shrunk by EPS_MM/2.
  - For a convex base whose radius of curvature is at least EPS/2 everywhere (an ellipse's smallest is b^2/a, 35.3 mm
    for the Tyrannofex), shrinking by d lowers its support function by exactly d. The shrunk bases intersect iff
    h_A(n) + h_B(-n) >= EPS for every direction n, i.e. iff the penetration depth is at least EPS; their interiors meet
    iff it is more than EPS. So overlap is `signed distance < -EPS_MM`.
  - *The round case is unchanged:* discs of radius r1 - EPS/2 and r2 - EPS/2 have meeting interiors iff the centre
    distance is less than r1 + r2 - EPS, which is `bases_overlap(Position, ...)`'s formula. The round-round case still
    calls it (bit-identical, tested). Bases in contact do not overlap.
- **D-009 areas.** `within_area(Rect, BasePose)`: some part of the base within EPS of the rectangle (the distance from
  the rectangle to the base, by the same support-function method). `wholly_within(Rect, BasePose)`: the half extents
  along x and y, `base_extents` (sqrt(a^2 sin^2 f + b^2 cos^2 f) along x, sqrt(a^2 cos^2 f + b^2 sin^2 f) along y),
  inside with the EPS tolerance. Round bases take the old functions.
- **03.01 straight lines.** A convex base translating with a fixed facing sweeps the convex hull of its start and end
  placements (its support is the larger of the two).
  - `translation_crosses(start, end, obstacle)`: the swept area and the obstacle overlap by the EPS/2 rule. Shrinking
    commutes with sweeping, so this is the same as "some placement along the line overlaps the obstacle", which the
    tests check by dense sampling. Round to round: `check_model_path`'s float capsule formula, bit-identical.
  - `translation_stays_within(battlefield, start, end)`: both ends wholly within (the battlefield is convex).
  - `translation_signed_distance`: the swept area's signed distance.
- **03.01 rotating a model.** The turn from the start facing to the target the shorter way; a round base's turn is
  its static check. `RotationCheck{blocked, facing_deg, clearance_mm, exact}`: the facing of the closest approach (or
  of the first facing not shown clear) and the smallest clearance, for M65's messages.
  - *Against the battlefield edge (exact):* the extent along x is largest at an end of the turn or at facing 90 mod
    180 (the length along x), along y at an end or at 0 mod 180.
  - *Against a round base (exact):* for a point at a fixed distance from the oval's centre, the signed distance grows
    from the major axis to the minor axis in each quadrant (envelope theorem: its derivative is proportional to
    x* y* (1/b^2 - 1/a^2) >= 0, inside and outside, x* the closest point). So over the turn it is smallest at an end or
    where the major axis points at the circle's centre (that facing found with atan2).
  - *Against an oval (conservative):* turning an oval by t radians moves its outline by at most (a - b) t in Hausdorff
    distance (its support radius has |s'| <= a - b, because (a + b)^2 c (1 - c) <= b^2 + (a^2 - b^2) c is
    (b - (a + b) c)^2 >= 0), so the signed distance is (a - b)-Lipschitz in the turn. Conservative advancement from the
    start facing: at each facing the certified lower bound s; blocked as soon as s < -EPS/2; else step
    (s + 3 EPS/4) / (a - b) radians, so the distance stays above -3 EPS/4 until the next facing tried. It never reports
    clear when the bases overlap at some facing of the turn; it reports blocked only when they come within EPS/2 of
    overlapping (intersect by more than EPS/2), *conservative*. The smallest step is EPS / (4 (a - b)): at most
    17,600 facings for a Tyrannofex's half turn (a budget of 10^6, never reached; exhausting it would report blocked).
- **Model-level overloads** (`PosedModel`): `models_engaged`, `units_engaged`, `engaged_models`, `check_coherency`,
  `PosedCoherencyWithOneMoved`, `closest_distance`, `closest_models`, `closest_units`, `has_line_of_sight` and
  `check_model_path(mover, base, points, facings, PosedPathContext)`. On round bases each gives exactly the round-only
  function's result (tested over random units and paths).
  - *Path semantics for M65:* `facings[i]` is the facing at `points[i]` (empty: 0 everywhere). Between points i and
    i + 1 the model first turns in place at points[i] from facings[i] to facings[i + 1], then moves straight to
    points[i + 1] keeping facings[i + 1]. Each turn and line is checked against the enemy models and the edge, and the
    end against every model (D-027). A turn that hits is `TurnCrossesEnemyModel` ("03.01: model X's base cannot be
    turned through enemy model Y (before segment k)") or `TurnLeavesBattlefield` ("03.01: model X's base cannot cross
    the edge of the battlefield while it turns (before segment k)"), new `PathViolationKind`s.
- **06.01 line of sight (extends D-017).** Inputs with only round bases take D-017's code, bit-identical. With an oval
  anywhere:
  - a line crosses an oval base along a closed-form chord (the quadratic of the ellipse's equation);
  - an oval blocker blocks a line that comes within 0.5 mm - EPS of it (it overlaps the 1 mm line by more than EPS, as
    D-017), by the closed-form segment-to-ellipse distance (the line's closest approach, else Eberly at the nearer end);
  - the candidate lines are D-017's: centre to centre; every pair of sample points on the two bases (64 on an oval,
    32 on a round base, plus the centre); and every common tangent of two of the shapes. Two circles: D-017's closed
    form. With an oval, the support-line tangents: outer tangents where h_X(n) = h_Y(n), bracketed every degree and
    bisected; inner tangents at the two ends of the single arc of normals where the support function of X - Y is
    negative, found from its minimum (golden-section search) so a gap far narrower than a degree is still found.
  - As D-017, this is not a proof of exactness.
- **Bounding circles (for M66's bots).** `circumscribed_radius(base)` (half the length) and `inscribed_radius(base)`
  (half the width); half the diameter for a round base.
- **Numerics.** Double inside the oval code, float results. Only + - * /, sqrt, fmod and floor on the decision paths:
  sine, cosine (Taylor series after reduction to +-45 degrees) and atan2 are polynomials in `oval_detail`, so results
  are the same on every platform (no libm transcendental results feed a decision). Iterations are capped, valid inputs
  never give NaN or an exception, and a degenerate oval (length = width) gives the circle's results within 0.001 mm.

### Part 2 (M65): the engine
**Decided:** 2026-11-08 (M65 of the pre-approved M59–M70 batch). Written before any code (design first) and approved
with conditions after review against the rules text; the implementation follows it. Readings marked *agreed* were
agreed with the user in planning; *conservative* marks the reading that permits less.
- **The facing lives on the model, not in `Position`.** This departs from PLAN.md's wording for M65 ("`Position` gets
  `facing_deg`"); the departure was approved with the design, for the reasons below.
  - `Model::facing_deg` (float, default 0). `Position` stays a plain 12-byte point. It is also a path vertex, a
    `ReachCircle` centre, `MoveReach::start` / `from`, a mouse or ruler point, a centroid and a bot's candidate end,
    where a facing means nothing; `BasePose` and `PosedModel` (part 1) would carry two facings; and every caller that
    builds a path from coordinates (the client's drags, the auto moves, the bots, the D-004 fills) would silently turn
    an oval to facing 0 at each vertex. On the model, a path given without facings keeps the model's facing: the safe
    default for every caller until M66. `sizeof(Position)` (the bots' hot loops, M54) is unchanged. The cost:
    `ProposeModelMove` gains a vector, so the `GameAction` variant grows from 40 to 64 bytes (`GameEvent` stays 72);
    the speed check below covers it.
  - The move carries what 03.01 needs ("returned to their positions at the start of that move": facings included):
    - `ModelStart::facing_deg`, the facing when the move started; `revert_move` restores it with the position;
    - `action::ProposeModelMove::facings`: empty (no turn), or exactly one per `path` vertex: `facings[k]` is the facing
      while moving along the line to `path[k]` and at `path[k]`;
    - `ModelPath::facings` and `event::ModelMoveProposed::facings`: empty when the proposal turns the model nowhere,
      otherwise one per point, the start included (normalised, below).
  - `operator==` is defaulted on all of them, so a turn changes the state and the action wherever they are compared.
    Nothing in the engine decides "has it moved" by comparing positions: a model is moved when it has a proposal in the
    move (11.04, 12.03, 12.08 "each model that is moved"; a turn-only proposal is a move, 03.01 "by moving it in a
    straight line and/or rotating it"), and the distance moved is the centre's path length (09.05–09.07 maxima,
    24.16 [HEAVY]: 03.01 "rotating a model does not count towards the distance it has moved"), both unchanged.
- **One facing per footprint (modulo 180)** (part 1's open note).
  - An oval's footprint at f and f + 180 is the same, and no core rule gives a model a front: every rule measures
    bases (01.04), and 06.01's "every part of another model that is facing the observing model" is about which parts
    face the observer, for "fully visible", which is not implemented (D-017).
  - The engine stores an oval's facing modulo 180, in (-90, 90] (`footprint_facing_deg`, `geometry/pose.hpp`; exact for
    float inputs, a non-finite input gives 0, never -0). The kernel's (-180, 180] normalisation is unchanged.
  - A turn between two vertices goes the shorter way between the footprints, at most 90 degrees
    (`footprint_turn_deg(from, to)` in (-90, 90]; a quarter turn is +90, counter-clockwise, as the kernel's half turn
    is +180). The engine gives the kernel one facing per point, unwrapped (each the previous one plus that turn), so the
    kernel's own "shorter way" sweeps exactly that turn.
  - *The longer way round* (03.01: "turn it any amount"): intermediate vertices at the same point; 0 -> 60 -> 120 ->
    170 at one point is a 170-degree turn. Zero-length segments are already accepted (nothing refuses them, and
    `path_length` adds 0).
  - Any finite facing is accepted in an action or a scenario and normalised when applied; the state JSON holds only the
    normalised form (below).
- **A round base has no facing.** Its facing is always 0. A non-zero facing on a round base is refused: in validation
  ("D-074: model X has a round base, which has no facing; its facings must be 0"), by the state JSON and by the scenario
  loader (`DataError`, below). Zeros are accepted for a round base; they change nothing.
- **Moves (03.01).**
  - Part 1's path semantics: between two vertices the model first turns in place, then moves in a straight line. The
    distance moved is the sum of the centre's straight lines; a turn adds nothing.
  - *A turn without moving* is `path = [start]` (one vertex at the move's start position, where every proposal's path
    begins) with `facings = [f]`. *A last turn after the last line* (the 03.01 diagram's "D) Rotates") repeats the last
    vertex with the new facing.
  - The kernel's `check_model_path(model, base, points, facings, PosedPathContext)` checks every turn and line against
    the enemy models and the battlefield edge and the end against every model (D-027), with its 03.01 messages: "03.01:
    model X's base cannot be turned through enemy model Y (before segment k)" and "03.01: model X's base cannot cross
    the edge of the battlefield while it turns (before segment k)" (*agreed*: a turn must clear enemy bases and the
    edge).
  - A turn may pass through friendly models (03.01), friendly MONSTERS included (D-073), but not end on any model. 09.07
    Desperate Escape: "each model that is moved can be moved through enemy models", its turns too. 17.01 (D-073): in a
    Normal or Advance move a MONSTER/VEHICLE model may turn through enemy models that are not MONSTER/VEHICLE models
    (`passable_enemy`), but not through an enemy MONSTER/VEHICLE.
  - Oval against oval the kernel's turn check is conservative (it refuses a turn that comes within EPS/2 of
    overlapping, part 1). The rosters have one oval, the Tyrannofex, so this needs an oval on each side.
  - *Validation order* of a `ProposeModelMove`: the model is in the moving unit; the path is not empty; then (D-074) the
    facings are none or one per vertex, every one finite, and 0 for a round base, each failure its single reason, as the
    empty path is; then the existing checks in their order (M '-', the maximum distance, the path, the charge's
    "closer", base contact, the pile-in's and consolidation's "closer").
- **Which moves may turn.** Every move type, since each "moves as described in Moving (03)".
  - *09.04 Remain Stationary* ("No models are moved (either in straight lines or rotated)", *agreed*): it starts no
    move, so no proposal, turns included, is possible (unchanged).
  - *02.02* M '-': such a model can't be moved, so the existing check refuses its turns too.
  - *12.03 / 12.08 Ongoing* "Models in base-contact with one or more enemy models cannot be moved": moving includes
    rotating (03.01; 09.04's own wording), so such a model can't be turned either (the existing check, at its start
    pose).
- **"Closer" and "engaged" with a turn (11.04, 12.03, 12.08; D-018, D-022).**
  - *Closer* (*agreed* for 12.03 and 12.08: a rotation does not count as moving closer; formalised *conservatively*,
    and applied to 11.04 as well, a conservative extension approved with the design): a moved model ends
    closer to a unit when its end distance (end position, end facing) is "more than" EPS below its start distance
    measured from the start position **both** at the start facing **and** at the end facing: `after < min(before(start
    facing), before(end facing)) - EPS`. So the straight lines must do the work. A turn in place gains nothing: a
    turn-only move is never closer, so it is refused in a charge, a pile-in and a consolidation. A turn that swings the
    near end away must be made up by the lines. A round base measures the same at every facing, so this is today's
    test, bit for bit.
    - 11.04 While Moving: closer to one or more charge targets, each so measured.
    - 12.03 / 12.08 While Moving (D-022 as written): the closest target is judged from the start position at the start
      facing (ties: any of them, D-010), and the model must end closer to it, so measured.
  - *Start and end states* are measured on the poses as they stand: 11.04 After Moving (engaged with every charge
    target and no other enemy unit); 12.03 After Moving (the unit engaged; each model that started engaged with a unit,
    at its start position and facing, still engaged with it); 12.08 Ongoing and Engaging After Moving; 09.05–09.07
    After Moving (unengaged). A turn alone can change engagement: in a Normal, Advance or Fall-back move, a turn that
    brings the base within 2" of an enemy model makes the commit revert the move. In a charge, pile-in or consolidation
    a turn alone can't make a moved model legal, since it must also end closer.
  - The TODO(if-possible) spots stay as they are: 11.04 "within 1" / engaged if it can", 12.03 / 12.08 "and engaged
    with it if possible".
- **Every measurement in `wh40k_core` takes the facing.** Engagement (03.04: who may fight, charge, shoot or fall back;
  Desperate Escape; ending engaged or unengaged), coherency (03.03, Regaining Coherency too), the closest model and
  unit (D-010), within and wholly within (D-009: the deployment zones and the battlefield; the engine has no Strategic
  Reserves), range and half range (04.02, 24.30), line of sight (06.01, part 1's oval overload), charge targets and
  charge distance (11.02, 11.04), pile-in and consolidation targets and moves (12.03, 12.08), the 5" pass (Appendix),
  base contact (`dist <= EPS_MM`), the set-up checks and `move_reach` use the pose-aware kernel with each model's facing
  (`pose_of(model)`, `posed_models(unit)`). [BLAST] only counts models.
  - The round-only functions (`base_radius`, `base_distance` / `bases_overlap` / `within_area` / `wholly_within` on
    `Position` and `Base`, the `PlacedModel` overloads, `check_model_path(... PathContext)`, `CoherencyWithOneMoved`)
    keep throwing for an oval and are left to the client and the bots until M66, so a site M66 misses stays loud. No
    core path reachable with an oval calls them: D-004 random play over the Tyrannofex fixture checks it.
  - Queries that take a `Position` for a model (`model_distance_to_unit`, `enemy_units_engaged_with_model`,
    `in_base_contact_with_enemy`, `ends_closer_to_a_charge_target`, `ends_closer_to_closest_target`) keep their
    signatures, meaning the model at that point with its current facing (what their client, bot and test callers mean),
    and gain `BasePose` overloads, which the engine uses. New views: `pose_of(model)`, `posed_of(model)`,
    `posed_models(unit)`, `posed_enemy_models` / `posed_friendly_models(state, side)`, and
    `move_start_facing_deg(state, model)` beside `move_start_position`. The `PlacedModel` views (`placed_models`,
    `enemy_models`, `friendly_models`) stay, round-only, for the client and the bots.
  - *Speed* (the round-round path stays bit-identical and cheap): the round-round case of `base_distance`,
    `signed_base_distance` and `bases_overlap` on `BasePose`s moves inline into `oval.hpp` (the same calls, so the same
    results); the posed `check_model_path` keeps the round-only inner loop for a round pair; the posed
    `has_line_of_sight` runs D-017's code on its spans without copying the blockers. Target: the Release arena's wall
    time (Strike, Normal vs Normal, 60 games, run alternately with the M64 binary) within 5% of M64. Measured +1.8%
    over eight alternated runs each (callgrind: 0.3% fewer instructions than M64), so the planned fallback (the old
    round-only code for an all-round state) was not needed.
- **`MoveReach` (D-040, D-041, D-044, D-046, D-053).** `move_reach(state, model)` is for the model's current facing;
  `move_reach(state, model, facing_deg)` for the facing being dragged (normalised; a round base ignores it). The next
  straight line starts at `from` after the model turns there to `facing_deg`.
  - *Round-to-round pairs* keep their `ReachCircle`s exactly. A state with only round bases gives exactly today's
    `MoveReach`: every new field at its default.
  - *Any other pair* (an oval mover, or an oval other model) is a `ReachShape{mover_base, mover_facing_deg, obstacle
    (BasePose), threshold_mm, engagement, model}` instead, in `forbidden_shapes`, `required_any_shapes` and
    `blocking_shapes`. `required_any_shapes` is empty (no group has a shape), or has one group per `required_any`
    group: group i is `required_any[i]` and `required_any_shapes[i]` together.
  - *Meaning:* the moving model's centre c is in a shape when `signed_base_distance(BasePose{c, mover_base,
    mover_facing_deg}, obstacle) < threshold_mm` (`reach_shape_contains`). A round pair's circle is exactly that set
    with radius both radii + threshold, so the thresholds are the circles' radii less both radii: not on a model 0; an
    engagement range 2" + EPS; closer `before - EPS`, with `before` the smaller of the start distances at the start
    facing and at `facing_deg` (the rule above); still engaged 2" + EPS; blocking -EPS (the line's swept base would
    overlap the model by more than EPS, as `translation_crosses`).
  - *For shading by rays (M66):* at a fixed facing a shape is convex (the signed distance to a convex set, here the
    Minkowski difference of the two bases, is a convex function of c), so on any ray it is one interval, possibly
    empty, whose ends bisection on `reach_shape_contains` finds. It contains the disc round `obstacle.position` of
    radius `inscribed_radius(mover) + inscribed_radius(obstacle) + threshold` and lies inside the disc of radius
    `circumscribed_radius(mover) + circumscribed_radius(obstacle) + threshold`, for culling and brackets. A blocking
    shape's shadow (D-044) is, on each ray from `from`, everything beyond the ray's first point in the shape.
  - `centre_bounds`: the battlefield shrunk by `base_extents(base, facing_deg)` (for a round base its radius: the same
    Rect). `facing_deg`: the facing the reach is for. `turn_blocked`: turning at `from` to `facing_deg` would pass
    through a blocking enemy model or cross the edge (03.01), so nothing is reachable at that facing; false when no
    turn is needed.
- **Set-up (03.02) and formations (D-008, D-064).** M59's refusal of non-round bases at placement goes. An oval whose
  `length_mm` is smaller than its `diameter_mm` is refused at placement too ("D-070: ... has an oval base whose
  length_mm is smaller than its diameter_mm (width)"), as the unit loader already refuses it in a datasheet file, since
  a datasheet can come from elsewhere.
  - `place_formation` uses each model's half extents along x and y at the formation's facing (`base_extents`, exactly
    the radius for a round base, so round formations are bit-identical): a rank is as wide as its models' x extents
    plus the gaps, and the rank pitch is the largest y extent in the unit plus the gap. Tilted ovals side by side stand
    at least the gap apart (their widest points need not be level).
  - New optional `formation.facing_deg` (degrees, default 0, any finite value, normalised modulo 180), given to every
    model of the unit. A non-zero value for a unit with a round-based model is corrupt data ("D-074: model type 'X' has
    a round base, which has no facing (formation.facing_deg must be 0)"). At 0 an oval's length runs south to north,
    towards the enemy on every north/south scenario. A value that is not a number, or not finite, is corrupt data too
    ("D-074: formation.facing_deg must be a (finite) number of degrees").
  - The 03.02 checks measure the poses: coherency (03.03), wholly within the zone (D-009, by the extents), no model on
    another (D-064, `bases_overlap`), unengaged (03.04).
- **JSON** (every new field omitted at its default, so every existing file loads the same and saves byte-identically).
  - `Model`: `"facing_deg"` when non-zero. On load it must be a finite number, 0 for a round base and in (-90, 90] for
    an oval (the stored form); otherwise `DataError` ("state JSON: D-074: ...").
  - `ModelStart`: `"facing_deg"` when non-zero (finite, in (-90, 90]). `ModelPath`: `"facings"` when non-empty (one per
    point, each in (-90, 90]).
  - `ProposeModelMove` and `ModelMoveProposed`: `"facings"` when non-empty. An action keeps an explicit list even when
    it is all 0: for an oval at a non-zero start facing, `[0]` turns it to 0 where `[]` keeps its facing.
  - A replay's `expect` models: `"facing_deg"` when non-zero.
  - Scenario files: `formation.facing_deg` (above).
- **Hashes.** `state_hash` (D-054) adds a model's and a `ModelStart`'s facing after its position only when it is
  non-zero, and a `ModelPath`'s facings only when present, each behind a tag (as M60–M62 did), so every round-only
  state hashes as before and the bots' own dice and plan memo are unchanged. The arena's `--per-game` hashes (the
  actions' JSON, the final state's JSON, the trace) follow from the JSON rule and unchanged play. Every replay
  regenerates byte-identically.
- **No new action, event or decision**, so there is no new log text, dice card, undo group or bot fallback answer; the
  existing `ProposeModelMove` and `ModelMoveProposed` gain their facings.
- **The auto moves and other move builders.** Core builds no move but the D-004 template (`ProposeModelMove{model, {}}`,
  no facings). `auto_fight_move` (D-050; the bots' fight moves, D-057) and `auto_charge_move` (D-056; the bots'
  charges) are client logic on round-only geometry, converted in M66. Until then, as a temporary measure marked
  `TODO(M66): remove when auto moves handle ovals`, they refuse cleanly: with any non-round base on the battlefield they
  return no arrangement, with the reason "D-074: Auto move does not handle oval bases yet", so they never throw or
  produce a move for an oval (a bot then makes no charge or fight move instead). M66 removes the guard.
- **The client's rings** (`ring_circles`, client logic): the replay viewer threw from `base_radius` on the new replay
  (the rings of a moving Tyrannofex). Converted, not guarded: a ring's radius is now the base's circumscribed radius
  plus the distance, exactly the radius for a round base (so round rings are unchanged). For an oval it is exact for
  the movement and charge rings (a model may turn freely, 03.01, so some part of its base can reach all of that disc);
  round its engagement, coherency and weapon rings it is the circle through the oval's ends, at most (length - width)
  / 2 wider across its width than the true range, until M66 draws the oval's own offset outline (`TODO(M66)`).
- **Left for M66 (part 3).** Client drawing and input (ovals are drawn and hit-tested as circles of their width; there
  is no rotation input); the client logic on round-only geometry (`fight_move_skip`, `casualty_order` and
  `casualty_picks`, `overlay_plan`, `reach_area`, `drop_fix`, `ring_outline`; `move_plan`, which drops
  `ModelPath::facings`, so extending a turned model's path undoes its turn; the log and action text for a turn); and the
  bots' own geometry (Normal's movement planner, move scoring and defence, Hard's lookahead, evaluator and coherency,
  the charge odds), which throws from `base_radius` with an oval on the battlefield. No scenario under `data/` has an
  oval until M67.
- **Fixture.** `tests/data/scenarios/rosters_tyrannofex_44x60.json`: the M59 roster fixture plus the Tyrannofex (unit
  14, Side B, the roster loadout: powerful limbs, stinger salvoes, fleshborer hive), behind the west Screamer-killer
  (`centre_x_in` 23, `depth_in` 4, facing 0). `tests/support/monster_fixtures.hpp` gains a hand-placed Tyrannofex
  layout for focused tests and the replay `m65_oval_tyrannofex.json`.

### Part 3 (M66): the client and the bots
**Decided:** 2026-11-08 (M66 of the pre-approved M59–M70 batch; no design stop). Conservative choices are marked
*conservative*. Nothing in `wh40k_core` changes: the client logic and the bots move off the round-only functions onto
core's pose-aware ones (part 2), and every move they build is still checked by the engine.
- **Drawing** (`src/client_2d`; the geometry in client logic `base_shape.hpp`). An oval is drawn at its facing: the
  model (fill and outline, D-065's shade), its ghosts (where it started the move, at its start facing; the drag preview,
  at the dragged facing), the selection and target outlines (offset outlines of the ellipse), the removal and casualty
  crosses (along its axes) and the D-051 marks. The unit's number stays at the centroid. The replay viewer and the board
  under the dice pop-ups (D-033) draw through the same code. The model tooltip says "base 92 x 120 mm oval, facing 45°".
- **Rings (D-028).** Round an oval, the Engagement Range, coherency and weapon range rings are the ellipse's offset curve
  at its facing (`offset_outline`: e(t) + d n(t), not a scaled ellipse), carried as `Circle::oval` with the
  circumscribed radius plus the distance as its bound. The movement and charge rings stay circles of the circumscribed
  radius (exact, part 2). Merging keeps D-028's rule: an oval ring hides the parts of a same-kind circle's rim inside it
  (sampled every degree, edges bisected) and shows only its own points outside the other rings (`outline_paths`). The
  ring tooltip tests the oval ring itself (`ring_contains`). While an oval is turned in a drag its rings turn with it.
  Round-only rings give exactly M65's arcs.
- **Hit-testing** (clicks, right-click datasheets, drags, hover): inside the ellipse at the model's facing
  (`base_contains`); a round base keeps the old test.
- **Turning while dragging (03.01 Rotating a Model: "turn it any amount around the centre of its base"; rotating does
  not count towards the distance).**
  - Q and E turn the dragged oval 15 degrees counter-clockwise and clockwise; Shift+wheel turns it 5 degrees a notch
    (the wheel still zooms without Shift, or for a round base, which has no facing). The help line and the move hint
    say so.
  - The drag keeps its facing: the shaded area and the fix-illegal-move region are `move_reach(state, model, facing)`.
  - The turn preview: the area the turn sweeps (`turn_swept_outline`, exact per direction: the largest radius of the
    base over the turn), drawn where the turn is made, with the footprint it started from, and the ghost at the new
    facing. The ruler reads "turned 45° then 1.50\" / 9.00\"".
  - A turn in place: drag without moving past the click threshold, turn, let go (path {where it is}, one facing).
  - The drop (`turned_drop`): turn then move (`path {to}`, `facings {f}`, the turn at the line's start) is tried first;
    if core's validation refuses it but accepts move then turn (`path {to, to}`, `facings {now, f}`: the line at its
    facing, then the turn at its end, 03.01's diagram "B) Rotates"), that; if it refuses both, turn then move, so the
    engine's reasons show. With no turn (the same footprint, or a round base) it is `extend_path`, as before. The
    client never turns a model the player didn't turn (*conservative*: no automatic rotation to make a drop fit).
  - `move_plan`: `extend_path` keeps `ModelPath::facings` (a proposal replaces the whole path, and one without facings
    returns the model to its start facing), so extending a turned model's path keeps its turns, the new line at its
    facing now; the whole-unit handle keeps every facing; `model_path_facings`, `Ruler::turned_deg`.
  - Text: the log says "model 73 turns 45° then moves 3.00\"" ("moves 3.00\" then turns 45°", "turns 90° and moves
    ...", "turns 30°"), the action label "Move model 73 (turns 45° then moves 3.00\")" (`describe_path`, the turn
    summed the shorter way between footprints, part 2). A path without facings keeps the old text exactly.
- **The shaded area (D-040, D-041, D-044, D-046, D-053) with shapes.** `ReachRegion` gains `inside_any_shapes`
  (empty, or one group per `inside_any` group), `outside_shapes` and `outside_shape_shadows` from `MoveReach`'s shapes;
  `turn_blocked` empties the region.
  - `in_region` is exact: a shape by core's `reach_shape_contains` (decided by its inner and outer discs first, outside
    a 0.001 mm margin), a shape's shadow by the engine's own line test (`translation_crosses` at the blocking threshold;
    for another threshold, an inset region of D-052, `translation_signed_distance < threshold`, the same set because
    shrinking a convex base commutes with sweeping it, part 1).
  - `reach_spans`: each shape crosses a row in one interval (convex, part 2), and so does a shape's shadow (the shadow
    of a convex set from a point outside it, {f + t (k - f): k in the set, t >= 1}, is convex: a convex combination of
    two such points is f + T (k - f) with T >= 1 and k a convex combination of the two k's). Each interval is found by
    bisection from a part known to be inside (the inner disc's chord, or the golden-section minimum of the signed
    distance along the row; for a shadow the inner disc's shadow, the ray beyond the obstacle's centre, or samples every
    0.5 mm) out to the outer disc, to within 0.5 mm (or half a row step). *The documented band:* the shading may differ
    from the exact area by that much at a shape's edge, and a sliver of a shadow narrower than the samples may be
    missed on a row; what is accepted is decided by the engine and `in_region`, never by the spans. The client keeps
    the spans of an area with shapes until the move, the model, the facing, the zoom or the grab point changes.
  - `unit_region` moves the shapes with the other areas; `trimmed_region` drops shapes by their outer discs;
    `RayRefusals` counts a group's shapes as "perhaps inside" by their outer discs, so it still never refuses a spot
    `in_region` accepts (M54).
- **Fix illegal moves (D-052) with shapes.** `fix_region` turns an engagement shape into "not on it" (threshold 0) and
  drops a "still engaged" group whose circles and shapes are all engagement ones; `inset` adds to a shape's threshold
  (a signed distance, so exact); `drop_problem` tests the shapes. The nearest spot: each shape near the drop becomes a
  chain of segments between points on its edge every 2 degrees round the obstacle (windowed to the directions that can
  be within 1" of the drop), at a threshold 0.05 mm on the legal side so the chords stay legal; the projections onto
  them, their crossings with the circles, lines and other chains, and a shape shadow's tangent lines from `from`
  (through its chain's extreme points) join the candidates, each checked by `in_region` as before. Along the drag, the
  chains' crossings cut the line. A round-only region takes exactly the old steps.
- **The auto moves (D-050, D-056).** M65's guard is removed. An oval mover keeps its facing (its proposals give no
  facings); with an oval on either side, contact uses the pose-aware distance:
  - the contact rings (and the charges' 1" and engaged rings) are `contact_ring`: for a round base and an oval, exact
    and closed form: by central symmetry the moving centre is r + gap from the oval centred on the target (whichever
    of the two is the oval), so it lies on that oval's offset curve, sampled evenly in the ellipse's parameter; for two
    ovals `ring_spot`, a bisection along each direction from the target's centre on core's `signed_base_distance` (its
    outside end, within 1e-4 mm). Either way the gap is the ring's, so a contact spot (0.005 mm) is in base contact.
    The spots are worked out once per target, moving base, facing and ring, and shared by the unit's models;
  - the direct line into contact is `distance_until_gap`, a bisection along the line;
  - gaps, engagement, coherency and the quick overlap test of two movers use the pose-aware distance.
  Round pairs, and a charge or fight move with only round bases, keep the old code (the same closed forms and the
  round-only coherency checks). The engine still checks every proposal and the end of the move.
- **Step skipping (D-047, D-048):** `could_make_new_contact` by the circumscribed radii (generous, as D-047 wants);
  `nearly_in_base_contact` by the pose-aware distance at the start pose.
- **Casualties (D-051, D-058):** the coherency table and the "closest enemy" and "engaged" keys measure each model at its
  facing; `coherency_losses` gains a `PosedModel` overload (the `PlacedModel` one is the same at facing 0, for round
  bases). The default order, the coherency removal, `CasualtyPicks`, Normal's casualty order and Hard's candidates work
  with an oval on the battlefield (M65's throw at the first casualty decision is gone).
- **The bots (D-054, D-066, D-067).**
  - *No bot level turns an oval:* every proposal a bot makes gives no facings, so a model keeps its facing from the
    start of the move. **Known limitation, for M68 / M69:** a bot's Tyrannofex can't turn to fit through a gap, to
    reach contact along its length or to present its narrow side; turning candidates are left to those milestones.
  - Bounding circles in the bots' own geometry (`src/ai/bounds.hpp`), chosen per site so the bot rarely plans a move the
    engine refuses:
    - *avoid* sites by the circumscribed circle (it holds the base at every facing): straight lines clear of enemy
      bases (Normal's `segment_clear`, Hard's `translation_ok`), ending out of Engagement Range, not ending on another
      model, the D-027 slot order and the parking step (D-075);
    - *reach* sites by core's exact pose-aware distances: range and half range and the gaps in Normal's move scoring,
      the charge reach (`kth_closest_model_distance`), the furthest standing move, coherency (Normal's slots, Hard's
      coherency estimate);
    - the battlefield edge by the base's exact half extents at its fixed facing (`base_extents`): Normal's
      `on_battlefield`, Hard's `translation_ok`, and the room to back away in the move score and Hard's evaluator.
    - For a round base every helper is the old formula and gives the same float (`avoid_gap_mm` is `base_distance`'s
      expression, `avoid_overlap` is `bases_overlap`'s, `base_extents` is the radius; tested bit for bit), so round-only
      play is unchanged. In the hot loops (Normal's move geometry, Hard's translations, the move score) the radii and
      extents are worked out once per move, and with only round bases the old round-only calls are made (the round
      fast path), so the round-only speed is M65's.
  - Every plan is still simulated on the engine first, and a refused or reverted plan is dropped (the existing path).
  - Hard's speed on the roster fixture is not tuned here (M69).
- **Client flags (screenshots).** `--turn <model>:<deg>[:<dx>,<dy>]` starts a scripted drag of that model in the first
  decision that moves it, turned `deg` degrees, the cursor `dx`, `dy` inches from it (it submits nothing). In a
  `--screenshot` run `--zoom` is applied every frame (a late window resize used to fit the board again).
- **Unchanged:** the state, the actions, the events, JSON and hashes (no new field); every round-only output of the
  client logic (M66's tests hash them against the M65 code's) and of the bots (the arena's `--per-game` hashes).

## D-075: Big-base polish: the grab offset, wounds past W 6, finer contact sampling, parking large models (M66)
**Decided:** 2026-11-08 (M66 of the pre-approved M59–M70 batch). Client and bot conveniences, no rule changes.
- **A drag keeps the grab offset.** A dragged model's centre keeps the offset from the cursor it was grabbed with
  (`drag_drop`): it moves exactly as the cursor does and no longer jumps, when the drag starts, so that its centre is
  under the cursor (up to 60 mm for the Tyrannofex). Kept for every base: for a 32 mm base the jump it removes is at most
  16 mm, and a model grabbed at its centre moves as before. The fixed-drop cue (D-052) is drawn at the raw drop, the
  model's spot plus the offset, instead of at the cursor. The whole-unit handle already moved by the cursor's offset.
- **Wounds past W 6.** Up to W 6 a damaged model keeps one pip per wound (D-028). Past W 6 (the Tyrannofex W 16, the
  Hive Tyrant and the Screamer-killers W 10) the pips would overrun the base: a bar filled to the wounds left, with
  "11/16" under it, below the unit's number (`wound_display`). A model at full wounds still shows nothing.
- **Finer contact sampling.** The auto moves' contact spots round a target model (D-050's contact ring, D-056's contact,
  1" and engaged rings) number 72 × ceil(perimeter / (π × 32 mm)), so they are no further apart round the target than
  72 round a 32 mm base (Ramanujan's second formula for an oval's perimeter): 72 round every base up to 32 mm, as before
  (every model of the four older scenarios), 144 round 40, 50 and 60 mm bases, 216 round a 90 mm base and 288 round the
  Tyrannofex. Round a round pair they lie along directions: 72 are the old ones; more come from core's platform-independent
  sine and cosine (`direction_table`) in charges, and from the same `std::cos` / `std::sin` expression as the 72 in fight
  moves (M54). Round a round–oval pair they are spread evenly in the oval's ellipse parameter (D-074 part 3's
  `contact_ring`). This changes the auto moves round the roster's bases bigger than 32 mm (on purpose).
- **The bots can park large models.** Normal's parking step (M43: breaking a D-027 cycle by moving one model beside
  its start first) moves it its circumscribed diameter plus 1 mm, clear of its start at every facing (121 mm for the
  Tyrannofex), to a spot clear of every model by the circumscribed circles.

## D-076: The points tiebreak and the portrait roster scenario (M67; amends D-007, extends D-064 and D-070)
**Decided:** 2026-11-08 (the result rule and the board agreed with the user in planning the M59–M70 batch; M67
pre-approved; the details below are the conservative choices made for it).
- **The rule (07.03, amends D-007 for scenarios that ask for it).** 07.03 leaves the number of battle rounds and what is
  scored to the mission; V1 has none, so the result is a house rule. A scenario with `"result": "points_destroyed"` is
  decided so:
  - A wipe-out still wins at once (D-007, unchanged and checked first: the game ends in the battle round it happens).
  - Otherwise, after the End of Battle Round step of the last battle round (07.03; nothing is triggered in its two
    steps here), the side that destroyed more points wins. Equal points is a draw.
  - Points destroyed by a side: for each enemy unit, its full points if it is destroyed (Rules Appendix, Destroyed),
    otherwise points x wounds lost / starting wounds.
  - Starting wounds: the sum of the W characteristic of every model in the unit as set up. The Rules Appendix's
    starting strength is counted at the start of the first battle round; with no deployment step and no reserves
    (D-003) that is the set-up, so the loader records it. Wounds lost: the starting wounds minus the wounds remaining on
    the unit's models still on the battlefield (destroyed models are gone from the unit), clamped to 0..starting wounds.
  - *Every loss counts, whatever caused it:* models lost to their own side's hazard rolls (24.15), a Desperate Escape
    (09.07) or coherency (03.03) count for the opponent. The agreed formula counts each enemy unit's losses, not who
    caused them; this is its literal reading.
  - A scenario without the option keeps D-007 (a draw after the last round), even if its units have points.
- **Exact arithmetic.** Both totals are numerators over one common denominator D, the lcm of the starting wounds of
  every unit with points (240 for the roster scenario), so they compare as integers: side s destroyed
  `sum over enemy units of points x (D / starting wounds) x lost`, with lost = starting wounds for a destroyed unit.
  No floating point anywhere.
  - *Bounds* (`check_points_data` in `core/state/points_destroyed.hpp`; the loader and the state JSON refuse anything
    else as `DataError`): a unit's points are an integer 1..100000 (a sanity cap far above any datasheet); its starting wounds a positive int; a unit has both or neither; with the option every unit
    has points; and D x (the sum of every unit's points) <= 2^59.
  - *No overflow for any legal input:* D is built one unit at a time (D x starting wounds / gcd), and a step that would
    take it past 2^59 is refused before it is multiplied, so it never overflows. Each term is points x (D / starting wounds) x lost <= points x D, and each partial product is at most that
    (lost <= starting wounds). Each side's numerator is at most D x the sum of the points <= 2^59 < 2^63 (int64). The
    display's long division keeps a remainder below D, so remainder x 10 < 10 x 2^59 < 2^63, and the rounding test
    2 x remainder < 2^60.
  - *Display:* each total as a decimal rounded half up, at 2 places without trailing zeros ("412.5", "380", "196.67"),
    or at more places when two unequal totals would read alike at 2 (`points_display`). Two unequal totals differ by at
    least 1 / D >= 2^-59 > 10^-18, and values more than 10^-k apart never round alike at k places, so 18 places always
    suffice; equal totals always read alike. Rounding is monotonic, so the larger total never reads smaller.
- **Data.**
  - *Scenario file:* `"result": "points_destroyed"` at the top (absent: D-007; any other value is refused, naming it).
    `"points"` on each unit entry: required with the option; without it, allowed, checked the same way and stored, but it
    decides nothing.
  - *GameState* (so a saved state or a replay's state can be decided): `result_rule` (JSON `"result_rule":
    "points_destroyed"`), and on each unit `points` and `starting_wounds`; each omitted from the JSON at its default
    (D-007, 0), so every older file loads and saves byte for byte (`D076_Json.AnOldFileLoadsAndSavesTheSame` re-saves
    every replay). The state JSON runs the same checks.
  - *The result:* `GameResult` and the `GameOver` event carry `points` (a `PointsTally`: the two numerators, indexed by
    the side that destroyed them, the denominator, the two display strings and the battle round they were counted in)
    only when the game was decided on points; JSON omitted otherwise. Such a `GameOver` cites D-076 (`event_rule`), any
    other D-007. The reason reads "D-076: battle round 5 has ended with both sides on the battlefield; points destroyed:
    Side A 412.5, Side B 380; Side A wins on points destroyed" (or "...; equal points destroyed: the game is a draw").
  - *The bots' `state_hash`* (D-054): a unit's points and starting wounds and the result rule are hashed only when
    set, behind tags, so every older state hashes as before (`M67_StateHash`, against a copy of the M66 hash).
- **Client** (`wh40k_client_logic` `phase_track`, `event_text`; `main.cpp`):
  - the victory pop-up (D-038) shows the title, "Points destroyed: Side A (Chaos Space Marines) 412.5, Side B
    (Tyranids) 380" and the reason; after a wipe-out in such a scenario, the totals as the game ended;
  - the Game panel shows "Points destroyed: Side A 120, Side B 55.5" above the unit list while the scenario uses the
    tiebreak, with the rule in its tooltip;
  - the log line: "[D-076] Battle round 5 ends: Side A wins on points destroyed (412.5 to 380)", the winner's total
    first; a draw "... a draw on points destroyed (380 each)";
  - the phase bar and the decision panel show the reason, which holds the totals. Nothing changes without the option.
- **Bots:** they don't know the tiebreak yet (M68 and M69). The engine ends their games as above; the arena reads the
  result's winner as before, so a points win counts as a win and a points draw as a draw. For such a scenario only, its
  summary adds "decided on points destroyed after the last battle round (D-076): A wins n, B wins n, draws n"; the
  `--per-game` lines are unchanged.
- **The scenario `data/scenarios/rosters_44x60.json`** ("Incursion - Chaos Space Marines vs Tyranids (44x60
  portrait)", extends D-064 and D-070): 44" wide by 60" deep, 12" zones on the short south (A, Chaos Space Marines,
  first turn) and north (B, Tyranids) edges, 36" between them, 5 battle rounds, the tiebreak. Every unit of both rosters
  with its roster loadout (as the M59 fixture) and its roster points; the roster prints a duplicated unit once with a
  count at the points of one unit (CSM 960 = 95 + 180 + 370 + 135 + 180; Tyranids 955 = 195 + 2 x 55 + 2 x 75 + 80 +
  2 x 125 + 170). Units are numbered west to east (in file order, so the board numbers read left to right):
  1 Legionaries with chainswords (x = 7"), 2 Chaos Lord (13"), 3 Chaos Terminator Squad (20.5"), 4 Havocs (29.5",
  held back 3"), 5 Legionaries with boltguns (37.5"); 6 Termagants (4"), 7 Genestealers (11"), 8 Screamer-killer
  (16.5"), 9 Tyrant Guard (19.25", held back 4"), 10 Hive Tyrant (22"), 11 Tyrannofex (24.75", held back 4", facing 0),
  12 Screamer-killer (27.5"), 13 Genestealers (33"), 14 Termagants (40"). The 10-model units stand in two ranks of 5,
  the others in one; every unit in base contact; the units not held back touch their zone's front line. Every unit is
  coherent, wholly within its zone, unengaged, and no bases overlap (`R03_02_SetUp.TheRostersScenarioIsALegalSetUp...`).
  Only the points and loadouts come from the rosters: `docs/datasheets/` (Games Workshop copyright) is not copied into
  `data/` or the package. Unlike the fixtures under `tests/data/`, the scenario is in the start screen's list and the
  package.
- **Replay** `m67_rosters_round1.json` (scenario-file, seed 1): the brief asked for battle round 1 with a shot, a
  charge and a fight, stopping at the start of round 2. With 36" between the zones and M 8" at most, no unit can charge
  in round 1, so the replay runs rounds 1 and 2 (the lascannon volley in round 1; the Genestealers' charge and the fight
  in round 2) and stops at Side A's first Move Units decision of round 3. The name is kept as briefed.

## D-077: Easy and Normal for the roster units: per-model loadouts, [HAZARDOUS], the -1s, engaged MONSTERS, 17.01, material weighted by points and the tiebreak objective (M68; amends D-066)
**Decided:** 2026-11-08 (M68 of the pre-approved M59–M70 batch; no design stop; the conservative choices are marked
*conservative*). Bot only (`src/ai`): no rule, core, state, action, event or JSON change, no new decision. Amends D-066
(levels Easy and Normal across several units) and the M40–M43 notes of D-054 where stated. Hard's own code (D-067) is
untouched.
- **The old scenarios play exactly as before.** Every change below but one acts only through a fact none of V1, the two
  bot scenarios or Strike has: a MONSTER/VEHICLE unit, an equipped [HAZARDOUS] weapon, a unit whose models differ in W,
  points on every unit, the points tiebreak. Without it the M67 expressions run unchanged (a weight of 1 is not even
  multiplied in). The exception, the Desperate Escape guard (12), acts only on an escape Normal would make whose likely
  losses leave no move that stands, which never happens in those scenarios' compared games. So they play identically: the
  arena's `--per-game` hashes match `baseline/m60` on all 24 files (4,380 games) and the replays regenerate byte-identical
  (PLAN.md, M68 Results).
- **1. Per-model loadouts (checked; one site fixed).** Every estimate of Easy and Normal already used each model's own
  weapons: the hypothetical volleys (`unit_ranged_volley`, `unit_melee_volley`: each model's best set, 24.07, then
  identical attacks merged into one batch, D-006), the engine-based ones (`best_shooting`, `best_melee`, Normal's
  `plan_shooting` and `plan_melee` over `shot_options` / `melee_options`), the move score's per-model shooting, the
  casualty worth (`model_worth`) and Easy's default plans (D-071). So a unit's volley is the sum over its loadout groups:
  the Havocs' four lascannons and the champion's plasma gun are two batches, the boltgun Legionaries' boltguns and their
  champion's bolt pistol one (identical S5 AP-1 D1 attacks), the Legionaries' champion fights with his chainsword and the
  others with close combat weapons; checked against the engine's own batches and 20,000 engine resolutions
  (`M68_Loadouts`). The one representative model in Normal was Desperate Escape's expected hazard losses (the first
  model's W): a unit whose models differ in W now goes through core's 06.02 allocation (`hazard_model_equivalents`), a
  unit whose models share one W keeps the M43 expression (every roster unit shares one W). The representative model of
  Hard's chain tables is M69's.
- **2. [EXTRA ATTACKS] in every melee estimate (checked; nothing to change).** Every Normal and Easy melee estimate goes
  through `unit_melee_volley` (M60: the best other weapon plus every [EXTRA ATTACKS] weapon) or the engine's
  `melee_options` with M60's candidate sets (`plan_melee`): the charge planner's and move score's `melee_share` (and so the
  charge value, the hold value, the fighters within pile-in reach, the threat of other units), the casualty stance and
  worth, the consolidation score and fight selection (`plan_melee`), the threat roles (`unit_role`), and Easy's melee plan
  (D-071). `M68_ExtraAttacks` checks the Hive Tyrant's talons in each.
- **3. The cost of [HAZARDOUS] (24.15, 06.03, 06.02).**
  - *The cost:* `analysis::hazard_model_equivalents(firer, rolls)`, the model equivalents the firing unit is expected to
    lose: whether a roll fails and its mortal wounds from core's `make_hazard_rolls` on each face (a failure on 1-2, 1
    mortal wound, 3 if every model is a MONSTER/VEHICLE), the failures binomial over the rolls, the mortal wounds inflicted
    by core's `inflict_mortal_wounds` (06.02: a wounded non-CHARACTER model first, then any non-CHARACTER, then
    CHARACTERS; the engine's first candidate each time, *conservative*: every roster unit's models share one W, so the
    choice changes nothing), a wound off a W-wound model counted 1/W as model equivalents count it. The Havocs: 1/9 model
    a roll; the Chaos Lord: 1/15; a MONSTER: 3/W. Checked against 20,000 engine hazard rolls (`M68_Hazard`).
  - *The brief's "the chance of a 1"* is read as the engine's 06.03: a roll of 1-2 fails (1 in 3).
  - *Normal's declarations:* `plan_shooting` and `plan_melee` value a declaration as its targets' shares less the firing
    unit's own share of that loss, the rolls being the engine's count (`hazardous_weapons_selected`: one per [HAZARDOUS]
    weapon selected). So the champion's plasma gun goes on supercharge only when its extra damage is worth more than the
    hazard: at the Tyrant Guard, standard at 18" (one shot), supercharge within half range ([RAPID FIRE 1], two shots); the
    Chaos Lord's pistol stays on standard against every roster target (its hazard costs 1/15 of a 95-point model).
  - *Hypothetical volleys:* a [HAZARDOUS] profile's value in the per-model pick subtracts its hazard roll at the exchange
    rate (`ShotContext::hazard_rate`, `best_shooting`'s `hazard_rate`: one of the firer's model equivalents in the target's,
    `normal::hazard_rate`, the ratio of their shares of one model). Normal passes it for both sides' volleys (each side's
    own rate: the enemy is assumed to pick as Normal does). Callers that pass nothing (Hard's chain, the tests) price
    nothing, as before.
  - *Easy:* unchanged. Its default plans (D-071) never pick a [HAZARDOUS] profile.
  - *Not priced:* the firer's loss is not taken off the move score's line (only the profile choice is priced there), and
    D-071's "no hazard roll after the last enemy model is destroyed" is ignored (both small).
- **4. The -1s in hypothetical volleys (10.06, 17.03, D-011).** `analysis::ShotContext` gains who is engaged:
  `target_engaged` (the target is engaged with another unit) and `elsewhere` (close-quarters shooting at a unit the
  shooter is not engaged with); the defaults are M67's (an unengaged target; close-quarters shooting at the unit engaged
  with). `unit_ranged_volley` judges the target as the engine's `target_ok` does (strategic knowledge, D-054: the same
  checks in the same order): an engaged target that isn't a MONSTER/VEHICLE only by a unit engaged with it (04.02); in
  close-quarters shooting a non-MONSTER model only at a unit engaged with its unit, a MONSTER/VEHICLE model with [BLAST]
  not at one (10.06); [BLAST] never at an engaged MONSTER/VEHICLE (17.03, the FAQ, D-073). It adds the -1s as
  `make_weapon_selections` adds them (D-073, `+=`): 10.06's for a MONSTER/VEHICLE model in close-quarters shooting, 17.03's
  at an engaged MONSTER/VEHICLE target, each except a [CLOSE-QUARTERS] weapon at a unit its unit is engaged with; the odds
  cap the net at -1 through core's `make_hit_rolls` (D-011). Checked against the engine's selections (`M68_Modifiers`).
- **5. Engaged MONSTERS (10.06 as M63 implemented it).** M67's planners assumed an engaged enemy can shoot only the units it
  is engaged with and an engaged unit can't be shot. Now:
  - *The charge planner:* `hold_value` lets the closest enemy, engaged with another of our units, shoot us if it is a
    MONSTER/VEHICLE (close-quarters shooting by its MONSTER/VEHICLE models, `elsewhere`, at -1; it still can't charge,
    11.02), and our shot at it, where the engine offers one, is at -1 (17.03). `others_threat`: while we are engaged after a
    charge, a MONSTER/VEHICLE of ours can still be shot by the enemy units outside the charge (17.03, at -1). An engaged
    enemy MONSTER/VEHICLE stays out of `others_threat`, as before: it always has another target, the unit it is engaged
    with, so by marginal exposure (D-066) it is busy.
  - *The consolidation score:* an engaged enemy MONSTER/VEHICLE may be shot (17.03) and shoots us (10.06).
  - *The move score:* our shooting at the closest enemy (or another enemy unit) when it is an engaged MONSTER/VEHICLE
    (17.03, at -1; M67 skipped it); the hold line's locked enemy, if a MONSTER/VEHICLE, still shoots us (10.06); while we
    are engaged, other enemy units shoot a MONSTER/VEHICLE of ours (17.03). A locked enemy MONSTER/VEHICLE other than the
    closest stays uncounted (marginal exposure, as above).
  - The helper `normal::next_turn_shot` gives the hypothetical shot of one unit at another from who is engaged now.
  - `M68_ChargePlanner` checks the hold value with an engaged Hive Tyrant carrying a heavy venom cannon.
- **6. MONSTERS walk through infantry (17.01, D-073).** Normal's movement planner (`segment_clear`) lets a MONSTER/VEHICLE
  model's Normal or Advance move (the engine's `moves_through_non_monster_enemies`) pass enemy models that aren't
  MONSTER/VEHICLE models; enemy MONSTER/VEHICLE models still block, and `ends_ok` still keeps every end off every model and
  out of Engagement Range. The candidates, formations and parking steps all use it, and every plan is still simulated on the
  engine. `M68_Movement`: the Hive Tyrant's 8" move through the Legionaries' rank is planned and accepted; the Tyrant Guard
  gets no such candidate. Hard's own translations (`translation_ok`) are M69's.
- **7. Material weighted by points.** `normal::share_weight(state, unit)`: 1 (as before) unless every unit in the state has
  points (the scenario gives them, D-076) and the unit's side has more than one unit type (datasheet name); then the unit's
  points over the mean points of every unit, both sides and destroyed ones (fixed for the game). Every unit share is
  weighted (`unit_share`, `alive_share`, `share_value`, `melee_share`, `ranged_share`), so the exchanges' ratios (a unit's
  share left over its share at the start) are unchanged and their values are in points. Roster weights (1915 points over
  14 units): Termagants 0.40, Genestealers 0.55, Tyrant Guard 0.59, Chaos Lord 0.69, Screamer-killer 0.91, Havocs 0.99,
  Tyrannofex 1.24, Legionaries 1.32, Hive Tyrant 1.43, Chaos Terminators 2.70.
  - *Normalisation (conservative):* the mean over both sides keeps a trade between the two armies a trade in points (the
    tiebreak's currency); a side of one unit type keeps weight 1 as the brief says, even with points (`M68_Points`; no such
    scenario exists).
  - *Fixed for the game:* destroyed units stay in the state, so the weights don't move while the game goes on.
- **8. The objective knows the tiebreak (D-076).** With points-weighted material, a unit's share lost times its weight is
  its points destroyed over the mean (exactly for a unit whose models share one W: model equivalents over starting
  strength are wounds lost over starting wounds), so the charge, fight and consolidation values, which are material, count
  the tiebreak's points; their horizon is cut at the game's end as before (`turns_left`). What assumed D-007's draw was the
  move score's game-length terms. Under `points_destroyed`:
  - *no time preference:* both discounts 1 (D-007's draw to wait for and wipe-out to win don't exist; losses past the end
    are cut by the horizon anyway);
  - *the survival term* only for its side's last unit (a wipe-out still loses at once, D-007), worth what surviving buys
    on the engine's exact tally (`core::points_destroyed`): (1 + R) times D-007's, R = +1 ahead, 0 level, -1 behind (a win
    over a loss 2, a draw 1, nothing);
  - *D-066's reply rule:* no side plays for the draw, so every side counts the other enemy units' replies by marginal
    exposure.
  - The tie-break, edge and coherency terms are unchanged. `M68_Tiebreak` checks each against a D-007 copy of the state.
- **9. Random damage (D-069): left as it is.** The bots still plan casualties against the preview's maximum damage. In the
  rosters the only random D is the Havocs' lascannon (D6+1), and the maximum and the mean give the same casualties against
  every Tyranid unit but one: a single model, W1 or W2 (any roll kills), or the Tyrant Guard (W4). There the maximum's set
  of models to lose holds the mean's (the same greedy choice, run for more models), and three Tyrant Guard keep coherency
  whichever of them is lost (the ends of a touching rank are 1.97" apart), so the plans can differ only in which Guard dies
  first. Normal's Tyranids lost 1 model to coherency in 200 games against Easy (M67 and M68 alike), so an expected-damage
  plan can't be clearly better; D-051's guarantee (every model a roll can reach is picked) keeps the maximum.
- **10. Easy:** no new judgement; it plays the roster units clean (the arena, below, and `M68_Games`).
- **11. Oval facing (D-074 part 3): left as it is.** Over 40 traced games (Normal Tyranids against Easy and Normal, seeds
  1–20) the Tyrannofex declared 20 charges and made 12 charge moves against 10.0 its declarations' odds expected (the Hive
  Tyrant 34 against 35.5, the Screamer-killers 33 against 33.7 and 32 against 29.6), and none of its moves was refused or
  reverted: planning with a fixed facing doesn't measurably cost it. Turning its long axis to a target could add at most
  (120 - 92) / 2 = 14 mm of reach; that is left for M69 / later if a measure shows it.
- **12. Desperate Escape only when it stands (found by the M68 arena runs).** Normal plans a Desperate Escape with no
  failed hazard roll (D-054) and checked nothing more. On the roster scenario the battle-shocked Chaos Terminators, engaged
  with the Hive Tyrant standing among them against the south edge with a Screamer-killer 2.7" north, chose one whose
  likely losses left no coherent arrangement out of Engagement Range (09.07, 03.03), so the move had to be reset (one
  UnitMoveReverted in 200 Normal vs Normal games, seed 193). Normal now makes a Desperate Escape only when every likely
  outcome (one to three failed rolls, 06.03, the casualties chosen as Normal chooses them) still leaves a fall-back move
  that stands (`escape_stands`, as Hard's, D-067); otherwise it takes its next best option (staying in the fight), and the
  reason says why. More failures (four or more of the unit's rolls) are not checked, as in Hard.
  - *Checked only for the escape Normal would make* (*conservative*): a first build checked every offered escape, which
    left the actions and states of the old scenarios as they were but dropped a losing escape option from the reasons of 3
    of the 200 Normal vs Normal games with 20 Termagants (their trace hashes). Checked only when chosen, every other
    decision and reason is as before, and the 24 old files' hashes match.
  - `M68_Movement.ADesperateEscapeIsMadeOnlyWhenTheLikelyLossesLeaveAMoveThatStands` plays seed 193 to that decision (the
    test fails with the guard bypassed).
- **Why Normal vs Normal never reached melee (the M66 note), and now.** Measured first on the new scenario with the M67
  binary (60 games): the Chaos Space Marines won 46, the Tyranids 14, with 22 pile-ins and consolidations, all of them the
  Chaos Space Marines'. Every Tyranid unit lost its exchange with the closest Chaos Space Marine unit counted in unit
  shares (a 75-point Genestealer unit loses about half of itself to a 180-point boltgun squad's shooting in a turn and
  takes under a fifth of it back in melee), so each kept away as a losing side does (D-066), and D-007's survival and
  time-preference terms paid for hiding to reach a draw the tiebreak no longer gives. With material weighted by points
  and the tiebreak in the objective the Tyranid MONSTERS close in and fight; the infantry still holds back from the Chaos
  Space Marine infantry, which is right for these armies: even counted in points, Genestealers charging boltgun
  Legionaries deal about 0.24 (points-weighted shares) and take about 0.50 back in the reply. Against Easy, which walks into
  them, the Genestealers and Tyrant Guard charge too.
  Normal vs Normal now (200 games): the Tyranids win 140, the Chaos Space Marines 60, with 462 pile-ins and consolidations
  (433 and 29); 20 traced games had 34 Tyranid charge moves, all by the Hive Tyrant and the Screamer-killers.
- **Measured** (Release arena, `rosters_44x60.json`, seeds 1–200 per pairing; the M67 binary with only the arena's new
  report lines on the same seeds for comparison; PLAN.md, M68 Results, has the rest):
  - Normal (Chaos Space Marines) vs Easy: 200 wins, all on points, 567.3 points destroyed to 127.8 (M67: 200 wins, 568.7 to
    141.5). Easy vs Normal (Tyranids): Normal 197 wins (122 by wipe-out, 75 on points) and 3 losses on points, 888.3 to
    316.6 (M67: 200 wins, 11 by wipe-out, 715.6 to 273.9). Easy vs Easy: 77 / 123, as at M67 (Easy is unchanged). Every
    pairing clean.
  - Normal's own decisions: p95 12.3 ms and max 556 ms as the Chaos Space Marines against Easy, 6.2 ms and 375 ms as the
    Tyranids, 10.8 / 6.4 ms and 296 / 76 ms in Normal vs Normal.
  - Strike (Normal vs Normal, 60 games, the M67 binary and this one alternately, four runs each): 9.04 s against 8.97 s
    on average (-0.8%).
- **Hard on the roster scenario.** Hard's code is untouched, and on the old scenarios it plays as before (hashes). Where it
  uses Normal's functions (its move plans and candidates, Normal's charge, casualty and consolidation answers as its
  defaults, the lookahead's shooting) and the shared volleys (its chain's close-quarters volleys get 10.06's and 17.03's -1s
  for MONSTER pairs), it gets the changes above on the roster scenario only; M69 measures from there.
- **Arena (`arena.*`):** for a points scenario the report adds the mean points each side destroyed by the end (D-076's
  tally, whatever decided the game), and every report the decision times of each side's bot alone (Normal's own p95 and
  maximum against Easy). `--per-game` lines are unchanged.

## D-078: Hard for the roster units: one chain cache per game under a memory cap, the coarse wounds grid and compact tables, loadout groups, [HAZARDOUS] and 17.01, the points tiebreak in the army value, the rollout budget by units, Normal's moves for a side of more than 7 units (M69; amends D-067)
**Decided:** 2026-11-08 (M69 of the pre-approved M59–M70 batch; design written before the code and stopped for review, as the
batch's run note asks; the conservative choices are marked *conservative*). Bot only (`src/ai`): no rule, core, state,
action, event or JSON change, no new decision. Amends D-067 (Hard across several units) and the M44 notes of D-054 where
stated. Normal and Easy are untouched (their roster play stays as M68's: `--per-game` hashes, below).
- **Measured first** (the M68 binary, Release, `rosters_44x60.json`, this machine; PLAN.md M69 has the full numbers):
  - *One game each seat (seed 1, `--trace`):* Hard as the Chaos Space Marines against Normal: 275 Hard decisions, p50
    0.0 ms, p95 20.3 s, max 36.7 s, 555 s a game, peak RSS 2.87 GB, lost on points. Hard as the Tyranids: 274 decisions,
    p95 24.2 s, max 33.7 s, 852 s, 2.59 GB, lost on points. Every Hard reason reads "win 0%, draw 100%, loss 0%": D-007's
    army value can't tell a points win from a points loss, so every option is worth about 0.
  - *Where the time goes:* the chain tables. The rosters have 5 x 6 = 30 pairs of unit types; the cache keeps 8 tables
    (`kMaxTables`), and every Evaluator asks for all 30 in the same order, so least-recently-used eviction misses every
    time: the first decision (a MoveUnits) constructed 8 Evaluators, built 240 tables (232 evicted, 0 hits) in 35.7 of its
    36.0 s. A full set of 30 tables takes 4.0–5.4 s and about 1.45 GB (10 half-turns x 780 line positions x the wounds
    grid, 24-byte nodes; the largest, Chaos Terminators (30 wounds) against the Tyrannofex (16), 525 ms and 108 MB). In a
    table build 97% is `build_half_turn` (the shooting sums 44%, the move choices 37%, the charges 10%); the volleys'
    distributions are 2%.
  - *With the cache fixed* (a scratch build keeping 64 tables, nothing else changed): the same two games, play unchanged,
    take 18.6 s and 25.0 s of Hard decisions; the first decision 5.2 s (the 30 builds), then at most 1.18 s; p95 about
    400 ms (as A) and 470 ms (as B); peak RSS 1.41 GB. A callgrind profile of 60 decisions of round 2 then puts 65% of
    the instructions in core's line of sight (06.01): with the Tyrannofex on the board every check takes D-074's oval path,
    which samples both bases. Two callers dominate: the army value's half-turn in progress (`pair_node` calls
    `best_shooting` once per pair, and each call computes `shot_options` against every enemy unit and keeps one: 33%) and
    the rollouts' shooting (Normal's `choose_shooting`, 39%). The Monte Carlo is 54% of a decision; its budgets never bind
    (a roster rollout is 60–130 simulated actions against the 400 cap, a Monte Carlo 400–600 against 6000; Strike's at
    most 87 and 443), so a decision's cost is set by its 6 Monte Carlos x 6 rollouts and by the cost of a simulated action
    (about 150 µs on the rosters, 50 µs on Strike).
  - *Hard's candidates per MoveUnits decision:* Normal's plan and up to 4 standing M43 candidates plus the furthest move
    for the Normal move, the same per D6 result for the Advance (7 `hard_plan_unit_move` calls, about 35 plans valued with
    the chain), 6 Monte Carlos of 6 rollouts (2,000–3,500 simulated actions).
- **1. One chain cache per game, sized to every pair of unit types in play, under a memory cap.**
  - *One cache per game:* the `ChainCache` lives with the bot for the whole game and serves every Hard decision of it. The
    client already plays both sides with one `Bot`; the arena's two Hard bots (Hard vs Hard) now share one cache (tables
    don't depend on the side evaluating; a cache never changes a value, D-054).
  - *Sized to every pair:* the cache's capacity in tables is the larger of 8 (as before, for the duel's many small tables)
    and the number of pairs the latest army Evaluator needed (30 on the rosters at the start).
  - *Memory cap: 640 MiB* of table storage per cache, counted exactly from each table's arrays. When a table must be built,
    least-recently-used tables are evicted first until the cache plus the tables to build fit under the cap; a table held
    by a live Evaluator is never evicted (if the working set alone is over the cap, the cache holds only it). Use order is
    a counter of lookups, so eviction is a function of the sequence of requests: deterministic.
  - *Trimming:* at the start of each Hard decision every cached table not in use drops its half-turns before the
    decision's (no evaluation of this or a later decision reads them; an undo to an earlier half-turn rebuilds). Memory
    falls as the game goes on.
  - *Never a source of behaviour:* a state's value is the same whichever table answers (unchanged, tested since M44), so
    the cap, the eviction and the trimming change only time and memory.
- **2. The tables' form.**
  - *Exact (as today):* every wound a grid point, 8-byte values. Every table of V1, the bot scenarios and Strike.
  - *The coarse wounds grid (the plan's "only above 20 wounds per side"):* for one side of a pair table, when that unit
    type's starting wounds exceed 20 (a side of the table, not an army: Strike's sides are 10 wounds; 20 Termagants are
    20, so they stay exact). Grid levels every s = ceil(W / 20) wounds, plus W itself: the Chaos Terminators (30) get 16
    levels (0, 2, ..., 30). A volley's exact outcome between two levels is split between them in proportion (the expected
    wounds left kept; destroyed stays exact); a coarse level's models alive are ceil(level / W); a lookup at wounds between
    two levels interpolates linearly, as positions between bands already are.
  - *Compact storage:* the stored nodes (start, fight, reply) as 4-byte floats, the arithmetic still in double, when the
    game's tables at full precision would take more than 256 MiB (estimated from the specs of the pairs of the root's
    unit types; Strike's one table is 23 MB, the rosters' 30 about 1.45 GB). The form is part of a table's key, so a
    compact table never answers an exact spec.
  - *Sizes on the rosters:* the never-read rows of 0 wounds are dropped from every table (values unchanged); with the
    coarse grid and compact storage the 30 tables take about 525 MB at the first decision (10 half-turns) instead of
    1.45 GB. Peak resident memory per Hard bot is then expected near 0.6 GB (target: well under 1 GB). Measured
    (`/usr/bin/time -v`, 200-game arena runs): 651–662 MB for Hard against Normal, 574–610 MB for Hard vs Hard.
- **3. Building the tables faster, with the same values** (D-068: the same expressions in the same order).
  - *Position-major layout:* a half-turn's nodes stored by line position, then by wounds cell, so each sum reads one
    position's grid (at most 12.6 KB) instead of rows scattered across the table; each pmf's nonzero range is found once;
    a [HEAVY]-free unit's moved and stationary shooting rows, which are the same sums, are computed once and copied.
    Prototyped in the scratchpad: the fingerprints of every stored value of the four old scenarios' tables (V1, both bot
    scenarios, Strike, along Normal games) and of the rosters' 30 tables are bit-identical to M68's; it saves 15–20% of
    the build time (the sums are compute-bound, not memory-bound).
  - *Parallel builds (approved at the design stop with conditions, all met):* the tables an Evaluator needs that the
    cache doesn't have are built by up to `LookaheadWeights::table_workers` threads (0, the default: min(4, the
    hardware's)), plain `std::thread`s (wh40k_ai already links `Threads::Threads`). Each worker uses its own `OddsCache`
    (whose values never depend on its contents, M40) and writes only its own output table; the calling thread works too,
    joins every worker before the Evaluator's constructor returns (nothing is detached or left running, whatever the
    client does next), and then rethrows the first error in spec order (a worker catches its own). The shared cache is
    changed only on the calling thread after the join, the new tables inserted in pair order, so its contents and order
    are a serial build's. One table to build, or one worker: built on the calling thread with the caller's `OddsCache`,
    as before M69 (every old scenario's case). A mixed unit's keep order (4) is worked out before the workers start, on
    the calling thread. A table is a pure function of its spec, so no value depends on the threads (tested: 1 and 4
    workers give the same table fingerprints and the same Hard decisions). Measured on a quiet machine: the first Hard
    decision of a roster game takes 0.75–0.93 s with the default 4 workers and 2.73–2.92 s with 1.
  - *The client's bot path, checked:* `BotDriver` runs one decision at a time on its `std::async` worker (a new job starts
    only when the previous future is ready; the destructor waits for it), and the client reaches the bot only through the
    driver, whose `set_config` / `invalidate` are applied in `start_job` or `decide_now`, when no worker runs. So nothing
    else touches the bot's cache while a decision (and its table workers) runs; the table workers join before `decide`
    returns.
- **4. A unit's volley is the sum of its loadout groups.** The chain's unit was copies of its most common model. Now:
  - *The unit at full strength:* its models in the order the defender keeps them, the most valuable against the pair's
    other unit type first (Normal's `model_worth`: the model's best shooting at 0" and its best melee weapon; ties in the
    unit's order), padded with copies of its most common loadout up to its starting strength (the models lost so far
    taken as rank and file, so the composition, and the table, stay the same while rank and file die).
  - *n models alive:* the first n of that order; their volley is the engine-shaped sum of each model's own picks
    (`unit_ranged_volley` / `unit_melee_volley` on those models, identical attacks merged, D-006), and the unit as a
    target is the same first n.
  - *Kept as before (conservative):* a unit whose models share one loadout keeps the representative arithmetic exactly
    (every unit of V1, the bot scenarios and Strike, and the Tyranids); a unit whose models differ in W keeps the
    representative (the chain needs one W per model; no roster unit is mixed). The type key of a mixed unit hashes its
    full-strength composition.
- **5. [HAZARDOUS] and 17.01 in Hard's own code.**
  - *[HAZARDOUS] (24.15, D-077):* the chain's volleys and the half-turn in progress (`best_shooting`) price a [HAZARDOUS]
    profile at Normal's `hazard_rate`, so the Havoc champion supercharges in the chain only where Normal would. As in
    Normal's move score, the firer's own expected loss is priced in the choice of profile, not taken off the chain's
    wounds (*conservative*). The rates are part of a table's key when a profile is hazardous.
  - *17.01:* the rollouts' translations (`translation_ok`) let a MONSTER/VEHICLE model's Normal or Advance move pass enemy
    models that aren't MONSTER/VEHICLE models (core's `moves_through_non_monster_enemies`, as Normal's `segment_clear`);
    ends stay off every model and out of Engagement Range, and the engine still decides.
- **6. The points tiebreak in the army value (D-076), the engine's tally.** Only under `"result": "points_destroyed"`:
  - *The end is exact:* a game over takes the engine's result (which already holds the points decision); every half-turn
    played without a result is decided by `core::points_destroyed` (the larger numerator wins, equal is a draw), not
    D-007's draw. A wipe-out still wins at once.
  - *Otherwise:* V = P(A wipes B out) - P(B wipes A out) + P(neither) x T, where T = P(A ahead on points at the end) -
    P(B ahead). Each unit is wiped out by the end with its D-067 fate (independent, as there); then it counts its full
    points, otherwise its losses now. In the engine's own units (numerators over D, its formula: a unit's increment if
    destroyed is points x D / starting wounds x wounds remaining, with core's `wounds_lost`), the expected margin is
    mu = (A's tally - B's tally now) + sum over B's units of P(wiped) x increment - the same over A's, its variance
    sigma^2 = sum of P(1 - P) x increment^2, and T = erf(mu / (sigma sqrt 2)) (Abramowitz and Stegun 7.1.28: arithmetic
    only, no libm, so Windows and Linux agree), the exact sign when sigma = 0 (0 for an exact tie, then a draw). The
    win, draw and loss shown in the reasons are these probabilities. The tempo and material terms are unchanged.
  - *The duel:* under the points rule Hard keeps the army path even with one unit per side (M44's duel is D-007's).
  - *Left out (conservative):* surviving units' future partial losses (the rollouts play the next turn on the engine,
    whose tally is exact); the pair chains still choose their moves on D-007 terms (each is a duel of two units).
- **7. The rollout budget scales with the number of units, counted in simulated actions.** With n the units with models
  in the state each Monte Carlo starts from (counted afresh for every Monte Carlo, so the budget rises again as units are
  destroyed during a game: 3 rollouts at 14 to 11 units, 4 at 10 and 9, 5 at 8, the full 6 from 7 down): up to 7 units
  (V1, the bot scenarios, Strike) the budget is as before, 6 rollouts of at most 400 simulated actions and 6000 a Monte
  Carlo; above 7 both the rollouts and the Monte Carlo's action budget scale by 7 / n (rounded down; at least 3
  rollouts): 14 units 3 rollouts and 3000 actions, 10 units 4 and 4200 (`hard::rollout_budget`). Each simulated
  action costs more with more units (measured 150 µs against 50 µs), so a 14-unit MoveUnits decision runs about 1,000–1,800
  simulated actions instead of 2,000–3,500. Every option of a decision gets the same number of rollouts and the same seeds
  (as before). A new weight `LookaheadWeights::full_budget_units = 7` holds the threshold (`--weights` can change it).
- **8. Other savings with the same values.** The army value's half-turn in progress computes each shooting unit's
  `shot_options` once per evaluation and picks each pair's declaration from them (`best_shooting` on shared options:
  the same options in the same order, filtered by target, so the same plan); measured as a third of a round-2 decision.
  Core is not touched: its line of sight is shared with Normal, which must play exactly as at M68.
- **9. The old scenarios play exactly as before.** Every change acts only through facts V1, the bot scenarios and Strike
  don't have: more than 7 units with models (7), a table over 256 MiB or a side over 20 wounds (2), a mixed loadout (4), a
  [HAZARDOUS] weapon or a MONSTER/VEHICLE (5), the points rule (6, and 11's keep-away move), more than 7 units with
  models on one side (11). Where code is shared (the layout, the cache, the shared
  options, the parallel builds) it gives bit-identical values: checked by table fingerprints against the M68 code, the
  arena's `--per-game` hashes against `baseline/m60` on all 24 files, the replays (`m45_bot_game.json`,
  `m58_strike_bot_game.json` included) and the `TheSavedFileMatchesTheGame` tests. Normal's roster play: `--per-game` on
  the rosters for Easy–Easy, Normal–Easy, Easy–Normal and Normal–Normal (seeds 1–200) against the M68 binary, and the
  M68 replay regenerated byte-identical.
- **10. Items left for M69, checked.** Done here: the representative model (4), [HAZARDOUS] (5), 17.01 in the
  translations (5), the points tiebreak (6), Hard's speed on the roster scenario and fixture (1–3, 7, 8). Left, with the
  reason: the chain has no state for a third unit, so an engaged target's 17.03 -1 or a MONSTER's close-quarters shot at
  another unit enter only the half-turn in progress (the engine's `best_shooting` applies them there); ovals are not
  turned (D-077's measure found no cost); the Desperate Escape guard checks one to three failed rolls (as Normal's);
  Strike's Tyranid seat (M57) can't change (Strike must play as before).
- **11. Strength on the rosters: two changes made after the design, gated as the go-ahead allows** (only on facts the
  old scenarios don't have; no weight that V1, the bot scenarios or Strike read was changed). Release arena against
  Normal, seeds 1–60 unless stated.
  - *The design as built:* over 200 seeds Hard won 159 and lost 41 as the Chaos Space Marines (79.5%), but won only 93
    and lost 107 as the Tyranids (46.5%): below Normal's own 140 of 200 from that seat against Normal (M68).
  - *Where it lost* (a scratch build, never in the repo, letting Hard answer only some decision kinds and Normal the
    rest): with Hard's charges, casualties, coherency and consolidations but Normal's moves the Tyranids won 45 of 60,
    with Hard's moves only 28 (both: 29); the Chaos Space Marines with Normal's moves won 13 of 60. Hard's move search
    is what wins the Chaos Space Marine seat and what lost the Tyranid one. Traced Tyranid games show the pattern: the
    army value rates the Tyranids' position high early (a win at 60–100% in battle round 1), the move search then holds
    units back (Remain Stationary for MONSTERS and infantry alike in rounds 2 and 3) while the Chaos Space Marines shoot
    them, and the estimate is under 10% by round 4; Normal's moves close in. The calibration of the chain for these
    units is left open (M70's tuning, or later).
  - *Kept, 1: no keep-away move under the points rule.* D-067's extra candidate, the move ending furthest from every
    enemy model, plays for D-007's draw; under the points tiebreak there is no draw to play for, so it is not offered
    there (`hard_plan_unit_move`). Tyranids 29 -> 35 of 60; the Chaos Space Marines won 54 of 60, and over seeds 1–200
    159 with the move and 159 without it.
  - *Kept, 2: a side with more than 7 units with models moves as Normal* (`BotWeights::move_search_units = 7`, counted
    at each decision as the rollout budget's n is). At MoveUnits and at a unit move's MoveModels Hard then answers as
    Normal does (its move type and plan, from `normal_policy`); it still makes its own charge, casualty, coherency and
    consolidation decisions, and searches its moves again once its side is down to 7 units. Tyranids 35 -> 44 of 60
    (counting the side's destroyed units too, so Normal's moves all game: 45, the same within noise; not kept); the
    Chaos Space Marines (5 units) play exactly as before; every side of V1, the bot scenarios and Strike has at most 4
    units. Hard's Tyranid decisions also get faster (p95 9.3 ms over seeds 1–200, against 258 ms as first built).
  - *With both, over seeds 1–200:* the Chaos Space Marines 159 wins and 41 losses (79.5%), the Tyranids 137 and 63
    (68.5%, against Normal's own 140 of 200 from that seat), every game on points (PLAN.md M69 has the times and memory).
  - *Tried and not kept* (each a scratch weight or build, removed): `margin` 0.05 (Tyranids 27 of 60 before change 1 and
    32 after it; the Chaos Space Marines 39 of 60 after it, against 54); the pair chains' payoffs on points (each pair's
    terminal valued by the points lead instead of D-007's duel terms: Tyranids 14, Chaos Space Marines 48); a wider
    points lead (0.25 x the half-turns left / all x the sum of the squared increments added to the variance: Tyranids
    29); 6 rollouts at every size (Tyranids 30, decision p95 354 ms, max 3.28 s); `share` 0.8 (Tyranids 33).
- **Ambiguities and the readings taken:**
  - "One cache per game": one cache serving every decision of the game, both sides where one bot plays both (the client)
    and shared by the arena's two Hard bots; not one per decision.
  - "Sized to every pair of unit types in play": its table capacity is at least the pairs the current army needs; the
    memory cap still bounds it.
  - "A coarser wounds grid only when a side's maximum wounds exceed 20": a side of a pair table, i.e. one unit type's
    starting wounds (an army side's total would make Strike coarse); the grid is at most 21 levels.
  - "The sum of its loadout groups" for a unit that has lost models: the defender keeps its most valuable models (as
    Normal's casualty order); the lost ones are taken as rank and file.
  - "Exactly as the engine computes it": the engine's tally for the current losses and the end of the game, its formula
    for what destroying a unit adds; the forecast between now and the end is the fates' (an estimate).
  - "Counted in simulated actions": the action budgets stay the unit of D-054's budgets; the number of rollouts scales
    with them, since the action budgets alone never bind.
  - Compact storage is not in the brief: it is the only way found to keep the rosters' tables well under 1 GB (the coarse
    grid alone leaves about 955 MB); it is gated on size so no old table changes.
  - Parallel builds are not in the brief either: the first decision's 30 builds are about 2.6 s of single-threaded work
    after the other savings, against a 3 s maximum; they are deterministic by construction.
  - "Hard beats Normal in at least 60% of games in each seat": Hard's wins over all 200 games of the seat (a draw or a
    loss is not a win); the seat's result is reported beside Normal's own from that seat (M68), since the scenario is
    not even (Normal vs Normal: Tyranids 140 of 200).
  - "Tune behaviour or weights inside M69 only where the change is gated on facts the old scenarios don't have": part 11's
    two changes are gated on the points rule and on more than 7 units with models on the deciding side; no default
    that V1, the bot scenarios or Strike read changed (the 24-file hashes).
  - "Hard vs Hard on Strike no slower than M68's by more than 5%": the run's wall time, alternating runs; the decision
    p95 is reported beside it.
  - The standing brief's replay for each milestone: none for M69 (*conservative*). Its flow is Hard's play on the
    rosters, which M70's weight sweep will change, and M70's plan adds the Hard vs Hard rosters replay; M57's bot-game
    replay likewise came in M58.
- **Tests (`M69_*`, `tests/ai/m69_test.cpp`):** the cache (its capacity for the rosters, deterministic eviction order,
  the cap respected, trimming, serial and parallel builds alike, the old scenarios' tables equal to the M68 code's by
  fingerprint); the coarse grid only above 20 wounds per side (and exact below); the volley as the sum of the loadout
  groups (the Havocs, the Legionaries, at full strength and with models lost); the tiebreak in the army value (the end
  exact by `core::points_destroyed`, the increments against the engine's tally of a copy with the unit destroyed, the
  sign and a tie, D-007 untouched without the rule); the rollout budget's scaling (7 units unchanged, 14 units 3
  rollouts and 3000 actions, the cap respected); 17.01 in the translations; no keep-away move under the points rule;
  Normal's moves for a side of more than 7 units (and Hard's own for the 5-unit side); legality: Hard vs Normal and
  Normal vs Hard on the rosters, a few games in ctest, more in a disabled test and the arena.

## D-079: List-building data from BSData: points, copy limits, the surcharge, wargear slots and limits (M71 part 1; M72 part 2)
**Decided:** 2026-11-09 (M71 of the pre-approved M71–M77 batch; design written before the code and stopped for review,
as the batch's run note asks; the conservative choices are marked *conservative*). Part 1 (M71): the data, how the
fetch tool reads it from BSData, and the core types. Part 2 (M72): the army lists, their problems and the auto-layout.
Loader-side only: `GameState`, the scenario loader's flat 02.07 check and every replay are unchanged.
- **Where the list rules come from.** 02.06 gives a unit's models and their default wargear; 02.07 lets its wargear
  options alter its weapons "when you include such a unit in your army". The core rules have no points, battle sizes or
  limits on how often a datasheet is taken (`docs/rules/` names a Muster Armies step in 19 without its text). Those come
  from BSData `0f90355`, as agreed with the user (PLAN.md, "List rules from BSData, enforced"): the `pts` cost type
  (`51b2-306e-1021-d207`), and the Army Roster force with the battle size "1. Incursion (1000 Point limit)"
  (`d62d-db22-4893-4bc0`, which also sets the force's 1000-point limit).
- **Survey.** Every selection entry, group, entry link, constraint and modifier under the ten datasheets was dumped
  (shared entries and links resolved, including the children and costs a link carries itself) and classified:
  - *read:* 10 force copy limits and their 10 Incursion modifiers; 5 size brackets; 5 surcharges; 15 slots (plus 2
    nested ones, flattened); 50 fixed weapons; 33 model-type min/max (02.06, already read by D-070); 88 per-model maxima
    (71 plain, 4 with a mutual-exclusion modifier: the Hive Tyrant's two cannons on both slots, 2 per link with
    `shared: false`: its talons, 2 lifted by a link's `set -1`: the Havoc Champion's power fists, 9 inside composites),
    of which 3 bind and are written (below) and the rest are implied by the slots; 22 unit-scoped maxima (8 with a
    "fewer than 10 models" modifier, 2 with an "at least 10 models" one, 3 with "+1 per 10 models"), written as 15
    limits; 2 model-type error groups; 2 option costs;
  - *dropped:* Crusade (constraints on its cost types, the modifier groups for experience names, Legendary Veterans,
    Weapon Modifications, Dark Destiny, Unrivalled Raiders); Boarding Actions (6 modifiers); the Enhancements cost type
    (6); leader `associations` (10); display-only modifiers (names, annotations, `hidden`, `info`, `add-info`); the
    "Heavy" keyword that the Devotees of Destruction detachment appends; 7 maxima and 8 error groups scoped to other
    datasheets (Khorne Berzerkers, Legionaries seen from Havocs, Chosen);
  - *skipped subtrees*, each checked to hold no weapon profile and no `pts` cost or modifier (Enhancements apart, below):
    Mark of Chaos, Chaos icon, Unholy Patronage upgrade, Warlord, Chaos Boons, Weapon Upgrades, and D-070's Crusade,
    Weapon Modifications, Enhancements and Crusade Relic Upgrades;
  - *unrecognised:* none. The same reading flags shapes it doesn't know in other datasheets (Chosen, Khorne Berzerkers,
    Chaos Rhino, Gargoyles, Neurogaunts, Intercessor Squad), so the tool's warning (below) is not idle.
- **The data:** a root key `list_building`, written after `weapons` by `tools/fetch_bsdata.py`. Every existing key stays
  byte-identical. The block for the Chaos Terminator Squad, shortened (`...` marks left-out entries):
  ```json
  "list_building": {
    "points": [{"min_models": 5, "points": 185}, {"min_models": 6, "points": 370}],
    "copy_surcharge": {"earlier_copies": 2, "points": 30},
    "max_copies": {"default": 3, "incursion": 2},
    "wargear": [
      {"model_type": "Accursed weapon and combi-bolter", "fixed": ["Combi-bolter", "Accursed weapon"],
       "slots": [], "limits": []},
      ...
      {"model_type": "Heavy weapon", "fixed": [],
       "slots": [
         {"name": "Ranged weapon", "default": "Heavy flamer",
          "options": [{"name": "Heavy flamer", "weapons": ["Heavy flamer"]},
                      {"name": "Reaper autocannon", "weapons": ["Reaper autocannon"]}]},
         {"name": "Melee weapon", "default": "Accursed weapon", "options": [...]}],
       "limits": []},
      ...
      {"model_type": "Terminator Champion", "fixed": [],
       "slots": [{"name": "Wargear", "default": "Accursed weapon and Combi-bolter",
                  "options": [{"name": "Accursed weapon and Combi-bolter",
                               "weapons": ["Accursed weapon", "Combi-bolter"]},
                              ...,
                              {"name": "Paired accursed weapons", "weapons": ["Paired accursed weapons"]}]}],
       "limits": []}
    ],
    "unit_limits": [
      {"weapons": ["Power fist"], "max": 6, "below_models": {"models": 10, "max": 3}},
      {"weapons": ["Chainfist"], "max": 2, "below_models": {"models": 10, "max": 1}},
      {"model_type": "Heavy weapon", "max": 2, "below_models": {"models": 10, "max": 1}},
      {"weapons": ["Paired accursed weapons"], "max": 2, "below_models": {"models": 10, "max": 1}}
    ]
  }
  ```
  The other forms: an option cost `{"name": "Rupture cannon", "weapons": ["Rupture cannon"], "points": 20}`; a limit
  counting an option `{"option": "Plasma pistol and Astartes chainsword", "max": 1}`; one per N models
  `{"weapons": ["Shardlauncher"], "max": 0, "per_models": {"models": 10, "add": 1}}`; a per-model limit over two weapons
  `"limits": [{"weapons": ["Heavy venom cannon", "Stranglethorn cannon"], "max": 1}]`. (The file itself is written with
  the tool's usual `indent=2`, one array element per line.)
- **What the fields mean.**
  - `points`: size brackets, `min_models` strictly increasing, the first equal to the composition's minimum. A unit of n
    models costs the last bracket with `min_models <= n`, plus each model's option points.
  - `copy_surcharge` (absent: none): a copy costs `points` more when at least `earlier_copies` copies of the same
    datasheet come earlier in the army.
  - `max_copies`: how many copies an army may hold: `default` (the Army Roster force) and `incursion` (with the
    Incursion battle size). Custom battles use `incursion` (PLAN.md: Incursion only).
  - `wargear`: one entry per model type, in `model_types` order. `fixed`: weapons every model of the type carries.
    `slots`: each model takes exactly one option of each slot (02.07). `default`: the option the slot starts with.
    An option gives one weapon or several (a composite); `points` is written only when not 0.
  - A limit counts exactly one of: `weapons` (how many of these weapons the models carry, summed), `option` (how many
    models took an option of that name) or `model_type` (how many models of that type). Its maximum is `max`; with
    `below_models` it is `below_models.max` while the unit has fewer than `below_models.models` models; with
    `per_models` it is `max + add x floor(models / per_models.models)`. A model type's `limits` apply to each model of
    it (only `weapons` or `option`, never a size term); `unit_limits` apply to the whole unit.
- **Patterns read** (the BSData shape behind each):
  - *Brackets:* the unit's `pts` cost is the first bracket; a unit-level `set` of `pts` with the one condition `atLeast N`
    (or `greaterThan N`) selections of `model` in the datasheet's own scope starts a bracket at N (or N + 1).
  - *Surcharge:* a unit-level `increment` of `pts` whose one condition group holds one local condition group: at least K
    selections in scope `parent` (the force) that come `before` this one and are `instanceOf` this datasheet.
  - *Copies:* the unit's `max` constraint on selections in scope `force`; its `set` modifier with the one condition "at
    least 1 '1. Incursion (1000 Point limit)' in the roster" gives `incursion`.
  - *Slots:* a group with min 1 and max 1 in scope `parent`, under a model type at any depth (through groups with no
    bounds, such as "Wargear"). Default: the group's `defaultSelectionEntryId`, else its first entry (D-070's rule; BSData
    names none for Legionary w/ other weapon's "Weapon" and the Terminator Heavy weapon's "Ranged weapon").
  - *Options:* an entry with a weapon profile is that weapon. An entry without one that holds min-1 weapon entries is a
    composite of them, in BSData order ("Plasma pistol and Astartes chainsword"). An entry that holds min 1 / max 1
    groups is flattened: one option per combination (the first nested group varying slowest), named by its choices
    joined ("Power fist and Combi-weapon"), weapons in nested-group order, points summed. Option names must be unique in
    their slot.
  - *Fixed weapons:* a weapon entry with min >= 1 in scope `parent` under the model type, outside every slot.
  - *Option costs:* the entry's `pts`; an entry link's own `pts` replaces the target's (the Acid spray's +10 is on the
    link, the Rupture cannon's +20 on the entry).
  - *Per-model maxima:* `max N` on an entry, scoped to the model (`parent` where the parent selection is the model,
    `model`, or the model type's own id). With `shared: true` it counts the entry through every link in the model, with
    `shared: false` through this link only. It is written only where it binds: where the model's fixed weapons and
    slots could give it more than N. A link's unconditional `set -1` removes it. A `max 1` with `set 0` when at least 1
    of another weapon is in the model, mirrored on that weapon, is one limit over both weapons, max 1. A written limit
    implied by another (a subset of its weapons, a larger or equal max) is left out.
  - *Unit maxima:* `max N` on an entry, scoped to the datasheet's own id or to `unit`, with at most one modifier on it:
    `set M` when fewer than T models (`below_models` {T, M}), `set M` when at least T models (`max` M, `below_models`
    {T, N}), or `increment A` repeated per N models rounding down (`per_models` {N, A}). It counts the weapon by name
    when the entry is one weapon and every carrier of that weapon name in the unit is the same BSData entry, and the
    option when the entry is a composite. A limit read on several links is written once.
  - *Error groups:* `add error` with one `and` group of exactly "fewer than T models in the unit" and "more than K of X
    in the unit": X at most K below T models; from T models X's own maximum applies (its unit maximum, or for a model
    type its 02.06 max). On a model type it is a `model_type` limit.
- **Dropped** (scope rule: a constraint or condition counts only if its scope is the model, `parent`, the unit itself
  (its own id or `unit`), `force` or `roster`): anything scoped to another datasheet (the Khorne Berzerkers'
  plasma-pistol error group, the Legionaries' Meltagun maximum seen from the Havoc Champion, the Chosen's combi-weapon
  and paired-accursed-weapons maxima seen from the Terminators); other game modes (Crusade, Boarding Actions: any
  modifier with a condition on another force, `hidden` toggles included); display (names, annotations, categories,
  `info`, `add-info`, and modifiers that change profiles, such as keyword appends); leader `associations`; the Crusade
  and Enhancements cost types (constraints on them and modifiers of those constraints); the skipped subtrees. A
  `hidden` toggle that doesn't depend on another game mode is unrecognised.
- **Anything else in scope** (a constraint, modifier, group or entry of another shape outside a skipped subtree, e.g. an
  optional weapon outside a slot, a min 0 / max 1 group, a points modifier of another shape, a hidden weapon option):
  the tool warns and writes no `list_building` for that datasheet (*conservative*: partial list rules would let the
  builder accept armies BSData refuses). None in the ten.
- **Readings** (each the literal BSData; the per-unit table below has the rest):
  - Model counts include the champion: BSData counts every `model` selection in the unit.
  - Brackets as BSData has them: a Chaos Terminator Squad costs 370 from 6 models on, not only at 10.
  - Terminators' surcharge is **+30**; Havocs, Genestealers, Screamer-killer and Tyrannofex +10; Legionaries,
    Termagants, Chaos Lord, Hive Tyrant and Tyrant Guard have none. At Incursion (2 copies, 4 for Battleline, and no
    Battleline datasheet has a surcharge) it never applies; it is kept in the data, as agreed. Earlier copies are counted
    in army order; M72 uses the list's order.
  - A Legionaries Aspiring Champion's plasma pistol and a "Plasma pistol and Astartes chainsword" special are **both
    allowed** in one unit: the special's limit counts that option (max 1 per unit), not plasma pistols, and the "Max 1
    plasma pistol per 5 models" error group is scoped to Khorne Berzerkers.
  - The Aspiring Champion takes at most one plasma pistol (the shared entry's max 1 per model, over both slots), but may
    take two accursed weapons, two Astartes chainswords or two heavy melee weapons (each slot has its own entries for
    those, with no maximum).
  - The Havoc Champion takes at most one accursed weapon (max 1 per model over both slots) but may take **two power
    fists** (both of its power fist links set that maximum to -1).
  - A Hive Tyrant may take **two Monstrous scything talons**, one per slot (each slot's link has its own max 1,
    `shared: false`), and at most one of Heavy venom cannon and Stranglethorn cannon.
  - **Terminator combi-weapons have no limit at any unit size**, 10 models included: BSData's combi-weapon limits (max
    4, "Max 2 {this} per 5 models") are scoped to Chosen. Only the model types bound them (up to 10 in a 10-model unit).
  - The Terminator Heavy weapon: BSData gives max 2 and an error group (more than 1 below 10 models), so 1 for 5–9
    models and 2 at 10; for the legal sizes that is the brief's "+1 per 5 models". Legionary w/ other weapon likewise.
  - Terminator paired accursed weapons count the champion's option too (the same BSData entry): at most 1 below 10
    models, 2 at 10.
  - Termagant specials: each of Shardlauncher, Spike rifle and Strangleweb at most 0 + 1 per 10 models (10–19 models:
    1, 20: 2).
  - The defaults equal `default_weapons`, including options the engine refuses today (the Chaos Lord's Daemon hammer,
    the Havoc Champion's Flamer, the Heavy weapon's Heavy flamer). The builder's own defaults skip those (M72, agreed).
  - Not offered: Enhancements (optional; 5 to 40 pts each in BSData; their rules aren't implemented), Marks of Chaos,
    Chaos icon, Unholy Patronage, Warlord and Chaos Boons (0 pts). A list without them is legal, so leaving them out
    never changes a legal list's points.
- **Noted for part 2 (M72), not part of the data:**
  - BSData's Army Roster force requires at least one CHARACTER (category constraint, min 1 per roster). **Decided with
    the user (2026-11-09): custom armies must include at least one CHARACTER** (here a Chaos Lord or a Hive Tyrant;
    both stock rosters have one). M72 enforces it (D-079 part 2).
  - Weapons the engine refuses today (D-026, D-071; from their abilities): Daemon hammer, Combi-weapon, Chainfist, Heavy
    flamer, Reaper autocannon, Meltagun, Flamer, Balefire tome, Strangleweb (marked † below). The data lists them; the
    builder greys them out (agreed).
- **Core (M71; `src/core/data/`).**
  - `datasheet.hpp`: `WargearOption {name, weapons, points}`, `WargearSlot {name, options, std::optional<std::string>
    default_option}`, `WargearLimit {counts (Weapons / Option / ModelType), names, max, std::optional<BelowModels>
    below_models, std::optional<PerModels> per_models}`, `ModelWargear {model_type, fixed, slots, limits}`,
    `PointsBracket {min_models, points}`, `CopySurcharge {earlier_copies, points}`, `ListBuilding {points, surcharge,
    max_copies_default, max_copies_incursion, wargear, unit_limits}`, and `std::optional<ListBuilding>
    Datasheet::list_building` (absent in a file without the key: `intercessor_squad.json`).
  - The loader refuses inconsistent data with a `DataError` citing D-079: a `wargear` list that isn't exactly the model
    types in order; an unknown weapon (not in the file's `weapons`); a fixed weapon or option weapon outside its model
    type's `weapon_options`; an empty slot, an option with no weapons, two options of one name in a slot, a default that
    isn't one of the slot's options; brackets not strictly increasing, not starting at the composition's minimum or
    past its maximum, or points outside 1..100000 (as D-076); negative option points; copies below 1; a surcharge with
    `earlier_copies` or `points` below 1; a limit that counts none or several of weapons / option / model type, names an
    unknown option or model type, has a negative max, both `below_models` and `per_models`, or (per model) a size term
    or a model type.
  - `army_rules.{hpp,cpp}` (new, read-only): `loadout_fits_slots(wargear, weapons)` decomposes a model's weapons (a
    multiset, order ignored) into its fixed weapons plus one option per slot. It returns the option chosen for each slot
    (the first decomposition in slot order and option order, by backtracking: deterministic) or a reason citing 02.07
    and D-079. In the ten datasheets every decomposition of one multiset costs the same (the only priced options are
    the Tyrannofex's, in its one slot). Limits are not part of it (M72's army checks apply them). Also
    `fit_option_points(wargear, fit)`, `bracket_points(list_building, models)` and `limit_max(limit, models)`, and the
    finders `ListBuilding::find_wargear` and `WargearSlot::find_option`. M72 builds the army checks on these. The
    scenario loader doesn't call them (its flat 02.07 check is unchanged); a test checks that every shipped scenario's
    units fit the slots.
- **Fetch tool.** `--fetched <YYYY-MM-DD>` writes that date to `source.fetched` (default: today). The ten files are
  regenerated with their own dates (2026-11-08; `termagants.json` 2026-11-02). The current tool reproduces all ten
  byte for byte apart from the date (checked before M71), so the diff of each is only the new block and the comma before
  it. `intercessor_squad.json` is not regenerated.
- **Per-unit table** (at BSData `0f90355`; † = refused by the engine today; "(d)" = the slot's default; model-type
  counts are 02.06's min–max):
  - **Chaos Lord** (CHARACTER). Points: 1 model 95. Copies 3, Incursion 2. Surcharge none.
    - Chaos Lord (1): fixed none. Slot "Daemon hammer": Daemon hammer† (d), Accursed weapon, Astartes chainblade. Slot
      "Plasma pistol": Power fist, Plasma pistol (d). No limits.
    - Stock: Astartes chainblade + Plasma pistol = 95.
  - **Legionaries** (BATTLELINE). Points: 5 models 95, 6–10 180. Copies 6, Incursion 4. Surcharge none.
    - Aspiring Champion (1): fixed Close combat weapon. Slot "Replace bolt pistol": Accursed weapon, Astartes
      chainsword, Heavy melee weapon, Bolt pistol (d), Plasma pistol. Slot "Replace boltgun": Accursed weapon, Astartes
      chainsword, Heavy melee weapon, Boltgun (d), Plasma pistol. Per model: Plasma pistol at most 1.
    - Legionary w/ boltgun (0–9): fixed Boltgun, Bolt pistol, Close combat weapon.
    - Legionary w/ chainsword (0–9): fixed Astartes chainsword, Bolt pistol, Close combat weapon.
    - Legionary w/ heavy melee weapon (0–1): fixed Heavy melee weapon, Bolt pistol, Close combat weapon.
    - Legionary w/ balefire tome (0–1): fixed Balefire tome†, Bolt pistol, Close combat weapon.
    - Legionary w/ other weapon (0–2): fixed Bolt pistol, Close combat weapon. Slot "Weapon": Havoc autocannon (d),
      Heavy bolter, Lascannon, Missile launcher, Reaper chaincannon, Plasma pistol and Astartes chainsword (Astartes
      chainsword + Plasma pistol), Meltagun†, Flamer†, Plasma gun.
    - Unit: each of Havoc autocannon, Heavy bolter, Lascannon, Missile launcher, Reaper chaincannon, Meltagun, Flamer
      and Plasma gun at most 1; the option "Plasma pistol and Astartes chainsword" at most 1; Legionary w/ other weapon
      at most 2, 1 below 10 models.
    - Stock (two units, 10 models each): Aspiring Champion Bolt pistol + Astartes chainsword (+ Close combat weapon)
      with 9 Legionary w/ chainsword = 180; the same champion with 9 Legionary w/ boltgun = 180.
  - **Chaos Terminator Squad**. Points: 5 models 185, 6–10 370. Copies 3, Incursion 2. Surcharge +30 from the 3rd copy.
    - Fixed-only types: Accursed weapon and combi-bolter (0–9): Combi-bolter, Accursed weapon. Accursed weapon and
      combi-weapon (0–9): Accursed weapon, Combi-weapon†. Power fist and combi-weapon (0–6): Power fist, Combi-weapon†.
      Chainfist and combi-bolter (0–2): Chainfist†, Combi-bolter. Power fist and combi-bolter (0–6): Combi-bolter,
      Power fist. Chainfist and combi-weapon (0–2): Chainfist†, Combi-weapon†. Paired accursed weapons (0–2): Paired
      accursed weapons.
    - Heavy weapon (0–2): slot "Ranged weapon": Heavy flamer† (d), Reaper autocannon†. Slot "Melee weapon": Accursed
      weapon (d), Chainfist†, Power fist.
    - Terminator Champion (1): slot "Wargear": Accursed weapon and Combi-bolter (d), Accursed weapon and Combi-weapon,
      Chainfist and Combi-bolter, Chainfist and Combi-weapon, Power fist and Combi-bolter, Power fist and Combi-weapon
      (the six flattened from BSData's "Combi-bolter and accursed weapon", whose nested groups are "Accursed Weapon":
      Accursed weapon / Chainfist / Power fist and "Combi-bolter": Combi-bolter / Combi-weapon), Paired accursed weapons.
    - Unit: Power fist at most 6, 3 below 10 models; Chainfist at most 2, 1 below 10; Heavy weapon models at most 2, 1
      below 10; Paired accursed weapons at most 2, 1 below 10. No combi-weapon limit.
    - Stock: Terminator Champion "Accursed weapon and Combi-bolter" with 9 Accursed weapon and combi-bolter = 370.
  - **Havocs**. Points: 5 models 135. Copies 3, Incursion 2. Surcharge +10 from the 3rd copy.
    - Havoc Champion (1): slot "Melee weapon": Accursed weapon, Power fist, Astartes chainsword (d). Slot "Ranged
      weapon": Plasma pistol, Accursed weapon, Power fist, Boltgun, Meltagun†, Flamer† (d), Plasma gun. Per model:
      Accursed weapon at most 1 (two power fists allowed).
    - Havoc w/ autocannon, w/ lascannon, w/ missile launcher, w/ reaper chaincannon, w/ heavy bolter (0–4 each, 4 in
      all): fixed that heavy weapon (Havoc autocannon, Havoc lascannon, Havoc missile launcher, Havoc reaper
      chaincannon, Havoc heavy bolter) and Close combat weapon.
    - Unit: none.
    - Stock: Havoc Champion Astartes chainsword + Plasma gun with 4 Havoc w/ lascannon = 135.
  - **Termagants** (BATTLELINE). Points: 10 models 55, 11–20 110. Copies 6, Incursion 4. Surcharge none.
    - Termagants (7–20): fixed Chitinous claws and teeth. Slot "Weapons": Fleshborer (d), Termagant spinefist,
      Termagant devourer.
    - Termagant w/ Shardlauncher, w/ Spike rifle, w/ Strangleweb (0–2 each): fixed that weapon (Strangleweb†) and
      Chitinous claws and teeth.
    - Unit: each of Shardlauncher, Spike rifle and Strangleweb at most 0 + 1 per 10 models.
    - Stock (two units): 10 Termagants with Fleshborer = 55 each.
  - **Genestealers**. Points: 5 models 75, 6–10 140. Copies 3, Incursion 2. Surcharge +10 from the 3rd copy.
    - Genestealer (5–10): fixed Genestealers claws and talons. No slots, no limits.
    - Stock (two units): 5 Genestealers = 75 each.
  - **Tyrant Guard**. Points: 3 models 80, 4–6 170. Copies 3, Incursion 2. Surcharge none.
    - Tyrant Guard (3–6): slot "Wargear": Scything talons and rending claws (d), Bone cleaver, lash whip and rending
      claws, Crushing claws and rending claws (each one weapon). No limits.
    - Stock: 3 with Scything talons and rending claws = 80.
  - **Screamer-killer**. Points: 1 model 125. Copies 3, Incursion 2. Surcharge +10 from the 3rd copy.
    - Screamer-killer (1): fixed Bio-plasmic scream, Screamer-killer talons.
    - Stock (two units): 125 each.
  - **Hive Tyrant** (CHARACTER). Points: 1 model 195. Copies 3, Incursion 2. Surcharge none.
    - Hive Tyrant (1): slot "Monstrous Bonesword and Lash Whip": Monstrous bonesword and lash whip (d), Heavy venom
      cannon, Stranglethorn cannon, Monstrous scything talons. Slot "Monstrous Scything Talons": Monstrous scything
      talons (d), Stranglethorn cannon, Heavy venom cannon. Per model: Heavy venom cannon + Stranglethorn cannon at
      most 1.
    - Stock: Monstrous bonesword and lash whip + Monstrous scything talons = 195.
  - **Tyrannofex**. Points: 1 model 170. Copies 3, Incursion 2. Surcharge +10 from the 3rd copy.
    - Tyrannofex (1): fixed Powerful limbs, Stinger salvoes. Slot "Fleshborer Hive": Rupture cannon (+20), Fleshborer
      hive (d), Acid spray (+10).
    - Stock: Fleshborer hive = 170.
- **Checked against the flat data and the stock rosters** (with the survey's reading; M71's tests pin each):
  - For every model type of the ten, its fixed weapons plus every option's weapons are exactly its `weapon_options`
    (no exceptions), and its fixed weapons plus each slot's default are exactly its `default_weapons`.
  - Every unit of every scenario in `data/scenarios/` and `tests/data/scenarios/` that uses one of the ten datasheets
    decomposes into the slots, within the limits, and its `points` (where given) is what the data computes: the
    Incursion rosters cost **960** (Legionaries 180 x 2, Chaos Lord 95, Chaos Terminator Squad 370, Havocs 135) and
    **955** (Termagants 55 x 2, Genestealers 75 x 2, Screamer-killer 125 x 2, Tyrant Guard 80, Hive Tyrant 195,
    Tyrannofex 170). No second copy pays the surcharge (it starts at the third).
