# Six-faction multiplayer development fork

This fork extends OpenNeoUA multiplayer to six simultaneous players, with one
player per faction: Resistance (owner 1), Ghorkov (6), Mykonian (3), Taerkasten
(4), Black Sect (5), and Sulgogars (2). Owner numbers remain the original game
data IDs; player slots are separate, zero-based indices.

It is based on TeuZzZ-17/OpenNeoUA commit
`ebf9f9f7abcb49387b2752e5855eb0098bf92ef9`. Credit for the engine remains with
TeuZzZ-17, the UA_source/OpenUA lineage, and the existing contributors. Coda
(Codex Lily), collaborating with Andrei Pemberton, developed this extension.
Lily (Lilitari) contributed independent source review, map/economy analysis,
and faction audio design feedback. Existing licensing and game-data notices apply.

## What changes

The lobby has six player rows and two additional faction controls. A faction
is selectable only when the chosen map declares a station for that owner.
Repeated or malformed faction selections prevent launch. Lobby initialization,
map changes, station creation, and faction availability share the same roster.
The lobby display and launch entry point share a roster check. Launch takes a
fresh snapshot before sending the load message, so an enabled button from an
earlier frame cannot certify selections or readiness that have since changed.
The existing peer negotiation and checksum policy are preserved.
When a WELCOME collision leaves no free faction, the player remains unselected
and cannot start; the engine does not broadcast an invalid zero-faction change.

The existing four host-station prototype choices are preserved. Black Sect and
Sulgogars use the `vehicle` value authored in their `begin_robo` map block; the
engine verifies that the prototype exists and is a host station. This supports
the original Anvil/Queen as well as explicitly authored mod stations without
assuming their prototype IDs.

Optional fifth and sixth values in `nw_vproto_num` and `nw_bproto_num` provide
technology rewards for Black Sect and Sulgogars. The historical first four
positions stay Resistance, Ghorkov, Mykonian, Taerkasten. Omitted extra positions
default to zero. A Sulgogar map should use its intended biological units and
construction rules; a human building list is not an implicit Sulgogar roster.

## Compatibility and maps

**All peers must run this fork and the same map/data variant.** The level-change
wire structure and lobby capacity have changed. The real transport handshake
uses the shared service identity `OpenNeoUA:six-factions:1`; host and client use
that identity before accepting application packets. Upstream clients use a
different identity and cannot join these sessions. Any separate session server
must use the same fork identity. The `-six-factions-mp1` build label is informational,
not the compatibility check. This separates incompatible protocols; it does not
authenticate players or change the inherited peer-trust design.

The handshake also carries a little-endian U16 wire protocol version
(`World::NetProtocol::ProtoVer`, currently 1) after the player name. Hosts
(`ZNDSingle`/`ZNDServer`) hard-reject missing or incompatible versions with
`ERR_CONN_VERSION`, so every peer must run a build with the same protocol
version; bump `ProtoVer` on any incompatible wire change.

Reconnect: `CONNECTED` carries a 128-bit resume token. A client that drops
without sending `DISCONNECT` is orphaned by the host for 60 seconds; reconnecting
with the token in the handshake restores the original transport identity and
session slot. Explicit leave/kick frees the slot immediately. Game-state resync
after rejoin uses the existing `REQUPDATE` snapshot flow. Inbound data packets
are token-bucket limited per source address (400 burst, 200/s sustained);
system packets are exempt.

Modernization status: protocol versioning, resume tokens, and rate limiting
above are on the wire today. The Phase 2 wire envelope (`src/game/yw_wire.h`)
and the Phase 3 fixed tick (`src/world/net_tick.h`, 20 Hz) and delta codec
(`src/world/delta_wire.h`, ForkDiff wire format) are implemented and
unit-tested but not yet cut over. The fixed tick is wired: world and shell
net flushes run through `NetTick` at the configured flush period. The
transport still runs ZNDNet struct
packets until the ENet swap (Phase 1) is built and accepted on a toolchain
machine. Phase 1 transport now exists beside it: vendored ENet 1.3.18
(`src/third_party/enet`) behind `INetTransport`/`EnetTransport`
(`src/netlib/transport.h`, `enet_transport.h`) with the same handshake,
resume-token, and two-channel contract, loopback-verified by
`test_enet_transport` (`OPENNEOUA_ENET_TEST=ON`, no SDL needed). The `windp`
provider still drives ZNDNet by default; set `OPENNEOUA_TRANSPORT=enet` to
run the engine on ENet through the `ZNDEnet` adapter (single-session direct
play, roster-synced user list). The adapter is loopback-verified
(`OPENNEOUA_ENET_ZND_TEST=ON`) and the engine builds and passes its full test
gate with it compiled in. Flipping the default awaits a live multiplayer
acceptance match on retail data.

