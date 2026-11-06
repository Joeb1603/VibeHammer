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
  checks by simulating `inflict_damage` on a copy of the unit). **Amended by D-051 (M35):** it is now
  skipped only when no save roll inflicts damage; a forced casualty is still asked for.
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
  takes the first legal order, and `OrderCasualties` takes `CasualtyPlan`'s default order. These are the
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
    skips the picking.
- **Default (user, 2026-11-06):** "Allocate damage automatically" is now **off** by default, so casualties
  are picked by hand unless the player ticks it. (The allocation-group order, 05.03, is then asked too, but
  only when a unit has more than one legal order.) A destroyed model's red cross and its "-N" have a black
  outline, so they show on red bases.
- **Replaces D-032's casualty bar:** the "Custom order..." grid is gone. Without the board (no board
  interaction), the sidebar offers only "Use default order".

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
  Stationary, SkipChargeMove, PassFight, the `MeleePlan` default, the `CasualtyPlan` default). The same state hash
  and decision three times means the fallback is used. If the fallback is rejected, the bot stops and shows the
  reasons instead of looping.
- **Client:** which sides are bots is client state (`std::array<bool,2>`), plus an optional `"players"` block in the
  save JSON; there is still no seat or player abstraction in core. Human input is ignored while a bot owns the
  pending decision. Auto-allocate (D-038) never answers a bot's decision. Undo pops the bot's trailing input groups
  and the human's last one. Bot pacing (thinking pauses, auto-rolled bot dice) is presentation only.
- **Levels:** Easy = the M12 test policy (baseline), Normal = the M41–M43 decisions, Hard = the M44 evaluator and
  lookahead. Tuned weights and arena results are recorded here as the milestones land.
- **M39 notes:**
  - **Files:** `src/ai/` holds `bot` (the `Bot` class, `BotConfig`, `BotChoice`, the level dispatch), `state_hash`,
    `simulator`, `baseline_bot` (level Easy), `fallback` (the fallback and `LoopGuard`), `bot_driver` (pacing) and
    `arena` (+ `arena_main.cpp`, exe `wh40k_bot_arena`). `BotLevel` and `BotSpeed` are the client's enums
    (`settings.hpp`); `ai::speed_factor` is the one place the speed multipliers live (the bot's dice pop-ups use it
    too). Normal and Hard play as Easy until M41/M44 (`effective_level`); the start screen and the game status say so.
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
    move, and the arena sees no `UnitMoveReverted` at all.
  - **Loop guard:** `LoopGuard` counts visits per state hash; the third visit uses the fallback. `Bot::forget()` (an
    undo, a new game, a load) clears it and the memo, so replaying a position after an undo is not a "loop".
  - **Driver:** `BotDriver::tick(now, inputs)` keys each decision on (state hash, history size, generation). A new key
    starts a job (on a `std::async` worker, or synchronously at Instant speed and in tests); a stale worker is waited
    for, its result dropped. The pause (0.8 s activation, 0.12 s placement, 0.4 s commit and step buttons, 0.6 s
    allocation and casualties, times the speed factor) starts when the decision appears and restarts while a pop-up
    is open or the bot is paused; Step hands over the next action at once. A refused or stopped bot stops the driver
    (the panel shows the reasons) until the next invalidate.
  - **Client:** the action bar shows "Bot (<army>) is thinking..." (or "is paused" / "has stopped") with Pause /
    Resume, Step, the bot speed and the next or last reason. While a bot owns the decision the board is inactive (no
    clicks, drags, move handle or menus) and the decision panel isn't drawn; `--steps` also stops there. Each bot
    action is its own Undo group, tagged `InputOwner::Bot`; against a bot, Undo (`undo_human`) removes the bot's
    groups after the human's last input and that input, and is disabled until the human has an input to undo (so it
    is always off in bot vs bot). "Undo last step" is never offered for the bot's placements. A bot's dice pop-ups
    roll after 0.35 s and continue after 1.2 s (times the speed factor; at Instant, at once, with no tumble) when "Bot
    rolls its own dice automatically" is on (a sidebar toggle in a game with a bot); a click or Enter skips the wait.
  - **Save players block (extends D-025):** `saved_game.{hpp,cpp}` writes
    `"players": {"A": "human"|"bot", "B": ..., "bot_level": "easy"|"normal"|"hard"}`. Loading restores the bot sides
    and that game's level (kept apart from the settings' level, so loading doesn't change the user's setting). A save
    without the block, a bare GameState, or a block that can't be read loads as hotseat; an unknown level keeps Easy.
  - **CLI:** `--bot A|B|both|none`, `--bot-level`, `--bot-speed`, `--bot-steps n` (bad values exit 2). `--bot-steps`
    runs after `--steps` and stops at a human's decision, so `--seed 1 --bot B --bot-steps 20` shows side A's first
    decision (A moves first); `--seed 1 --bot B --steps 6 --bot-steps 4 --bot-speed slow` shows the bot mid-move.
  - **Build:** `src/client_2d/logic` is now added from the root CMakeLists (before `src/ai`, which links it, and
    `src/client_2d`, whose exe links `wh40k_ai`).

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
