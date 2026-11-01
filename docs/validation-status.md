# Development validation status

Recorded 2026-11-01. These results concern the six-faction extension based on
upstream `ebf9f9f7abcb49387b2752e5855eb0098bf92ef9`.

* Windows x86-64 engine: GCC 16.2 RelWithDebInfo build with the agent interface
  and snapshot repair passed. The subsequent pre-swap capture rebuild produced
  SHA-256 `f278c3fea0af8e6f90221328ad074014c7582ace3eb6f91a65308bb5161bfb50`.
* Ubuntu 26.04 x86-64 engine: GCC 15.2 build with those changes passed. Executable SHA-256
  `144d3d6709b50cbb880abd5f431eeaf30c5e118adbc199f4e83dbf8cc1ca103d`.
  This local binary requires GLIBC 2.43 and is not the older Ubuntu CI build.
* Focused C++ tests: faction validation, speech routing/variation, and six-client
  loopback UDP transport passed with assertions enabled. The transport test uses
  the production handshake identifier, checks reliable multipart delivery,
  capacity, reconnect, and rejection of the original upstream identifier while
  slots remain available. These are transport endpoints, not six game clients.
* Python map generator: ten regression tests passed. A new test catches title
  suffixes lost by the game's first-token parser; the generator now keeps the
  six-faction suffix in that token. Crosshill's six-player
  variant preserves 192 nominal intact-plant power for each faction. Static
  geometry and nominal power are not proof of live income or competitive balance.
* Local Linux packaging: repeated packaging of the same executable and source
  produced identical archives. This does not establish compiler reproducibility
  across systems.

Normal Linux startup has rendered the intro in a virtual display. The first
launch exposed an inherited portability defect: a DOS-style config path was
passed to a native `ifstream` existence check. Using the engine's existing VFS
for this check fixed the failure; the normal launch was repeated without the
temporary diagnostic filesystem alias. Windows also rebuilt after this fix.

Six real game clients subsequently joined, readied, and loaded the Crosshill
variant together in separate Linux virtual displays. Both added factions
created a unit through the normal build UI with energy deducted, and entered
that unit through the squad UI. These observations are actual engine behavior,
not the transport harness.

The first run exposed an inherited snapshot-restore defect: repeated host-gun
records could overwrite the first gun, and gun missiles could get the previous
object as their launcher. The receiver now validates record topology before
removing the old station and restores distinct guns/parents. The sender refuses
oversized complete snapshots before transmission instead of overflowing or
silently omitting units. The caller now reports the actual restore result.
Focused regression and mutation checks passed. In a second six-client match,
each of the five remote observers restored once after the same initial Black
entity death/release mismatch. All five restored successfully, with no subsequent
missing-vehicle EVD errors. All six factions created and entered units through
the normal UI. This establishes the recovery improvement in that run; the
initial mismatch remains under investigation.

Source analysis then identified a duplicate first unit in the building-vehicle
receiver. It created packet entry zero as the commander and again as a child;
commander death and promotion could therefore resolve the duplicate live ID.
The receiver now consumes each of the bounded eight entries once. A lifecycle
regression reproduces the former wrong-ID lookup, and the duplicate-entry and
unbounded-array mutations fail. The local binary hashes above precede this
last correction; its full engine and six-client rerun are pending.

The stock debug configuration changes F5–F8 behavior. A brief AI-ignore toggle
was identified and restored in the first run; no combat correctness is claimed
from that interval. Subsequent normal-gameplay profiles disable that setting.
Replicated combat, capture and completion remain separate acceptance work.
The fork is a development candidate, not a completed multiplayer release.

The `linux-build` branch carries a manually invoked Ubuntu 22.04 container build
for an older userspace baseline. Run 41361160000 succeeded at source revision
`1528ada4740ce945ab88b9c9b08382dbe289efe2`. Its 1,599 packaged payload hashes,
180 bundled libraries, five host-library exclusions and 64 provisional cue
hashes were independently checked. Every bundled ELF's maximum required GLIBC
version is 2.35. This artifact predates the new agent hooks, snapshot repair,
and map-title correction. It contains no generated original-data-derived maps.
Future execution in another AI collaborator's sandbox has not yet occurred.

The common agent module has focused Windows checks for command parsing,
expiry, sequence/replay behavior, bounded holds, text pacing, actual SDL queue
delivery and file transport. The Python MCP wrapper has fifteen passing tests,
including a separate-process MCP handshake, native image response encoding,
timeouts without automatic replay, and stale-session rejection. These tests
do not establish actual rendered game control by themselves. A direct Linux
engine test then verified the stdio MCP handshake, observation, a normal menu
click opening the connection screen, a bounded key hold and release, and
rejection of an out-of-bounds click. The final PNG's decoded RGB bytes matched
an independent virtual-display capture exactly. This caught and corrected an
initial black-frame defect by capturing the completed back buffer before swap.
The native Windows executable also passed a hidden-window menu smoke using the
real stdio adapter: rendered 640x480 images, multiplayer menu click and return,
150 ms key hold, release and out-of-frame rejection. A normal Exit click ended
the process with code zero before its input receipt could be written; the
adapter correctly reported the missing receipt without retrying. This is
native menu control, not proof of a Windows multiplayer match. Private-fork
integration remains a separate check.

The committed engine adds the verified startup config-path repair to the
reviewed six-faction candidate, plus two removed whitespace-only lines in
`yw_shell_ui_screens.cpp`. Original game data, generated retail-derived maps,
local build trees, private correspondence and recovery material are not included.
