#!/usr/bin/env python3
"""Pull a unit datasheet from the BSData wh40k-11e repo (the data New Recruit uses)
and convert it into the engine's unit JSON format.

Usage:
    python tools/fetch_bsdata.py "<catalogue file>" "<unit name>" <out.json> [--ref <git sha>] [--fetched <YYYY-MM-DD>]

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

D-079 adds a "list_building" block after "weapons": points by unit size, the copy surcharge, the copy
limits (default and at Incursion), each model type's fixed weapons, wargear slots (min 1 / max 1
groups, nested ones flattened into composite options) with option points, and the limits per model
and per unit that BSData scopes to the model, the unit, the force or the roster. Limits scoped to
other datasheets, other game modes and the non-weapon subtrees (Mark of Chaos, icons, Warlord, ...)
are dropped. Anything else in scope is warned about, and the datasheet then gets no list_building.
--fetched keeps a file's existing "fetched" date when it is regenerated.

Downloads are cached in data/.cache/bsdata/<sha>/ (gitignored). The output records the
exact commit it came from, so re-running with --ref <sha> reproduces it.
"""
from __future__ import annotations

import argparse
import datetime as dt
import itertools
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

    out = {
        "name": unit_name,
        "faction_keywords": faction,
        "keywords": keywords,
        "abilities": abilities,
        "composition": {"min_models": comp_min, "max_models": comp_max},
        "model_types": models,
        "weapons": weapons,
    }
    lb = read_list_building(idx, unit, comp_min, comp_max, models)
    if lb is not None:
        out["list_building"] = lb  # D-079
    return out


# ---------------------------------------------------------------------------------------------------
# D-079: list-building data: points by unit size, the copy surcharge, copy limits, wargear slots and
# limits. Anything in scope that matches no pattern below is reported, and the datasheet then gets no
# list_building at all (partial list rules would let a builder accept armies BSData refuses).
# ---------------------------------------------------------------------------------------------------

PTS = "51b2-306e-1021-d207"                    # the "pts" cost type
INCURSION = "d62d-db22-4893-4bc0"              # battle size "1. Incursion (1000 Point limit)"
OTHER_FORCES = {"1d6e-2579-8e7f-1ed4",         # Boarding Actions
                "cac3-71d1-ea4b-795d"}         # Crusade Force
# D-079: subtrees that hold no weapon option of the datasheet; checked to cost 0 and hold no weapon.
NON_WEAPON_SUBTREES = {"mark of chaos", "chaos icon", "unholy patronage upgrade", "warlord", "chaos boons",
                       "weapon upgrades"}
DISPLAY_FIELDS = {"name", "annotation", "category", "info", "add-info", "page"}
SCOPE_KEYWORDS = {"parent", "self", "model", "unit", "force", "roster", "ancestor", "root-entry",
                  "primary-catalogue", "model-or-unit", "upgrade"}


def lb_skipped(name: str) -> bool:
    key = name.strip().lower()
    return key in SKIPPED_SUBTREES or key in NON_WEAPON_SUBTREES


def joined(names: list[str]) -> str:
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]


def flat_conditions(m: dict) -> list[dict]:
    out = list(m.get("conditions", []))
    for g in m.get("conditionGroups", []):
        out += flat_conditions(g)
    return out


