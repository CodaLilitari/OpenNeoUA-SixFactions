# Linux build and packaging

This path builds the six-faction engine without original Urban Assault data.
It produces an x86-64 Linux executable, the repository's source/configuration/
resources, license and dependency/hash records. It does not supply the original
game assets and does not establish that a six-player match has been completed.

## Build locally

Use a clean source checkout, not a directory merged with retail game data.
On Ubuntu 22.04 or a newer Ubuntu installation, install the official packages:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential cmake ninja-build pkg-config binutils python3 libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-net-dev libopenal-dev libvorbis-dev libgl1-mesa-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev liblua5.3-dev
```

From the clean checkout:

```sh
export SOURCE_DATE_EPOCH=0
bash tools/linux/build.sh /var/tmp/openneoua-build-lab
```

The helper permits one or two compiler jobs (`JOBS=1` or `JOBS=2`). Its build,
tests, temporary files, logs and package remain outside the source. Do not
symlink those generated directories back into the checkout.

The engine uses Release with `-O2`; the focused tests use Debug so assertions
are enabled. The helper runs the C++ faction, speech and real loopback transport
tests, the Python map regressions, and a dynamic-library resolution check.

Results appear under the external build directory:

* `engine/OpenNeoUA`: native executable.
* `logs/`: configure/build/test output and resolved runtime libraries.
* `package/OpenNeoUA-linux.tar.gz`: executable plus repository content.
* `package/BUILD-INFO.json`: compiler, operating system, required libraries,
  GLIBC symbol versions, source-file hashes and executable hash.
* Archive `.json` and `.sha256` sidecars: size and SHA-256 receipt.

`SOURCE_DATE_EPOCH` controls compiler date macros and normalized archive dates.
Zero is an explicit reproducible default, not a claim about the source's age.
Set `SOURCE_REVISION` to the actual commit ID when building outside CI. The
packager normalizes ordering, ownership, modes and gzip metadata: identical
source, executable and build-environment metadata produce identical archives.
It does not claim byte-identical executables across different compiler versions.
Choose a new package filename when rerunning the packager; it refuses overwrite.

## CI baseline

The workflow also creates an optional [runtime dependency bundle](linux-runtime-bundle.md)
with audited non-glibc libraries and Mesa software-rendering modules. It retains
the host glibc/loader and X-server requirements; original game data is supplied
separately. This is the more useful starting point for a sandbox without package
installation access.

The manual `Linux build and source package` workflow uses an Ubuntu 22.04
container on an Ubuntu 24.04 hosted runner. This retains the older userspace
baseline while avoiding the retiring Ubuntu 22.04 runner image. It requires
read-only repository permission, uploads a temporary Actions artifact and does
not publish a release. The workflow uses the same helper as local builds.

A local Ubuntu 26.04 result is evidence only for that environment. Its binary
can require newer GLIBC or shared-library versions than the Ubuntu 22.04 build;
inspect `BUILD-INFO.json` before moving it to an older sandbox. A successful
workflow run and its downloaded artifact must be verified separately.

Official references: [runner images](https://github.com/actions/runner-images),
[Ubuntu 22.04 FFmpeg development package](https://packages.ubuntu.com/jammy/libavcodec-dev),
and [Ubuntu 22.04 Lua 5.3 development package](https://packages.ubuntu.com/jammy/liblua5.3-dev).

## Supply original data and run

Extract the archive into a separate writable runtime directory. The top level
contains `OpenNeoUA`, `run-openneoua.sh`, `BUILD-INFO.json` and `Data/`.
Merge a legitimate original installation's DATA contents into `Data/`, retaining
the repository configuration/resources from the archive. Also supply the
original LEVELS, LOCALE, ENV, SAVE, HELP, MUSIC and NUCLEUS.INI where present,
following the layout explained in the main README. Preserve the original
installation and avoid accidental `Data/Data` nesting or directory pairs that
differ only by case on Linux.

Supply the same generated six-faction map variants and original gameplay data
on all peers. Use the six-faction documentation for map generation. Generated
retail-derived maps are not embedded in this clean-source package.

Run from the extracted directory:

```sh
bash run-openneoua.sh
```

The launcher sets the correct working directory. Runtime dependencies are
dynamically linked system libraries; this is not a self-contained AppImage.
Use the listed Ubuntu dependency packages or matching runtime packages for the
exact build distribution. Do not copy another distribution's GLIBC into it.

## Headless smoke and future sandbox testing

For an approved local smoke test, Xvfb and software rendering can keep the test
off the user's visible display:

```sh
sudo apt-get install --no-install-recommends xvfb xauth mesa-utils
LIBGL_ALWAYS_SOFTWARE=1 ALSOFT_DRIVERS=null timeout 30s xvfb-run -a -s '-screen 0 1280x720x24' bash run-openneoua.sh
```

A timeout can mean the process remained running; it does not prove the menu
rendered or gameplay worked. Preserve startup logs, inspect the virtual display
when authorized, and record the executable and data hashes. Menu visibility,
station control, combat and six-peer networking are separate acceptance steps.

This branch prepares a Linux artifact for later GPT-6 Pro testing if that
environment becomes available and permits it. It does not claim future access,
compatible graphics/network capability, a completed upload, or a gameplay test
in that environment.

## Several instances on one machine

`tools/linux/live/` runs scripted network matches on one Linux box: every
instance gets its own virtual display and runtime copy, all of them inside a
private network namespace, and the [unattended lobby flags](agent-control.md#unattended-lobby-flags)
walk the lobby. It needs Xvfb, Mesa (llvmpipe), `unshare` and `nsenter`
(util-linux), Python 3.10 or newer and a complete retail package: the engine
stops at init without `LOCALE/*.DLL`, `DATA/SET1`..`SET6`, `DATA/SET46`,
`DATA/FONTS` and `DATA/MC2RES`.

One command starts a three-player match (host, client and a `-bot` client),
waits until all three passed the start barrier, collects the logs and stops
everything again:

```sh
python3 -B tools/linux/live/drive.py match \
    --retail "/path/to/OpenUA Standalone Package" \
    --binary /var/tmp/openneoua-build-lab/engine/OpenNeoUA \
    --lab /var/tmp/openneoua-live --level 100 \
    --host HOST1:taerkasten --client C1:mykonian --bot BOT1:ghorkov:veteran
```

The exit code is 0 only when every instance reached the game. The run
directory (outside any Git checkout) keeps the runtimes, `logs/`, `pids/`,
one agent directory per instance and `collected-<stamp>/`, whose
`net-summary.txt` has the netauto, connection, roster and READY lines of every
instance. Options: `--transport enet` (set for every instance), `--keep` to
leave the match running for drills, `--sample 10` for a CPU/RSS log
(`logs/resources.csv`), and `--fresh` to assemble the runtimes again (after a
change of the retail package or the repository's data; the binary under test
is copied into every runtime on each run). Levels 200 and 201 are the
generated six-start maps; `match` generates them from the retail levels into
`maps/` of the run directory on first use, and on every run copies the levels
there into runtimes assembled before, so one run directory serves any level.
`drive.py status --lab DIR` prints every instance's lobby, seat, bot and flag
state; `drive.py stop --lab DIR` stops a kept match.

What these tools do, and what any other multi-instance setup must do too:

* **One runtime copy per instance** (`assemble_runtime.py`, then real copies).
  The engine writes `Data/Env`, `Data/Save` and its INI files. Its file system
  ignores symlinks and takes the first case-insensitive match, so retail
  `SCRIPTS`, `LEVELS`, `LOCALE`, ... and the repository's `Scripts`, `Levels`,
  `Locale`, ... are merged into one directory each.
* **Unattended settings**: `gfx.movie_player = no`, `menu.windowed = on` and
  `game.new.debug = no`. Retail `DATA/MOV` is left out by default: the first
  load of the retail SDU7 profile resets the options and writes
  `gfx.movie_player = yes` back, after which the intro would hold the menu,
  and the agent bridge, for about a minute.
* **One Xvfb per instance** (`launch_instance.sh`): focus changes on a shared
  display cancel agent requests. Software rendering with `LP_NUM_THREADS=1`
  and null audio.
* **A private network namespace** (`ns_holder.sh`; enter it with
  `nsenter -t PID -n`). UDP port 61234 is fixed, so a second match or another
  program on the box would collide, and the namespace keeps `iptables`
  partition drills off the host network.
* **Unique callsigns** without spaces, at most 20 characters.
* **Logs**: the engine truncates `Data/Env/ypa_log.txt` (log and netlog) at
  every start; `launch_instance.sh` archives the previous one in `logs/`
  before it starts an instance again.

Drills on a match started with `--keep` (`LAB` is the run directory):

* Freeze a client: `kill -STOP $(cat LAB/pids/C1.pid)`, later `kill -CONT`.
* Partition it: `python3 -I tools/linux/live/udp_ports.py $(cat LAB/pids/C1.pid)`
  prints its UDP port; then
  `nsenter -t $(cat LAB/pids/netns.bgpid) -n iptables -I INPUT -p udp --sport PORT -j DROP`
  (and the same with `--dport`); delete both rules with `-D` to heal it.
* Crash and relaunch it: `kill -9 $(cat LAB/pids/C1.pid)`, then
  `nsenter -t $(cat LAB/pids/netns.bgpid) -n bash tools/linux/live/launch_instance.sh C1 $(cat LAB/pids/C1.display) LAB/rt-C1 LAB -net-join 127.0.0.1 -net-name C1 -net-ready`.
  The instance's Xvfb and transport are reused (without `OPENNEOUA_TRANSPORT`
  the relaunch takes the one in `pids/C1.transport`, so an ENet match stays on
  ENet; set the variable to override it), and the crashed engine's log is
  archived in `logs/` first. Rejoining a running match does not exist yet (the
  rejoin work in the netcode design): today the relaunched client enters the
  still open session as a new player, and a client without a loaded level can
  crash on the first in-game sector message. Leave out `-net-join` to bring
  it back to the menu only.

Measured on a shared 2-vCPU, 8 GB box on 2026-11-02 with the retail package:
about 160 MB resident per instance in the menu. Two instances on level 132
(ZNDNet and ENet), and three (host, client, `-bot` client) on level 100
(ZNDNet and ENet) and on the generated levels 200 and 201 (ZNDNet), reached
the game about ten seconds after launch. These runs stop at the start; they
are not played matches. Six instances need a bigger machine.
