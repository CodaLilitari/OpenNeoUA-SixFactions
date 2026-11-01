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