class LNode:
    """D-079: an entry or group as reached, with the link that reached it: the link's own constraints,
    modifiers, costs and children count too."""

    def __init__(self, idx: Index, raw: dict, link: dict | None = None):
        self.idx, self.raw, self.link = idx, raw, link or {}
        self.name = raw.get("name", "")
        self.id = raw.get("id")
        self.link_id = self.link.get("id")
        self.cons = self.link.get("constraints", []) + raw.get("constraints", [])
        self.mods = self.link.get("modifiers", []) + raw.get("modifiers", [])
        self.modgroups = self.link.get("modifierGroups", []) + raw.get("modifierGroups", [])
        self.type = raw.get("type")
        self.is_group = self.type not in ("upgrade", "model", "unit")
        self.hidden = bool(raw.get("hidden") or self.link.get("hidden"))
        self.missing = "_missing" in raw   # an entry link whose target isn't in the catalogues

    def pts(self) -> float:
        for src in (self.link, self.raw):  # D-079: an entry link's own pts replace the target's
            for c in src.get("costs", []):
                if c.get("typeId") == PTS:
                    return float(c.get("value", 0))
        return 0.0

    def _resolve(self, l: dict) -> "LNode":
        t = self.idx.by_id.get(l.get("targetId"))
        return LNode(self.idx, t if t is not None else {"name": l.get("name", "?"), "_missing": True}, l)

    def kids(self) -> list["LNode"]:
        out = []
        for key in ("selectionEntries", "selectionEntryGroups"):
            for src in (self.raw, self.link):
                out += [LNode(self.idx, e) for e in src.get(key, [])]
        for src in (self.raw, self.link):
            out += [self._resolve(l) for l in src.get("entryLinks", [])]
        return out

    def bound(self, kind: str) -> int | None:
        for c in self.cons:
            if c.get("type") == kind and c.get("field") == "selections" and c.get("scope") == "parent":
                return int(float(c["value"]))
        return None

    def weapon(self) -> bool:
        cands = list(self.raw.get("profiles", []))
        cands += [self.idx.deref(l) for l in self.raw.get("infoLinks", []) if l.get("type") == "profile"]
        return any(p.get("typeName") in WEAPON_TYPES for p in cands)


def find_model_lnodes(idx: Index, raw: dict, out: list[LNode]) -> None:
    """The model types in find_models' order (D-070), with the links that reached them."""
    entries = [LNode(idx, e) for e in raw.get("selectionEntries", [])]
    entries += [LNode(idx, idx.by_id[l["targetId"]], l) for l in raw.get("entryLinks", [])
                if l.get("targetId") in idx.by_id]
    groups = [LNode(idx, g) for g in raw.get("selectionEntryGroups", [])]
    for e in entries:
        if e.type == "model":
            out.append(e)
    for g in entries + groups:
        if g.is_group and not skipped(g.raw):
            find_model_lnodes(idx, g.raw, out)


