# Rejoin implementation contract: review checkpoint

Recorded 2026-11-08 against source `e7b267a`. This is a design checkpoint for
unfinished work, informed by Red's subsystem analysis and a subsequent review by
Coda and Lily, AI collaborators. It does not describe a working game-rejoin
feature. The current game still removes a disconnected player.

## First prerequisite: endpoint lifecycle

The first change makes endpoint shutdown and same-object restart safe. Stop must
dispatch through `ZNDNet*`, join workers before replacing sockets or destroying
client members, and clear all old packet/event/session queues. Tests use real
loopback endpoints after orderly stop, refusal, timeout and remote disconnect.
An opaque transport token surviving Stop does not establish game recovery.

This prerequisite changes neither the wire format nor the game's seat policy.

## Seat identity and authority

After launch, six fixed game seats must be independent of transport roster order.
A seat retains its owner and units across a recoverable loss. The host decides
its controller; individual peers do not independently elect an AI controller.

Every owner-state mutation needs a binding to the controlling connection and an
authority epoch. An epoch only on a seat-status announcement cannot identify old
queued unit packets. For each seat and effective simulation tick, at most one
controller may produce accepted state. Snapshot receivers cannot send owner
traffic before the final handoff commits.

Host loss ends the match. Host migration is outside this increment.

## Admission to a full match

Rejecting every new transport identity after launch prevents a restarted client
from presenting its saved reclaim credential. A bounded pending-resume connection
must exist outside the six gameplay seats, or authentication must occur within the
transport handshake itself. Pending connections receive no gameplay broadcasts,
cannot send gameplay mutations, have a short deadline, and consume no player row.

Reclaim records must bind to a match incarnation, seat, content/protocol identity
and credential generation. A kick revokes the old generation and pending claims.
Two candidates for one seat must not race into simultaneous authority.

The concrete credential protocol remains a design gate. A sketch containing both
`Hash(secret)` on the host and a challenge MAC made with the original secret does
not itself explain how the host verifies the proof. Select a standard construction
with explicit verifier storage, replay protection, rotation, and recovery after a
lost commit acknowledgment; do not invent a cryptographic primitive.

## Envelope and sequences

The earlier proposed fields total 18 serialized bytes, not 16. A later proposal
adds stream, seat and authority epoch and totals 24 bytes. That proposal is also
different from the currently unused 24-byte `yw_wire` helper, which has type and
acknowledgment fields. The final cutover needs an explicit offset table, endian
rules, golden vectors, bounds checks and a version change on both transports.

Directed traffic and broadcast traffic cannot share a gap-detecting sequence
counter: other recipients never receive directed frames. Counter identity must
include recipient/stream, reliability class and authority generation. This also
applies to directed reconnect/control traffic; assigning it one global control
stream would repeat the same false-gap defect.

An unseen late reliable frame is not necessarily a duplicate. Define bounded
reordering, duplicate detection and repair for the actual transport ordering
guarantees. A unit-tree update alone cannot repair a lost sector/building change.

## State available before a crash

Remote transforms are not a complete station checkpoint. Batteries, budgets,
construction, timers, command/entity counters and other local simulation state
must reach the host during normal play, before the owner disappears. Capture and
acknowledge a complete checkpoint at a defined frame boundary, then account for
accepted changes after that checkpoint. Audit the production state inventory
before claiming a complete schema.

If the host lacks a valid recent checkpoint, an explicit failure policy is
required. Starting AI from old level-start batteries would silently change the
match. The policy must not leave an indefinitely invulnerable, unsimulated seat.

## Snapshot and handoff transaction

The host remains the controller during bulk transfer. Use a bounded immutable
snapshot plus subsequent accepted changes, with transfer identity, schema/content
identity, total size, chunk count, digest and deadlines. Restore only a complete,
validated transfer; suppress ordinary creation broadcasts and other restore-time
side effects. Preserve entity identities and advance allocation counters.

A short final synchronization barrier is a simpler first increment than claiming
fully pause-free handoff. A final commit selects the new epoch and effective tick.
Abort, candidate loss or transfer timeout leaves host AI in control. Destruction
during transfer aborts the claim. Duplicate acknowledgments and lost commit
messages must not enable two controllers or permanently strand the credential.

## Acceptance remains pending

Both transports need real matches with at least three peers, including a full
six-seat resume case. Exercise a network interruption, process crash/relaunch,
still-running old client, duplicate claim, kick/revocation, destruction during
transfer, content mismatch, active construction/missiles, failed handoff, and
host loss. Compare post-handoff state and control, not merely socket connection.

Transport regression tests, build success and this review do not close those
gameplay acceptance gates.
