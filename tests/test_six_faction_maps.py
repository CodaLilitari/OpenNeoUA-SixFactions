"""Map transformation contracts using original synthetic fixtures, no game data."""
import importlib.util
from pathlib import Path
import re
import sys
import unittest

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location(
    "maps", Path(__file__).resolve().parents[1] / "tools" / "make_six_faction_map.py")
maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maps)


def fixture(width=16, height=16):
    result = "; Synthetic regression fixture, not an original game map.\n"
    result += "begin_level\n title_default = Test\nend\ninclude data:scripts/net_ypa.scr\n"
    for owner, vehicle, x, y in ((1, 56, 5, 5), (3, 58, 10, 10),
                                  (4, 60, 10, 5), (6, 57, 5, 10)):
        result += (f"begin_robo\n owner = {owner}\n vehicle = {vehicle}\n"
                   f" sec_x = {x}\n sec_y = {y}\n energy = 950000\n reload_const = 162500\nend\n")
        result += f"begin_enable {owner}\n vehicle = {owner}\n building = 1\nend\n"
    result += "begin_gem\n nw_vproto_num = 10_20_30_40\n nw_bproto_num = 1_2_3_4\nend\n"
    result += "begin_maps\n"
    for name, value in (("typ_map", 1), ("own_map", 1), ("hgt_map", 7), ("blg_map", 0)):
        result += f" {name} =\n {width} {height}\n"
        result += (" ".join([f"{value:02x}"] * width) + "\n") * height
    return result + "end\n"


def powered_fixture(uniform_building=None):
    source = fixture()
    planes, spans = maps.read_maps(source)
    for y, row in enumerate(planes["own_map"]):
        for x in range(len(row)):
            row[x] = (1 if x < 8 else 4) if y < 8 else (6 if x < 8 else 3)
    for x, y, building in ((2, 2, 54), (13, 2, 57), (2, 13, 55), (13, 13, 56),
                           (7, 7, 1), (8, 7, 17), (7, 8, 12), (8, 8, 10)):
        planes["blg_map"][y][x] = building if uniform_building is None else uniform_building
    for key, (a, b) in sorted(spans.items(), key=lambda item: item[1][0], reverse=True):
        body = f" {key} =\n 16 16\n"
        body += "".join(" ".join(f"{v:02x}" for v in row) + "\n" for row in planes[key])
        source = source[:a] + body + source[b:]
    return source


POWER_BUILDINGS = {ident: {"power": power, "sector": sector}
                  for ident, power, sector in ((1, 64, 201), (54, 128, 201),
                                               (12, 64, 15), (55, 128, 15),
                                               (10, 64, 229), (56, 128, 229),
                                               (17, 64, 14), (57, 128, 14))}