class ListReader:
    def __init__(self, idx: Index, unit: dict, model_nodes: list[LNode], model_types: list[dict]):
        self.idx = idx
        self.unit = LNode(idx, unit)
        self.uid = unit["id"]
        self.single = unit.get("type") == "model"
        self.model_nodes = model_nodes
        self.model_types = model_types
        self.unread: list[str] = []
        self.dropped_cons: set[str] = set()
        self.cost_types = {o["id"] for o in idx.by_id.values()
                           if isinstance(o, dict) and "defaultCostLimit" in o and o.get("id")}
        self.unit_limits: list[dict] = []
        self.per_model: dict[str, list[dict]] = {}
        self.carriers: dict[str, set[str]] = {}
        self._index_carriers(self.unit, 0)

    def fail(self, where: str, what: str) -> None:
        self.unread.append(f"{where}: {what}")

    def _index_carriers(self, node: LNode, depth: int) -> None:
        """Every weapon entry in the unit, by weapon name (a unit limit counts a weapon by name only when
        every carrier of that name is the same BSData entry)."""
        for k in node.kids():
            if lb_skipped(k.name):
                continue
            if k.missing:
                self.fail(f"{self.unit.name}/{k.name}", "an entry link to a missing target")
            if not k.is_group and k.weapon():
                self.carriers.setdefault(k.name, set()).add(k.id)
            if depth < 32:
                self._index_carriers(k, depth + 1)

    # -- scopes and the modifiers that are not list rules --------------------------------------------
    def own_ids(self) -> set[str]:
        return {self.uid} | {m.id for m in self.model_nodes}

    def other_datasheet(self, scope) -> bool:
        if not scope or scope in SCOPE_KEYWORDS or scope in self.own_ids():
            return False
        n = self.idx.by_id.get(scope)
        return isinstance(n, dict) and n.get("type") in ("unit", "model")

    def unit_scope(self, scope) -> bool:
        return scope in (self.uid, "unit")

    def dropped_modifier(self, m: dict, extra=()) -> bool:
        """D-079: other game modes, display, profile changes, Crusade / Enhancements, other datasheets."""
        conds = flat_conditions(m) + list(extra)
        if any(c.get("childId") in OTHER_FORCES for c in conds):
            return True
        f = m.get("field")
        if f in DISPLAY_FIELDS or "affects" in m:
            return True
        if f in self.dropped_cons or f in (self.cost_types - {PTS}):
            return True
        if f in ("error", "warning") and conds and all(self.other_datasheet(c.get("scope")) for c in conds):
            return True
        return False

    def check_modgroups(self, node: LNode, where: str) -> None:
        for g in node.modgroups:
            extra = flat_conditions(g)
            associations = any(c.get("field") == "associations" for c in extra)  # leader attachments
            if g.get("modifierGroups"):
                self.fail(where, "nested modifier groups")
            for m in g.get("modifiers", []):
                if not associations and not self.dropped_modifier(m, extra):
                    self.fail(where, f"modifier {m}")

    def dropped_constraint(self, c: dict) -> bool:
        """Constraints on Crusade / Enhancement cost types and on leader associations."""
        if c.get("field") in (self.cost_types - {PTS}) or c.get("field") == "associations":
            self.dropped_cons.add(c.get("id"))
            return True
        return False

    def skipped_ok(self, node: LNode, where: str) -> bool:
        """D-079: True for a skipped subtree, after checking that it costs no points (Enhancements: only
        when optional) and, for a non-weapon subtree, that it holds no weapon."""
        if not lb_skipped(node.name):
            return False
        key = node.name.strip().lower()
        pts, weapons, seen = [], [], set()

        def walk(o):
            if isinstance(o, dict):
                if any(c.get("typeId") == PTS and float(c.get("value", 0)) != 0 for c in o.get("costs", [])) or \
                        any(m.get("field") == PTS for m in o.get("modifiers", [])):
                    pts.append(o.get("name"))
                if o.get("typeName") in WEAPON_TYPES:
                    weapons.append(o.get("name"))
                for k, v in o.items():
                    if k == "targetId" and isinstance(v, str) and v in self.idx.by_id:
                        if v not in seen:
                            seen.add(v)
                            walk(self.idx.by_id[v])
                    else:
                        walk(v)
            elif isinstance(o, list):
                for v in o:
                    walk(v)
        walk(node.raw)
        walk(node.link)
        if key == "enhancements":
            if (node.bound("min") or 0) > 0:
                self.fail(where, "a required Enhancements group")
        elif pts:
            self.fail(where, f"skipped subtree '{node.name}' costs points ({pts[:3]})")
        if key in NON_WEAPON_SUBTREES and weapons:
            self.fail(where, f"skipped subtree '{node.name}' holds weapons ({weapons[:3]})")
        return True

    # -- the unit ---------------------------------------------------------------------------------
    def read(self, comp_min: int, comp_max: int) -> dict | None:
        u = self.unit
        base = next((int(float(c["value"])) for c in u.raw.get("costs", []) if c.get("typeId") == PTS), None)
        if base is None:
            self.fail(u.name, "no pts cost")
        copies_id, copies, incursion = None, None, None
        for c in u.cons:
            if self.dropped_constraint(c):
                continue
            if (c.get("field") == "selections" and c.get("scope") == "force" and c.get("type") == "max"
                    and copies_id is None):
                copies_id, copies = c.get("id"), int(float(c["value"]))
            else:
                self.fail(u.name, f"constraint {c}")
        brackets, surcharge = [], None
        for m in u.mods:
            if self.dropped_modifier(m):
                continue
            conds = m.get("conditions", [])
            plain = not m.get("conditionGroups") and not m.get("repeats") and len(conds) == 1
            c = conds[0] if plain else {}
            if (plain and m.get("field") == PTS and m.get("type") == "set" and c.get("childId") == "model"
                    and c.get("field") == "selections" and c.get("scope") == self.uid
                    and c.get("type") in ("atLeast", "greaterThan")):
                n = int(float(c["value"])) + (1 if c["type"] == "greaterThan" else 0)
                brackets.append({"min_models": n, "points": int(float(m["value"]))})
            elif m.get("field") == PTS and m.get("type") == "increment" and self.surcharge_shape(m):
                k = m["conditionGroups"][0]["localConditionGroups"][0]["value"]
                surcharge = {"earlier_copies": int(float(k)), "points": int(float(m["value"]))}
            elif (plain and copies_id and m.get("field") == copies_id and m.get("type") == "set"
                    and c.get("childId") == INCURSION and c.get("scope") == "roster"
                    and c.get("type") == "atLeast" and float(c.get("value", 0)) == 1):
                incursion = int(float(m["value"]))
            else:
                self.fail(u.name, f"modifier {m}")
        self.check_modgroups(u, u.name)
        if copies is None:
            self.fail(u.name, "no copy limit (a max on selections in scope force)")
        if not self.single:
            self.check_unit_children(u)

        wargear = [self.read_model(n, mt) for n, mt in zip(self.model_nodes, self.model_types)]
        points = [{"min_models": comp_min, "points": base}] + sorted(brackets, key=lambda b: b["min_models"])
        if any(a["min_models"] >= b["min_models"] for a, b in zip(points, points[1:])) or \
                points[-1]["min_models"] > comp_max:
            self.fail(u.name, f"size brackets {points} don't fit {comp_min}-{comp_max} models")
        if self.unread:
            return None
        out = {"points": points}
        if surcharge:
            out["copy_surcharge"] = surcharge
        out["max_copies"] = {"default": copies, "incursion": copies if incursion is None else incursion}
        out["wargear"] = wargear
        out["unit_limits"] = self.unit_limits
        return out

    def surcharge_shape(self, m: dict) -> bool:
        """D-079: +X when at least K selections in the force come before this one and are this datasheet."""
        groups = m.get("conditionGroups", [])
        if m.get("conditions") or m.get("repeats") or len(groups) != 1:
            return False
        g = groups[0]
        locs = g.get("localConditionGroups", [])
        if g.get("conditions") or g.get("conditionGroups") or len(locs) != 1:
            return False
        lg = locs[0]
        lc = lg.get("conditions", [])
        return (lg.get("type") == "atLeast" and lg.get("scope") == "parent" and lg.get("field") == "selections"
                and not lg.get("conditionGroups") and len(lc) == 2
                and any(c.get("type") == "before" and c.get("childId") == "any" for c in lc)
                and any(c.get("type") == "instanceOf" and c.get("childId") == self.uid for c in lc))

    def check_unit_children(self, node: LNode) -> None:
        """Unit-level children: model types, the groups holding them (02.06, D-070), skipped subtrees."""
        for k in node.kids():
            where = f"{self.unit.name}/{k.name}"
            if k.type == "model" or self.skipped_ok(k, where):
                continue
            if not k.is_group:
                self.fail(where, "a unit-level entry")
                continue
            for c in k.cons:
                if not (c.get("field") == "selections" and c.get("scope") == "parent"):
                    self.fail(where, f"constraint {c}")
            for m in k.mods:
                if not self.dropped_modifier(m):
                    self.fail(where, f"modifier {m}")
            self.check_modgroups(k, where)
            if not any(x.type == "model" for x in k.kids()):
                self.fail(where, "a unit-level group without model types")
            self.check_unit_children(k)

    # -- one model type ---------------------------------------------------------------------------
    def read_model(self, m: LNode, mt: dict) -> dict:
        where = f"{self.unit.name}/{m.name}"
        if not self.single:
            if m.hidden or m.pts() != 0:
                self.fail(where, "a hidden model type, or one with its own points")
            for c in m.cons:
                if not self.dropped_constraint(c) and not (
                        c.get("field") == "selections" and c.get("scope") == "parent"
                        and c.get("type") in ("min", "max")):
                    self.fail(where, f"constraint {c}")
            for mod in m.mods:
                if not self.dropped_modifier(mod) and not self.model_error_group(mod, m, mt["max"]):
                    self.fail(where, f"modifier {mod}")
            self.check_modgroups(m, where)
        fixed, slots, places = [], [], []   # places: per fixed weapon or slot, the entries each choice gives
        self.visit(m, m, where, fixed, slots, places)
        limits = self.model_limits(m, places)
        weapons = fixed + [w for s in slots for o in s["options"] for w in o["weapons"]]
        for w in weapons:
            if w not in mt["weapon_options"]:
                self.fail(where, f"'{w}' is not one of the model type's weapon options")
        if set(weapons) != set(mt["weapon_options"]):
            print(f"warning: D-079: '{where}': the slots don't cover the weapon options exactly", file=sys.stderr)
        defaults = fixed + [w for s in slots for o in s["options"] if o["name"] == s["default"] for w in o["weapons"]]
        if sorted(defaults) != sorted(mt["default_weapons"]):
            print(f"warning: D-079: '{where}': the slot defaults differ from default_weapons", file=sys.stderr)
        return {"model_type": m.name, "fixed": fixed, "slots": slots, "limits": limits}

    def visit(self, model: LNode, node: LNode, where: str, fixed, slots, places) -> None:
        for k in node.kids():
            kw = f"{where}/{k.name}"
            if k.type == "model" or self.skipped_ok(k, kw):
                continue
            if k.hidden:
                self.fail(kw, "a hidden entry or group")
            elif k.is_group:
                lo, hi = k.bound("min"), k.bound("max")
                self.check_group(k, kw)
                if lo == 1 and hi == 1:
                    slot = self.read_slot(model, k, kw, in_composite=False)
                    if slot:
                        slots.append(slot)
                        places.append([o["_entries"] for o in slot["options"]])
                elif lo is None and hi is None:
                    self.visit(model, k, kw, fixed, slots, places)   # a container, such as "Wargear"
                else:
                    self.fail(kw, f"a group with min {lo} and max {hi}")
            elif not k.weapon() or (k.bound("min") or 0) < 1 or k.pts() != 0:
                self.fail(kw, "an entry outside a slot that is not a required weapon without points")
            else:
                self.check_entry(model, k, kw, in_composite=False)
                self.only_skipped_kids(k, kw)
                fixed.append(k.name)
                places.append([[k]])

    def check_group(self, g: LNode, where: str) -> None:
        for c in g.cons:
            if not (c.get("field") == "selections" and c.get("scope") == "parent"):
                self.fail(where, f"constraint {c}")
        for m in g.mods:
            if not self.dropped_modifier(m):
                self.fail(where, f"modifier {m}")
        self.check_modgroups(g, where)

    def only_skipped_kids(self, node: LNode, where: str) -> None:
        for k in node.kids():
            if not self.skipped_ok(k, f"{where}/{k.name}"):
                self.fail(where, f"'{k.name}' inside '{node.name}'")

    def read_slot(self, model: LNode, g: LNode, where: str, in_composite: bool) -> dict | None:
        entries = []
        for k in g.kids():
            if self.skipped_ok(k, f"{where}/{k.name}"):
                continue
            if k.is_group or k.hidden:
                self.fail(where, f"'{k.name}' in slot '{g.name}'")
            else:
                entries.append(k)
        dflt = g.raw.get("defaultSelectionEntryId")
        if dflt in (None, "none"):
            dnode = entries[0] if entries else None   # D-070: BSData names none: the first entry
        else:
            dnode = next((e for e in entries if dflt in (e.id, e.link_id)), None)
            if dnode is None:
                self.fail(where, f"slot '{g.name}': its default {dflt} is not one of its entries")
        options, default = [], None
        for e in entries:
            combos, d = self.option_combos(model, e, f"{where}/{e.name}", in_composite)
            for i, (name, weapons, pts, ents) in enumerate(combos):
                options.append({"name": name, "weapons": weapons, "points": pts, "_entries": ents})
                if e is dnode and i == d:
                    default = name
        names = [o["name"] for o in options]
        if not options or len(set(names)) != len(names):
            self.fail(where, f"slot '{g.name}' has no options, or two of one name")
            return None
        return {"name": g.name, "default": default, "options": options}

    def option_combos(self, model: LNode, e: LNode, where: str, in_composite: bool) -> tuple[list, int]:
        """D-079: an option's choices (name, weapons, points, entries) and which one is its default: one
        weapon; a composite of min-1 weapons; or one choice per combination of nested min 1 / max 1 groups
        (flattened, the first group varying slowest, named by its choices joined)."""
        if any(m.get("field") == PTS for m in e.mods):
            self.fail(where, "a points modifier on an option")
        self.check_entry(model, e, where, in_composite)
        weapons, ents, nested = ([e.name], [e], []) if e.weapon() else ([], [], [])
        pts = e.pts()
        for k in e.kids():
            kw = f"{where}/{k.name}"
            if self.skipped_ok(k, kw):
                continue
            if k.hidden:
                self.fail(kw, "a hidden entry or group")
            elif k.is_group and k.bound("min") == 1 and k.bound("max") == 1:
                self.check_group(k, kw)
                slot = self.read_slot(model, k, kw, in_composite=True)
                if slot:
                    nested.append(slot)
            elif not k.is_group and k.weapon() and (k.bound("min") or 0) >= 1:
                if any(m.get("field") == PTS for m in k.mods):
                    self.fail(kw, "a points modifier on a composite part")
                self.check_entry(model, k, kw, in_composite=True)
                self.only_skipped_kids(k, kw)
                weapons.append(k.name)
                ents.append(k)
                pts += k.pts()
            else:
                self.fail(kw, f"'{k.name}' inside option '{e.name}'")
        if nested and weapons:
            self.fail(where, f"option '{e.name}' mixes weapons and nested groups")
            return [], 0
        if not nested:
            if not weapons:
                self.fail(where, f"option '{e.name}' gives no weapon")
                return [], 0
            return [(e.name, weapons, int(pts), ents)], 0
        combos = []
        for pick in itertools.product(*[s["options"] for s in nested]):
            combos.append((joined([p["name"] for p in pick]), [w for p in pick for w in p["weapons"]],
                           int(pts) + sum(p["points"] for p in pick), [x for p in pick for x in p["_entries"]]))
        default_name = joined([s["default"] for s in nested])
        return combos, next((i for i, c in enumerate(combos) if c[0] == default_name), 0)

    # -- constraints and modifiers on a wargear entry --------------------------------------------------
    def check_entry(self, model: LNode, e: LNode, where: str, in_composite: bool) -> None:
        handled = set()
        for c in e.cons:
            handled.add(c.get("id"))
            if self.dropped_constraint(c):
                continue
            scope, kind = c.get("scope"), c.get("type")
            mods = [m for m in e.mods if m.get("field") == c.get("id")]
            if c.get("field") != "selections":
                self.fail(where, f"constraint {c}")
            elif kind == "min":
                if not (scope == "parent" or float(c["value"]) == 0):
                    self.fail(where, f"constraint {c}")   # min 1 in scope parent makes a fixed weapon or part
            elif kind != "max":
                self.fail(where, f"constraint {c}")
            elif self.other_datasheet(scope):
                pass                                       # D-079: scoped to another datasheet: dropped
            elif scope == "parent" and in_composite:
                pass                                       # per composite: it holds each part once
            elif scope in ("parent", "model", model.id):
                self.per_model_max(model, e, c, mods, where)
            elif self.unit_scope(scope):
                self.unit_max(e, c, mods, where)
            else:
                self.fail(where, f"constraint {c}")
        for m in e.mods:
            if m.get("field") not in handled and not self.dropped_modifier(m):
                self.fail(where, f"modifier {m}")
        self.check_modgroups(e, where)

    def per_model_max(self, model: LNode, e: LNode, c: dict, mods: list[dict], where: str) -> None:
        value = int(float(c["value"]))
        shared = c.get("shared", True)
        rec = {"key": e.id if shared else e.link_id, "shared": shared, "weapon": e.name, "id": e.id,
               "max": value, "excludes": None, "where": where}
        for m in mods:
            conds = flat_conditions(m)
            if m.get("type") == "set" and float(m.get("value", 0)) == -1 and not conds and not m.get("repeats"):
                return                                     # D-079: an unconditional set -1 removes it
            if (m.get("type") == "set" and float(m.get("value", 1)) == 0 and value == 1 and len(conds) == 1
                    and not m.get("conditionGroups") and not m.get("repeats") and rec["excludes"] is None
                    and conds[0].get("type") == "atLeast" and float(conds[0].get("value", 0)) == 1
                    and conds[0].get("scope") == "model" and conds[0].get("field") == "selections"):
                rec["excludes"] = conds[0].get("childId")  # D-079: a mutual exclusion, read with its mirror
            elif not self.dropped_modifier(m):
                self.fail(where, f"modifier {m} on a per-model maximum")
        if not e.weapon():
            self.fail(where, "a per-model maximum on an entry that is not a weapon")
        else:
            self.per_model.setdefault(model.id, []).append(rec)

    def unit_max(self, e: LNode, c: dict, mods: list[dict], where: str) -> None:
        value = int(float(c["value"]))
        if e.weapon():
            if self.carriers.get(e.name, set()) - {e.id}:
                self.fail(where, f"'{e.name}' comes from several BSData entries; it can't be counted by name")
                return
            limit = {"weapons": [e.name], "max": value}
        else:
            limit = {"option": e.name, "max": value}
        live = [m for m in mods if not self.dropped_modifier(m)]
        if len(live) > 1:
            self.fail(where, "several modifiers on a unit maximum")
            return
        for m in live:
            conds = m.get("conditions", [])
            reps = m.get("repeats", [])
            c0 = conds[0] if len(conds) == 1 else {}
            if (m.get("type") == "set" and len(conds) == 1 and not m.get("conditionGroups") and not reps
                    and c0.get("childId") == "model" and c0.get("field") == "selections"
                    and self.unit_scope(c0.get("scope")) and c0.get("type") in ("lessThan", "atLeast")):
                t, new = int(float(c0["value"])), int(float(m["value"]))
                if c0["type"] == "lessThan":
                    limit["below_models"] = {"models": t, "max": new}
                else:
                    limit["max"], limit["below_models"] = new, {"models": t, "max": value}
            elif (m.get("type") == "increment" and not conds and not m.get("conditionGroups") and len(reps) == 1
                    and reps[0].get("childId") == "model" and reps[0].get("field") == "selections"
                    and self.unit_scope(reps[0].get("scope")) and not reps[0].get("roundUp")):
                limit["per_models"] = {"models": int(float(reps[0]["value"])),
                                       "add": int(float(m["value"])) * int(reps[0].get("repeats", 1))}
            else:
                self.fail(where, f"modifier {m} on a unit maximum")
                return
        self.add_unit_limit(limit)

    def model_error_group(self, m: dict, model: LNode, own_max: int) -> bool:
        """D-079: 'add error' when fewer than T models and more than K of this model type in the unit:
        at most K below T models; from T models the model type's own (02.06) max."""
        if m.get("field") != "error" or m.get("type") != "add" or m.get("conditions") or m.get("repeats"):
            return False
        groups = m.get("conditionGroups", [])
        if len(groups) != 1 or groups[0].get("type") != "and" or groups[0].get("conditionGroups") \
                or groups[0].get("localConditionGroups"):
            return False
        conds = groups[0].get("conditions", [])
        lt = [c for c in conds if c.get("type") == "lessThan" and c.get("childId") == "model"
              and self.unit_scope(c.get("scope")) and c.get("field") == "selections"]
        gt = [c for c in conds if c.get("type") == "greaterThan" and c.get("childId") == model.id
              and self.unit_scope(c.get("scope")) and c.get("field") == "selections"]
        if len(conds) != 2 or len(lt) != 1 or len(gt) != 1:
            return False
        self.add_unit_limit({"model_type": model.name, "max": own_max,
                             "below_models": {"models": int(float(lt[0]["value"])), "max": int(float(gt[0]["value"]))}})
        return True

    def add_unit_limit(self, limit: dict) -> None:
        order = ("weapons", "option", "model_type", "max", "below_models", "per_models")
        limit = {k: limit[k] for k in order if k in limit}
        if limit not in self.unit_limits:     # read once although BSData repeats it on several links
            self.unit_limits.append(limit)

    def model_limits(self, model: LNode, places: list) -> list[dict]:
        """D-079: per-model maxima, written only where they bind (the fixed weapons and slots could give the
        model more than the maximum); a mutual exclusion and its mirror as one limit over both weapons."""
        recs = self.per_model.get(model.id, [])

        def occurrences(match) -> int:
            return sum(max(sum(1 for x in ents if match(x)) for ents in place) for place in places)

        names = {r["id"]: r["weapon"] for r in recs}
        limits = []
        for r in recs:
            if r["excludes"] is None:
                by_key = occurrences(lambda x: (x.id if r["shared"] else x.link_id) == r["key"])
                if by_key <= r["max"]:
                    continue                                   # implied by the slots
                if not r["shared"] or occurrences(lambda x: x.name == r["weapon"]) != by_key:
                    self.fail(r["where"], f"a binding maximum on '{r['weapon']}' that can't be counted by name")
                    continue
                limits.append({"weapons": [r["weapon"]], "max": r["max"]})
            else:
                other = r["excludes"]
                if other not in names or not any(x["excludes"] == r["id"] and x["id"] == other for x in recs):
                    self.fail(r["where"], f"a one-sided exclusion with {other}")
                    continue
                pair = [r["weapon"], names[other]]
                if any(set(l["weapons"]) == set(pair) for l in limits):
                    continue
                if occurrences(lambda x: x.name in pair) > 1:
                    limits.append({"weapons": pair, "max": 1})
        out = []
        for l in limits:   # leave out a limit implied by another (a subset of its weapons, max no smaller)
            if l not in out and not any(set(l["weapons"]) < set(o["weapons"]) and o["max"] <= l["max"]
                                        for o in limits):
                out.append(l)
        return out


