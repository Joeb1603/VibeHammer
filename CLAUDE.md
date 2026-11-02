# VibeHammer: Warhammer 40,000 11th Edition Rules Engine

A deterministic C++20 rules engine for Warhammer 40k 11th Edition, with a 2D top-down Windows client.
V1 goal: one Space Marine Intercessor Squad vs one Tyranid Termagant unit, played hotseat on a 24"x24" board,
following the real core rules.

The milestone plan is in `PLAN.md`. Before starting work, check which milestone is current and stay inside its scope.

## Repository layout

```
CLAUDE.md                 this file
PLAN.md                   milestone plan with acceptance criteria
docs/rules/               verbatim 11th ed Core Rules, one file per section (gitignored, regenerate it)
  00_index.md             rule number -> file lookup (all 156 rule numbers)
data/units/*.json         unit datasheets generated from BSData (do not hand-edit)
data/scenarios/*.json     battlefield size, deployment, armies, loadouts
tools/extract_rules.py    PDF -> docs/rules/   (needs pdftotext on PATH)
tools/fetch_bsdata.py     BSData wh40k-11e -> data/units/
src/core/                 wh40k_core: headless rules library
src/client_2d/            wh40k_client_2d: raylib + Dear ImGui executable
tests/                    wh40k_tests: GoogleTest, run via CTest
```

## Build and test (Windows, VS 2026 Community, CMake 4.x)

Use the presets in `CMakePresets.json` (created in milestone M1):

```powershell
cmake --preset default                       # configure (Visual Studio 18 2026 generator, x64)
cmake --build --preset debug                 # build all targets
ctest --preset debug                         # run all tests
ctest --preset debug -R AttackSequence       # run a subset
.\build\default\src\client_2d\Debug\wh40k_client_2d.exe
```

- Dependencies come from CMake `FetchContent` pinned to exact tags: raylib, Dear ImGui, rlImGui, nlohmann_json, GoogleTest.
- Treat warnings as errors in `wh40k_core` (`/W4 /WX /permissive-`). Don't silence a warning to get a build through.
- A change is not done until it builds and `ctest` passes. Report test failures as they are; never skip, delete or weaken a test to get to green.

## Regenerating source material

```powershell
python tools/extract_rules.py                # rebuild docs/rules/ from 11th-ed-core-rules.pdf
python tools/fetch_bsdata.py "Imperium - Space Marines.json" "Intercessor Squad" data/units/intercessor_squad.json --ref <sha>
python tools/fetch_bsdata.py "Tyranids.json" "Termagants" data/units/termagants.json --ref <sha>
```

Each unit JSON records the BSData commit it came from in `source.commit`. Pin to that sha unless you are deliberately updating the data.

## Rules: the source of truth

- **Before implementing or changing any game mechanic, look up its rule number in `docs/rules/00_index.md` and read that section file.** Don't implement from memory: 11th edition differs from 10th in many details (e.g. 2" engagement range, allocation groups, consolidation modes, Bolt Rifle firing modes).
- The rules text is verbatim pdftotext output. Sidebars, diagrams and example captions can appear out of order. Rule text is the authority, and examples only illustrate it.
- In section 05, the dice-face tables (hit, wound and save) were transcribed from page images. Everything else is raw extraction.
- If a rule is ambiguous, or `docs/rules/` disagrees with what you expected, **stop and ask**. Don't guess. Record the decision in `docs/decisions.md` (create it if missing), with the rule number.
- Cite rule numbers in code and tests: `// 05.04 Inflict Damage: lowest save roll first` and `TEST(R05_04_InflictDamage, ResolvesLowestRollFirst)`.
- **Known gaps (app-only rules):** the PDF names some rules under "See also" without including their text, because they live in the Warhammer 40,000 app. The ones V1 touches are: Modifying Dice Rolls, Modifying Damage, Modified Characteristics, Characteristic Modifiers, Random Characteristics, Multiple Weapon Profiles (D-001), Target No Longer Eligible or Viable, Closest Model/Unit (M3, M10), Within/Wholly Within (M3, M8), Multiple Battle-shock Rolls (M7, M11), Re-rolls and Rules Sequencing. "Destroyed" is partly covered: the Rules Appendix (`05_reference/25_rules_appendix.md`) has a DESTROYED entry; use that text. Starting Strength/Half-strength, Different Move Characteristics and fight-phase passing are also in the appendix. Never invent the app-only rules. When a milestone needs one, ask the user for the wording and record it in `docs/decisions.md`.
- If `docs/rules/` is missing (fresh clone), ask the user to run `python tools/extract_rules.py`. Don't reconstruct rules from memory.

## Architecture (non-negotiable)

### Library split
- `wh40k_core` is a pure C++20 static library. **It must never include raylib, ImGui, Windows or any rendering/IO-device headers.** It depends only on the standard library and nlohmann_json.
- `wh40k_client_2d` links `wh40k_core` and owns everything visual. It **never mutates `GameState`**. It only calls the engine API below.
- `wh40k_tests` links only `wh40k_core`.

### Two sides
- There are exactly two sides: `enum class Side : uint8_t { A, B };`. Every unit belongs to a side.
- Rules use "active player / opposing player" (01.03). Model that as `Side active_side()`, which changes while a unit is selected to move, shoot or fight. Don't hard-code "the player whose turn it is" where the rule says active player.
- Multi-seat play is out of scope. Don't add seat or player abstractions.

