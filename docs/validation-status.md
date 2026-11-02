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
last correction. The subsequent full Linux build passed; its six-client rerun
is in progress.

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
Newer run 41362951089 succeeded at revision
`2b317835083a632d11ab6888b74cc7498bcb2c7e`, including the agent hooks and both
network repairs. Its 1,617 packaged payload hashes and archive digest were
independently checked. It retains the same GLIBC 2.35 maximum and 180 bundled
libraries. Its executable SHA-256 is
`4ed9029a433fd149efb743b3d3a808102734eff6153126c1fbc88da46ebd8fd6`.
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
native menu control, not proof of a Windows multiplayer match.

The expanded menu run then found that legacy list selection reads the last
mouse-motion position rather than the coordinates of a button event. Clicks
now send an absolute motion event before the press, with zero relative motion.
An optional native hidden-window regression failed on the previous bridge and
passed on the correction; all seven selected Windows CTest checks passed.
The CI artifact above predates this last click correction. Full-game list
selection with the updated bridge remains a separate verification step.

The committed engine adds the verified startup config-path repair to the
reviewed six-faction candidate, plus two removed whitespace-only lines in
`yw_shell_ui_screens.cpp`. Original game data, generated retail-derived maps,
local build trees, private correspondence and recovery material are not included.

## Netcode modernization and agentic forks/MP bots (Muse Code session)

Recorded after the entries above, written while the work was an uncommitted
tree on top of `99716e6`. It was later committed and pushed to `six-factions`
as `429f3f4`, `ff4dadb`, `6001c99` and `6b65506`; only `6b65506` configures
and builds (the earlier three list sources that arrive in later commits).
Toolchain: MSYS2 MINGW64 (GCC) bootstrapped locally; full
`tools/windows/build.sh` run is green (engine links, 14/14 CTest, agent MCP
Python tests, map tests, package zip).

* Handshake now carries a little-endian U16 wire protocol version; hosts
  hard-reject missing/incompatible versions with `ERR_CONN_VERSION`. The
  six-endpoint production transport test passes with the trailer, including
  silent-drop resume: a 128-bit token issued at `CONNECTED` restores the
  original identity after the 15 s host timeout plus 60 s orphan grace
  (observed: "Resume after silent drop restored the original identity").
* Inbound data packets are token-bucket limited per source (400 burst,
  200/s); unit-tested (burst/refill/cap/backwards clock).
* Blast-radius FIXME closed: legacy impulse scans the blast neighbourhood on
  sender and receiver (radius in spare `p[1]`; old senders stay home-only).
* Agentic forks: deterministic fork/step/observe/diff/commit-by-replay with
  fog-filtered observation, dispatched through the agent bridge and MCP
  `game_fork_*` tools (MCP suite 17/17). MP bots: six distinct faction
  profiles, three difficulties, join/leave/takeover lifecycle, `-bot
  Faction:Difficulty`. Fork/bot logic is covered by standalone tests; no
  live bot match has been run.
* Fixed 20 Hz tick (`NetTick`) now drives world and shell net flushes at
  the configured period. `yw_wire` envelope and `delta_wire` ForkDiff
  codecs are unit-tested but not yet cut over.
* ENet 1.3.18 is vendored behind `INetTransport`/`EnetTransport` with the
  same handshake/resume/two-channel contract (loopback-verified without
  SDL), plus a `ZNDEnet` adapter preserving the windp session model
  (loopback-verified with SDL). `OPENNEOUA_TRANSPORT=enet` selects it;
  ZNDNet remains the default until a live acceptance match justifies the
  flip. The Windows engine binary starts and stays up with and without the
  opt-in (10-12 s smoke, no retail data in the working directory).