Existing maps retain their original number of playable starts. Six-player maps
require six distinct stations, suitable starting territory, owner-specific build
permissions, and consistent technology rewards. Merely selecting Black Sect in
the engine cannot create those missing map resources. Keep generated variants
separate from original levels and use unused numeric level IDs.

Original Urban Assault assets are external dependencies. This source repository
does not relicense proprietary game data. Generated local variants must retain
their source hashes and transformation receipts.

`tools/make_six_faction_map.py` creates separate variants from locally supplied
original maps. It verifies the Anvil (62), Queen (61), Sulgogar units (71–74),
Queen turret (79), and weapon (72) against the supplied original scripts. It
requires an unused level ID and refuses to overwrite an existing map or receipt.

```text
python tools/make_six_faction_map.py <installation>/Levels/Multi/l100100.ldf <new-package> --script-root <installation>/DATA/SCRIPTS --level-id 200
python tools/make_six_faction_map.py <installation>/Levels/Multi/l140140.ldf <new-package> --script-root <installation>/DATA/SCRIPTS --level-id 201
```

Copy only the generated `Levels/Multi/*.ldf` files into the corresponding game
directory; leave `receipts` and inspection files outside the game's map folder.
The engine scans that folder for levels. Use the same generated files on every
peer. To remove the variants, remove only their uniquely named level files.

The Artificial variant keeps its 54×32 dimensions. The Crosshill variant extends
the arena from 16×16 to 21×16, with a third column of stations at the original
five-sector spacing. Both preserve the original four stations, add two separate
starts and starting pads, and repartition territory among six owners. Source
credits remain in the files. These are experimental scenario designs, with
unequal terrain and starting territory; they do not establish competitive balance.

The initial Black Sect build list comes from the latent `begin_enable 5` block in
the original Crosshill map. That list existed without a Black Sect host or engine
selection path; it was not a working six-faction implementation. The new maps
use that list and the source Resistance construction permissions. A broader
scavenged roster remains a design question. Sulgogars receive their four original
biological units and no human construction permissions. Their Queen receives a
map-local camera configuration, and weapon 72 explicitly uses the authored
5,000 ms AI interval for player firing instead of inheriting the 1,000 ms engine
default. Camera position and combat balance still need runtime observation.

The generator reads power-plant prototypes from `net_bldg.scr` and records
nominal power before and after territory reassignment. For compact arenas whose
four original factions have equal starting power, it adds verified plants in
empty owned cells to preserve that same amount for all six factions. Crosshill
therefore retains 192 nominal power per faction: Mykonians and Taerkasten receive
a compensating 128-power plant, and Black Sect and Sulgogars receive a preplaced
64-power captured plant. Existing plants, host cells, and the hosts' 3x3 landing
pads remain intact. This does not grant Sulgogars construction permissions.
The Artificial's original power distribution is asymmetric, so the generator
reports its values without imposing the compact-arena rule. Plant health,
terrain access, energy income and practical balance still need runtime checks.

## Validation

The standalone roster tests do not require SDL or original game data:

```text
cmake -S tests -B <external-build-directory>
cmake --build <external-build-directory>
ctest --test-dir <external-build-directory> --output-on-failure
python tests/test_six_faction_maps.py -v
```

These cover all one-byte faction inputs against all one-byte map masks,
duplicate availability, legacy order, and authored host selection. They do not
establish that six networked game clients have completed a match.
Transition cases cover a valid six-player roster becoming duplicated or unready
after a prior UI check, then recovering; missing map factions and invalid slots
are rejected, while one-player and original four-faction rosters remain valid.
WELCOME cases cover exhaustion, the remaining Black/Sulgogar choice, unchanged
valid selections and legacy priority. The optional transport regression uses
the same service identity as the actual provider and attempts the exact upstream
identity while the host has free slots, as well as six-peer delivery and capacity.

The map regression suite uses synthetic data and checks separate starts, preserved
original station blocks, consistent grid dimensions, neutral boundary sectors,
explicit roster/cadence/technology changes, six-way nominal power preservation
for symmetric arenas, and rejection of unsupported sources
or repeated conversion. Generated-map receipts preserve SHA-256 hashes of the
original level, scripts, and output. Grid diagrams are static inspection aids;
straight-line distances do not measure traversable paths or time to power.

Runtime acceptance requires six real client instances joining one session,
unique faction selection, ready/CRC completion, map loading, each faction
controlling its station and spawning its own units, replicated combat and
ownership changes, technology capture, disconnect handling, and match completion.
Include a six-player lobby with deliberately mismatched map data on all five
remote peers; the host must display all five CRC mismatch notices without an
overflow, and users must correct the mismatch before the gameplay acceptance run.
This specifically exercises the game-level mismatch-message buffer; the roster
and transport tests do not cover that path.
Record the executable/map hashes and distinguish local multi-instance testing
from testing across separate machines.

Development status must be updated from actual build, map inspection, and
runtime results before describing a release as tested or playable.
