# VibeHammer: Warhammer 40,000 11th Edition Rules Engine

A deterministic C++20 rules engine for Warhammer 40k 11th Edition, with a 2D top-down Windows client.
V1 goal: one Space Marine Intercessor Squad vs one Tyranid Termagant unit, played hotseat on a 24"x24" board,
following the real core rules. Since M38-M45 the client also plays against a bot (or watches bot vs bot, D-054).
Since M53-M58 there is also a full-size 60"x44" scenario with several units per side
(`data/scenarios/strike_44x60.json`: 3 Intercessor Squads vs 4 Termagant units, D-064), which every bot level plays.

The milestone plan is in `PLAN.md`. Before starting work, check which milestone is current and stay inside its scope.
V1 (M0–M13) is complete. For post-V1 work, first add a milestone to `PLAN.md` with its rules and acceptance
criteria (see "Later (post-V1)"), agree it with the user, then work inside it.

## Repository layout

```
CLAUDE.md                 this file
README.md                 overview for people visiting the repo (play, build, layout)
PLAN.md                   milestone plan with acceptance criteria
docs/rules/               verbatim 11th ed Core Rules, one file per section (tracked: private repo only)
  00_index.md             rule number -> file lookup (all 156 rule numbers)
docs/decisions.md         rule interpretations and design decisions (D-001 ...), cited in code
data/units/*.json         unit datasheets generated from BSData (do not hand-edit)
data/scenarios/*.json     battlefield size, deployment, armies, loadouts
tools/extract_rules.py    PDF -> docs/rules/   (needs pdftotext on PATH)
tools/fetch_bsdata.py     BSData wh40k-11e -> data/units/
tools/package.ps1         Release build -> VibeHammer.zip for sending the game to someone
src/core/                 wh40k_core: headless rules library
src/client_2d/            wh40k_client_2d: raylib + Dear ImGui executable (hotseat play + replay viewer)
  logic/                  wh40k_client_logic: the client's raylib-free code (GameSession, editors) (D-024)
src/ai/                   wh40k_ai: the bot opponent, headless (D-054); wh40k_bot_arena exe (bot vs bot stats)
tests/                    wh40k_tests: GoogleTest for wh40k_core, run via CTest
  client/                 wh40k_client_tests: GoogleTest for wh40k_client_logic
  ai/                     wh40k_ai_tests: GoogleTest for wh40k_ai
  replays/*.json          saved replays; each must replay cleanly (regression tests, D-014)
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

The presets are Windows-only. On Linux (e.g. cloud sessions) configure with Ninja and GCC instead; raylib
needs the X11/GL dev packages (libxrandr, libxinerama, libxcursor, libxi, libgl1-mesa):

```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux && (cd build/linux && ctest)
# Regenerate tests/replays/ on purpose only, then check the diff:
./build/linux/tests/wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=Replay.DISABLED_RegenerateReplays
# The bot-game replay m45_bot_game.json (Hard vs Hard, seed 9) comes from the ai tests instead (M45):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M45_Replay.DISABLED_RegenerateBotGameReplay
# and m46_bot_fight_moves.json (Normal vs Normal, 20 Termagants, seed 1) likewise (M46):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M46_Replay.DISABLED_RegenerateFightMoveReplay
# and m47_default_casualties.json (Easy vs Easy, 20 Termagants, seed 121) likewise (M47):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M47_Replay.DISABLED_RegenerateDefaultCasualtiesReplay
# and m58_strike_bot_game.json (Hard vs Hard on the Strike scenario) likewise (M58):
./build/linux/tests/ai/wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M58_Replay.DISABLED_RegenerateStrikeBotGameReplay
```

On Windows the same two tests are `build/default/tests/Debug/wh40k_tests.exe` and
`build/default/tests/ai/Debug/wh40k_ai_tests.exe` with the same flags.

- To send the game to someone: `powershell -ExecutionPolicy Bypass -File tools\package.ps1`. It builds Release,
  stages the exe, VC++ runtime DLLs, `data/` and `tests/replays/` with a README, smoke-tests the staged exe, and
  writes `VibeHammer.zip` in the repo root (gitignored). The exe looks for `data/` next to itself first, then the
  source tree. Never add `docs/rules/` or the PDF to the package.
- To publish a GitHub release (M52, D-063): set the version in `CMakeLists.txt` and `src/core/version.cpp`, write
  `docs/releases/vX.Y.Z.md`, then push tag `vX.Y.Z` (or run the Release workflow by hand with that tag).
  `.github/workflows/release.yml` builds on Windows, runs ctest, packages and attaches
  `VibeHammer-vX.Y.Z-windows-x64.zip`.
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
- **Known gaps (app-only rules):** the PDF names some rules under "See also" without including their text, because they live in the Warhammer 40,000 app. The ones V1 touches are: Modifying Dice Rolls, Modifying Damage, Modified Characteristics, Characteristic Modifiers, Random Characteristics, Multiple Weapon Profiles (D-001), Target No Longer Eligible or Viable, Closest Model/Unit (M3, M10), Within/Wholly Within (M3, M8), Multiple Battle-shock Rolls (M7, M11), Re-rolls and Rules Sequencing. "Destroyed" is partly covered: the Rules Appendix (`05_reference/25_rules_appendix.md`) has a DESTROYED entry; use that text. Starting Strength/Half-strength, Different Move Characteristics and fight-phase passing are also in the appendix. Never invent the app-only rules. When a milestone needs one, first check `docs/decisions.md`: wording the user supplied is already recorded for Within/Wholly Within (D-009), Closest Model/Unit (D-010), Modifying Dice Rolls and Re-rolls (D-011), Target No Longer Eligible or Viable (D-012) and Multiple Battle-shock Rolls (D-015). That wording is AI-sourced and not verified against the app, so treat it as a working assumption. For any other app-only rule, ask the user for the wording and record it in `docs/decisions.md`.
- If `docs/rules/` is missing, ask the user to run `python tools/extract_rules.py`. Don't reconstruct rules from memory.

## Architecture (non-negotiable)

### Library split
- `wh40k_core` is a pure C++20 static library. **It must never include raylib, ImGui, Windows or any rendering/IO-device headers.** It depends only on the standard library and nlohmann_json.
- `wh40k_client_2d` links `wh40k_client_logic` (which links `wh40k_core`) and owns everything visual. It **never mutates `GameState`**. Every game change goes through `GameSession::submit`, which calls `validate_action` before `apply_action`. The client may call core's read-only queries for display and editor choices (`shot_options`, `melee_options`, `max_move_distance_mm`, `move_end_problems`, `check_save_roll`, ...) but contains no rules logic (D-024).
- `wh40k_client_logic` must not include raylib, ImGui or OS headers (checked on every build, like core).
- `wh40k_ai` (the bot, D-054) links `wh40k_client_logic` and must not include raylib, ImGui or OS headers. The bot is a player, not the rules: in a live game it acts only through `GameSession::submit`, and legality comes only from the engine. Only `src/ai/simulator.cpp` may call `apply_action`, on a scratch copy with the bot's own dice, so the bot never sees the real future rolls.
- `wh40k_tests` links only `wh40k_core`. `wh40k_client_tests` links `wh40k_client_logic`. `wh40k_ai_tests` links `wh40k_ai`.

### Two sides
- There are exactly two sides: `enum class Side : uint8_t { A, B };`. Every unit belongs to a side.
- Rules use "active player / opposing player" (01.03). Model that as `Side active_side()`, which changes while a unit is selected to move, shoot or fight. Don't hard-code "the player whose turn it is" where the rule says active player.
- Multi-seat play is out of scope. Don't add seat or player abstractions to core. The client may let the bot (D-054) control a side; which sides are bots is client state, never part of `GameState`.

### Engine API (the only way in or out)
```cpp
// All free functions or members of a stateless Engine; GameState is a plain value type.
std::vector<GameAction> legal_actions(const GameState&, Side);          // what this side may do now
ValidationResult        validate_action(const GameState&, Side, const GameAction&); // ok + rule-cited reasons
std::vector<GameEvent>  apply_action(GameState&, Side, const GameAction&);          // precondition: validated
std::vector<GameEvent>  start_game(GameState&);                                     // NotStarted -> first decision
Side                    active_side(const GameState&);                              // 01.03
// apply_action/start_game also have DiceSource& overloads so engine tests can use ScriptedDice.
// apply_action also has a (..., DiceSource&, GameState& backup) overload that reuses a scratch rollback copy (M54, D-068).
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
- Tests that check a decision rather than a numbered rule are named after it (`TEST(D020_FightSequence, ...)`), Rules Appendix tests use `Appendix_...`, and client tests (`wh40k_client_tests`) use the milestone (`M12_...`, `M13_...`).
- At the end of each milestone, add at least one replay file under `tests/replays/` covering that milestone's flow. Stop the replay before any phase a later milestone will change (as `m9_charge.json` and `m10_fight.json` do), so it stays valid.
- Replays are regenerated only on purpose (`Replay.DISABLED_RegenerateReplays`, and `M45_Replay.DISABLED_RegenerateBotGameReplay` / `M46_Replay.DISABLED_RegenerateFightMoveReplay` / `M47_Replay.DISABLED_RegenerateDefaultCasualtiesReplay` / `M58_Replay.DISABLED_RegenerateStrikeBotGameReplay` in the ai tests for `m45_bot_game.json` / `m46_bot_fight_moves.json` / `m47_default_casualties.json` / `m58_strike_bot_game.json`; see Build and test). Existing replay files must come out byte-identical; check with `git diff tests/replays`. `M47_Replay.TheSavedFileMatchesTheGame` also checks that `m47_default_casualties.json` is what Easy plays now, and `M58_Replay.TheSavedFileMatchesTheGame` that `m58_strike_bot_game.json` is what Hard plays now.

