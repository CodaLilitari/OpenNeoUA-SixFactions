#!/usr/bin/env python3
"""Generate a separate six-start MP variant from a locally owned Urban Assault LDF.

Original map bytes are never changed or shipped with this tool. Terrain and
original four station placements are preserved, apart from opening a compact
map's east border when extending it. Two new station pads copy the original
Resistance start's local terrain. Initial territory is repartitioned by nearest
station. Symmetric compact arenas retain their original nominal power per
faction through explicit additional plants. These are scenario choices, not
a claim of competitive balance or measured energy income.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re

OWNERS = (1, 6, 3, 4, 5, 2)
BLACK_UNITS = (1, 2, 3, 4, 5, 6, 7, 9, 10, 12, 14, 15)
SULG_UNITS = (71, 72, 73, 74)


def field(block, key, default=None):
    m = re.search(r"(?mi)^\s*" + re.escape(key) + r"\s*=\s*([^;\r\n]+)", block)
    return m[1].strip() if m else default


def scopes(text, kind):
    return list(re.finditer(r"(?msi)^[ \t]*" + re.escape(kind) +
                           r"\b[^\r\n]*\r?\n.*?^[ \t]*end\b[^\r\n]*", text))


def read_maps(text):
    result, spans = {}, {}
    for key in ("typ_map", "own_map", "hgt_map", "blg_map"):
        matches = list(re.finditer(r"(?mi)^[ \t]*" + key +
                                  r"\s*=\s*\n[ \t]*(\d+)[ \t]+(\d+)[ \t]*\r?\n", text))
        if len(matches) != 1:
            raise ValueError(f"Need one inline {key}")
        m = matches[0]
        width, height = map(int, m.groups())
        if not (4 <= width <= 255 and 4 <= height <= 255):
            raise ValueError("Map dimensions must be 4..255")
        rows = text[m.end():].splitlines(keepends=True)[:height]
        grid = [[int(x, 16) for x in row.split(";", 1)[0].split()] for row in rows]
        if len(grid) != height or any(len(row) != width for row in grid):
            raise ValueError(f"Malformed {key}")
        if any(not 0 <= v <= 255 for row in grid for v in row):
            raise ValueError("Map byte outside 0..255")
        result[key] = grid
        spans[key] = (m.start(), m.end() + sum(map(len, rows)))
    dimensions = {(len(g[0]), len(g)) for g in result.values()}
    if len(dimensions) != 1:
        raise ValueError("Map plane dimensions disagree")
    return result, spans


def read_stations(text):
    stations = {}
    for match in scopes(text, "begin_robo"):
        block = match[0]
        owner = int(field(block, "owner", "0"))
        if owner in stations:
            raise ValueError("Duplicate station owner")
        sx, sy = field(block, "sec_x"), field(block, "sec_y")
        x = int(sx) if sx is not None else int(float(field(block, "pos_x")) / 1200)
        y = int(sy) if sy is not None else int(-float(field(block, "pos_z")) / 1200)
        stations[owner] = {"x": x, "y": y, "vehicle": int(field(block, "vehicle", "0")), "block": block}
    return stations


def verify_prototypes(script_root):
    paths = {p.name.lower(): p for p in script_root.iterdir() if p.is_file()}
    required = {"robos.scr": (61, 62), "feinde.scr": SULG_UNITS,
                "flaks.scr": (79,), "weap_f.scr": (72,), "net_ypa.scr": (),
                "net_bldg.scr": ()}
    hashes = {}
    for name, ids in required.items():
        if name not in paths:
            raise ValueError(f"Missing original script {name}")
        data = paths[name].read_bytes()
        hashes[name] = hashlib.sha256(data).hexdigest()
        kind = "new_weapon" if name == "weap_f.scr" else "new_vehicle"
        blocks = {int(re.match(r"\s*" + kind + r"\s+(\d+)", m[0], re.I)[1]): m[0]
                  for m in scopes(data.decode("latin-1"), kind)}
        for ident in ids:
            if ident not in blocks:
                raise ValueError(f"Missing vehicle {ident} in {name}")
            if name == "robos.scr" and field(blocks[ident], "model", "").lower() != "robo":
                raise ValueError(f"Vehicle {ident} is not a host station")
    return hashes


def read_power_buildings(text):
    result = {}
    for match in scopes(text, "new_building"):
        block = match[0]
        if field(block, "model", "").lower() == "kraftwerk":
            ident = int(re.match(r"\s*new_building\s+(\d+)", block, re.I)[1])
            power = int(field(block, "power", "0"))
            sector = int(field(block, "sec_type", "-1"))
            if power <= 0 or not 0 <= ident <= 255 or not 0 <= sector <= 255:
                raise ValueError("Invalid power-building prototype")
            result[ident] = {"power": power, "sector": sector}
    if not result:
        raise ValueError("No power-building prototypes found")
    return result


def nominal_power(grids, buildings):
    totals = {o: 0 for o in OWNERS}
    for y, row in enumerate(grids["blg_map"]):
        for x, building in enumerate(row):
            owner = grids["own_map"][y][x]
            if owner in totals and building in buildings:
                totals[owner] += buildings[building]["power"]
    return totals


def preserve_compact_power(original, grids, stations, buildings, regular):
    """Preserve a symmetric arena's per-faction power after territory changes.

    Only empty, owned cells at the host's terrain height are eligible. Existing
    structures and every host's 3x3 landing pad remain intact. Sulgogars acquire
    a preplaced captured plant, not permission to construct human buildings.
    """
    if any(b["power"] <= 0 or not 0 <= b["sector"] <= 255 for b in buildings.values()):
        raise ValueError("Invalid power-building prototype")
    before = nominal_power(original, buildings)
    after_split = nominal_power(grids, buildings)
    legacy = [before[o] for o in (1, 6, 3, 4)]
    target = legacy[0] if regular and len(set(legacy)) == 1 and legacy[0] > 0 else None
    added = []
    if target is not None:
        if any(value > target for value in after_split.values()):
            raise ValueError("Territory split exceeds symmetric power target; review map manually")
        choices = {1: (54, 1), 6: (55, 12), 3: (56, 10), 4: (57, 17),
                   5: (54, 1), 2: (54, 1)}
        for owner in OWNERS:
            remaining = target - after_split[owner]
            host = stations[owner]
            sx, sy = host["x"], host["y"]
            while remaining:
                options = [b for b in choices[owner] if b in buildings and
                           buildings[b]["power"] <= remaining]
                if not options:
                    raise ValueError("Cannot preserve arena power with verified prototypes")
                building = max(options, key=lambda b: buildings[b]["power"])
                candidates = [(x, y) for y in range(1, len(grids["own_map"]) - 1)
                              for x in range(1, len(grids["own_map"][0]) - 1)
                              if grids["own_map"][y][x] == owner and
                              grids["blg_map"][y][x] == 0 and
                              grids["hgt_map"][y][x] == grids["hgt_map"][sy][sx] and
                              all(max(abs(x-s["x"]), abs(y-s["y"])) > 1
                                  for s in stations.values())]
                if not candidates:
                    raise ValueError("No clear owned site for compensating power plant")
                def site_rank(cell):
                    x, y = cell
                    separation = min((x-s["x"])**2 + (y-s["y"])**2
                                     for o, s in stations.items() if o != owner)
                    return (abs(math.hypot(x-sx, y-sy) - 3), -separation, y, x)
                x, y = min(candidates, key=site_rank)
                grids["blg_map"][y][x] = building
                grids["typ_map"][y][x] = buildings[building]["sector"]
                added.append({"owner": owner, "x": x, "y": y, "building": building,
                              "power": buildings[building]["power"]})
                remaining -= buildings[building]["power"]
    final = nominal_power(grids, buildings)
    if target is not None and any(value != target for value in final.values()):
        raise ValueError("Failed to preserve six-way nominal power")
    return {"original_by_owner": before, "after_territory_split": after_split,
            "symmetric_target_per_faction": target, "added_plants": added,
            "output_by_owner": final,
            "limitation": "Nominal intact plant values; runtime income, reachability and combat balance unverified"}


def generate(text, power_buildings=None):
    # The retail templates use one known multiplayer adjustment include. Unknown
    # scripts could alter owners or geometry and must not be silently transformed.
    includes = re.findall(r"(?mi)^\s*include\s+([^;\r\n]+)", text)
    if any(p.strip().lower().replace("\\", "/") != "data:scripts/net_ypa.scr" for p in includes):
        raise ValueError("Unrecognized include; review this map manually")
    maps, spans = read_maps(text)
    stations = read_stations(text)
    if set(stations) != {1, 3, 4, 6}:
        raise ValueError("Source must have exactly the original four station owners")
    old_width, height = len(maps["own_map"][0]), len(maps["own_map"])
    if any(v not in {0, 1, 3, 4, 6} for r in maps["own_map"] for v in r):
        raise ValueError("Source already has extra or unsupported territorial owners")
    if any(not (1 <= s["x"] < old_width - 1 and 1 <= s["y"] < height - 1) for s in stations.values()):
        raise ValueError("Original station is outside the playable interior")

    original_maps = {key: [row[:] for row in grid] for key, grid in maps.items()}
    donor = stations[1]
    dx, dy = donor["x"], donor["y"]
    if not (2 <= dx < old_width - 2 and 2 <= dy < height - 2):
        raise ValueError("Need a complete 3x3 original station pad")
    # Extend a regular 2x2 compact arena to 3x2 with the same spacing. Farthest-point
    # placement there would give the new factions a much more remote start.
    xs = sorted({s["x"] for s in stations.values()})
    ys = sorted({s["y"] for s in stations.values()})
    regular = (old_width < 24 and len(xs) == len(ys) == 2 and
               {(s["x"], s["y"]) for s in stations.values()} ==
               {(x, y) for x in xs for y in ys})
    extension = xs[1] - xs[0] if regular else (14 if old_width < 24 else 0)
    width = old_width + extension
    if extension:
        for key, grid in maps.items():
            filler = 0
            if key in {"typ_map", "hgt_map"}:
                clear = [original_maps[key][y][x] for y in range(1, height - 1)
                         for x in range(1, old_width - 1) if original_maps["blg_map"][y][x] == 0]
                filler = Counter(clear).most_common(1)[0][0]
            for y, row in enumerate(grid):
                border = row[-1]
                row.extend([filler] * extension)
                if y in (0, height - 1):
                    row[old_width - 1:] = [border] * (extension + 1)
                else:
                    row[old_width - 1:] = [filler] * extension + [border]

    starts = [(s["x"], s["y"]) for s in stations.values()]
    added = {}
    for owner in (5, 2):
        if regular:
            x, y = xs[1] + extension, ys[0 if owner == 5 else 1]
        else:
            candidates = [(x, y) for y in range(3, height - 3) for x in range(3, width - 3)]
            # Deterministic farthest-point placement, with enough clearance for a pad.
            x, y = max(candidates, key=lambda p: (min((p[0]-a)**2 + (p[1]-b)**2 for a, b in starts), -p[1], -p[0]))
        distance = min(math.hypot(x-a, y-b) for a, b in starts)
        minimum = min(6, xs[1]-xs[0], ys[1]-ys[0]) if regular else 6
        if minimum < 3 or distance < minimum or not (2 <= x < width-2 and 2 <= y < height-2):
            raise ValueError("Map has no separated sixth start; use a larger source")
        added[owner] = {"x": x, "y": y, "vehicle": 62 if owner == 5 else 61}
        starts.append((x, y))
        # Copy only the local start pad, preserving the original asset IDs.
        for key in ("typ_map", "hgt_map", "blg_map"):
            for oy in (-1, 0, 1):
                for ox in (-1, 0, 1):
                    maps[key][y+oy][x+ox] = original_maps[key][dy+oy][dx+ox]

    all_stations = {**stations, **added}
    for y in range(1, height - 1):
        for x in range(1, width - 1):
            maps["own_map"][y][x] = min(OWNERS, key=lambda o: ((x-all_stations[o]["x"])**2 +
                                                                (y-all_stations[o]["y"])**2, OWNERS.index(o)))
    # Keep a neutral boundary even if a source mod used nonzero border ownership.
    for x in range(width):
        maps["own_map"][0][x] = maps["own_map"][-1][x] = 0
    for row in maps["own_map"]:
        row[0] = row[-1] = 0

    power_receipt = (preserve_compact_power(original_maps, maps, all_stations,
                                          power_buildings, regular)
                     if power_buildings is not None else None)

    replacements = []
    for key, (a, b) in spans.items():
        body = f"    {key} =\n        {width} {height}\n"
        body += "".join("        " + " ".join(f"{v:02x}" for v in row) + "\n" for row in maps[key])
        replacements.append((a, b, body))
    for a, b, body in sorted(replacements, reverse=True):
        text = text[:a] + body + text[b:]
    text = re.sub(r"(?mi)^([ \t]*title_\w+[ \t]*=[ \t]*)([^;\r\n]+)",
                  lambda m: m[1] + m[2].strip() + " [Six Factions]", text)
    # Clear a latent owner-5 build list before writing the explicit new scenario roster.
    for match in reversed(scopes(text, "begin_enable")):
        if re.match(r"\s*begin_enable\s+[25]\b", match[0], re.I):
            text = text[:match.start()] + text[match.end():]

    energy = int(field(stations[1]["block"], "energy", "1280000"))
    reload_const = int(field(stations[1]["block"], "reload_const", "183125"))
    additions = "\n; Six-faction multiplayer additions by Coda and Andrei.\n"
    for owner, s in added.items():
        additions += (f"begin_robo\n    owner = {owner}\n    vehicle = {s['vehicle']}\n"
                      f"    sec_x = {s['x']}\n    sec_y = {s['y']}\n    pos_y = -330\n"
                      f"    energy = {energy}\n    reload_const = {reload_const}\n    viewangle = 0\nend\n")
    additions += "\n; Explicit Queen camera for human control; original SP prototypes stay unchanged.\n"
    additions += ("modify_vehicle 61\n    robo_viewer_x = 0\n    robo_viewer_y = 0\n"
                  "    robo_viewer_z = 140\n    robo_viewer_max_up = 0.6\n"
                  "    robo_viewer_max_down = 1.2\n    robo_viewer_max_side = 3.5\nend\n")
    additions += ("\n; Match authored AI cadence instead of inheriting the 1000ms player default.\n"
                  "modify_weapon 72\n    shot_time_user = 5000\nend\n")
    additions += "\nbegin_enable 5\n" + "".join(f"    vehicle = {v}\n" for v in BLACK_UNITS)
    # Black Sect salvages the source Resistance construction list; Sulgogars do not.
    resist = next(m[0] for m in scopes(text, "begin_enable") if re.match(r"\s*begin_enable\s+1\b", m[0], re.I))
    black_buildings = [int(v) for v in re.findall(r"(?mi)^\s*building\s*=\s*(\d+)", resist)]
    additions += "".join(f"    building = {v}\n" for v in black_buildings) + "end\n"
    additions += "\nbegin_enable 2\n" + "".join(f"    vehicle = {v}\n" for v in SULG_UNITS) + "end\n"
    position = re.search(r"(?mi)^\s*begin_maps\b", text).start()
    text = text[:position] + additions + "\n" + text[position:]
    # Preserve original four tech rewards; new scenario owners have no implicit human reward.
    def tech(match):
        entries = re.split(r"[_\s]+", match[2].strip())
        if len(entries) != 4:
            raise ValueError("Unexpected technology arity")
        return match[1] + "_".join(entries + [entries[0], "0"])
    text = re.sub(r"(?mi)^([ \t]*nw_[vb]proto_num\s*=\s*)([^;\r\n]+)", tech, text)
    counts = Counter(v for row in maps["own_map"] for v in row)
    receipt = {"source_dimensions": [old_width, height], "output_dimensions": [width, height],
               "east_extension_columns": extension,
               "placement": "third column with original arena spacing" if regular else "farthest separated points",
               "starts": {str(o): {k: s[k] for k in ("x", "y", "vehicle")} for o, s in all_stations.items()},
               "owned_cells": dict(counts), "black_units": list(BLACK_UNITS),
               "black_buildings": black_buildings, "sulgogar_units": list(SULG_UNITS),
               "sulgogar_buildings": [], "queen_camera": [0, 0, 140],
               "sulgogar_weapon_72_player_cadence_ms": 5000,
               "power_audit": power_receipt,
               "status": "generated and structurally checked; runtime acceptance pending"}
    validate(text)
    return text, receipt


def validate(text):
    maps, _ = read_maps(text)
    stations = read_stations(text)
    if set(stations) != set(OWNERS):
        raise ValueError("Output does not contain exactly six distinct faction stations")
    coords = [(s["x"], s["y"]) for s in stations.values()]
    if len(set(coords)) != 6:
        raise ValueError("Overlapping station cells")
    for owner, station in stations.items():
        if maps["own_map"][station["y"]][station["x"]] != owner:
            raise ValueError("Station starts in another faction's territory")
    enabled = {int(re.match(r"\s*begin_enable\s+(\d+)", m[0], re.I)[1]): m[0]
               for m in scopes(text, "begin_enable")}
    if not set(OWNERS) <= set(enabled):
        raise ValueError("Missing faction build permissions")
    if re.search(r"(?mi)^\s*building\s*=", enabled[2]):
        raise ValueError("Sulgogars unexpectedly inherited human construction")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="New package directory; levels and receipts go in separate subfolders")
    parser.add_argument("--script-root", required=True, type=Path)
    parser.add_argument("--level-id", required=True, type=int)
    args = parser.parse_args()
    if not 1 <= args.level_id <= 255:
        parser.error("Level ID must be 1..255 and unused in the installation")
    target = args.output / "Levels" / "Multi" / f"l{args.level_id:03d}{args.level_id:03d}.ldf"
    receipt_path = args.output / "receipts" / f"level-{args.level_id}.json"
    if target.exists() or receipt_path.exists() or target.resolve() == args.source.resolve():
        parser.error("Refusing to replace an existing file")
    try:
        scripts = verify_prototypes(args.script_root)
        script_paths = {p.name.lower(): p for p in args.script_root.iterdir() if p.is_file()}
        power_buildings = read_power_buildings(script_paths["net_bldg.scr"].read_text(encoding="latin-1"))
        raw = args.source.read_bytes()
        result, receipt = generate(raw.decode("latin-1").replace("\r\n", "\n"), power_buildings)
    except (ValueError, IndexError, StopIteration) as exc:
        parser.error(str(exc))
    data = result.encode("latin-1")
    receipt.update({"source": str(args.source.resolve()), "source_sha256": hashlib.sha256(raw).hexdigest(),
                    "output": str(target.resolve()), "output_sha256": hashlib.sha256(data).hexdigest(),
                    "script_sha256": scripts, "level_id": args.level_id})
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("xb") as out:
        out.write(data)
    receipts = args.output / "receipts"
    receipts.mkdir(parents=True, exist_ok=True)
    with receipt_path.open("x", encoding="utf-8") as out:
        out.write(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
