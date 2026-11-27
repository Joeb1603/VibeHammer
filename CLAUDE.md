# VibeHammer: Warhammer 40,000 11th Edition Rules Engine

A deterministic C++20 rules engine for Warhammer 40k 11th Edition, with a 2D top-down Windows client.
V1 (M0–M13): Intercessor Squad vs Termagants, hotseat on a 24"x24" board, following the real core rules.
Since then: a bot opponent (D-054; one level since M85, D-085), the 60"x44" Strike scenario (D-064), the 44"x60"
rosters scenario with oval bases and a points result (D-074, D-076), custom battles with an army builder and
hand deployment (D-079, D-081, D-082), army-level teamwork for the bot (D-083), and objectives with a mission of our
own, scored in VP (14.02, D-084), which the bot plays for since M87 (D-086), and 2D terrain (13, D-087–D-089: areas
and features, cover, Hidden, obscuring and solid walls) on the objective battlefield, the default scenario since M88,
which the bot plays for cover since M89 (D-090). M90 added Objective Consolidation and [IGNORES COVER] (D-091), M91
the bot's terrain and objective play in every phase behind weight keys, off by default (none measured better, D-092),
M92 the app-only rules audit (D-093): the scraped app text in `docs/gdmissions/` overrides the AI-sourced wording, and
M93 its open items (D-094): per-weapon new targets in the client, 40 mm objective markers (14.01.01) on the objective
scenario, and Gone to Ground in the bot's move score behind a key, off by default. M94 made every dice roll a decision
of its roller's (D-095): the engine stops at each, a human presses Roll on its card (dice pop-ups on), the game session
makes the rest, and saves and replays record each roll as a step (format 2). M95 added reaction windows and the first
stratagems (D-096): a side that can use a stratagem there is asked just after one of its rolls (Command Re-roll, 15.02)
or just before a battle-shock roll (Insane Bravery, 15.04), spending CP (15.01); a random D is rolled per attack, and
saves and replays record each answer (format 3). The bot plays Command Re-roll by default (it measured better) and
Insane Bravery behind a key, off. M96 added the opponent-turn stratagems (D-097), in windows that follow no roll: Fire
Overwatch with snap shooting at the end of the opponent's Movement phase (15.08, 15.09), Heroic Intervention at the end
of their Charge phase (15.11), Counteroffensive just after one of their units has fought (15.12); objective control is
determined on entering a phase's end (14.02.01), and saves and replays record the new answers (format 4). The bot plays
Fire Overwatch by default (it measured better), Heroic Intervention and Counteroffensive behind keys, off. M97 added the
own-turn stratagems that end in mortal wounds (D-099): Explosives at the Shoot step (15.05) and Crushing Impact just
after a charge move (15.06), each with a step that selects a model and an enemy unit, its roll and its mortal wounds
(06.02), the enemy's first; saves and replays record the new window (format 5). The bot plays both behind keys, off
(neither measured better). M98 picks that model and enemy unit on the board, and the bot's charge planning prices
Crushing Impact behind its key, through a read-only forecast of a window's refusal (D-100). M99 started the roadmap's
Phase 1 (finish the core rules), a batch the user asked Claude to work through without questions (`PLAN.md`; each
milestone's own decisions are marked in its decision file): [ANTI-X Y+], [DEVASTATING WOUNDS], [MELTA X] and [LANCE]
(D-101), where a critical wound's mortal wounds are inflicted on one model after the batch's normal damage (06.02).
M100 added [LETHAL HITS], [PSYCHIC], [CLEAVE X], [ONE SHOT] (a model's own weapon is spent once selected) and the
keyword-conditioned abilities of 24.01 (D-102); no weapon on the datasheets in `data/units/` is refused any more.
The finished milestones are in `docs/plan_archive.md`.

The milestone plan is in `PLAN.md`. Before starting work, read the current milestone's section there and stay
inside its scope. For post-V1 work, first add a milestone to `PLAN.md` with its rules and acceptance criteria
(see "Later (post-V1)"), agree it with the user, then work inside it.

## Repository layout

```
CLAUDE.md                 this file
README.md                 overview for people visiting the repo (play, build, layout)
PLAN.md                   current and upcoming milestones, with acceptance criteria
docs/plan_archive.md      the finished milestones M0–M99, verbatim
docs/roadmap.md           long-range map: what a full game with any army on any mission still needs, by phase
docs/rules/               verbatim 11th ed Core Rules, one file per section (tracked: private repo only)
  00_index.md             rule number -> file lookup (all 156 rule numbers)
docs/datasheets/          the two New Recruit army rosters behind rosters_44x60.json (GW copyright: private repo only)
docs/gdmissions/          gdmissions.app's 11th ed pages as Markdown: core rules incl. app-only entries, missions, layouts,
                          detachments, changelogs (third-party, unverified; GW copyright: private repo only)
  00_index.md             what is in each file
docs/decisions.md         index of rule interpretations and design decisions (D-001 ...), cited in code
docs/decisions/D-NNN.md   one file per decision
docs/dev/                 reference: replays, client flags, bot arena, packaging and data regeneration
data/units/*.json         unit datasheets generated from BSData (do not hand-edit)
data/scenarios/*.json     battlefield size, deployment, armies, loadouts
data/factions.json        the custom battles' factions and their datasheets (D-079)
data/armies/*.json        the factions' stock army lists for custom battles (units and loadouts only; D-079)
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
  data/scenarios/*.json   test-only fixture scenarios (roster units on smaller boards, custom battles; not in the client's list)
  data/armies/*.json      test-only army lists (custom battles of the M73 and M77 replays)
  data/legacy/            format-1 replays and an M93 save: the fixtures of D-095's migration to format 2
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
```
Regenerating `tests/replays/` (on purpose only) and the rules for saved replays: `docs/dev/replays.md`.

- Packaging (`tools\package.ps1`; never add `docs/rules/`, `docs/datasheets/` or the PDF to the package), GitHub
  releases and regenerating `docs/rules/` and `data/units/` (`tools/extract_rules.py`, `tools/fetch_bsdata.py`, the
  pinned BSData sha) are in `docs/dev/release_and_data.md`.
- Dependencies come from CMake `FetchContent` pinned to exact tags: raylib, Dear ImGui, rlImGui, nlohmann_json, GoogleTest.
- Treat warnings as errors in `wh40k_core` (`/W4 /WX /permissive-`). Don't silence a warning to get a build through.
- A change is not done until it builds and `ctest` passes. Report test failures as they are; never skip, delete or weaken a test to get to green.
  Exception: a change that touches only `.md` files needs no build or `ctest` (no build step or test reads them).

## Rules: the source of truth

- **Before implementing or changing any game mechanic, look up its rule number in `docs/rules/00_index.md` and read that section file.** Don't implement from memory: 11th edition differs from 10th in many details (e.g. 2" engagement range, allocation groups, consolidation modes, Bolt Rifle firing modes).
- The rules text is verbatim pdftotext output. Sidebars, diagrams and example captions can appear out of order. Rule text is the authority, and examples only illustrate it.
- In section 05, the dice-face tables (hit, wound and save) were transcribed from page images. Everything else is raw extraction.
- If a rule is ambiguous, or `docs/rules/` disagrees with what you expected, **stop and ask**. Don't guess. Record the decision as a new `docs/decisions/D-NNN.md`, with the rule number, and add its line to the index in `docs/decisions.md`.
- Cite rule numbers in code and tests: `// 05.04 Inflict Damage: lowest save roll first` and `TEST(R05_04_InflictDamage, ResolvesLowestRollFirst)`.
- **Known gaps (app-only rules):** the PDF names some rules under "See also" without including their text, because they live in the Warhammer 40,000 app. The ones V1 touches are: Modifying Dice Rolls, Modifying Damage, Modified Characteristics, Characteristic Modifiers, Random Characteristics, Multiple Weapon Profiles (D-001), Target No Longer Eligible or Viable, Closest Model/Unit (M3, M10), Within/Wholly Within (M3, M8), Multiple Battle-shock Rolls (M7, M11), Re-rolls and Rules Sequencing. "Destroyed" is partly covered: the Rules Appendix (`05_reference/25_rules_appendix.md`) has a DESTROYED entry; use that text. Starting Strength/Half-strength, Different Move Characteristics and fight-phase passing are also in the appendix. Never invent the app-only rules. When a milestone needs one, first check `docs/decisions.md`: wording the user supplied is already recorded for Within/Wholly Within (D-009), Closest Model/Unit (D-010), Modifying Dice Rolls and Re-rolls (D-011), Target No Longer Eligible or Viable (D-012), Multiple Battle-shock Rolls (D-015) and Random Characteristics (D-069). That wording is AI-sourced and not verified against the app, so treat it as a working assumption. Since M92 (D-093) the user's ruling is that the app text scraped into `docs/gdmissions/core_rules/` (its *APP* entries) overrides that wording: D-012 is replaced (re-targeting), and D-093 lists what agreed, what changed and what the app adds; cite those app-only rules through D-093. For any other app-only rule, ask the user for the wording and record it in `docs/decisions.md`.
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
// D-095 (M94): every dice roll is a RollDice decision for its roller. apply_action/start_game answer each one inline,
// from the same dice (as before M94); apply_action_paced/start_game_paced (the same overloads) stop at it, as
// GameSession and format-2 replays play. Answering every RollDice after a paced action gives exactly the inline result.
bool                    roll_pending(const GameState&);                             // a RollDice is pending
std::optional<RollDiceInfo> roll_dice_info(const GameState&);                       // what it rolls, for display
// D-096 (M95): a reaction window is a UseStratagems decision for the side that can use a stratagem there, answered by
// DeclineStratagems (listed first) or UseStratagem. apply_action/start_game decline every window inline (so inline play
// is M94's); the paced overloads stop at it. D-097 (M96): windows that follow no roll (EndOfPhase, AfterAttacks) too;
// Fire Overwatch's unit then answers a SnapShoot decision with a Shoot of type Snap. D-099 (M97): UseStratagem also
// answers the Shoot decision (15.05 Explosives, after the Shoot templates), and an AfterChargeMove window offers 15.06
// Crushing Impact; each starts FlowState::effect: SelectModelAndEnemy, its RollDice, then OrderCasualties (06.02).
bool                    window_pending(const GameState&);                           // a window is pending
std::optional<WindowInfo> window_info(const GameState&);                            // what it offers, for display and the bot
std::optional<std::string> forecast_stratagem_problem(const GameState&, Side, Stratagem, WindowTiming,
                                                      std::optional<UnitId>, UnitId, ...); // D-100: a window's refusal, were it opened now
```
- `GameState` holds a **`PendingDecision`** describing whose input the engine is waiting for, and of what kind (choose unit to move, choose move type, place model, select targets, order allocation groups, pick model to remove, pile-in, pass, make a dice roll, ...). Every action answers the current pending decision. Reactive decisions belong to the opposing side (allocation order, model removal, save rolls, etc.) and flow through this same mechanism.
- `GameAction` and `GameEvent` are `std::variant`s of small serializable structs. Events drive UI animation and the dice/combat log, and they carry rule numbers.
- `ValidationResult` reasons cite rules, e.g. `"03.03: model 4 is more than 2\" from every other model in its unit"`.

### Determinism and state
- `GameState` is a value type: copyable, comparable (`operator==`), and fully JSON-serializable with nlohmann (`to_json`/`from_json`). Round-trip tests are mandatory for every new state field.
- All randomness goes through a `DiceSource` interface. Production uses a seeded PRNG whose seed and position live in `GameState`. Tests use `ScriptedDice` with exact rolls, which is how every combat test is written.
- Never use `std::rand`, `std::random_device`, wall-clock time, unordered-container iteration order, or pointer values in game logic.
- Replays: an initial `GameState` plus a list of `(Side, GameAction)` must reproduce the final state exactly. Saved replays in `tests/replays/` are regression tests.

### Geometry
- Units: `float` millimetres internally. `constexpr float MM_PER_INCH = 25.4f;`. Convert at the boundary: data files use inches, the UI shows inches.
- Positions are 2D (x, y) on the battlefield plane. Terrain is 2D too (D-087, M88): areas and features are footprints
  (convex polygons or circles, `geometry/terrain_shape.hpp`) with no floors, so `z` stays 0 and vertical rules ("5\"
  vertically", 13.06 Moving Vertically, Plunging Fire) are not modelled (`TODO(z)`). Without terrain every terrain
  check is skipped, so old scenarios play exactly as before.
- Distances are measured edge-to-edge between bases (01.04): `center_distance - r1 - r2`, clamped at 0.
- Bases are `struct Base { BaseShape shape; float diameter_mm; float length_mm; }`, round or oval (D-074). An oval
  model has a `facing_deg` (stored modulo 180, omitted from JSON at 0), and a move's path vertices can carry facings.
  Use the shape helpers (`geometry/` in core, `logic/base_shape` in the client, `src/ai/bounds.hpp` in the bot), and
  don't assume circles anywhere else. Round-to-round results must stay bit-identical to the circle code.
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
- Stratagems (15): Command Re-roll and Insane Bravery (D-096, M95), Fire Overwatch with Snap Shooting, Heroic
  Intervention and Counteroffensive (D-097, M96) are implemented, through reaction windows; Explosives (at the Shoot
  step) and Crushing Impact (in a window just after a charge move) too (D-099, M97). Epic Challenge, Rapid Ingress and
  Smokescreen are not in the game yet.
- A weapon ability the engine doesn't implement is never ignored. A profile with one is refused when selected, with a
  rule-cited reason (D-026, D-071). Implemented: Assault, Heavy, Pistol, Rapid Fire, Blast, Torrent, Hazardous,
  Sustained Hits, Extra Attacks, Twin-linked, Ignores Cover (D-091), Anti, Devastating Wounds, Melta, Lance (D-101),
  Lethal Hits, Psychic, Cleave, One Shot and the keyword-conditioned forms of Lethal Hits, Sustained Hits and
  Devastating Wounds (D-102), and random A/D (except random A on melee weapons). Still refused: Precision, Indirect
  Fire, a random X, and Lethal Hits together with Devastating Wounds (its choice is not built). The tests of the refusal
  run on stand-in data (D-101 part 8).

## Tests

- Every rule implemented gets tests named after its rule number. Use `ScriptedDice` for all combat. Include the failure cases (illegal moves, out-of-range targets) as well as the happy path.
- Geometry tests use explicit coordinates in inches, converted with a helper, so the numbers read like the rulebook.
- Tests that check a decision rather than a numbered rule are named after it (`TEST(D020_FightSequence, ...)`), Rules Appendix tests use `Appendix_...`, and client tests (`wh40k_client_tests`) use the milestone (`M12_...`, `M13_...`).
- At the end of each milestone, add at least one replay file under `tests/replays/` covering that milestone's flow. Stop the replay before any phase a later milestone will change (as `m9_charge.json` and `m10_fight.json` do), so it stays valid.
- Replays are regenerated only on purpose, and existing replay files must come out byte-identical (check with
  `git diff tests/replays`). The commands, the `TheSavedFileMatchesTheGame` checks and the pre-batch replay tests
  are in `docs/dev/replays.md`.

## Working style for Claude in this repo

- One milestone per session. Run `/clear` between milestones, batches included (a batch is approved up front;
  each milestone still gets a fresh session). At the start read the current milestone in `PLAN.md`, the
  decisions it cites (`docs/decisions/D-NNN.md`) and the relevant `docs/rules/` files, not whole archives.
- While iterating, run only the tests for the work at hand (`ctest --preset debug -R <milestone or rule>`). Run the
  full `ctest`, arena runs and `--per-game` checks once, when the milestone is otherwise done.
- For milestones marked "plan mode" in `PLAN.md`, propose the design (types, files, decision flow) and wait for approval before writing code.
- Commit at the end of each green milestone with a message naming the milestone and rule numbers covered, then push. In cloud sessions the environment's stop hook requires uncommitted work to be committed and pushed. Then open a pull request and merge it into `main` (rebase merge, to keep `main` linear) without asking: the user gave standing permission on 2026-11-23.
- The client's mouse input can't be scripted, so in a cloud session check the UI through the logic-level tests
  (`wh40k_client_tests`) and screenshots under `xvfb-run`; the client's flags are in `docs/dev/client_flags.md`.
  A full game by mouse still needs a manual run on Windows.
- Bot strength is measured with the Release `wh40k_bot_arena`. Its flags, the bot scenarios and the "identity set"
  that bot changes must keep (proved with `--per-game`) are in `docs/dev/bot_arena.md`.
- Don't add dependencies beyond those listed without asking.
- Don't edit `data/units/*.json` by hand. Re-run the fetch tool, or put overrides in the scenario file.
- Never commit the PDF or `rules-summary.txt`. They are gitignored on purpose (Games Workshop copyright).
- `docs/rules/` is no longer gitignored and may be committed, because the repo is private and will stay that way. It is still Games Workshop's copyrighted text: never make the repo public or copy `docs/rules/` anywhere public. If the repo ever goes public, add `docs/rules/` back to `.gitignore` and remove it from the history first.
- `docs/gdmissions/` (scraped from gdmissions.app) is Games Workshop's copyrighted text as well, under the same rules. It is a
  third-party transcription: `docs/rules/` stays the source of truth, and its app-only entries are unverified.
- `docs/datasheets/` (the New Recruit roster exports) is Games Workshop's copyrighted text too. The same rules apply: never copy it into `data/`, the package or a release, and the scenario file keeps only unit points and loadouts.