Still open (unchanged in kind from above): replicated combat, capture and
completion in real game clients, a live human+bot match and
disconnect-takeover drill, and the release gate. These need retail game
data and real client processes, not more transport work.
Data staging update: the user's `OpenUA Standalone Package.zip` (retail UA +
Metropolis Dawn, OpenUA build 2025-03-09) was extracted in scratch space and
caught up to OpenNeoUA: current `OpenNeoUA.exe` + runtime DLLs in the root,
OpenNeoUA `Data` overlay merged over the retail `DATA` (overlay wins),
retail root `LEVELS`/`ENV`/`LOCALE` copied into `DATA` (259 level
files incl. `MULTI`). The merged install starts and runs (20 s smoke,
config written). Originals (zip and Downloads) untouched. Live multiplayer
acceptance via the agent bridge is the remaining step.
## Live multiplayer acceptance (2026-11-02, two real engine processes)

Two engine instances were run from the staging copies and driven through the
real menus with the agent bridge/MCP on loopback (host UDP 61234):

* ZNDNet (default transport): HOST1 hosted retail level UNDER; CLIENT1
  joined, readied, and both entered the match (`screen=game`, level 132,
  owners 1 and 4). Netlogs show mutual READY TO PLAY; both stayed in sync
  for minutes with no snapshot errors. Fork create/observe/discard was
  exercised live against the running match: fog-filtered state showed both
  own units and foreign-owned sectors. Disconnect drill: the client process
  was killed mid-match; the host logged "Waiting for player CLIENT1", then
  "Received a destroy player for CLIENT1", and kept playing.
* MP bot: a third instance started with `-bot Ghorkov:Veteran` was driven
  into the same lobby (roster HOST1 + BOT1), readied, and entered the match.
  Its fork-observed world state minutes later showed the native station AI
  expanding under the bot profile (own sectors energized across the map).
* ENet opt-in (`OPENNEOUA_TRANSPORT=enet`): with distinct callsigns, the
  client joined the host lobby over the `ZNDEnet` adapter, readied, and both
  instances entered the same match with mutual READY TO PLAY. With
  *identical* default callsigns ("Unnamed" on both sides) the lobby deadlocked
  instead: name-targeted WELCOME delivery resolves the first matching name,
  which is the host itself when names collide. Unique callsigns are required;
  this matches the legacy name-addressed design and also affects readability
  of the roster (both players display as "Unnamed").

Driving notes: bridge clicks must target the legacy widgets' own 640x480
hit rects (use widget geometry, not the rendered background art, which is
offset); retail profiles also need `gfx.movie_player = no` in
`NUCLEUS.INI` for unattended launches, or startup blocks in the intro movie
before the agent bridge initializes. `fork_step`/`fork_commit` were verified
by unit tests only, not driven live in this run. Six-faction roster matches,
replicated combat outcomes, capture and completion remain open acceptance
work, as does the release gate. Per the recorded user decisions, ENet stays
opt-in.

## Six-faction roster + live fork commit (2026-11-02, headless instances)

Both engine windows were hidden (bridge-driven only) for this phase.

* Generated a six-station variant of retail `THE_ARTIFICIAL` (level id 200,
  `THE_ARTIFICIAL_Six_Factions`) with `tools/make_six_faction_map.py` and
  staged it into the instance data. The multiplayer level list shows it
  alongside the retail entry; the lobby offers all six factions (Resistance,
  Ghorkov, Mykonians, Taerkasten text/icon buttons plus Black Sect and
  Sulgogars). A match on it ran with host owner 1 and a bot owner 6
  (`screen=game`, `level_id` 200, six-station starts, 320000 station energy).
* Live fork drive: `fork_create` + the new `game_fork_clone` produced two
  forks with identical base hash; a Move step on the source advanced its
  hash; `fork_commit` then succeeded and the clone's fog-filtered state
  showed the replayed move (robo displaced toward the step target).
  `fork_clone FORK OWNER` was added to the agent protocol, engine dispatch,
  bridge capability list and MCP tools for this: two independent live
  captures can never share a base hash (the state hash covers timestamps
  and energies), so commit-by-replay needs a same-base destination. MCP
  tests now 18/18. Committing between two unrelated live captures still
  fails loudly (`fork_commit_failed`), as designed.
