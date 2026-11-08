#!/usr/bin/env python3
"""Pull a unit datasheet from the BSData wh40k-11e repo (the data New Recruit uses)
and convert it into the engine's unit JSON format.

Usage:
    python tools/fetch_bsdata.py "<catalogue file>" "<unit name>" <out.json> [--ref <git sha>]

Examples:
    python tools/fetch_bsdata.py "Imperium - Space Marines.json" "Intercessor Squad" data/units/intercessor_squad.json
    python tools/fetch_bsdata.py "Tyranids.json" "Termagants" data/units/termagants.json
    python tools/fetch_bsdata.py "Chaos - Chaos Space Marines.json" "Havocs" data/units/havocs.json

What it reads (D-070): composition from the model types and the groups holding them (champions
counted, single models 1/1); wargear at any depth, skipping the Crusade, Weapon Modifications,
Enhancements and Crusade Relic Upgrades subtrees; weapon profiles behind infoLinks; each profile's
own ranged/melee type; default weapons from min-1 entries and required groups' defaults; round and
"AxBmm" oval bases (null with a warning for anything else); "Hunter: ..." profiles marked with a
"condition"; and characteristic modifiers that depend only on the unit's or model's own identity.

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


def apply_modifiers(profile: dict, ancestry: set[str], where: str) -> dict:
    """D-070: BSData changes some characteristics with modifiers, e.g. "set A to 5 when an ancestor
    is a HAVOCS unit" on the shared Astartes chainsword profile. Only modifiers whose conditions are
    all instanceOf / notInstanceOf an ancestor (the unit, its model or their categories) are applied;
    any other modifier of a characteristic is reported and left out."""
    mods = profile.get("modifiers", [])
    by_type = {c.get("typeId"): c.get("name") for c in profile.get("characteristics", [])}
    if not any(m.get("field") in by_type for m in mods):
        return profile
    out = dict(profile)
    out["characteristics"] = [dict(c) for c in profile.get("characteristics", [])]
    for m in mods:
        field = m.get("field")
        if field not in by_type:
            continue  # e.g. "hidden": not a characteristic
        conds = m.get("conditions", [])
        readable = (not m.get("conditionGroups") and m.get("type") == "set" and
                    all(c.get("type") in ("instanceOf", "notInstanceOf") and c.get("scope") == "ancestor"
                        for c in conds))
        if not readable:
            print(f"warning: {where}: profile '{profile.get('name')}': '{m.get('type')}' modifier of "
                  f"{by_type[field]} not applied (only 'set' with ancestor instanceOf conditions is read)",
                  file=sys.stderr)
            continue
        hit = all((c.get("childId") in ancestry) == (c.get("type") == "instanceOf") for c in conds)
        if hit:
            for c in out["characteristics"]:
                if c.get("typeId") == field:
                    c["$text"] = str(m.get("value"))
    return out


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


# D-070: "32mm" / "28.5mm" are round; "120x92mm" is an oval, length x width (the larger number is
# the length). Anything else ("Hull", "~40mm", "32mm Flying", "Unique", "-") can't be read: None.
_ROUND_BASE = re.compile(r"^(\d+(?:\.\d+)?)\s*mm$")
_OVAL_BASE = re.compile(r"^(\d+(?:\.\d+)?)\s*x\s*(\d+(?:\.\d+)?)\s*mm$")


def _mm(v: str) -> int | float:
    f = float(v)
    return int(f) if f.is_integer() else f


def parse_base(text: str) -> dict | None:
    t = text.strip()
    if m := _ROUND_BASE.match(t):
        return {"shape": "round", "diameter_mm": float(m.group(1))}
    if m := _OVAL_BASE.match(t):
        a, b = float(m.group(1)), float(m.group(2))
        length, width = max(a, b), min(a, b)
        if width == length:
            return {"shape": "round", "diameter_mm": width}
        return {"shape": "oval", "diameter_mm": width, "length_mm": length}
    return None


def parse_unit_profile(p: dict) -> dict:
    c = chars(p)
    base_text = c.get("Base", "")
    base = parse_base(base_text)
    if base is None and base_text.strip():
        print(f"warning: '{p.get('name')}': can't read base '{base_text}'; writing null (the loader refuses it)",
              file=sys.stderr)
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
        "base": base,
    }


WEAPON_TYPES = ("Ranged Weapons", "Melee Weapons")
# D-070: a profile that applies only against some targets, e.g. "➤ Chainfist - Hunter: Monster/Vehicle".
_HUNTER = re.compile(r"\b(Hunter:\s*.+)$")


def parse_weapon_profile(p: dict) -> dict:
    c = chars(p)
    melee = p.get("typeName") == "Melee Weapons"
    kw = [k.strip() for k in c.get("Keywords", "").split(",") if k.strip() and k.strip() != "-"]
    name = re.sub(r"^➤\s*", "", p.get("name", ""))
    out = {
        "name": name,
        "range_in": None if melee else num(c.get("Range", ""), '"'),
        "A": num(c.get("A", "")),
        "skill": num(c.get("WS" if melee else "BS", ""), "+"),
        "S": num(c.get("S", "")),
        "AP": num(c.get("AP", "")),
        "D": num(c.get("D", "")),
        "abilities": kw,
    }
    if m := _HUNTER.search(name):
        out["condition"] = m.group(1).strip()
    return out


def profile_kind(p: dict) -> str:
    return "melee" if p["typeName"] == "Melee Weapons" else "ranged"


def weapon_from_entry(idx: Index, entry: dict, ancestry: set[str]) -> tuple[str, dict] | None:
    # D-070: profiles on the entry itself, then profiles reached through infoLinks of type "profile".
    candidates = list(entry.get("profiles", []))
    candidates += [idx.deref(l) for l in entry.get("infoLinks", []) if l.get("type") == "profile"]
    profs = [apply_modifiers(p, ancestry, f"weapon '{entry.get('name')}'")
             for p in candidates if p.get("typeName") in WEAPON_TYPES]
    if not profs:
        return None
    kinds = [profile_kind(p) for p in profs]
    parsed = [parse_weapon_profile(p) for p in profs]
    if len(set(kinds)) > 1:
        # D-070: a mixed weapon; every profile carries its own type. The weapon's type stays the
        # first profile's, as before.
        parsed = [{"type": k, **pp} for k, pp in zip(kinds, parsed)]
    return entry["name"], {"type": kinds[0], "profiles": parsed}


# D-070: subtrees that hold no wargear of the datasheet itself (Crusade also holds real weapon
# profiles such as Vortex Grenade, which must not leak into a unit's options).
SKIPPED_SUBTREES = {"crusade", "weapon modifications", "enhancements", "crusade relic upgrades"}


def skipped(node: dict) -> bool:
    return node.get("name", "").strip().lower() in SKIPPED_SUBTREES


def is_group(node: dict) -> bool:
    return node.get("type") not in ("upgrade", "model", "unit")


def kids(idx: Index, node: dict) -> list[dict]:
    return [k for k in children(idx, node, "selectionEntries") + children(idx, node, "selectionEntryGroups")
            if not skipped(k)]


def group_default(idx: Index, group: dict, entries: list[dict]) -> dict | None:
    """The entry a required group starts with: its defaultSelectionEntryId, else its first entry."""
    dflt = group.get("defaultSelectionEntryId")
    if dflt and dflt != "none":
        return next((e for e in entries if dflt in (e.get("id"), e.get("_link_id"))), None)
    return entries[0] if entries else None


def ancestry_of(node: dict) -> set[str]:
    return {node["id"]} | {l["targetId"] for l in node.get("categoryLinks", []) if l.get("targetId")}


def collect_model(idx: Index, model: dict, weapons: dict, unit_ancestry: set[str]) -> dict:
    ancestry = unit_ancestry | ancestry_of(model)
    profiles = model.get("profiles", []) + [idx.deref(l) for l in model.get("infoLinks", [])]
    unit_prof = next((p for p in profiles if p.get("typeName") == "Unit"), None)
    if unit_prof:
        unit_prof = apply_modifiers(unit_prof, ancestry, f"model '{model.get('name')}'")
    info = parse_unit_profile(unit_prof) if unit_prof else {"stats": None, "base": None}
    default, options = [], []

    def record(e: dict, selected: bool) -> None:
        w = weapon_from_entry(idx, e, ancestry)
        if w:
            if w[0] in weapons and weapons[w[0]] != w[1]:
                print(f"warning: two different weapons named '{w[0]}'; keeping the first", file=sys.stderr)
            weapons.setdefault(w[0], w[1])
            options.append(w[0])
            if selected and w[0] not in default:
                default.append(w[0])

    # D-070: wargear is read at any depth. A node is in the default loadout when its parent is and
    # it is a min-1 entry, or it is the default entry of a required (min-1) group. Groups without a
    # min are plain containers.
    def visit(node: dict, selected: bool) -> None:
        for k in kids(idx, node):
            if k.get("type") == "model":
                continue
            if is_group(k):
                if (constraint(k, "min") or 0) >= 1:
                    entries = [e for e in children(idx, k, "selectionEntries") if not skipped(e)]
                    pick = group_default(idx, k, entries)
                    pick_key = (pick.get("id"), pick.get("_link_id")) if pick else None
                    for e in kids(idx, k):
                        is_pick = (e.get("id"), e.get("_link_id")) == pick_key
                        sel = selected and (is_pick or (constraint(e, "min") or 0) >= 1)
                        if is_group(e):
                            visit({"selectionEntryGroups": [e]}, sel)
                        else:
                            record(e, sel)
                            visit(e, sel)
                else:
                    visit(k, selected)
            else:
                sel = selected and (constraint(k, "min") or 0) >= 1
                record(k, sel)
                visit(k, sel)

    visit(model, True)
    single = model.get("type") == "model" and model.get("_unit_is_model")
    return {
        "name": model["name"],
        "min": 1 if single else (constraint(model, "min") or 0),
        "max": 1 if single else constraint(model, "max"),
        **info,
        "default_weapons": default,
        "weapon_options": sorted(set(options)),
    }


def composition(idx: Index, node: dict) -> tuple[int, int | None]:
    """D-070: how many models a unit may have, from its model types and the groups holding them.
    A group's own min/max bound the sum of what it holds; champions outside the group add to it."""
    lo, hi = 0, 0
    for k in kids(idx, node):
        if k.get("type") == "model":
            kmin, kmax = constraint(k, "min") or 0, constraint(k, "max")
        elif is_group(k):
            kmin, kmax = composition(idx, k)
            gmin, gmax = constraint(k, "min"), constraint(k, "max")
            if gmin is not None:
                kmin = max(kmin, gmin)
            if gmax is not None:
                kmax = gmax if kmax is None else min(kmax, gmax)
        else:
            continue
        lo += kmin
        hi = None if hi is None or kmax is None else hi + kmax
    return lo, hi


