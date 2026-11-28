#!/usr/bin/env python3
"""Download Wahapedia's 11th edition data export into docs/wahapedia/.

Usage:
    python tools/fetch_wahapedia.py [--out docs/wahapedia]

Wahapedia publishes its data as pipe-delimited UTF-8 CSV files, linked through string ids, and
documents them in "Export Data Specs.xlsx" (https://wahapedia.ru/wh40k11ed/the-rules/data-export/).
This script downloads the spec and every file it lists, one at a time, with retries. Text fields
(description, legend, ...) hold HTML as shown on the site.

The data is Games Workshop's copyrighted text in a third-party transcription: keep it in the private
repo only, never in data/, the package or a release. docs/rules/ stays the source of truth.
"""
import argparse
import pathlib
import time
import urllib.request

BASE = "https://wahapedia.ru/wh40k11ed/"
SPEC = "Export%20Data%20Specs.xlsx"
FILES = [
    "Factions", "Source", "Datasheets", "Datasheets_abilities", "Datasheets_keywords",
    "Datasheets_models", "Datasheets_options", "Datasheets_wargear", "Datasheets_unit_composition",
    "Datasheets_models_cost", "Datasheets_stratagems", "Datasheets_enhancements",
    "Datasheets_detachment_abilities", "Datasheets_leader", "Stratagems", "Abilities",
    "Enhancements", "Enhancements_wargear", "Detachment_abilities", "Detachments",
    "Detachments_chapter_dp", "Last_update",
]


def fetch(url: str, tries: int = 4) -> bytes:
    for attempt in range(tries):
        try:
            with urllib.request.urlopen(url, timeout=60) as resp:
                return resp.read()
        except OSError:
            if attempt == tries - 1:
                raise
            time.sleep(2 ** attempt)
    raise AssertionError("unreachable")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default="docs/wahapedia")
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    (out / "Export Data Specs.xlsx").write_bytes(fetch(BASE + SPEC))
    for name in FILES:
        data = fetch(BASE + name + ".csv")
        (out / f"{name}.csv").write_bytes(data)
        lines = data.count(b"\n")
        print(f"{name}.csv: {lines} lines, {len(data)} bytes")
        time.sleep(1)  # be gentle with a one-person site


if __name__ == "__main__":
    main()
