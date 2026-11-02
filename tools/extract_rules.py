#!/usr/bin/env python3
"""Regenerate docs/rules/ from the 11th Edition Core Rules PDF.

Usage:
    python tools/extract_rules.py [path/to/11th-ed-core-rules.pdf] [out_dir]

Defaults: ./11th-ed-core-rules.pdf -> ./docs/rules
Requires `pdftotext` (poppler) on PATH.

Text is verbatim pdftotext output (reading order), split per section with
`<!-- page N -->` markers. Only cleanup applied:
  * bullet/arrow icon glyphs -> "- " list items
  * drop-cap spacing artifacts ("R esolve" -> "Resolve")
  * the hit/wound/save tables in 05, whose dice faces are images that pdftotext
    cannot read, are replaced with tables transcribed from the page images.
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SECTIONS = [
    # (num, title, folder, first_page, last_page)
    ("01", "Core Concepts", "01_basic_rules", 8, 9),
    ("02", "Datasheets", "01_basic_rules", 10, 11),
    ("03", "Moving", "01_basic_rules", 12, 15),
    ("04", "Making Attacks", "01_basic_rules", 16, 17),
    ("05", "Attack Sequence", "01_basic_rules", 18, 23),
    ("06", "Other Concepts", "01_basic_rules", 24, 25),
    ("07", "The Battle Round", "02_battle_round", 28, 29),
    ("08", "Command Phase", "02_battle_round", 30, 31),
    ("09", "Movement Phase", "02_battle_round", 32, 33),
    ("10", "Shooting Phase", "02_battle_round", 34, 35),
    ("11", "Charge Phase", "02_battle_round", 36, 37),
    ("12", "Fight Phase", "02_battle_round", 38, 43),
    ("13", "Terrain", "03_battlefields_and_tactics", 46, 51),
    ("14", "Objectives", "03_battlefields_and_tactics", 52, 53),
    ("15", "Stratagems", "03_battlefields_and_tactics", 54, 57),
    ("16", "Actions", "03_battlefields_and_tactics", 58, 58),
    ("17", "Monsters and Vehicles", "04_advanced_rules", 62, 63),
    ("18", "Transports", "04_advanced_rules", 64, 65),
    ("19", "Attached Units", "04_advanced_rules", 66, 67),
    ("20", "Strategic Reserves", "04_advanced_rules", 68, 69),
    ("21", "Flying and Surging", "04_advanced_rules", 70, 71),
    ("22", "Other Rules and Abilities", "04_advanced_rules", 72, 73),
    ("23", "Aircraft", "04_advanced_rules", 74, 74),
    ("24", "Core Abilities", "05_reference", 78, 85),
    ("25", "Rules Appendix", "05_reference", 86, 88),
]

BULLETS = "▪▫►•"
HEADING = re.compile(r"^(.{2,80}?)\s+(\d\d\.\d\d)\s*$")
DROP_CAP = re.compile(r"(?m)(^|- |\d\. |: )([B-HJ-Z]) ([a-z]+)")
DROP_CAP_FALSE_POSITIVES = {"C within", "B this", "B unit", "W characteristic"}
TABLE_NOTE = ("<!-- Table reconstructed from the PDF page image: dice faces are icons "
              "that pdftotext cannot extract. -->\n")

HIT_TABLE = TABLE_NOTE + """| Condition (first that applies) | Result |
|---|---|
| Unmodified 1 | FAILS |
| Unmodified 6 | CRITICAL HIT |
| Equal to or greater than that attack's BS/WS characteristic | HIT |
| Any other result | FAILS |"""

WOUND_TABLE = TABLE_NOTE + """| Condition (first that applies) | Result |
|---|---|
| Unmodified 1 | FAILS |
| Unmodified 6 | CRITICAL WOUND |
| Equal to or greater than the required result below | WOUND |
| Any other result | FAILS |

| ATTACK'S STRENGTH VS TARGET'S TOUGHNESS | REQUIRED RESULT |
|---|---|
| Strength is TWICE (or more than twice) the Toughness | 2+ |
| Strength is GREATER than the Toughness | 3+ |
| Strength is EQUAL to the Toughness | 4+ |
| Strength is LESS than the Toughness | 5+ |
| Strength is HALF (or less than half) the Toughness | 6+ |"""

SAVE_TABLE = TABLE_NOTE + """| Condition (first that applies) | Result |
|---|---|
| Unmodified 1 | INFLICTS DAMAGE |
| Invulnerable Save: The models in the current allocation group have an InSv characteristic, and the result is equal to or greater than that characteristic. | FAILS |
| Save and AP: After modifying the result by the attacking weapon's AP characteristic, it is equal to or greater than the Sv characteristic of models in the current allocation group. | FAILS |
| Any other result | INFLICTS DAMAGE |