* The bot's station maneuvers toward the host under its conquer wishes
  (observed closing ~500 map units per 8 minutes on the 54x32 map); sector
  ownership and unit counts were stable while out of contact. Replicated
  weapon combat, sector capture and match completion are being observed
  over a longer bot-driven run; results appended when contact occurs.
## Live replicated bot combat (2026-11-02)

### Bot station dispatch defects found and fixed

Two dispatch defects prevented multiplayer bot stations from using the native
station AI. This made fork-driven orders ineffective because those orders only
steer the station wish weights.

1. `NC_STACK_yparobo::AI_layer3` skipped `AI_checkWorld` and `AI_doWish`
   for the local human station. A bot station is marked as the player robo,
   so its production and strategic wishes never executed.
2. `NC_STACK_yparobo::AI_layer1` dispatched directly to `User_layer`
   whenever the local station had the inputting flag. This prevented the bot
   station from reaching `AI_layer3` at all.

The fix is narrowly scoped to bot processes:

- `World::NetBot::StationUsesNativeAI(isPlayerRobo, botControlled)` in
  `src/world/net_bot.h` centralizes the gate.
- `NC_STACK_yparobo` now has `_botControlled` plus `SetBotControlled()`
  and `IsBotControlled()` in `src/game/yparobo.h`.
- `BotRuntime::Tick` in `src/game/yw_bot.cpp` marks the local robo as
  bot-controlled when a valid `-bot` spec is active.
- `AI_layer3` computes `nativeAI` from `StationUsesNativeAI` and uses it
  for movement, world checks, wish execution, service, attacker-slot cleanup,
  and danger handling in `src/game/yparobo_ai.cpp`.
- `NC_STACK_ypabact::UsesAIControl()` was added in `src/game/ypabact.h`;
  its default preserves human behavior. `NC_STACK_yparobo` overrides it to
  return true for bot-controlled stations.
- The generic dispatch in `src/game/ypabact_ai.cpp` and robo dispatch in
  `src/game/yparobo_ai.cpp` now use `UsesAIControl()` instead of checking
  the raw inputting flag. Human-controlled stations retain the original
  `User_layer` path.

`tests/test_net_bot.cpp` now covers all four `StationUsesNativeAI`
combinations. The test failed before the helper existed and passes after the
fix.

### Headless live match

Two hidden engine processes were launched from the updated staging instances:

- Host: `OpenNeoUA -bot Resistance:Veteran` (callsign BHOST, owner 1).
- Client: `OpenNeoUA -bot Ghorkov:Veteran` (callsign BCLIENT). On UNDER,
  the client was assigned owner 4 / Taerkasten (station prototype 60).

The client connected to 127.0.0.1, both players appeared in the lobby, the
client readied, and the host started level 132. Both observations reported
`screen=game`, `level_id=132`, and mutual READY TO PLAY appeared in the
network logs.

Within about 100 seconds of simulated time the armies expanded from the
initial 2 units per side to host 8 and client 5; about 100 seconds later the
host had 28 units and the client 12. Production spent station energy (host
station 200000 -> 159754 during the buildup). Units dispersed across the map
and the client station advanced toward the host (x about 9985 -> 9498).

Combat was then observed from fork snapshots on both processes:

- At t about 317 s, the closest opposing units were 745-747 map units apart
  on both observations, with damaged units already present on both sides.
- At t about 457 s, the closest opposing units were 7-8 map units apart.
  Multiple units had non-prototype (damaged) energies and several wreck /
  recently-killed entries had negative energies on both clients.
- Host owned-unit count collapsed 34 -> 15 -> 1. By t about 631 s the host
  station (gid 16842752) was at energy -17800 on the host observation and
  the identical -17800 value was observed as a foreign unit on the client,
  proving the destruction replicated.
- Sector control collapsed with the army: host-visible owner-1 sectors went
  28 -> 19 -> none visible, while the client observed 64 owner-4 sectors
  and still held 13 units. This is replicated sector capture / map control
  following the battle.

