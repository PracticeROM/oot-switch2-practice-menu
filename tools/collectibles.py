"""Build collectibles.json: every Gold Skulltula, Heart Container and Piece of Heart, the save flag that records it
and where it is.

    python collectibles.py        writes ../data/collectibles.json (needs OOTMENU_NUP and zstandard)

From the map layouts (romfs/Banc/<Area>/<Map>_Static|_Dynamic.bcett.byml.zs; the map name is the save's "map"
string):
  - Gold Skulltulas: the placed Enemy_Skulltula_Gold actors. Each is recorded by the Bool64bitKey flag keyed by the
    actor's instance Hash (FINDINGS "Bool64bitKey").
  - Heart Containers: the placed Obj_Heart_Container actors, the same way.
  - Pieces of Heart: the HeartPieceAnchor actors, which put the pieces on the map screen. One without a
    MapIconHeartPieceAnchorType links to its free-standing piece: that piece's Bool64bitKey flag. Type 1 links to a
    chest: the Bool IsTreasureBoxOpen.<chest Hash>. Type 2 names its Bool (HeartPieceAnchor_GameDataPath).
Every flag is checked against the game's field list (romfs/GameData/GameDataList.*.byml.zs). "area" is the
LocationArea zone a collectible is in where a layout holds several (Hyrule Field's holds Lon Lon Ranch and Lake
Hylia): the zone it links to, else a round zone it is inside horizontally ("area_approximate", FINDINGS §5.22).
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import byml  # noqa: E402
import gamefiles  # noqa: E402

OUT = os.path.join(os.path.dirname(HERE), "data", "collectibles.json")
LAYOUT_SUFFIX = ".bcett.byml.zs"
GAMEDATA_LIST = "romfs/GameData/GameDataList.Product."

GOLD_SKULLTULA = "Enemy_Skulltula_Gold"
HEART_CONTAINER = "Obj_Heart_Container"
HEART_PIECE_ANCHOR = "HeartPieceAnchor"
ANCHOR_CHEST, ANCHOR_FLAG = 1, 2  # MapIconHeartPieceAnchorType; none: a free-standing piece
ROUND_ZONE = 3  # a LocationArea's RigidBodyShapeType that is taken as a vertical cylinder of radius Scale X
EXPECTED = {"gold_skulltulas": 100, "heart_containers": 8, "heart_pieces": 36}

# Anchors whose flag isn't the one the game's own count checks (main+0xC7FBF0 in 1.1.1): anchor flag -> counted.
COUNTED_INSTEAD = {
    "IsTreasureBoxOpen.5534919361313859554": "UnderTheGrave_DampeRace_HeartGeted",  # the race's goal chest
    "KakarikoVillage_IsAfterSave_Npc_Hylia_025": "KakarikoVillage_GatheringTokenGold_GetReward50",
}


def murmur3(text):
    """MurmurHash3_x86_32 with seed 0, the hash GameData keys its fields by (gamedata.c murmur3)."""
    data = text.encode("utf-8")
    c1, c2, mask = 0xCC9E2D51, 0x1B873593, 0xFFFFFFFF

    def scramble(k):
        k = (k * c1) & mask
        k = ((k << 15) | (k >> 17)) & mask
        return (k * c2) & mask

    h = 0
    whole = len(data) // 4 * 4
    for i in range(0, whole, 4):
        h ^= scramble(int.from_bytes(data[i:i + 4], "little"))
        h = ((h << 13) | (h >> 19)) & mask
        h = (h * 5 + 0xE6546B64) & mask
    if len(data) > whole:
        h ^= scramble(int.from_bytes(data[whole:], "little"))
    h ^= len(data)
    h = ((h ^ (h >> 16)) * 0x85EBCA6B) & mask
    h = ((h ^ (h >> 13)) * 0xC2B2AE35) & mask
    return h ^ (h >> 16)


def load_layouts():
    """({instance Hash: [(map, placed actor)]}, {map: [LocationArea actors]}) from every layout."""
    placements, zones = {}, {}
    nup = gamefiles.nup()
    for path in sorted(p for p in nup.index if p.startswith("romfs/Banc/") and p.endswith(LAYOUT_SUFFIX)):
        stem = path.rsplit("/", 1)[1][:-len(LAYOUT_SUFFIX)]
        part = stem.rsplit("_", 1)[-1]
        map_name = stem.rsplit("_", 1)[0] if part in ("Static", "Dynamic") else stem
        try:
            root = byml.load(gamefiles.read(path))
        except Exception as e:  # a file this reader can't handle: report and go on
            print(f"skip {path}: {e}")
            continue
        for placed in root.get("Actors") or []:
            placements.setdefault(placed.get("Hash"), []).append((map_name, placed))
            if placed.get("Gyaml", "").startswith("LocationArea"):
                zones.setdefault(map_name, []).append(placed)
    return placements, zones


def gamedata_fields():
    """(Bool64bitKey keys, Bool hashes) from the game's field list."""
    paths = sorted(p for p in gamefiles.nup().index if p.startswith(GAMEDATA_LIST) and p.endswith(".byml.zs"))
    data = byml.load(gamefiles.read(paths[-1]))["Data"]
    return {e["Hash"] for e in data["Bool64bitKey"]}, {e["Hash"] for e in data["Bool"]}