Example: An AP characteristic of -1 would modify a save roll of 3 to a 2. For models with a Sv characteristic of 2+ or better, that attack would fail."""

RESOLVE_ATTACKS_TAIL = """total of six D6).
3. Resolve Attack Dice: Resolve the attack sequence (05) for all of the attack dice you just gathered.
4. Other Attacks: Follow the first of the instructions below that applies:
- If there are any weapons targeting the same unit that have not yet been used to make attacks, return to the Gather Attack Dice step.
- Otherwise, if there are any weapons with unresolved attacks targeting a different unit, return to the Select Enemy Unit step.
- Otherwise, if all weapons have been used to make all of their attacks, this sequence ends and the attacking unit's attacks have been resolved."""

R = "�"


def splice(text: str, start: str, end: str, new: str) -> str:
    s = text.index(start)
    e = text.index(end, s) + len(end)
    return text[:s] + new + text[e:]


def fix_section(num: str, text: str) -> str:
    if num == "04":
        text = splice(text, "total of six D6). 3. Resolve", "been resolved.", RESOLVE_ATTACKS_TAIL)
    if num == "05":
        text = splice(text, f"{R} Unmodified {R} Unmodified", "HIT FAILS", HIT_TABLE)
        text = splice(text, f"{R} Unmodified\n\nFAILS\n\n{R} Unmodified\n\nCRITICAL WOUND",
                      f"{R} Any other result\n\nFAILS", WOUND_TABLE)
        text = splice(text, f"{R} Unmodified\n\nINFLICTS DAMAGE",
                      f"{R} Any other result\n\nINFLICTS DAMAGE", SAVE_TABLE)
    text = re.sub(rf"\s*{R}\s*", "\n- ", text)  # remaining unknown glyphs are bullet/arrow icons
    text = DROP_CAP.sub(
        lambda m: m.group(0) if f"{m.group(2)} {m.group(3)}" in DROP_CAP_FALSE_POSITIVES
        else m.group(1) + m.group(2) + m.group(3), text)
    return text


def slug(title: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", title.lower()).strip("_")


def clean(page: str) -> str:
    out = []
    for line in page.splitlines():
        line = line.replace("\t", " ")
        line = re.sub(r"[\x00-\x08\x0b-\x1f]", "", line).rstrip()
        for b in BULLETS:
            line = line.replace(b, "\n- ")
        line = re.sub(r" {2,}", " ", line)
        out.append(line)
    text = "\n".join(out)
    text = re.sub(r"\n- \s*\n", "\n- ", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def pdf_pages(pdf: Path) -> list[str]:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "rules.txt"
        subprocess.run(["pdftotext", "-enc", "UTF-8", str(pdf), str(out)], check=True)
        return out.read_text(encoding="utf-8").split("\f")


def main() -> None:
    pdf = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "11th-ed-core-rules.pdf"
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "docs" / "rules"
    pages = pdf_pages(pdf)
    index_rows = []
    for num, title, folder, first, last in SECTIONS:
        rel = Path(folder) / f"{num}_{slug(title)}.md"
        body = [f"# {num}. {title.upper()}", "",
                f"> Source: 11th Edition Core Rules PDF, pages {first}-{last}. "
                "Verbatim pdftotext extraction; `<!-- page N -->` marks page breaks. "
                "Sidebar/diagram text may appear out of order.", ""]
        for p in range(first, last + 1):
            body += [f"<!-- page {p} -->", ""]
            for line in clean(pages[p - 1]).splitlines():
                m = HEADING.match(line)
                if m and m.group(2).startswith(num) and m.group(1).isupper():
                    index_rows.append((m.group(2), m.group(1).title(), rel.as_posix(), p))
                    line = f"## {m.group(2)} {m.group(1)}"
                body.append(line)
            body.append("")
        dest = out_dir / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(fix_section(num, "\n".join(body)) + "\n", encoding="utf-8", newline="\n")

    seen, rows = set(), []
    for r in sorted(index_rows):
        if r[0] not in seen:
            seen.add(r[0])
            rows.append(r)
    idx = ["# Core Rules Index (11th Edition)", "",
           "Look up a rule number here, then read the linked file. "
           "Rule text is verbatim from the PDF (regenerate with `python tools/extract_rules.py`).", "",
           "| Section | File | Pages |", "|---|---|---|"]
    for num, title, folder, first, last in SECTIONS:
        rel = f"{folder}/{num}_{slug(title)}.md"
        idx.append(f"| {num}. {title} | [{rel}]({rel}) | {first}-{last} |")
    idx += ["", "## Rule numbers", "", "| Rule | Title | File | Page |", "|---|---|---|---|"]
    idx += [f"| {r[0]} | {r[1]} | [{r[2]}]({r[2]}) | {r[3]} |" for r in rows]
    (out_dir / "00_index.md").write_text("\n".join(idx) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {len(SECTIONS)} section files + index ({len(rows)} rule numbers) to {out_dir}")


if __name__ == "__main__":
    main()
