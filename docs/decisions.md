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
  the raw die once, then apply modifiers.

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
  checks by simulating `inflict_damage` on a copy of the unit).
- The same approach applies to mortal wounds (06.02) when they are wired into the engine.
- D-004 extension: a permutation is too large to enumerate, so `legal_actions` returns
  `OrderCasualties` as a **template** with an empty `priority`; any permutation of the target unit's
  models fills it.
- Revisit if a rule adds dice during 05.04 (e.g. Feel No Pain, 24.12; not in V1).

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

## D-022: Pile-in and consolidation movement details (12.03, 12.08)
**Decided:** 2026-11-02 (M10). **Confirmed by the user** (2026-11-02).
- **Closest target.** "Closer to the closest pile-in target / selected enemy unit" is measured from the
  moved model's start position, edge to edge. If units tie for closest, any of them counts (D-010: the
  controlling player chooses). The check applies only to models that are moved (03.01, as D-018 for
  charges).
- **Base contact.** A model is in base contact with an enemy model if it is within `EPS_MM` of it at the
  start of the move. Such a model cannot be moved in a pile-in move or an Ongoing Consolidation.
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
  - **Casualties.** The casualty editor starts in unit order (D-013).
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
  means; the sidebar legend uses the same text (`ring_name`, `ring_meaning`). Engagement range stays
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
  can't be clicked is drawn dark grey (80% towards {55, 55, 60}). Nothing is grey while models are being
  dragged, in the sidebar's decisions, or when only the end-phase button is left. The unit picked first
  has a yellow outline and chosen targets a red one.
- **Sidebar.** Unit buttons are not listed while the board handles the decision; the sidebar shows the
  decision, a hint, the step-level buttons (End shooting, End charges, Pass, Commit / Reset move, Make no
  charge move, ...), a confirm button for a chosen target set, and the selected shooter's per-model
  editor. Other decisions (melee declarations, allocation order, casualties, attack batches, coherency)
  stay in the sidebar as before, with nothing grey.
- Esc cancels a board selection; it no longer closes the window.

