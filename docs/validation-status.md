# Development validation status

Recorded 2026-11-01. These results concern the six-faction extension based on
upstream `ebf9f9f7abcb49387b2752e5855eb0098bf92ef9`.

* Windows x86-64 engine: full GCC 16.2 RelWithDebInfo build passed. Executable
  SHA-256 `64c6d2f208c6cd3834b13be0b8fb8a62f287b89948de16479923bdbeef25c9b9`.
* Ubuntu 26.04 x86-64 engine: full GCC 15.2 build passed. Executable SHA-256
  `dd741d9a6b455c17a057106a62e1f7ed913356ed31a129168edbfeccf13bbba9`.
  This local binary requires GLIBC 2.43 and is not the older Ubuntu CI build.
* Focused C++ tests: faction validation, speech routing/variation, and six-client
  loopback UDP transport passed with assertions enabled. The transport test uses
  the production handshake identifier, checks reliable multipart delivery,
  capacity, reconnect, and rejection of the original upstream identifier while
  slots remain available. These are transport endpoints, not six game clients.
* Python map generator: nine regression tests passed. Crosshill's six-player
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

Six real game clients, station controls, unit creation,
replicated combat, capture and completion remain separate runtime acceptance
work. The fork is a development candidate, not a completed multiplayer release.

The `linux-build` branch carries a manually invoked Ubuntu 22.04 container build
for an older userspace baseline. Its Actions result and downloadable artifact
must be checked independently. Future execution in another AI collaborator's
sandbox has not yet occurred.

The committed engine adds the verified startup config-path repair to the
reviewed six-faction candidate, plus two removed whitespace-only lines in
`yw_shell_ui_screens.cpp`. Original game data, generated retail-derived maps,
local build trees, private correspondence and recovery material are not included.
