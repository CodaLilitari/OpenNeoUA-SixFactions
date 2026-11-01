# Developed faction voice packs, version 3

Each faction has 32 original cues: two variants for each of the engine's 16 unit
events. The filenames match the implemented faction voice resolver.

Black Sect uses the same Microsoft David Desktop SAPI voice as the approved
pilot, with Lily's revised dialogue. Frequent acknowledgments use a slightly
faster delivery. Light radio filtering and compression improve level consistency
without adding carrier noise, music, a vocoder or a reverberation tail.

Sulgogars use newly synthesized procedural sounds. Lily's revisions distinguish
friendly acknowledgment, hostile acknowledgment, direct control, boundary entry,
recovery, destruction, idle, support and power capture by pulse structure and
pitch movement. No existing recording or actor's performance was sampled.

The separately retained local review WAVs contain all 32 cues for their faction in engine event order,
variant 1 then variant 2, with 200 ms silence between cues. The manifest preserves
every phrase, event, duration, hash and measured loudness result.

The original vanilla speech references measured -14.4 to -11 LUFS. These
development packs conservatively target -18 LUFS for routine cues and -17 LUFS
for long strategic Sulgogar cues, with a -3 dBFS true-peak ceiling. Several Black
lines remain slightly quieter because the ceiling takes priority over gain.
These measurements do not establish combat intelligibility or perceived quality.

Andrei approved developing the pilot direction. Coda and Lily have performed
technical and design review; neither claims to have heard playback. Listening
through the actual in-game speech bus remains pending.

To install in the separate test runtime, copy the two faction directories into
`Data/Sounds/speech/voicepacks/`. Keep this README, manifests and review sequences
outside that voicepack directory. The original installation remains untouched.

The engine selects these owner-default packs only for Black Sect and Sulgogars
in multiplayer, after any explicit unit-specific pack. Existing strategic
announcer cues and original four-faction speech remain in their existing paths.

To regenerate the development set on Windows with the same installed SAPI voice,
use Python with `comtypes`, FFmpeg, and the scripts in `tools/audio`. Supply
external output and processing directories; the generator refuses output inside
this checkout. It writes speech directly to WAV files without speaker playback.

```text
python tools/audio/develop_voicepacks_v3.py --output <external-new-output> --work-dir <external-new-scratch> --ffmpeg <ffmpeg.exe> --sapi-python <python-with-comtypes.exe>
```

The procedural Sulgogar renderer uses only standard Python math and its recorded
seeds. Speech synthesis depends on the installed Microsoft David Desktop voice;
another OS or voice version need not produce byte-identical speech. Generation
does not require Suno or a paid API. The WAVs are synthetic development assets,
not recovered original game recordings or performances by Coda or Lily.
