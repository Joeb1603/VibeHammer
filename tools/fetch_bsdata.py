#!/usr/bin/env python3
"""Pull a unit datasheet from the BSData wh40k-11e repo (the data New Recruit uses)
and convert it into the engine's unit JSON format.

Usage:
    python tools/fetch_bsdata.py "<catalogue file>" "<unit name>" <out.json> [--ref <git sha>]

Examples:
    python tools/fetch_bsdata.py "Imperium - Space Marines.json" "Intercessor Squad" data/units/intercessor_squad.json
    python tools/fetch_bsdata.py "Tyranids.json" "Termagants" data/units/termagants.json

Downloads are cached in data/.cache/bsdata/<sha>/ (gitignored). The output records the
exact commit it came from, so re-running with --ref <sha> reproduces it.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

REPO = "BSData/wh40k-11e"
GAME_SYSTEM = "Warhammer 40,000.json"
ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "data" / ".cache" / "bsdata"


def http_get(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "vibehammer-fetch"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def resolve_ref(ref: str | None) -> str:
    if ref:
        return ref
    data = json.loads(http_get(f"https://api.github.com/repos/{REPO}/commits/main"))
    return data["sha"]


def load_catalogue(name: str, sha: str) -> dict:
    path = CACHE / sha / name
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://raw.githubusercontent.com/{REPO}/{sha}/{urllib.parse.quote(name)}"
        print(f"downloading {name} @ {sha[:10]}", file=sys.stderr)
        path.write_bytes(http_get(url))
    doc = json.loads(path.read_text(encoding="utf-8"))
    return doc.get("catalogue") or doc.get("gameSystem") or doc


def linked_catalogues(cat: dict) -> list[str]:
    """Library catalogues this one imports (e.g. 'Library - Tyranids')."""
    return [link["name"] + ".json" for link in cat.get("catalogueLinks", []) if link.get("name")]


class Index:
    def __init__(self, cats: list[dict]):
        self.by_id: dict[str, dict] = {}
        for c in cats:
            self._walk(c)

    def _walk(self, o):
        if isinstance(o, dict):
            if "id" in o:
                self.by_id.setdefault(o["id"], o)
            for v in o.values():
                self._walk(v)
        elif isinstance(o, list):
            for v in o:
                self._walk(v)

    def deref(self, node: dict) -> dict:
        """Follow an entryLink/infoLink to its target, keeping the link's constraints."""
        if "targetId" not in node:
            return node
        tgt = self.by_id.get(node["targetId"])
        if tgt is None:
            return node
        merged = dict(tgt)
        merged["constraints"] = node.get("constraints", []) + tgt.get("constraints", [])
        merged["_link_id"] = node.get("id")
        return merged


def constraint(node: dict, kind: str, scope: str = "parent") -> int | None:
    for c in node.get("constraints", []):
        if c.get("type") == kind and c.get("field") == "selections" and c.get("scope") == scope:
            return int(float(c["value"]))
    return None


def children(idx: Index, node: dict, key: str) -> list[dict]:
    out = list(node.get(key, []))
    if key == "selectionEntries":
        out += [idx.deref(l) for l in node.get("entryLinks", [])]
    return [idx.deref(c) for c in out]


def chars(profile: dict) -> dict[str, str]:
    return {c["name"]: c.get("$text", "").strip() for c in profile.get("characteristics", [])}


def num(v: str, suffix: str = "") -> int | str | None:
    v = v.strip().replace('"', "").replace("+", "") if suffix else v.strip()
    if v in ("", "-"):
        return None
    try:
        return int(v)
    except ValueError:
        return v  # random values such as "D3" or "D6+1"


def parse_unit_profile(p: dict) -> dict:
    c = chars(p)
    base = c.get("Base", "").replace("mm", "").strip()
    return {
        "stats": {
            "M": num(c.get("M", ""), '"'),
            "T": num(c.get("T", "")),
            "Sv": num(c.get("Sv", ""), "+"),
            "InSv": num(c.get("InSv", ""), "+"),
            "W": num(c.get("W", "")),
            "Ld": num(c.get("LD", c.get("Ld", "")), "+"),
            "OC": num(c.get("OC", "")),
        },
        "base": {"shape": "round", "diameter_mm": float(base)} if base else None,
    }


def parse_weapon_profile(p: dict) -> dict:
    c = chars(p)
    melee = p.get("typeName") == "Melee Weapons"
    kw = [k.strip() for k in c.get("Keywords", "").split(",") if k.strip() and k.strip() != "-"]
    name = re.sub(r"^➤\s*", "", p.get("name", ""))
    return {
        "name": name,
        "range_in": None if melee else num(c.get("Range", ""), '"'),
        "A": num(c.get("A", "")),
        "skill": num(c.get("WS" if melee else "BS", ""), "+"),
        "S": num(c.get("S", "")),
        "AP": num(c.get("AP", "")),
        "D": num(c.get("D", "")),
        "abilities": kw,
    }