def in_round_zone(placed, zone):
    """Inside a round zone, horizontally (its height isn't known)."""
    scale = zone.get("Scale")
    if (zone.get("Dynamic") or {}).get("RigidBodyShapeType") != ROUND_ZONE or not isinstance(scale, list):
        return False
    x, _, z = placed["Translate"]
    zone_x, _, zone_z = zone["Translate"]
    return math.hypot(x - zone_x, z - zone_z) <= scale[0]


def actor_name(placements, key):
    """The actor (Gyaml) placed with instance Hash `key`, or ""."""
    places = placements.get(key)
    return places[0][1].get("Gyaml", "") if places else ""


def is_zone(placements, key):
    return actor_name(placements, key).startswith("LocationArea")


def area_of(placed, map_name, placements, zones):
    """(zone name, approximate?) for a placed actor, or (None, False)."""
    for link in placed.get("Links") or []:
        if is_zone(placements, link.get("Dst")):
            return (placements[link["Dst"]][0][1].get("Dynamic") or {}).get("Location"), False
    for zone in zones.get(map_name, []):
        if in_round_zone(placed, zone):
            return zone["Dynamic"]["Location"], True
    return None, False


class Checker:
    """Checks each flag against the game's field list and records the ones it doesn't have."""

    def __init__(self):
        self.keys, self.bools = gamedata_fields()
        self.unknown = []

    def key(self, key):
        if key not in self.keys:
            self.unknown.append(f"Bool64bitKey {key:016x}")
        return f"{key:016x}"

    def flag(self, name):
        if murmur3(name) not in self.bools:
            self.unknown.append(f"Bool {name}")
        return {"flag": name, "hash": f"{murmur3(name):08x}"}


def entry(places, placements, zones, holder=()):
    """What every collectible has: the maps it (or the actor holding it) is placed in, its area and its position
    (from the first of `places`)."""
    maps = sorted({map_name for map_name, _ in list(places) + list(holder)})
    map_name, placed = places[0]
    area, approximate = area_of(placed, map_name, placements, zones)
    out = {"maps": maps, "area": area}
    if approximate:
        out["area_approximate"] = True
    out["t"] = [round(float(v), 2) for v in placed["Translate"]]
    return out


def by_actor(placements, actor):
    """The placements of one actor, by instance Hash."""
    return {key: places for key, places in placements.items() if actor_name(placements, key) == actor}


def gold_skulltulas(placements, zones, check):
    out = []
    for key, places in by_actor(placements, GOLD_SKULLTULA).items():
        dynamic = places[0][1].get("Dynamic") or {}
        out.append({"key": check.key(key), **entry(places, placements, zones),
                    "room": dynamic.get("DefaultDungeonRoomIndex"),
                    "night": bool(dynamic.get("Enemy_Skulltula_Gold_Nocturnal")),
                    "bean_soil": bool(dynamic.get("Enemy_Skulltula_Gold_IsMagicBeanSoil")),
                    "pops_out": bool(dynamic.get("Enemy_Skulltula_Gold_AppearPopOut"))})
    return out


def heart_containers(placements, zones, check):
    return [{"key": check.key(key), **entry(places, placements, zones)}
            for key, places in by_actor(placements, HEART_CONTAINER).items()]


def heart_piece_record(anchor, holder, check):
    """How the save records an anchor's piece: the key of the free-standing piece it links to (`holder`), or the
    Bool the game counts (a chest's, or the one the anchor names)."""
    dynamic = anchor.get("Dynamic") or {}
    kind = dynamic.get("MapIconHeartPieceAnchorType")
    if kind == ANCHOR_FLAG:
        name, kind_name = dynamic["HeartPieceAnchor_GameDataPath"], "event"
    elif kind == ANCHOR_CHEST:
        name, kind_name = f"IsTreasureBoxOpen.{holder}", "chest"
    else:
        return {"kind": "free-standing", "key": check.key(holder)}
    return {"kind": kind_name, **check.flag(COUNTED_INSTEAD.get(name, name))}


def heart_pieces(placements, zones, check):
    out = []
    for places in by_actor(placements, HEART_PIECE_ANCHOR).values():
        anchor = places[0][1]
        linked = [link["Dst"] for link in anchor.get("Links") or [] if not is_zone(placements, link["Dst"])]
        holder = linked[0] if linked else None
        out.append({**heart_piece_record(anchor, holder, check),
                    **entry(places, placements, zones, placements.get(holder, ()))})
    return out


def main():
    placements, zones = load_layouts()
    check = Checker()
    tables = {"gold_skulltulas": gold_skulltulas(placements, zones, check),
              "heart_containers": heart_containers(placements, zones, check),
              "heart_pieces": heart_pieces(placements, zones, check)}
    problems = [f"{len(tables[name])} {name}, expected {count}" for name, count in EXPECTED.items()
                if len(tables[name]) != count]
    problems += [f"not in the field list: {flag}" for flag in check.unknown]
    if problems:
        sys.exit("\n".join(problems))
    for rows in tables.values():
        rows.sort(key=lambda row: (row["maps"], row["t"]))
    with open(OUT, "w", newline="\n") as f:
        json.dump({"source": "game files (tools/collectibles.py)", **tables}, f, indent=1)
        f.write("\n")
    approximate = sum(row.get("area_approximate", False) for rows in tables.values() for row in rows)
    print(", ".join(f"{len(rows)} {name.replace('_', ' ')}" for name, rows in tables.items())
          + f" -> {OUT} ({approximate} areas by position)")


if __name__ == "__main__":
    main()