### Engine API (the only way in or out)
```cpp
// All free functions or members of a stateless Engine; GameState is a plain value type.
std::vector<GameAction> legal_actions(const GameState&, Side);          // what this side may do now
ValidationResult        validate_action(const GameState&, Side, const GameAction&); // ok + rule-cited reasons
std::vector<GameEvent>  apply_action(GameState&, Side, const GameAction&);          // precondition: validated
std::vector<GameEvent>  start_game(GameState&);                                     // NotStarted -> first decision
Side                    active_side(const GameState&);                              // 01.03
// apply_action/start_game also have DiceSource& overloads so engine tests can use ScriptedDice.
```
- `GameState` holds a **`PendingDecision`** describing whose input the engine is waiting for, and of what kind (choose unit to move, choose move type, place model, select targets, order allocation groups, pick model to remove, pile-in, pass, ...). Every action answers the current pending decision. Reactive decisions belong to the opposing side (allocation order, model removal, etc.) and flow through this same mechanism.
- `GameAction` and `GameEvent` are `std::variant`s of small serializable structs. Events drive UI animation and the dice/combat log, and they carry rule numbers.
- `ValidationResult` reasons cite rules, e.g. `"03.03: model 4 is more than 2\" from every other model in its unit"`.

### Determinism and state
- `GameState` is a value type: copyable, comparable (`operator==`), and fully JSON-serializable with nlohmann (`to_json`/`from_json`). Round-trip tests are mandatory for every new state field.
- All randomness goes through a `DiceSource` interface. Production uses a seeded PRNG whose seed and position live in `GameState`. Tests use `ScriptedDice` with exact rolls, which is how every combat test is written.
- Never use `std::rand`, `std::random_device`, wall-clock time, unordered-container iteration order, or pointer values in game logic.
- Replays: an initial `GameState` plus a list of `(Side, GameAction)` must reproduce the final state exactly. Saved replays in `tests/replays/` are regression tests.

### Geometry
- Units: `float` millimetres internally. `constexpr float MM_PER_INCH = 25.4f;`. Convert at the boundary: data files use inches, the UI shows inches.
- Positions are 2D (x, y) on the battlefield plane. Vertical rules ("5\" vertically") are trivially satisfied in V1 (no terrain). Keep a `z` field defaulting to 0 so terrain can be added later.
- Distances are measured edge-to-edge between bases (01.04): `center_distance - r1 - r2`, clamped at 0.
- Bases are `struct Base { BaseShape shape; float diameter_mm; float length_mm; }`. V1 is round only, but don't assume circles outside `geometry/`.
- **Tolerance policy:** all "within X" checks use `dist <= X + EPS` and all "more than X" checks use `dist > X + EPS`, with `constexpr float EPS_MM = 0.01f;`. Base contact means `dist <= EPS_MM`. Use the helpers in `geometry/`. Never compare raw floats in rules code.
- Moves are lists of straight-line segments (03.01). Distance moved is the sum of segment lengths, and rotation is free. Validate the path against enemy bases and battlefield edges, not only the end point.

### "If possible" rules
Charge, pile-in and consolidation rules contain "if possible" and "as close as possible" clauses. For V1, enforce the hard constraints (end closer, end engaged, distance limits, coherency) and leave "if possible" optimality to the player. Mark each such spot with `// TODO(if-possible): <rule>` so it can be tightened later.

## Code conventions

- C++20. Use `std::variant`/`std::visit` for actions and events. Use `std::optional` instead of sentinel values. Use strong ID types (`struct UnitId { uint32_t v; }`) instead of raw ints.
- Namespaces: `wh40k::core`, `wh40k::client`.
- Files: `snake_case.hpp/.cpp`. Types: `PascalCase`. Functions and variables: `snake_case`. Constants: `kPascalCase` or `UPPER_CASE` for unit-conversion constants.
- No exceptions for rule violations. Those go in `ValidationResult`. Exceptions are only for programmer errors and corrupt data files.
- No globals or singletons in core.
- Keep functions small and rule-shaped: one function per rule step where practical (`make_hit_rolls`, `make_wound_rolls`, `create_allocation_groups` ...).
- Unit-specific abilities (Bolter Discipline, Synapse, etc.) are **out of scope for V1**. Load them, display their text, and don't implement them.

## Tests

- Every rule implemented gets tests named after its rule number. Use `ScriptedDice` for all combat. Include the failure cases (illegal moves, out-of-range targets) as well as the happy path.
- Geometry tests use explicit coordinates in inches, converted with a helper, so the numbers read like the rulebook.
- At the end of each milestone, add at least one replay file under `tests/replays/` covering that milestone's flow.

## Working style for Claude in this repo

- One milestone per session. Run `/clear` between milestones. Re-read `PLAN.md` and the relevant `docs/rules/` files at the start.
- For milestones marked "plan mode" in `PLAN.md`, propose the design (types, files, decision flow) and wait for approval before writing code.
- Commit at the end of each green milestone with a message naming the milestone and rule numbers covered. Only commit when the user asks.
- Don't add dependencies beyond those listed without asking.
- Don't edit `data/units/*.json` by hand. Re-run the fetch tool, or put overrides in the scenario file.
- Never commit the PDF, `rules-summary.txt` or `docs/rules/`. They are gitignored on purpose (Games Workshop copyright).
