# Optional Linux runtime dependency bundle

`tools/linux/bundle-runtime.py` produces an additional archive for a trusted Linux x86_64/glibc build. It includes the executable, the same clean source/configuration/assets selected by `package.py`, resolved non-glibc libraries, their available Debian package/copyright metadata, and a relative launcher. Original Urban Assault game data is not included.

The release profile is built on Ubuntu 22.04. A build on another distribution records that distribution and its actual libraries; it is not equivalent to the Ubuntu 22.04 profile. This is a relocatable dependency bundle, not a promise to run on every Linux system.

## Build and package

Use the existing `tools/linux/build.sh` to build into an external directory. The helper requires Python 3.10+, `readelf` from `binutils`, and `ldd` from `libc-bin`; `dpkg-query` supplies package provenance on Ubuntu. All runtime dependencies must already be installed and resolvable on the build host. The helper installs or downloads nothing.

For the optional software graphics profile, install `libgl1-mesa-dri` and `libglx-mesa0` in the Ubuntu build environment. Existing development dependencies will usually provide their other prerequisites. Then, for example:

```sh
python3 -B tests/test_linux_runtime_bundle.py -v
python3 tools/linux/bundle-runtime.py \
  --source "$GITHUB_WORKSPACE" \
  --binary "$RUNNER_TEMP/openneoua-linux/engine/OpenNeoUA" \
  --output "$RUNNER_TEMP/openneoua-linux/package/OpenNeoUA-linux-runtime.tar.gz" \
  --epoch "$SOURCE_DATE_EPOCH" \
  --mesa-swrast /usr/lib/x86_64-linux-gnu/dri/swrast_dri.so
```

Adjust the external build path to the one supplied to `build.sh`. Omit `--mesa-swrast` for the basic dependency bundle. When Mesa is requested, the helper also includes `libGLX_mesa.so.0` from the directory above `dri/`; `--mesa-glx /absolute/path/libGLX_mesa.so.0` overrides that location. Both modules and their resolved dependencies are included. Additional explicit library roots can be allowed with repeated `--library-root /absolute/path` arguments; the normal `/lib`, `/lib64`, and `/usr/lib` roots are always allowed.

Outputs are the `.tar.gz`, its `.json` receipt and `.sha256` file. The archive contains `RUNTIME-INFO.json`, which records source revision, build distribution, ELF requirements, resolved library paths, SHA-256 hashes, and available package versions and copyright notices. Missing Debian package metadata is recorded as unavailable rather than invented. Copyright notices are retained under `licenses/`; the manifest provides the package/version identity needed to locate corresponding upstream or distribution source packages.

Archive order, timestamps, ownership and modes are normalized. Identical source/binary/library bytes, resolved provenance metadata and epoch produce the same archive bytes. Library upgrades, source revisions, build distributions or provenance paths are meaningful input changes.

## Boundaries enforced by the helper

- The binary, output and temporary archive are outside the source tree and any Git checkout. Existing output files are never overwritten.
- Clean source is selected from the existing packager's allowlist. Retail `Scripts/STARTUP.SCR`, a merged `Data` directory, unsafe source symlinks and common generated dependency/build directories are rejected.
- `readelf` checks the architecture and interpreter before `ldd` is used. Package only a trusted build; `ldd` is not a safe inspection tool for an arbitrary downloaded executable. Its dependency output includes transitive libraries. See the [ldd manual](https://man7.org/linux/man-pages/man1/ldd.1.html).
- Missing dependencies, conflicting library names, escaping library symlinks, unsupported path-bearing dependencies and host-bound RPATH entries fail packaging. RUNPATH entries are recorded and allowed because the launcher's bundled `LD_LIBRARY_PATH` takes precedence; every declared dependency must still resolve in the audited bundled/host closure. Allowed system-library symlinks are dereferenced into ordinary archive files; no symlink is emitted.
- The glibc runtime family and ELF loader stay on the target host. Replacing the host libc/loader with mismatched copies is not part of this bundle. Library search and RPATH precedence are described in the [dynamic loader manual](https://man7.org/linux/man-pages/man8/ld.so.8.html).

## Running without root

Extract the archive into a writable external directory, then assemble licensed original UA assets using the Data-first instructions in the main README. Preserve original scripts, copy original data first and overlay the bundle's `Data` contents second. Do not nest `Data` inside `Data`.

Run `./run-openneoua.sh` from the extracted `OpenNeoUA-linux-runtime` folder. The launcher sets its own working directory and prepends its relative `lib/` directory to `LD_LIBRARY_PATH`, forwarding all arguments. The Mesa profile also sets `LIBGL_DRIVERS_PATH`, selects Mesa GLX, defaults software rendering on, and defaults `LP_NUM_THREADS` to 2. Existing thread/software-rendering overrides are respected. Mesa documents its environment switches in the [Mesa environment reference](https://docs.mesa3d.org/envvars.html).

The target still needs a compatible x86_64 Linux kernel, sufficiently new glibc/loader, an X server and suitable writable paths. The bundle does not install Xvfb, fonts for the X server, GPU drivers, kernel interfaces, input/capture tools or original game data. A container/sandbox with an older libc cannot be repaired by this launcher. Modules loaded through application-specific `dlopen` calls are outside ordinary ELF dependency discovery; the Mesa swrast and GLX modules are included explicitly for this reason.

For an existing authorized Xvfb sandbox, the tested profile uses `SDL_VIDEODRIVER=x11`, `LIBGL_ALWAYS_SOFTWARE=1` and `LP_NUM_THREADS=2`. `ALSOFT_DRIVERS=null` and `SDL_AUDIODRIVER=dummy` can silence a smoke run, but such a run does not validate audible sound. Supply `DISPLAY` and X authentication from the existing sandbox setup. Use separate writable `Data/ENV`, `Data/SAVE`, configuration and XDG state for each client; shared read-only bulk assets are an independent runtime setup choice outside Git.

The helper and its tests establish packaging behavior. Final Ubuntu 22.04 archive execution, rendering, input, sound and multiplayer require separate runtime checks. Keep their receipts distinct from compilation or archive integrity.
