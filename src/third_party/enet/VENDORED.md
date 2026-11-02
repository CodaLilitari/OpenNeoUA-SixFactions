# Vendored ENet

- Upstream: https://github.com/lsalzman/enet
- Version: v1.3.18 (commit 2662c0d, "1.3.18 release prep")
- License: MIT (see LICENSE in this directory)
- `include/enet/enet.h` SHA-256: 1BF5EDCD7F197900131231087A1AEB45AA249BF23D3E8F3B5C5CF67DD4413193
- Contents: `include/enet/*.h` + top-level `*.c` (callbacks, compress, host,
  list, packet, peer, protocol + win32/unix platform), unmodified.
- Consumption: tests build these sources directly
  (`OPENNEOUA_ENET_TEST`); the engine links them at the Phase 1 transport
  cutover. Platform source is selected by the consumer (`win32.c` on Windows,
  `unix.c` elsewhere); do not compile both.
