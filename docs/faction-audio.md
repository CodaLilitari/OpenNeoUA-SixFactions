# Faction voice packs: Suno + REAPER candidate packs (version 5)

Retail Urban Assault and Metropolis Dawn give every faction's units the same
vehicle-class voices. This fork gives four factions their own multiplayer unit
voices: Black Sect (owner 5), Sulgogars (owner 2), Ghorkov (owner 6) and
Taerkasten (owner 4). Resistance (1) and Mykonians (3) keep the original
voices. Each pack covers the engine's 16 unit events with two variants each,
under the filenames the faction voice resolver already uses. Taerkasten's two
`unit_idle` variants are still pending.
`docs/faction-audio-manifest.json` records every file's source take, source
window, gain, loudness, hash and review basis.

**Listening status.** Andrei listened to the version 4 Black Sect and Sulgogar
reels and called them good. He asked for one fix, made here: Black Sect
message 19 now reports a fleeing enemy. The reels of the first render of the
new material (run v04: the Ghorkov and Taerkasten packs and the two new Black
Sect lines) were then shared with him, and he replied "Yeah, these are great."
After review, version 5 re-cut 53 of those cues: long pauses closed, and the
quiet decay after the last word (40 dB or more below each line's peak), an
inhale and low thumps trimmed. The takes and words are the same, but the lines
end sooner, by a median 0.14 s; that is one thing to listen for. Nobody has
heard the re-cut yet. Speech recognition verifies the words. A language-ID cue
gives only a weak hint of the accent. In-game acceptance remains with Andrei.

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
KYTERNESER. The brief asks for a Liberation Forces officer reporting to his
Commander: proud, disciplined, formal and clipped. He should have a natural
Russian accent and standard English grammar. The takes were primed with
Russian radio phrases. Whether the accent and delivery come through is for
Andrei's ear; the accent cue below is only a hint. The lines use the faction's
own vocabulary: "Liberation Forces reporting.", "Attacking the misguided.",
"Reactionary sector entered.", "Energy for our people!".

## Taerkasten (owner 4)

The brief asks for a brother of the Order, reporting to the Commander over an
old field line: grave, devout, unhurried and calm even in loss. The light German
accent comes from a reading of the Order's German machine names (Eisenhans,
Zeppelin, the Festung houses) and of Metropolis Dawn's Luther-Bible German
texts. Register matters more than accent strength here. The takes were primed
with German phrases. On the accent cue, the German barely separates from native
English (see below), so whether any accent comes through is for the ear. The lines lean on the
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

## Lines checked against the engine

A review checked every new line against what its engine event and the faction
lore support.

Candidates for the next Suno batch:

- `unit_retreating`: "Energy low, pulling back." (Ghorkov). Message 9 is also
  raised when the player recalls a healthy squad with Aggressiveness Level 1,
  which the retail help recommends for bringing a squad home. Message 9 shows
  no log text, so the voice is the only report. The pack's two message-9 lines
  alternate across all of its message-9 reports, so about half of such recalls
  would play this line and report low energy that the squad does not have. The
  retail clip for this slot ("Tank squad pulling back.") names no cause.
  "Returning to recharge." is a weaker case: it states a purpose that is false
  for a full squad.
- "Attacking the misguided." (`order_ack_enemy`) and "Misguided force in view."
  (`enemy_unit_spotted`), both Ghorkov. In the Kyternesian briefings "the
  misguided" are the people misled by Resistian tyranny and Taerkasten
  propaganda, and the same briefings call the Mykonians "invaders". Against
  Mykonian targets these lines name the wrong foe. Orders into empty neutral
  sectors are fine: the game marks every such order as an attack (the Attack
  cursor and the tooltip "Attack This Sector"; the locale's message-16 text
  "Attack: Acknowledged" is never shown).

Kept after review:

- `unit_recovered`: "Recharged and ready." (Ghorkov) and "Restored by the
  stone." (Taerkasten) make the same claim as the retail clip for this slot,
  "Weapons and shields recharged." The claim holds in this fork as well as in
  retail, for every voice:
  - An escaping squad below Level 5 reports in only above half energy, as in
    the decompiled engine. An upstream readability refactor (`7bc426f`, 2021)
    had flattened the condition in `GetFightMotivation`, so such a squad
    resumed at a 0.2 energy ratio. The retail condition is restored (see
    [validation status](validation-status.md)); it has not been playtested.
  - Several cases report in without a recharge in retail too: a squad raised
    again after a Level 1 recall, an escaping squad set to Level 5, a squad
    reinforced with a new unit, and a squad whose weakest member dies.
- `enemy_unit_spotted`: "The godless approach." (Taerkasten). Message 7 fires
  when an enemy other than the last one the squad reported becomes its nearest
  target, at most every 45 s, whether or not anything approaches: a parked
  enemy the Taerkasten squad closes on, or the next enemy squad after a kill.
  Of the two retail clips for this slot, "Enemy tangos engaged!" claims more
  than that, and "Visual ID on enemy." reports only a sighting.

