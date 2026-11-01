# Windows build branch

`windows-build` is the explicit Windows build branch. It shares engine behavior
and the local agent-control interface with `linux-build` and `six-factions`.
Platform branch names are build entry points; fixes should remain in all three.

Use the official MSYS2 MINGW64 environment and install:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-python \
  mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image \
  mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_net \
  mingw-w64-x86_64-openal mingw-w64-x86_64-libvorbis \
  mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-lua
bash tools/windows/build.sh /c/OpenNeoUA-Build-Lab/windows
```

Choose an external build directory. The script limits compiler concurrency to
two, builds the engine, runs C++ and Python tests, and creates a ZIP containing
the source overlay, executable, recursively required MinGW DLLs, license corpus,
launcher and hash manifest. It verifies every archived source/binary hash after
writing. Windows system DLLs are resolved against the build OS and not copied.

The ZIP requires original game data; it is not a complete game distribution.
Place the source overlay in a complete game's `Data` directory and executable/
DLLs beside it. Keep saves and profiles outside the source checkout.

The GitHub workflow can be started manually and also runs for relevant changes
on `windows-build`. It uses the official
[MSYS2 setup action](https://github.com/msys2/setup-msys2), with artifacts retained
for fourteen days. A successful hosted build is not a Windows gameplay test.

See [agent control](agent-control.md) for the optional MCP adapter and
[validation status](validation-status.md) for current evidence and limitations.
