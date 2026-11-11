# Packaging, releases and regenerating source material

Moved verbatim from `CLAUDE.md` in M84. `CLAUDE.md` keeps a pointer here; read this file when the task needs it.

## Packaging and releases

- To send the game to someone: `powershell -ExecutionPolicy Bypass -File tools\package.ps1`. It builds Release,
  stages the exe, VC++ runtime DLLs, `data/` and `tests/replays/` with a README, smoke-tests the staged exe, and
  writes `VibeHammer.zip` in the repo root (gitignored). The staged `data/` includes `data/factions.json` and the
  stock lists in `data/armies/` (M72); the builder saves a player's lists in `armies/` next to the exe (gitignored
  as `/armies/` in the repo). The exe looks for `data/` next to itself first, then the source tree. Never add `docs/rules/`, `docs/datasheets/` or the PDF to the package.
- To publish a GitHub release (M52, D-063): set the version in `CMakeLists.txt` and `src/core/version.cpp`, write
  `docs/releases/vX.Y.Z.md`, then push tag `vX.Y.Z` (or run the Release workflow by hand with that tag).
  `.github/workflows/release.yml` builds on Windows, runs ctest, packages and attaches
  `VibeHammer-vX.Y.Z-windows-x64.zip`.

## Regenerating source material

```powershell
python tools/extract_rules.py                # rebuild docs/rules/ from 11th-ed-core-rules.pdf
python tools/fetch_bsdata.py "Imperium - Space Marines.json" "Intercessor Squad" data/units/intercessor_squad.json --ref <sha>
python tools/fetch_bsdata.py "Tyranids.json" "Termagants" data/units/termagants.json --ref <sha>
```

The nine roster units (M59: Hive Tyrant, Genestealers, Tyrant Guard, Screamer-killer, Tyrannofex, Chaos Lord,
Legionaries, Chaos Terminator Squad, Havocs) come from the same tool and catalogues (D-070). Pass the BSData entry
name and the output file in the same way.

Each unit JSON records the BSData commit it came from in `source.commit` (all of them at `0f9035541630...` today). Pin to that sha unless you are deliberately updating the data.
Pass `--fetched <YYYY-MM-DD>` to keep a file's existing `source.fetched` date when regenerating it. Since M71 the tool
also writes a `list_building` block (points by unit size, wargear slots, costs and limits, copy limits; D-079) for the
nine roster datasheets and Termagants.