def find_models(idx: Index, node: dict, out: list[dict]) -> None:
    for e in children(idx, node, "selectionEntries"):
        if e.get("type") == "model":
            out.append(e)
    # Groups, including groups reached through entryLinks, minus the D-070 skipped subtrees.
    for g in kids(idx, node):
        if is_group(g):
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
        # D-070: an infoLink to an Abilities profile (e.g. "Damaged: 1-5 Wounds Remaining") keeps its
        # text in the profile's Description characteristic, not in a rule's description.
        text = tgt.get("description") or chars(tgt).get("Description", "")
        abilities.append({"name": tgt.get("name", link.get("name")), "text": text, "core": True})

    weapons: dict[str, dict] = {}
    model_nodes: list[dict] = []
    if unit.get("type") == "model":
        model_nodes = [{**unit, "_unit_is_model": True}]  # D-070: a single model is min 1 / max 1
    else:
        find_models(idx, unit, model_nodes)
    models = [collect_model(idx, m, weapons, ancestry_of(unit)) for m in model_nodes]

    if unit.get("type") == "model":
        comp_min, comp_max = 1, 1
    else:
        comp_min, comp_max = composition(idx, unit)
        if comp_min == 0:
            comp_min = None
    for m in models:
        if m["max"] is None or m["base"] is None or m["stats"] is None:
            print(f"warning: '{unit_name}' model type '{m['name']}' has a missing max, base or profile; "
                  "the engine's loader will refuse this file", file=sys.stderr)
    if comp_min is None or comp_max is None:
        print(f"warning: '{unit_name}': composition not found; the engine's loader will refuse this file",
              file=sys.stderr)

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
