# Faction voice packs: Suno + REAPER candidate packs (version 5)

Retail Urban Assault and Metropolis Dawn give every faction's units the same
vehicle-class voices. This fork gives four factions their own multiplayer unit
voices: Black Sect (owner 5), Sulgogars (owner 2), Ghorkov (owner 6) and
Taerkasten (owner 4). Resistance (1) and Mykonians (3) keep the original
voices. Each pack has two variants for each of the engine's 16 unit events,
under the filenames the faction voice resolver already uses.
`docs/faction-audio-manifest.json` records every file's source take, source
window, gain, loudness, hash and review basis.

**Listening status.** Andrei listened to the version 4 Black Sect and Sulgogar
reels and called them good. He asked for one fix, made here: Black Sect
message 19 now reports a fleeing enemy. Nobody has heard the new Ghorkov and
Taerkasten files or the two new Black Sect lines yet. Speech recognition
verifies the words. A language-ID cue checks the accent. Auditory, accent and
in-game acceptance remain with Andrei.

## What version 5 changes

- **Ghorkov:** a new pack of 32 cues.
- **Taerkasten:** a new pack of 30 of 32 cues. The two `unit_idle` slots are
  pending (see below), and the idle report plays the original voice meanwhile.
- **Black Sect, message 19 (`enemy_unit_engaged`):** "Hostile unit fleeing." and
  "Target retreating." replace "Engaging target." and "Weapons on target.". In
  the engine, message 19 is raised when an enemy squad loses heart and escapes
  (`yparobo_ai.cpp`, `checkCommander`). The locale calls it "Enemy Squad
  Escapes!", and the retail heavy-tank clip says "Enemy tangos retreating."
  The Ghorkov and Taerkasten lines for this event also report the enemy fleeing.

The 30 other Black Sect cues and all 32 Sulgogar cues are unchanged. Re-rendered
in the version 5 REAPER run, the 30 Black Sect cues came out byte-identical to
version 4.

## Ghorkov (owner 6)

Metropolis Dawn's own texts call the faction Kyternesian. In them, "Ghorkov" is
the Resistance's name for it, and its army is the Liberation Forces of
Kyternesia. The base-game scripts already name both Ghorkov host stations
KYTERNESER. The voice is a Liberation Forces officer reporting to his
Commander: proud, disciplined, formal and clipped. He speaks with a natural
Russian accent and standard English grammar. The lines use the faction's own
vocabulary: "Liberation Forces reporting.", "Attacking the misguided.",
"Reactionary sector entered.", "Energy for our people!".

## Taerkasten (owner 4)

The voice is a brother of the Order, reporting to the Commander over an old
field line: grave, devout, unhurried and calm even in loss. He has a light
German accent, a reading of the Order's German machine names (Eisenhans,
Zeppelin, the Festung houses) and of Metropolis Dawn's Luther-Bible German
texts. Register matters more than accent strength here. The lines lean on the
Order's stone and earth worship and its contempt for the godless: "Restored by
the stone.", "The godless approach.", "The earth receives us.", "We claim the
stone.".

**Pending: `unit_idle_1` and `unit_idle_2`.** No downloaded take has a cut of
either line that two of the three models hear exactly. The accent turns "We
await orders." into "We invite orders.", and "still as stone" becomes "still a
storm". The second line has been reordered to "We wait, still as stone.". A
fresh Suno batch with both lines (`taerkasten_r2`) is generated but could not be
downloaded: the account was out of downloads until 2026-11-30. Until then, a
Taerkasten unit's idle report plays the original vanilla voice. The Taerkasten
reel ends with the two near-miss cuts, for listening only.

## How the accented voices were made and checked

Both packs use Suno Speech (beta): male voice, background music off, variety
off. The lines were written and judged per event against the engine's trigger
meanings and the faction lore.

- **Accent.** Suno ignored accent wording in the tone prompt: the English came
  out near-native. A short native-language radio phrase before every English
  line works instead ("Так точно.", "Verstanden.", ...), with an ellipsis
  paragraph after each line. The priming phrases are cut away and never ship.