class SixFactionMapTests(unittest.TestCase):
    def test_small_map_gains_two_separate_starts(self):
        source = fixture()
        generated, receipt = maps.generate(source)
        self.assertEqual(receipt["output_dimensions"], [21, 16])
        stations = maps.read_stations(generated)
        self.assertEqual(set(stations), {1, 2, 3, 4, 5, 6})
        self.assertEqual(len({(s["x"], s["y"]) for s in stations.values()}), 6)
        self.assertEqual(stations[5]["vehicle"], 62)
        self.assertEqual(stations[2]["vehicle"], 61)
        for owner, old in maps.read_stations(source).items():
            self.assertEqual(stations[owner], old)
        for owner in (5, 2):
            s = stations[owner]
            for other, other_s in stations.items():
                if owner != other:
                    self.assertGreaterEqual((s["x"]-other_s["x"])**2 +
                                            (s["y"]-other_s["y"])**2, 25)
            self.assertEqual(maps.field(s["block"], "energy"), "950000")

    def test_all_planes_expand_and_border_stays_neutral(self):
        generated, _ = maps.generate(fixture())
        planes, _ = maps.read_maps(generated)
        for grid in planes.values():
            self.assertEqual((len(grid[0]), len(grid)), (21, 16))
        own = planes["own_map"]
        self.assertTrue(all(v == 0 for v in own[0] + own[-1]))
        self.assertTrue(all(row[0] == row[-1] == 0 for row in own))

    def test_large_map_keeps_dimensions(self):
        _, receipt = maps.generate(fixture(54, 32))
        self.assertEqual(receipt["output_dimensions"], [54, 32])
        self.assertEqual(receipt["east_extension_columns"], 0)

    def test_explicit_rosters_technology_and_cadence(self):
        source = fixture().replace("begin_maps", "begin_enable 5\n vehicle = 999\nend\nbegin_maps")
        generated, receipt = maps.generate(source)
        self.assertNotIn("vehicle = 999", generated)
        self.assertEqual(receipt["sulgogar_units"], [71, 72, 73, 74])
        self.assertEqual(receipt["sulgogar_buildings"], [])
        self.assertEqual(receipt["black_buildings"], [1])
        self.assertIn("nw_vproto_num = 10_20_30_40_10_0", generated)
        self.assertIn("nw_bproto_num = 1_2_3_4_1_0", generated)
        self.assertIn("modify_weapon 72\n    shot_time_user = 5000", generated)
        self.assertEqual(len(maps.scopes(generated, "modify_vehicle")), 1)

    def test_rejects_unsupported_or_ambiguous_sources(self):
        invalid = (
            fixture().replace("data:scripts/net_ypa.scr", "data:scripts/unknown.scr"),
            fixture().replace("owner = 6", "owner = 5"),
            fixture().replace("owner = 6", "owner = 1"),
            fixture().replace("own_map =\n 16 16", "own_map =\n 15 16"),
            fixture().replace("10_20_30_40", "10_20_30_40_50"),
        )
        for source in invalid:
            with self.subTest(source=source[:100]), self.assertRaises(ValueError):
                maps.generate(source)

    def test_rejects_double_conversion(self):
        generated, _ = maps.generate(fixture())
        with self.assertRaises(ValueError):
            maps.generate(generated)

    def test_compact_arena_preserves_power_for_all_six_factions(self):
        source = powered_fixture()
        original, _ = maps.read_maps(source)
        generated, receipt = maps.generate(source, POWER_BUILDINGS)
        planes, _ = maps.read_maps(generated)
        self.assertEqual(maps.nominal_power(original, POWER_BUILDINGS),
                         {1: 192, 6: 192, 3: 192, 4: 192, 5: 0, 2: 0})
        self.assertEqual(maps.nominal_power(planes, POWER_BUILDINGS),
                         {owner: 192 for owner in maps.OWNERS})
        audit = receipt["power_audit"]
        self.assertEqual(audit["after_territory_split"][3], 64)
        self.assertEqual(audit["after_territory_split"][4], 64)
        self.assertEqual(len(audit["added_plants"]), 4)
        for plant in audit["added_plants"]:
            x, y = plant["x"], plant["y"]
            self.assertEqual(planes["own_map"][y][x], plant["owner"])
            self.assertEqual(planes["typ_map"][y][x], POWER_BUILDINGS[plant["building"]]["sector"])
            self.assertEqual(planes["blg_map"][y][x], plant["building"])
        for y, row in enumerate(original["blg_map"]):
            for x, building in enumerate(row):
                if building:
                    self.assertEqual(planes["blg_map"][y][x], building)
        self.assertEqual(receipt["sulgogar_buildings"], [])

    def test_power_preservation_rejects_missing_compensation_prototypes(self):
        # The source has two recognized 96-power plants per original owner,
        # but none of the faction-specific compensation prototypes exists.
        with self.assertRaisesRegex(ValueError, "verified prototypes"):
            maps.generate(powered_fixture(99), {99: {"power": 96, "sector": 201}})

    def test_power_parser_excludes_radar_and_rejects_invalid_plants(self):
        text = ("new_building 1\n model = kraftwerk\n power = 64\n sec_type = 201\nend\n"
                "new_building 3\n model = radar\n power = 10\n sec_type = 204\nend\n")
        self.assertEqual(maps.read_power_buildings(text), {1: {"power": 64, "sector": 201}})
        with self.assertRaises(ValueError):
            maps.read_power_buildings(text.replace("power = 64", "power = 0"))


if __name__ == "__main__":
    unittest.main()
