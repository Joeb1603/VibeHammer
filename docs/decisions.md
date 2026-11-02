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
- V1 values: Intercessors 1 rank of 5; Termagants 2 ranks of 5; gap 1"; centred at x = 12".
  The two front lines are exactly 12" apart, so a unit is within 12" of the enemy (charge-eligible,
  11.02) from turn 1. Both formations satisfy 03.03 (a Termagant line of 10 would not: it is about 20"
  wide and breaks the 9" clause).
- The loader must check the result is in coherency (03.03) and wholly within the deployment zone, and
  fail with an exception (corrupt data) if not.
- Accepted consequence (2026-11-02): at these positions only the centre Intercessor is within 12" (half
  Bolt Rifle range) of the Termagants; the other four are 0.01-0.07 mm further, so on turn 1 they don't
  get [RAPID FIRE] (24.30). This is intentional: it exercises per-model range checks. Two of them miss
  by only 0.008 mm, just beyond `EPS_MM` (0.01 mm), so changing `EPS_MM` would change this; see
  `R24_30_RapidFire.HalfRangeIsMeasuredToTheClosestTargetModel`.

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

## Note on app-only rule wording (2026-11-02)
D-009, D-010, D-011 and D-012 use wording the user obtained from an AI assistant, not copied from the
Warhammer 40,000 app. Treat them as working assumptions: if the official app text differs, update the
decision, the code that cites it, and its tests.
