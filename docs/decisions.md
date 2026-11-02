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
