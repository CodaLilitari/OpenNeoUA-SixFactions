"""Voice pack files agree with docs/faction-audio-manifest.json and the engine's routing table.

Run: python tests/test_faction_audio_manifest.py -v
"""
from pathlib import Path
import hashlib
import json
import re
import sys
import unittest
import wave

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
PACKS = ROOT / "Sounds" / "Speech" / "Voicepacks"
MANIFEST = json.loads((ROOT / "docs" / "faction-audio-manifest.json").read_text(encoding="utf-8"))
EVENTS = ["unit_ready", "unit_idle", "order_ack_friendly", "order_ack_enemy", "unit_control_entered",
          "enemy_sector_entered", "unit_recovered", "enemy_unit_spotted", "enemy_host_found",
          "request_support", "unit_retreating", "unit_lost", "enemy_unit_engaged",
          "enemy_unit_destroyed", "enemy_host_destroyed", "powerstation_captured"]


class FactionAudioManifest(unittest.TestCase):
    def test_every_cue_matches_its_file(self):
        for cue in MANIFEST["cues"]:
            path = PACKS / cue["faction"] / cue["file"]
            with self.subTest(file=f"{cue['faction']}/{cue['file']}"):
                data = path.read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), cue["sha256"])
                self.assertEqual(len(data), cue["bytes"])
                with wave.open(str(path)) as w:
                    self.assertEqual((w.getnchannels(), w.getsampwidth(), w.getframerate()), (1, 2, 22050))
                    self.assertAlmostEqual(w.getnframes() / w.getframerate(), cue["duration_seconds"], places=5)

    def test_each_pack_covers_every_event_slot_once(self):
        slots = {}
        for row in MANIFEST["cues"] + MANIFEST.get("pending", []):
            key = (row["faction"], row["event"], row["variant"])
            self.assertNotIn(key, slots, key)
            self.assertEqual(row["file"], f"{row['event']}_{row['variant']}.wav")
            slots[key] = row
        factions = {f for f, _, _ in slots}
        self.assertEqual(factions, {"black-sect-mp", "sulgogars-mp", "ghorkov-mp", "taerkasten-mp"})
        for faction in factions:
            want = {(faction, e, v) for e in EVENTS for v in (1, 2)}
            self.assertEqual({k for k in slots if k[0] == faction}, want, faction)

    def test_pack_directories_hold_only_manifest_files(self):
        listed = {(c["faction"], c["file"]) for c in MANIFEST["cues"]}
        for pack in {c["faction"] for c in MANIFEST["cues"]}:
            on_disk = {(pack, p.name) for p in (PACKS / pack).iterdir() if p.is_file()}
            self.assertEqual(on_disk, {x for x in listed if x[0] == pack}, pack)

    def test_pending_slots_have_no_file(self):
        # A missing pack file falls back to the original vanilla voice.
        for row in MANIFEST.get("pending", []):
            self.assertFalse((PACKS / row["faction"] / row["file"]).exists(), row["file"])

    def test_pack_filenames_use_the_engine_event_keys(self):
        # The engine finds pack files by these keys; a renamed key would silently
        # send every pack back to the vanilla voice for that event.
        source = (ROOT / "src" / "world" / "speech.cpp").read_text(encoding="utf-8")
        table = source[source.index("Events[EventCount]"):]
        table = table[:table.index("};")]
        keys = [k for _, k in re.findall(r'\{(\d+), "([a-z_]+)"\}', table)]
        self.assertEqual(keys, EVENTS)

    def test_routing_table_names_every_pack(self):
        source = (ROOT / "src" / "world" / "speech.cpp").read_text(encoding="utf-8")
        routes = dict(re.findall(r'case (\d+): return "sounds/speech/voicepacks/([a-z-]+)";', source))
        self.assertEqual(routes, {"2": "sulgogars-mp", "4": "taerkasten-mp", "5": "black-sect-mp", "6": "ghorkov-mp"})
        packs = {c["faction"] for c in MANIFEST["cues"]}
        self.assertEqual(set(routes.values()), packs)


if __name__ == "__main__":
    unittest.main()
