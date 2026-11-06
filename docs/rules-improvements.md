# Rules docs: improvements to make

A review of `docs/rules/` (the pdftotext extraction of the 11th edition Core Rules), written 2026-11-06 and
checked against `main` at M46 (`3ce1842`). Nothing here has been done yet.

## Summary

The rule text is complete and sound. All 156 rule numbers in `00_index.md` have a matching `## NN.NN` heading in
the right file, every section is present, and the rule bodies read as clean, coherent text. The problems are
mostly in sidebars, diagrams and examples, plus a few spots inside the rules that could mislead a reader.

**Fixes go in `tools/extract_rules.py`, not the markdown.** `docs/rules/` is generated, so a hand edit is undone
by the next `python tools/extract_rules.py` run. The extractor already has `fix_section()` / `splice()` for the
section 05 dice tables; use the same mechanism. The PDF isn't in the repo, so regenerate on Windows.

## 1. Fixes inside the rules (worth doing)

- [ ] **02.02 / 02.04 Datasheets: model stats under WEAPONS.** The two-column layout interleaved the model and
  weapon characteristics. InSv, W, Ld and OC are model stats, but they sit under "02.04 WEAPONS", mixed in with
  Range/A/BS/WS/S/AP/D; 02.02 PROFILES lists only M, T and Sv. Move InSv, W, Ld and OC back under 02.02.
- [ ] **15.11 Heroic Intervention: duplicate heading.** Page 55 has a sample stratagem card (the "how to read a
  stratagem" key) whose overlapping text layers came out jumbled ("es. woanrsrliaourgshtsuhregaed‑foonrth…").
  Its heading is the *first* `## 15.11` in the file; the real rule is on page 57. Drop the page-55 heading, or
  replace that block with "[stratagem key diagram: unreadable, see 15.11 below]".
- [ ] **15.11 CP costs land in odd places.** In the real copy, "1CP" appears in the middle of the mode list
  (after Leap to Defend) and "+1CP" after Into the Fray. Most likely the base cost is 1CP and Into the Fray costs
  +1CP, but the text doesn't say so. Decide, record it in `docs/decisions.md`, then add a note. Needed for the
  stratagem milestone.
- [ ] **17.01 heading split.** "MOVING MONSTERS" / "## 17.01 AND VEHICLES", so the index title is "And
  Vehicles". Change it to "Moving Monsters and Vehicles" (heading and index).
- [ ] **Letters split by the extractor** (they read fine but break text search):
  - 01.07 "I t is not eligible"
  - 10.06 "M ONSTER/VEHICLE Models"
  - 13.06 "S ETTING UP OR ENDING A MOVE"

## 2. Garbled, but only in examples and diagrams (low priority)

The rule text is the authority; these only illustrate it.

- [ ] 02: the Boyz sample datasheet has scrambled flavour text ("w an o d rk v o ic f…") and unit composition
  ("equi 1 p b p i e g d ch…").
- [ ] 15, page 55: the stratagem key. Its legend for which turn each stratagem is used in (yours, the
  opponent's, either) depended on colour and icons, and that's gone. The WHEN lines cover the same thing.
- [ ] Diagram captions in 13 (page 51), 21 (surge example) and 12 (pages 39 and 41) are out of order but still
  understandable.
- Lost icons: a few "marked with this icon" references point at icons that didn't extract. Harmless.

## 3. Possibly missing text (check against the PDF)

- [ ] **[BLAST] can't target engaged units.** 10.06, the 17.03 diagram and two appendix FAQs all assume this,
  but 24.05 [BLAST] doesn't contain it. Either a sidebar was dropped or it's app-only. The engine implements it
  from 10.06 (`docs/decisions.md`, the 10.06 MONSTER/VEHICLE note), whose wording "it *still* cannot target a
  unit your unit is engaged with" implies a general rule elsewhere.
- [ ] **Missing pages 26–27, 44–45, 59–61, 75–77.** No file covers them. Probably section title or art spreads,
  but check **44–45** in particular: it sits between Fight and Terrain and might hold the battle sequence.
- [ ] **The pre-battle sequence is referenced but never defined.** Sections 18, 19, 20, 23, 24 and the appendix
  mention the Muster Armies, Declare Battle Formations, Resolve Pre-battle Abilities and Begin the Battle steps.
  No file defines that sequence, or deployment zones. Probably mission-pack material, but the Deployment
  milestone will need the wording (record it like D-009 to D-015).

## 4. App-only rules referenced but not included

Named under "See also" without their text (expected).

- **Wording already in `docs/decisions.md`:** Within/Wholly Within (D-009), Closest Model/Unit (D-010),
  Modifying Dice Rolls and Re-rolls (D-011), Target No Longer Eligible or Viable (D-012), Multiple Battle-shock
  Rolls (D-015), Multiple Weapon Profiles (D-001).
- **Still missing:** Rules Sequencing, Player's Rules, Modifying Damage, Modified Characteristics,
  Characteristic Modifiers, Random Characteristics, Automatically Successful, Roll Off, Table Quarter, Random
  Movement, Modifying CP Cost, Persisting Rules Effects, Not on the Battlefield, Setting Up Large Models,
  Regaining Lost Wounds.
- **Already in the appendix (`05_reference/25_rules_appendix.md`):** Destroyed, Starting Strength and
  Half-strength, Different Move Characteristics, Mixed Keywords in Units, Revived, Objectives Not Within a
  Terrain Area, Eligible to Fight but Unable to Fight (the fight pass rule).
- **Needed soon:** Roll Off (deployment and first turn), Modifying CP Cost and Rules Sequencing (stratagems).

## 5. Consistency checks that passed

The rules are internally consistent and cross-reference correctly. Nothing contradicts what the engine does.

- 11.04 "within 1"" of charge targets: odd next to the 2" engagement range, but consistent with the text and
  already recorded as a `TODO(if-possible)`.
- 14.02 "OC of 1 or more" and 16.01 "OC of 0 or '-'" line up.
- 10.07 Indirect Fire gives the benefit of cover (13.08), and the reference is correct.
- 12.03 / 12.06 / the appendix FAQ on overrun fights hang together.

## Suggested order

1. Fix section 1 items (except the 15.11 CP split) in `extract_rules.py` and regenerate on Windows.
2. Check the [BLAST] text and pages 44–45 against the PDF.
3. Settle the 15.11 CP split when stratagems come up, and get the Roll Off / pre-battle wording before the
   Deployment milestone.
