# Wahapedia 11th edition data export

Wahapedia's own data export for 11th edition (https://wahapedia.ru/wh40k11ed/the-rules/data-export/), downloaded
2026-11-28 by `tools/fetch_wahapedia.py`; the data is of 2026-11-23 01:43:36 (GMT+3, `Last_update.csv`). Re-run the
script to refresh it. Powered by Wahapedia.

**Status.** Third-party transcription of Games Workshop's copyrighted text: private repo only, never in `data/`, the
package or a release (same rules as `docs/gdmissions/`). `docs/rules/` stays the source of truth for the core rules;
this is a source for what the repo had no text for: faction rules, detachment rules, enhancements and stratagems
(`docs/roadmap.md`, question 4), plus a cross-check for BSData's datasheets. Nothing in the engine reads it.

## Format

`Export Data Specs.xlsx` documents every column (English and Russian sheets). In short: pipe-delimited CSV, UTF-8 with
a BOM, a header row, a trailing empty column on every row; ids are strings (keep the leading zeros); `description`,
`legend` and similar fields hold HTML as shown on the site; booleans are `true`/`false`.

## Files

| File | Rows | What it holds |
|---|---|---|
| `Factions.csv` | 25 | Factions (id, name, link) |
| `Source.csv` | 70 | Books and add-ons with edition, errata version and date |
| `Datasheets.csv` | 1485 | Datasheets: faction, source, role, loadout, transport, Leader/Support, damaged profile |
| `Datasheets_models.csv` | 1653 | Model profiles: M T Sv inv W Ld OC, base size |
| `Datasheets_wargear.csv` | 7550 | Weapon profiles (range, A, BS/WS, S, AP, D, abilities as text) |
| `Datasheets_options.csv` | 2295 | Wargear options (text) |
| `Datasheets_unit_composition.csv` | 1896 | Unit composition (text) |
| `Datasheets_models_cost.csv` | 5352 | Points by unit size |
| `Datasheets_keywords.csv` | 12234 | Keywords, per model where they differ, faction keywords marked |
| `Datasheets_abilities.csv` | 6328 | Each datasheet's abilities: core/faction ones by `ability_id`, its own with name and text |
| `Datasheets_leader.csv` | 1250 | Which Leader/Support datasheet can join which unit |
| `Datasheets_stratagems.csv` | 54513 | Which stratagems a datasheet can be the target of |
| `Datasheets_enhancements.csv` | 7350 | Which enhancements a datasheet can take |
| `Datasheets_detachment_abilities.csv` | 8610 | Which detachment rules affect a datasheet |
| `Abilities.csv` | 264 | Core and faction abilities (name, text) |
| `Detachments.csv` | 315 | Detachments: type, DP, force disposition (256 have both: the matched-play ones) |
| `Detachments_chapter_dp.csv` | 3 | Space Marine chapter overrides for DP |
| `Detachment_abilities.csv` | 333 | Detachment rules (name, text) |
| `Enhancements.csv` | 935 | Enhancements: detachment, points, text, Leader/Support restriction |
| `Enhancements_wargear.csv` | 8 | Weapon profiles granted by enhancements |
| `Stratagems.csv` | 1520 | Stratagems: faction, detachment, type, CP, turn, phase, text |
| `Last_update.csv` | 1 | Export timestamp |

## By faction

| Faction | Datasheets | Detachments | Detachment rules | Enhancements | Stratagems |
|---|---|---|---|---|---|
| (none: core, Boarding Actions) | 0 | 0 | 5 | 1 | 75 |
| Adepta Sororitas | 38 | 10 | 10 | 29 | 44 |
| Adeptus Custodes | 36 | 15 | 15 | 33 | 49 |
| Adeptus Mechanicus | 38 | 13 | 13 | 40 | 63 |
| Adeptus Titanicus | 4 | 0 | 0 | 0 | 0 |
| Aeldari | 99 | 19 | 21 | 60 | 100 |
| Astra Militarum | 135 | 13 | 13 | 42 | 65 |
| Chaos Daemons | 101 | 14 | 17 | 39 | 66 |
| Chaos Knights | 37 | 8 | 8 | 28 | 39 |
| Chaos Space Marines | 89 | 20 | 23 | 68 | 105 |
| Death Guard | 49 | 12 | 12 | 36 | 57 |
| Drukhari | 46 | 13 | 13 | 38 | 61 |
| Emperor’s Children | 23 | 11 | 12 | 36 | 55 |
| Genestealer Cults | 146 | 12 | 13 | 36 | 57 |
| Grey Knights | 31 | 11 | 11 | 34 | 53 |
| Imperial Agents | 46 | 7 | 7 | 24 | 38 |
| Imperial Knights | 28 | 8 | 9 | 26 | 39 |
| Leagues of Votann | 22 | 12 | 12 | 38 | 59 |
| Necrons | 64 | 16 | 16 | 50 | 79 |
| Orks | 67 | 17 | 17 | 42 | 48 |
| Space Marines | 193 | 39 | 39 | 101 | 158 |
| Thousand Sons | 38 | 12 | 12 | 36 | 57 |
| Tyranids | 57 | 14 | 14 | 41 | 67 |
| T’au Empire | 62 | 9 | 11 | 27 | 39 |
| World Eaters | 36 | 10 | 10 | 30 | 47 |

## Caveats

- **Not everything is 11th edition.** 1441 datasheets come from 11th edition sources; 41 from Forge World and Legends
  sources (edition "0"), one from a 10th edition book, two with no source. `Source.csv` still lists some 10th edition
  books (Boarding Actions, 500 Worlds, the 10th Balance Dataslate). Filter on `Source.csv`'s `edition` and on
  `Detachments.csv`'s `type` (Boarding Actions detachments are not matched play).
- **Core stratagems appear more than once.** The 75 faction-less stratagems mix the core ones with the Boarding
  Actions and other expansions' versions (e.g. three COMMAND RE-ROLLs). The core book's are `type` "Core Stratagem"
  or "Core – ..."; compare with `docs/rules/` section 15, which wins.
- **Checked:** Intercessor Squad's model profiles, bases and weapon list match `data/units/intercessor_squad.json`
  (BSData), with the Bolt Rifle's two 11th edition modes; the 256 matched-play detachments match the count in `docs/roadmap.md`. Nothing else is verified yet.
- **Text, not mechanics.** Abilities, rules and stratagems are prose in HTML. Implementing them is Phase 4's effect
  layer; this only supplies the wording.