def weapon_from_entry(entry: dict) -> tuple[str, dict] | None:
    profs = [p for p in entry.get("profiles", []) if p.get("typeName") in ("Ranged Weapons", "Melee Weapons")]
    if not profs:
        return None
    kind = "melee" if profs[0]["typeName"] == "Melee Weapons" else "ranged"
    return entry["name"], {"type": kind, "profiles": [parse_weapon_profile(p) for p in profs]}


def collect_model(idx: Index, model: dict, weapons: dict) -> dict:
    profiles = model.get("profiles", []) + [idx.deref(l) for l in model.get("infoLinks", [])]
    unit_prof = next((p for p in profiles if p.get("typeName") == "Unit"), None)
    info = parse_unit_profile(unit_prof) if unit_prof else {"stats": None, "base": None}
    default, options = [], []

    def visit_entry(e: dict, required: bool):
        w = weapon_from_entry(e)
        if w:
            weapons.setdefault(w[0], w[1])
            options.append(w[0])
            if required:
                default.append(w[0])

    for e in children(idx, model, "selectionEntries"):
        visit_entry(e, (constraint(e, "min") or 0) >= 1)
    for g in children(idx, model, "selectionEntryGroups"):
        entries = children(idx, g, "selectionEntries")
        dflt = g.get("defaultSelectionEntryId")
        group_required = (constraint(g, "min") or 0) >= 1
        for i, e in enumerate(entries):
            is_default = dflt in (e.get("id"), e.get("_link_id")) if dflt else (group_required and i == 0)
            visit_entry(e, group_required and is_default)

    return {
        "name": model["name"],
        "min": constraint(model, "min") or 0,
        "max": constraint(model, "max"),
        **info,
        "default_weapons": default,
        "weapon_options": sorted(set(options)),
    }


def find_models(idx: Index, node: dict, out: list[dict]) -> None:
    for e in children(idx, node, "selectionEntries"):
        if e.get("type") == "model":
            out.append(e)
    for g in children(idx, node, "selectionEntryGroups"):
        if g.get("name", "").lower() == "crusade":
            continue
        find_models(idx, g, out)


def extract(unit_name: str, cats: list[dict], idx: Index) -> dict:
    unit = None
    for c in cats:
        for e in c.get("sharedSelectionEntries", []) + c.get("selectionEntries", []):
            if e.get("name") == unit_name and e.get("type") in ("unit", "model"):
                unit = e
                break
        if unit:
            break
    if unit is None:
        sys.exit(f"unit '{unit_name}' not found")

    keywords, faction = [], []
    for link in unit.get("categoryLinks", []):
        n = link.get("name", "")
        if n.startswith("Faction: "):
            faction.append(n.removeprefix("Faction: "))
        elif n != unit_name:
            keywords.append(n)

    abilities = []
    for p in unit.get("profiles", []):
        if p.get("typeName") == "Abilities":
            abilities.append({"name": p["name"], "text": chars(p).get("Description", "")})
    for link in unit.get("infoLinks", []):
        tgt = idx.deref(link)
        abilities.append({"name": tgt.get("name", link.get("name")), "text": tgt.get("description", ""),
                          "core": True})

    weapons: dict[str, dict] = {}
    model_nodes: list[dict] = []
    if unit.get("type") == "model":
        model_nodes = [unit]
    else:
        find_models(idx, unit, model_nodes)
    models = [collect_model(idx, m, weapons) for m in model_nodes]

    comp_min = comp_max = None
    for g in children(idx, unit, "selectionEntryGroups"):
        if any(m in model_nodes for m in children(idx, g, "selectionEntries")) or g.get("name", "").lower() != "crusade":
            comp_min = comp_min or constraint(g, "min")
            comp_max = comp_max or constraint(g, "max")

    return {
        "name": unit_name,
        "faction_keywords": faction,
        "keywords": keywords,
        "abilities": abilities,
        "composition": {"min_models": comp_min, "max_models": comp_max},
        "model_types": models,
        "weapons": weapons,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("catalogue")
    ap.add_argument("unit")
    ap.add_argument("out", type=Path)
    ap.add_argument("--ref", help="git commit sha of BSData/wh40k-11e (default: latest main)")
    a = ap.parse_args()

    sha = resolve_ref(a.ref)
    main_cat = load_catalogue(a.catalogue, sha)
    cats = [main_cat]
    for lib in linked_catalogues(main_cat):
        try:
            cats.append(load_catalogue(lib, sha))
        except Exception as e:  # noqa: BLE001 - libraries are optional for most units
            print(f"warning: could not load {lib}: {e}", file=sys.stderr)
    cats.append(load_catalogue(GAME_SYSTEM, sha))
    idx = Index(cats)

    data = {
        "source": {
            "repo": f"https://github.com/{REPO}",
            "commit": sha,
            "catalogue": a.catalogue,
            "fetched": dt.date.today().isoformat(),
            "note": "Generated by tools/fetch_bsdata.py - do not hand-edit; re-run the tool instead.",
        },
        **extract(a.unit, cats, idx),
    }
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {a.out} ({len(data['model_types'])} model types, {len(data['weapons'])} weapons)")


if __name__ == "__main__":
    main()