## Working style for Claude in this repo

- One milestone per session. Run `/clear` between milestones (except where `PLAN.md` records a batch the user pre-approved, such as M38–M45). Re-read `PLAN.md` and the relevant `docs/rules/` files at the start.
- For milestones marked "plan mode" in `PLAN.md`, propose the design (types, files, decision flow) and wait for approval before writing code.
- Commit at the end of each green milestone with a message naming the milestone and rule numbers covered, then push. In cloud sessions the environment's stop hook requires uncommitted work to be committed and pushed. Open a pull request and merge it (rebase merge, to keep `main` linear) only when the user asks.
- The client's mouse input can't be scripted, so in a cloud session check the UI through the logic-level tests (`wh40k_client_tests`) and screenshots under `xvfb-run`. The client's flags for this are `--screenshot <file.png>`, `--steps <n>` (click the first decision button n times), `--select <unit id>`, `--select-model <model id>`, `--seed <n>` (fixes the seed; without it each game's seed is random, D-038), `--scenario <file>`, `--replay <file>`, `--frame <n>`, `--zoom <px per mm>`, `--dice <n>` (1 keeps the dice pop-ups that the `--steps` rolls made, 2 also reveals the first; they're dropped otherwise, D-033) `--menu 1` (shows the start screen, which any other flag skips; D-055), `--bot A|B|both|none` (the sides the bot plays), `--bot-level easy|normal|hard`, `--bot-speed slow|normal|fast|instant` and `--bot-steps <n>` (after `--steps`, runs up to n bot decisions at once before the first frame, stopping at a human's decision; `--steps` itself stops at a bot's decision; D-054; it prints `bot-steps: <taken> of <n> bot actions taken` to stdout, which `package.ps1`'s second smoke test checks, M45). A run with any flag uses the default settings and never reads or writes `vibehammer_settings.json`. `--steps` only clicks panel buttons, so it can't reach decisions made by clicking the board (picking shooters, targets, charges), or the rolls they lead to. A bad flag value exits with code 2. A full game by mouse still needs a manual run on Windows. Bot strength is measured with the Release arena: `cmake --build --preset release --target wh40k_bot_arena`, then `.\build\default\src\ai\Release\wh40k_bot_arena.exe --games 200 --a easy --b easy` (flags `--games`, `--a`, `--b` (levels `easy`, `normal`, `hard`, plus the hidden `easy-m55` / `normal-m55`, which play as at M55 for regression comparisons, D-066), `--scenario`, `--seed`, `--step-cap` (default 5000 steps; a Strike game takes about 300), `--per-game 1` (one line per game with hashes of its actions, final state and decision trace, for proving play unchanged, M54), `--trace <seed>` (one game's bot decisions with their reasons and times) and `--weights key=value,...` (overrides level Hard's `BotWeights` for tuning, M44); it exits 1 on any rejected action, reverted move or unfinished game). The bot scenarios are `data/scenarios/bot_termagants_first.json` and `bot_20_termagants.json` (M44), and the several-unit `strike_44x60.json` (M53). With one unit per side the bots play exactly as before M56/M57; changes to the multi-unit path must keep 1v1 `--per-game` output identical.
- Don't add dependencies beyond those listed without asking.
- Don't edit `data/units/*.json` by hand. Re-run the fetch tool, or put overrides in the scenario file.
- Never commit the PDF or `rules-summary.txt`. They are gitignored on purpose (Games Workshop copyright).
- `docs/rules/` is no longer gitignored and may be committed, because the repo is private and will stay that way. It is still Games Workshop's copyrighted text: never make the repo public or copy `docs/rules/` anywhere public. If the repo ever goes public, add `docs/rules/` back to `.gitignore` and remove it from the history first.