`OH NO! The DEATH CACHE BUG is back!` lines in the client log are a legacy
engine diagnostic printed during the death cascade; no crash occurred and
both processes remained responsive to bridge observations afterwards.

### Final verification

After the dispatch fix, the focused `test_net_bot` binary passes, and the
full `tools/windows/build.sh` run is green: engine build, 14/14 CTest,
10 map tests, 18 MCP tests, 5 package tests, and a fresh Windows package
(`OpenNeoUA-windows-x86_64.zip`, binary sha256
`3f653d7bf9077409b949e01b6a7e425f621c76ae25ade9279cd2a98cbfabafd1`).
ENet remains opt-in per the recorded user decision.

## Independent review and Linux validation (2026-11-02, Red)

Scope: the four Muse Code commits above, reviewed from source, rebuilt on
Linux and exercised with new focused tests. No retail data or live match was
used in this pass, so none of the gameplay acceptance gaps above is closed.

* `6b65506` builds on Ubuntu 24.04 x86-64 with GCC 13.3 through
  `tools/linux/build.sh`: 14/14 CTest, 10 map tests, 18 MCP tests, package and
  dynamic-library check green. This is the first Linux build of the Muse work.
  The two ENet suites were off in both build helpers; enabled, they passed.
* ENet adapter, three or more players: a client's broadcast never reached the
  other clients and directed client-to-client sends were sent to the host and
  dropped, because the host delivered client frames only to itself. The
  two-peer tests could not see this. Frames are now addressed (`'D'`/`'B'` +
  U64) and the host relays them with the original sender id, as `ZNDSingle`
  does. `test_enet_znd_relay` failed before the fix ("other client missed
  relayed broadcast") and passes after.
* ENet adapter, threading: the pump thread serviced the ENet host while the
  game thread sent on it. ThreadSanitizer reported 80 data races in the
  existing `test_enet_znd`; with one lock around every transport call it
  reports none in that test or the relay test.
* ENet handshake: duplicate callsigns are refused (`ERR_CONN_NAME`) instead of
  joining and deadlocking name-addressed lobby messages; a full or hidden
  session refuses new joiners (`ERR_CONN_FULL`), restoring `ZNDSingle`
  parity; broadcasts reach only peers that passed the handshake. An ENet-only
  framing byte (`FramingVersion = 2`) keeps mismatched ENet builds from
  misparsing each other; the ZNDNet wire and `ProtoVer` are unchanged.
* `windp`: two nested-if guards made `CloseSession` and the session-list
  refresh no-ops on the ENet path; both now test the active connection.
* Bots: a `-bot Ghorkov:Veteran` client seated as Taerkasten (as in the live
  run above) kept thinking as owner 6, so its decisions used another
  faction's own-unit set and fog view; it now binds to its seated owner. The
  order steering wrote 100 into wishes and never restored them, so every
  faction drifted to the same all-100 station; weights are now recomputed
  from the profile each think. `test_net_bot` covers both.
* Forks: live capture copied engine `(x, altitude, z)` into a model that
  treats `(x, y)` as the map plane, so sector lookup and sensor range used
  altitude. Capture now maps to `(x, -z)`; `test_world_fork` checks a unit two
  rows away at the same column and altitude stays fogged. In a network match
  the agent's `fork_observe` is pinned to the local player's owner.
* Documentation now states that resume tokens and the bot takeover/reclaim
  lifecycle are not reachable from the game yet (the host still destroys a
  dropped player, `windp::Reset` discards the client token, and no engine code
  drives `BotLifecycle`).

Re-run after the fixes on the same Linux host: 17/17 CTest (now including
`enet_transport`, `enet_znd` and `enet_znd_relay`), 10 map tests, 18 MCP
tests, package and library check green. Windows was not rebuilt in this pass;
`tools/windows/build.sh` now also builds the ENet suites.

