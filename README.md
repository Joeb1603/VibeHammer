# VibeHammer

A Warhammer 40,000 (11th edition) skirmish for Windows. It is a deterministic C++20 rules engine with a 2D
top-down client. A Space Marine Intercessor Squad fights a Tyranid Termagant unit on a 24" x 24" board,
following the core rules, either hotseat or against a bot.

![Bot vs bot: the Termagants advance away from the Intercessors](docs/images/bot_vs_bot.png)

## Download and play

Get `VibeHammer-vX.Y.Z-windows-x64.zip` from the [Releases](https://github.com/Joeb1603/VibeHammer/releases)
page, unzip the whole folder and run `VibeHammer.exe`. If SmartScreen warns you, click "More info" and then
"Run anyway". The start screen offers these modes:

- **Play hotseat:** two players share the computer.
- **Play vs bot:** you choose your side. The bot plays at Easy, Normal or Hard.
- **Watch bot vs bot**, with Pause (key P), Step (key N) and the bot speed.
- **Continue saved game** or **Watch a replay**.

Follow the decision panel at the top of the screen. Click the board to pick units, targets and casualties,
and drag models to move them. Every roll shows in a dice pop-up. The event log cites the rule number behind
each step.

## What's implemented

- **The full turn:** the Command, Movement, Shooting, Charge and Fight phases and end of turn, over five
  battle rounds. This includes battle-shock, the attack sequence (hit, wound, save, damage), allocation
  groups, and pile-in and consolidation.
- **Geometry:** distances are measured edge to edge between bases, with 2" Engagement Range and unit
  coherency. Moves are checked along their whole path, not only where they end.
- **The bot** (`src/ai`) plays only through the same validated actions a human uses, and it never sees the
  game's future dice.
  - **Easy:** a baseline.
  - **Normal:** plans from the odds.
  - **Hard:** adds a game-aware evaluator and a lookahead.
- **Replays and saves:** a replay is the starting state plus every action, and it reproduces the game
  exactly. The saved replays in `tests/replays/` double as regression tests.

**Not yet:** terrain, objectives and missions, stratagems, deployment, unit-specific abilities, and other
units. `PLAN.md` lists what comes next.

## Building from source

The build needs CMake 3.28 or newer and a C++20 compiler. CMake's `FetchContent` downloads the dependencies,
each pinned to an exact tag: raylib, Dear ImGui, rlImGui, nlohmann_json and GoogleTest.

**Windows** (Visual Studio 2026, using the presets):

```powershell
cmake --preset default
cmake --build --preset debug
ctest --preset debug
.\build\default\src\client_2d\Debug\wh40k_client_2d.exe
```

**Linux** (Ninja and GCC). raylib needs the X11 and GL development packages (libxrandr, libxinerama,
libxcursor, libxi and libgl1-mesa):

```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux && (cd build/linux && ctest)
```

To package the game, run `powershell -ExecutionPolicy Bypass -File tools\package.ps1` on Windows. It writes
`VibeHammer.zip`. To publish a release, set the version in `CMakeLists.txt` and `src/core/version.cpp`,
add `docs/releases/vX.Y.Z.md`, and push the tag `vX.Y.Z`. The Release workflow then builds the zip, tests
it and publishes it.

## Repository layout

| Path | What it is |
|---|---|
| `src/core/` | `wh40k_core`: the headless rules engine (standard library and nlohmann_json only) |
| `src/client_2d/` | the raylib and Dear ImGui client; `logic/` holds its raylib-free code |
| `src/ai/` | `wh40k_ai`: the bot, plus `wh40k_bot_arena` for bot vs bot statistics |
| `tests/` | GoogleTest suites for core, client logic and the bot, plus the saved replays |
| `data/units/`, `data/scenarios/` | unit datasheets generated from BSData, and the scenarios |
| `docs/decisions.md` | rule interpretations and design decisions (D-001 ...), cited in code |
| `docs/rules/` | the core rules text the engine follows (private; see below) |
| `PLAN.md`, `CLAUDE.md` | the milestone plan, and the architecture and conventions |

## Rules text and copyright

Warhammer 40,000 and its rules are © Games Workshop. VibeHammer is an unofficial fan project and is not
endorsed by Games Workshop. `docs/rules/` holds Games Workshop's copyrighted text and is committed only
because this repository is private. Never publish it, and never include it in a release.
