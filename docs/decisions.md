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

## D-009: Wholly Within (01.04 "See also", app-only rule)
**Decided:** 2026-11-02 (wording confirmed by the user)
"Within/Wholly Within" is an app-only rule; its wording is not in `docs/rules/`. Needed by D-008's
deployment-zone check.

- A model is **wholly within** an area if every part of its base is inside that area. In V1 (2D, round
  bases) this means the base circle lies inside the area, using the `EPS_MM` tolerance
  (`geometry/distance.hpp`: `wholly_within`).
- A unit is wholly within an area if every one of its models is.
- Plain **within** (e.g. "within range of an objective") is not covered here; M3 asks for its wording.