- **Accent check.** The cue is the probability that Whisper small's language
  ID assigns to the speaker's first language. It was calibrated on the GMU
  Speech Accent Archive with 2-second chunks, as per-speaker medians. Native
  English speakers measure P(Russian) 0.0001–0.0008. Russian second-language
  speakers measure 0.004–0.21, and German second-language speakers measure
  P(German) 0.0012–0.011. It is a cue, not a measure of how strong the accent
  sounds.
  - On the final files, Ghorkov reads a median P(Russian) of 0.006
    (range 0.0003–0.5548); 19 of 32 lines are in the
    second-language range.
  - Taerkasten reads a median P(German) of 0.0022 (range
    0.0004–0.0254); 25 of 30 lines are in the
    second-language range. That fits the light accent the voice brief chose for the Order.
- **Drone bed.** With "cinematic" wording in the tone, some takes carried a
  low drone even with background music off. Those takes were separated with
  Demucs (htdemucs), and their vocal stems are used. A plain "dry voice
  recording, no ambience" tone avoids the bed.
- **Selection.**
  1. Whisper word alignment locates each line, and the cut sits at the silence
     around its vocal island. Where two lines run together, the cut falls at the
     quietest valley between phrases.
  2. An edge audit transcribes and language-identifies each island inside a cut.
     It trims away priming phrases, breaths and clicks, and rejects a cut with a
     priming phrase between the words.
  3. A residue check rejects cuts with sound outside the line's words.
  4. A cut is eligible only if base.en, small.en and medium.en, transcribing it
     in isolation, hear exactly the planned line in at least two of three cases.
  5. Among eligible cuts, cleaner onsets and a stronger accent cue win.
- **Final files.**
  - Ghorkov: 27 of 32 lines are heard exactly by all three models.
    In the other 5, one model mishears part of the line: base.en in
    four ("Liberation Force is reporting", "Reaction is strong, hold found",
    ...) and small.en in one ("Misguided forcing view").
  - Taerkasten: all 30 lines are heard exactly by all three models.
  - The two new Black Sect lines: both are heard exactly by all three models.

Several lines run longer than the plan's duration targets. No word was shortened
to fit, and the Taerkasten delivery is deliberately slow. The longest are
"Brothers, they hold this ground." (4.0 s) and "Welcome among us." (3.3 s).

## Black Sect

Twenty-eight lines come from Suno v6 takes, cut from Demucs (htdemucs) vocal
stems. Twenty-six come from Coda's production plan (batches 01 to 07, two or
three takes each, spoken-word style reused from her Production 01 prompt). The
two message-19 lines come from Black Sect Production 08, made with the same
prompt. Every line was located by Whisper word alignment and cut at the silence
around its vocal island in the stem. Each cut was then transcribed again *in
isolation* by three models (base.en, small.en, medium.en). A take was eligible
only if at least two models heard exactly the planned line. The best eligible
take won on onset cleanliness, stem bleed, music under the voice and duration
fit.

Four lines are Coda's reviewed v02 cuts at their exact windows: "Asset online."
(Black-B-01), "Route confirmed." (Black-A-02), "Reinforcements requested."
(Black-A-03) and "Host station neutralized." The plan named Black-A-04 for the
last one. All three models hear its first word as "How" or "House", but they all
hear Black-B-04 as "Host station neutralized.", so B-04 is used here.

On the final 22050 Hz files, 29 of 32 lines are heard correctly by all
three models and the other 3 by two of three. Some lines run longer than
the plan's duration targets because no word was shortened to fit. In 18 of
the 28 Suno lines, the backing under the voice was within 3 dB of the voice or
louder, including the new "Target retreating." Those lines are the most likely to
carry faint separation artifacts. The manifest gives the measure for each line
(`music_under_voice_db`).

## Sulgogars

Suno ignored the requested call order and the silences between calls: the seven
creature takes are dense streams of drones, click trains and short calls. The
miner therefore treated every take as a source for every event. It measured each
valley-to-valley window for pulse count, spectral-peak and centroid contour,
sustained body, edge depth and quiet surroundings, and matched those against
each event's motif. No window fills two slots. A contact sheet of spectrograms
was then checked visually, and each slot was rated:

- plausible (12): unit_ready_1, unit_idle_2, unit_recovered_1,
  order_ack_enemy_1, order_ack_enemy_2, enemy_sector_entered_1,
  enemy_sector_entered_2, enemy_unit_engaged_1, unit_lost_1,
  enemy_host_destroyed_2, order_ack_friendly_2, enemy_unit_spotted_2
- uncertain (7): enemy_unit_destroyed_2, enemy_host_found_2,
  request_support_2, unit_lost_2, enemy_host_destroyed_1,
  unit_control_entered_2, powerstation_captured_2
