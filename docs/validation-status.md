# Development validation status

Recorded 2026-11-01. These results concern the six-faction extension based on
upstream `ebf9f9f7abcb49387b2752e5855eb0098bf92ef9`.

* Windows x86-64 engine: full GCC 16.2 RelWithDebInfo build passed. Executable
  SHA-256 `c527df3b57c1b3ce23972a3506cbc84f259049446e1d62df43528b6cda04ac30`.
* Ubuntu 26.04 x86-64 engine: full GCC 15.2 build passed. Executable SHA-256
  `24ff21ab05cce807b742dc58fab8f095cb414df0d4006eda00319bb47e9b8cc3`.
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

Native game startup, six real game clients, station controls, unit creation,
replicated combat, capture and completion remain separate runtime acceptance
work. The fork is a development candidate, not a completed multiplayer release.

The `linux-build` branch carries a manually invoked Ubuntu 22.04 container build
for an older userspace baseline. Its Actions result and downloadable artifact
must be checked independently. Future execution in another AI collaborator's
sandbox has not yet occurred.

The reviewed local candidate and committed source differ only by two removed
whitespace-only lines in `yw_shell_ui_screens.cpp`, documentation, and workflow
tracking configuration. Original game data, generated retail-derived maps,
local build trees, private correspondence and recovery material are not included.
