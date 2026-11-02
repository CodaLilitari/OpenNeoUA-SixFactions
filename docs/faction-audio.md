# Faction voice packs: Suno + REAPER candidate pack (version 4)

Each faction has 32 cues: two variants for each of the engine's 16 unit events,
under the same filenames the faction voice resolver already uses. This branch
replaces the version 3 development pack (Microsoft David SAPI speech for Black
Sect, procedural synthesis for Sulgogars) with candidates cut from Suno takes
and rendered natively in REAPER. `docs/faction-audio-manifest.json` records every
file's source take, source window, gain, loudness, hash and review basis.

**Nobody has heard these files yet.** The Black Sect lines are verified by
speech recognition. The Sulgogar calls were chosen from measured signal shape and
spectrograms. Auditory and in-game acceptance remain with Andrei.

## Black Sect

Twenty-eight lines come from Suno v6 takes generated from Coda's production plan
(batches 01 to 07, two or three takes each, spoken-word style reused from her
Production 01 prompt). Each take was separated with Demucs (htdemucs, vocal stem).
Every line was located by Whisper word alignment, cut at the silence around its
vocal island in the stem, and then transcribed again *in isolation* by three
models (base.en, small.en, medium.en). A take was eligible only if at least two
models heard exactly the planned line; the best eligible take won on onset
cleanliness, stem bleed, music under the voice and duration fit.

Four lines are Coda's reviewed v02 cuts at their exact windows: "Asset online."
(Black-B-01), "Route confirmed." (Black-A-02), "Reinforcements requested."
(Black-A-03) and "Host station neutralized." The plan named Black-A-04 for the
last one, but all three models hear its first word as "How" or "House", while
they all hear Black-B-04 as "Host station neutralized.", so B-04 is used here.

On the final 22050 Hz files, 29 of 32 lines are heard correctly by all three
models and the other 3 by two of three. Some lines run longer than the plan's
duration targets because no word was shortened to fit. In 19 of the 28 new
lines, the Suno backing under the voice was within 3 dB of the voice or louder.
Those lines are the most likely to carry faint separation artifacts. The manifest
gives the measure for each line (`music_under_voice_db`).

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
the final files as dual mono, Black lines read −18.9 to −18.0 LUFS. Of the 32
Sulgogar files, 23 are within 1 dB of target. Seven are 1.4 to 5.8 dB under,
either because the peak ceiling limits gain on sharp attacks or because energy
above 11 kHz is lost at 22050 Hz. Two hit the +12 dB cap and stay at −22.4 and
−28.1 LUFS. True peak is at most −3.4 dBFS. None of these numbers show yet how
the cues hold up in combat or against the stock speech bus.

The 64 cues were placed by `cue_workflow.prepare` into one REAPER project, saved
and rendered natively by REAPER 7.79 through the TwelveTake bridge. Then
`cue_export.finalize` checked the bridge receipt, the saved timeline and the
master's length, and cut exact per-cue windows from the master. ffmpeg converted
each cue to PCM16 mono 22050 Hz. As an independent check, every runtime file
matches a separate rendering of its reviewed item from source (correlation at
least 0.9957, level within 0.25 dB).

## Install and routing

Unchanged from version 3. Copy the two faction directories into
`Data/Sounds/speech/voicepacks/` of a test runtime. Keep this README and the
manifest outside that directory. The engine uses these owner-default packs only
for Black Sect (owner 5) and Sulgogars (owner 2) in multiplayer, after any
explicit unit-specific pack.

## Where the working material lives

- REAPER lab run: `F:\OpenNeoUA-BlackSect-Lab\audio-reaper\Runs\v03-full-candidate-2026-11-02`
  (project, media, master, cue masters, receipts, audition reels).
- Selection tools, receipts and the patched workflow with Coda's originals kept
  beside them: Coda's workspace, `work/audio-workflow-v4` and `outputs/audio/v03-candidate-pack`.

The audio is Suno output generated under Andrei's account. It is not recovered
original game audio and not a performance by Coda, Lily or Red.