- weak (10): unit_ready_2, unit_idle_1, unit_recovered_2, unit_retreating_1,
  unit_retreating_2, enemy_unit_engaged_2, enemy_unit_destroyed_1,
  enemy_host_found_1, request_support_1, enemy_unit_spotted_1
- Coda's v02 salvage fragments, still under evaluation (3):
  order_ack_friendly_1 (Sulgogar-01), unit_control_entered_1 (Sulgogar-02),
  powerstation_captured_1 (Sulgogar-04)

The weak slots are the first to replace. Better sources would be fresh
generations that really leave silence between calls, or the version 3
procedural renderer. To put a version 3 file back for one slot:

```text
git checkout red/review-fixes -- Sounds/Speech/Voicepacks/sulgogars-mp/<file>.wav
```

## Levels and render

Gain follows Coda's v02 rule on the unfaded excerpt: the smaller of
(target − integrated LUFS), (−3.5 dBFS − true peak) and +12 dB. Targets are −18 LUFS,
or −17 LUFS for Sulgogar events whose duration target starts at 0.8 s. Measured on
the final files as dual mono:

- Ghorkov reads −20.9 to −18.0 LUFS (median −18.2),
  with true peak at most −3.7 dBFS. The quietest cue is "Moving out,
  Commander." at −20.9 LUFS. It pauses between its two phrases, and the
  true-peak ceiling limits its gain.
- Taerkasten reads −18.9 to −18.1 LUFS (median −18.2),
  with true peak at most −3.6 dBFS.
- Black lines read −18.9 to −18.0 LUFS, including the two new lines at
  −18.4 and −18.5 LUFS.
- The version 4 Sulgogar figures still hold. Of the 32 files, 23 are within
  1 dB of target, seven are 1.4 to 5.8 dB under, and the last two stay at
  −22.4 and −28.1 LUFS at the +12 dB cap.

None of these numbers show yet how the cues hold up in combat or against the
stock speech bus.

There are two native REAPER 7.79 runs, both through the TwelveTake bridge:

- `v03-full-candidate-2026-11-02` (version 4): Black Sect and Sulgogars.
- `v04-faction-voices-2026-11-02` (this version): Black Sect, Ghorkov and
  Taerkasten, 94 cues. The device shell was unavailable for v04, so the bridge
  mailbox was driven by file transfer. The guards stayed the same: the active
  project was checked as expected and clean before switching, there was one
  operation at a time, and a fresh render file.

Each run had `cue_workflow.prepare` place the cues into one project. REAPER
saved and rendered it. Then `cue_export.finalize` checked the bridge receipt,
the saved timeline and the master's length, and cut exact per-cue windows from
the master. ffmpeg converted each cue to PCM16 mono 22050 Hz. As an independent
check, every v04 runtime file matches a separate rendering of its reviewed item
from source (correlation at least 0.9991, level within 0.06 dB).

## Install and routing

Copy the four faction directories into `Data/Sounds/speech/voicepacks/` of a
test runtime. Keep this README and the manifest outside that directory. In
multiplayer, the engine looks up a unit's sounds in this order:

1. any explicit unit-specific pack;
2. the owner's default pack: `black-sect-mp` (5), `sulgogars-mp` (2),
   `ghorkov-mp` (6) or `taerkasten-mp` (4) (`FactionPackRoot` in
   `src/world/speech.cpp`);
3. the vanilla voice, when the pack has no file for the event.

`tests/test_faction_audio_manifest.py` checks that every pack file matches its
manifest entry (hash, size, PCM16 mono 22050 Hz). It also checks that each pack
covers all 32 event slots once, counting pending slots, and that the routing
table names exactly these four packs.

## Where the working material lives

- REAPER lab runs: `F:\OpenNeoUA-BlackSect-Lab\audio-reaper\Runs\v03-full-candidate-2026-11-02`
  and `...\Runs\v04-faction-voices-2026-11-02` (project, media, master, cue
  masters, receipts). The v04 media copies carry a trailing C2PA provenance
  chunk added in transfer. Their audio data is byte-identical to the hashed
  sources whose names they keep.
- Selection tools, plans, generation receipts and reels: Coda's workspace,
  `work/audio-workflow-v5` and `outputs/audio/v04-candidate-pack`, beside the
  version 4 material.

The audio is Suno output generated under Andrei's account. It is not recovered
original game audio and not a performance by Coda, Lily or Red.