def read_list_building(idx: Index, unit: dict, comp_min, comp_max, model_types: list[dict]) -> dict | None:
    """D-079: the datasheet's list_building block, or None (with warnings) when anything in scope is
    unrecognised."""
    if unit.get("type") == "model":
        nodes = [LNode(idx, unit)]
    else:
        nodes = []
        find_model_lnodes(idx, unit, nodes)
    reader = ListReader(idx, unit, nodes, model_types)
    lb = None
    if comp_min is None or comp_max is None or [n.name for n in nodes] != [m["name"] for m in model_types]:
        reader.fail(unit.get("name", "?"), "the model types or the composition were not read")
    else:
        lb = reader.read(comp_min, comp_max)
    if lb is None:
        for u in reader.unread:
            print(f"warning: D-079: {u}", file=sys.stderr)
        print(f"warning: D-079: '{unit.get('name')}': no list_building written (unrecognised data above)",
              file=sys.stderr)
        return None
    for w in lb["wargear"]:
        for s in w["slots"]:
            for o in s["options"]:
                del o["_entries"]
                if o["points"] == 0:
                    del o["points"]   # D-079: option points only when not 0
    return lb


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("catalogue")
    ap.add_argument("unit")
    ap.add_argument("out", type=Path)
    ap.add_argument("--ref", help="git commit sha of BSData/wh40k-11e (default: latest main)")
    ap.add_argument("--fetched", type=dt.date.fromisoformat,
                    help="date to record as source.fetched, e.g. the file's existing one (default: today)")
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
            "fetched": (a.fetched or dt.date.today()).isoformat(),
            "note": "Generated by tools/fetch_bsdata.py - do not hand-edit; re-run the tool instead.",
        },
        **extract(a.unit, cats, idx),
    }
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {a.out} ({len(data['model_types'])} model types, {len(data['weapons'])} weapons)")


if __name__ == "__main__":
    main()