## How the accented voices were made and checked

Both packs use Suno Speech (beta): male voice, background music off, variety
off. The lines were written and judged per event against the engine's trigger
meanings and the faction lore.

- **Accent.** Suno ignored accent wording in the tone prompt: the English came
  out near-native. A short native-language radio phrase before every English
  line works instead ("Так точно.", "Verstanden.", ...), with an ellipsis
  paragraph after each line. The priming phrases are cut away and never ship.
  One Ghorkov cue, "Enemy in retreat." (`enemy_unit_engaged_1`), comes from a
  Russian-primed dry-run pilot take (`pilot_ru8c`) made before the production
  batches, with older tone wording; its script, tone and settings were not
  saved.
- **Accent check.** The cue is the probability that Whisper small's language
  ID assigns to the speaker's first language. It was calibrated on the GMU
  Speech Accent Archive (six speakers per group, 2-second chunks padded like
  the final files, per-speaker medians). Native English speakers measure
  P(Russian) 0.0001–0.0006 and P(German) 0.0002–0.0007. Russian
  second-language speakers measure P(Russian) 0.0025–0.15, and German
  second-language speakers measure P(German) 0.0012–0.011. A single short line
  is noisier than those medians, so the packs are also compared with a
  native-English control measured exactly like the final files: the 32 Black Sect
  files and 25 retail English clips. The control reads P(Russian) median
  0.0012 (max 0.0061) and P(German) median 0.0011
  (max 0.0064).
  - Ghorkov reads a median P(Russian) of 0.006, about 5 times the
    control. 15 of 32 lines read above every control clip. Against the
    calibration, 15 lines fall within the Russian second-language range
    and 13 below it; listen to those first. Four lines read above the
    whole range (0.19–0.62: "Stronghold destroyed, Commander!", "We need support, Commander.", "Liberation Forces reporting.", "Reporting for duty."); listen to these for caricature.
  - Taerkasten reads a median P(German) of 0.0022, about 2 times the
    control. Only 6 of 30 lines read above every control clip, so the
    cue cannot confirm a German accent. Per-line counts against the calibrated
    range mean little here: 13 of the 32 native-English Black Sect lines also
    fall within the German second-language range.
  - Selection gave a bonus to cuts with a higher cue, so these figures lean
    upward.
- **Drone bed.** With "cinematic" wording in the tone, 14 of the 22 downloaded
  takes carried a low drone, even with background music off: eight of ten
  Taerkasten takes and six of twelve Ghorkov takes. Those takes were separated
  with Demucs (htdemucs). 44 of the 62 new cues (Ghorkov 20,
  Taerkasten 24) are vocal stems and may carry faint separation artifacts. The
  manifest gives the bed level under each line (`music_under_voice_db`). In
  14 of them, the bed was within 3 dB of the voice or louder. The worst is
  "Moving out, Commander.", where the bed is 10 dB above the voice across a quiet
  line with a pause in it. These 14 are listening priorities; the manifest
  names them by their `music_under_voice_db`. A plain "dry voice recording, no
  ambience" tone avoided the bed (Taerkasten batch r1).
- **Selection.**
  1. Whisper word alignment locates each line, and the cut sits at the silence
     around its vocal island. Where two lines run together, the cut falls at the
     quietest valley between phrases.
  2. An edge audit transcribes and language-identifies each island inside a cut
     (sound within 30 dB of the cut's peak, merged across 0.15 s, at least
     0.05 s long). It trims away priming phrases and other islands before the
     first or after the last line island, and rejects a cut with a priming
     phrase between the words. Noise islands between the words stay; the inhale
     after "Brothers," in "Brothers, they hold this ground." was one. Fainter or
     shorter sounds never form an island, so the audit cannot see soft breaths
     or low thumps. The v05 re-cut below removes only sound more than 40 dB
     below the cut's peak, so it keeps such sounds when they are louder than
     that; the inhales and thumps the audio review found were cut out with
     explicit windows.
  3. A residue check rejects cuts with sound outside the line's words.
  4. A cut is eligible only if base.en, small.en and medium.en, transcribing it
     in isolation, hear exactly the planned line in at least two of three cases.
  5. Among eligible cuts, cleaner onsets and a stronger accent cue win.
- **Final files.**
  - Ghorkov: 26 of 32 lines are heard exactly by all three models.
    In each of the other 6, one model mishears part of the line (for example
    "Liberation Force is reporting" and "Returning to the Church").
  - Taerkasten: 29 of 30 lines are heard exactly by all three models, and the other by two ("Into battle": one model hears "into better").
  - The two new Black Sect lines: both are heard exactly by all three models.

**Re-cut after the audio review.** The first render (run v04) had problems in
several cues: pauses of up to 1.3 s inside a line, a half-second inhale before
"Brothers, they hold this ground.", low thumps at three cut edges and quiet
tails of up to 0.8 s after the last word. The thumps are htdemucs leakage of
the drone bed, too faint or too short for the edge audit (the loudest, at
42.55 s, rises within 30 dB of the cut peak for only 0.02 s). Each pause and
tail kept the engine's voice slot busy. So run v05 re-cut 54 of the 62 new cues:

- Heads and tails were trimmed to 40 dB below each cut's peak, with 0.05 s and
  0.10 s pads.
- In 6 lines, the pause inside the line was closed to 0.25 s by playing it as
  two parts: "Orders, Commander?", "Recharged and ready.", "Moving as
  ordered.", "Welcome among us.", "Brothers, stand with us." and "Brothers,
  they hold this ground." The last also lost about half a second of low-level
  sound before its first word, and another inside the pause, both inhales.
- Explicit windows from the review left the inhales and thumps outside the
  cuts.

A second review checked the re-cut. The first review had also counted a
"click" at the end of "Attacking the misguided.". It is the release of the
word's final /d/: a closure, a burst and aspiration. The re-cut had removed it,
so that cue ships its v04 cut, and version 5 keeps 53 re-cut cues. A scan
of everything else the re-cut removed finds nothing within 40 dB of a cut's
peak except the inhales and the thumps; the loudest remaining removed sound is
about 41 dB below its cut's peak. The re-cut lines end a median 0.14 s sooner
than in v04, at most 0.42 s (0.66 s where the tail held thumps).

16 lines (12 Ghorkov, 4 Taerkasten) still run longer than the
plan's duration targets, by at most 0.73 s. The longest are
"Reactionary stronghold found." (2.58 s), "Stronghold destroyed, Commander!" (2.57 s) and
"Welcome among us." (2.48 s).

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
the plan's duration targets because no word was shortened to fit. In 19 of
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

- Ghorkov reads −20.9 to −17.7 LUFS (median −18.1),
  with true peak at most −3.8 dBFS. The quietest cue is "Moving out,
  Commander." at −20.9 LUFS. It pauses between its two phrases, and the
  true-peak ceiling limits its gain.
- Taerkasten reads −18.9 to −17.7 LUFS (median −18.2),
  with true peak at most −3.6 dBFS.
- Black lines read −18.8 to −18.0 LUFS, including the two new lines at
  −18.4 and −18.5 LUFS.
- The version 4 Sulgogar figures still hold. Of the 32 files, 23 are within
  1 dB of target, seven are 1.4 to 5.8 dB under, and the last two stay at
  −22.4 and −28.1 LUFS at the +12 dB cap.

None of these numbers show yet how the cues hold up in combat or against the
stock speech bus.

There are three native REAPER 7.79 runs, all through the TwelveTake bridge:

- `v03-full-candidate-2026-11-02` (version 4): Black Sect and Sulgogars.
- `v04-faction-voices-2026-11-02`: the first render of Black Sect, Ghorkov and
  Taerkasten (94 cues). It was superseded by v05 after the audio review,
  except for "Attacking the misguided.", which still ships from v04.
- `v05-faction-voices-recut-2026-11-02` (this version): the same 94 cues with
  54 re-cut. The other 40 came out byte-identical to v04.

The device shell was unavailable for v04 and v05, so the bridge mailbox was
driven by file transfer. The guards stayed the same:

- the active project was checked as expected and clean before switching;
- one operation ran at a time;
- each render went to a fresh file.

Each run had `cue_workflow.prepare` place the cues into one project. REAPER
saved and rendered it. Then `cue_export.finalize` checked the bridge receipt,
the saved timeline and the master's length, and cut exact per-cue windows from
the master. ffmpeg converted each cue to PCM16 mono 22050 Hz. As an independent
check, every v05 cue master (48 kHz, before conversion) matches a separate rendering
of its reviewed item from source exactly: correlation 1.0, level within 0.01 dB.
The v04 master of the cue that ships from v04 matches in the same way.
The runtime files correlate at least 0.9741 with a 22050 Hz reference rendering. The
lowest values are two-part cues, whose second part sits half a sample off that
reference's grid.

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
manifest entry (hash, size, PCM16 mono 22050 Hz). It also checks four more
things: each pack covers all 32 event slots once, counting pending slots; the
filenames use the engine's event keys; the routing table names exactly these
four packs; and pending slots have no file. Both build scripts run it.

## Where the working material lives

- REAPER lab runs: `F:\OpenNeoUA-BlackSect-Lab\audio-reaper\Runs\v03-full-candidate-2026-11-02`,
  `...\Runs\v04-faction-voices-2026-11-02` and
  `...\Runs\v05-faction-voices-recut-2026-11-02`. Each holds the project,
  media, master, cue masters and receipts. The v04 and v05 media copies carry a
  trailing C2PA provenance chunk added in transfer. Their audio data is
  byte-identical to the hashed sources whose names they keep.
- Selection tools, plans, generation receipts and reels: Coda's workspace,
  `work/audio-workflow-v5` and `outputs/audio/v05-candidate-pack`. The first
  render's material is in `outputs/audio/v04-candidate-pack`.

The audio is Suno output generated under Andrei's account. It is not recovered
original game audio and not a performance by Coda, Lily or Red.
