# Transport recovery development checkpoint

This document describes the experimental `coda/rejoin-protocol2` branch. It is a
prerequisite for the [rejoin implementation contract](rejoin-design-review.md),
not a completed game-rejoin feature or a tested gameplay release.

## Compatibility and implemented scope

The branch uses protocol version 2 and ENet framing version 3. It is incompatible
with the published `dd80d607` platform checkpoint and earlier protocol layouts.
Use the same exact development revision on every peer. The supported transport
paths under test are direct-IP ZNDSingle/ZNDClient and the ENet adapter. The legacy
multi-session ZNDServer route refuses this protocol; it has not been upgraded.

Both transports now distinguish recoverable absence from terminal leave, kick,
expiry and session end. A temporarily absent participant retains its explicit
slot, identity and reservation. Authoritative roster snapshots carry session and
connection identity, a revision and bounded transition history. Acknowledgment
and retransmission repair missing control updates; receivers reject malformed or
inconsistent snapshots before applying them. History overflow requests
consolidation instead of inventing lost intermediate transitions.

Resume credentials and handshake identifiers come from checked operating-system
randomness. A credential can resume a reserved identity in a full or hidden
session. A successful new binding supersedes the old connection. Unknown,
expired or revoked credentials cannot silently become fresh admissions.
Ordinary transport Stop retains a credential; explicit cancellation revokes
local custody, invalidates the attempt, clears queued sends and rejects late
acceptance. These bearer credentials are not the future game-control reclaim
protocol and do not encrypt or authenticate arbitrary game state.

Incoming multipart allocation, raw queues, pending connections, control history
and per-poll work have explicit bounds. Source-address rate limits bound repeated
handshake response work. UDP terminal notification remains subject to packet
loss; a passing loopback test does not guarantee delivery across a network.

The provider retains the resolved endpoint and canonical identity outside a
failed transport object. Its frame-driven recovery policy has a fixed grace
window, bounded attempts and an explicit reclaim barrier that blocks outgoing
gameplay. Provider and lobby consumers use event-time slots rather than positions
in a compact roster vector.

**The game does not arm automatic recovery.** The provider API and transport
reservations do not establish that an absent player's world remains correct.

## Validation at checkpoint preparation

The Windows build of all affected standalone test targets passed after the final
cancellation correction. All 39 affected tests passed, including six cancellation
cases, ZND transport/control cases, lifecycle cases and the provider policy test.
The cancellation regressions first reproduced late credential reinstatement,
post-cancel retries, and a queued handshake surviving terminal cancellation.
Fresh admission, ordinary Stop custody and close-countdown traffic remain
positive controls.

A separately registered real ZND host/proxy/client test also passed on Windows.
It drops lifecycle rosters and applied acknowledgments, distinguishes repeated
snapshots of the same final revision from reliable-packet retries, checks exact
lifecycle delivery, and observes retransmission settle after the host receives
the applied acknowledgment. Bidirectional gameplay markers establish that both
endpoints remain live during the final quiet interval. This is a transport test,
not a game recovery test.

An independent harness compiled 43 actual provider methods with fake clock and
transport boundaries. Its unchanged 50-case suite passed after reproducing and
fixing repeated enable calls and a late direct reclaim call. All six changed
provider/game translation units also passed MinGW syntax compilation. These
checks do not replace a full engine link, actual socket integration or gameplay
recovery validation.

An earlier native Linux standalone run passed 78 tests before the final
cancellation changes. The final cancellation revision still needs Linux
verification; the earlier result is not evidence for the changed source.
The Python regression suite passed 78 tests on Linux from an external dependency
snapshot with shell-script line endings normalized. No audio listening or
gameplay acceptance is inferred from those tests.

CI results must be checked against the exact commit being considered. Building
an executable or passing transport tests does not close the gameplay gates below.

## Game identity prerequisite

The game now binds lobby and loaded owner rows to the transport session, player
ID and stable slot. Names are presentation labels. Initial host/join setup seeds
these bindings before peer announcements; terminal cleanup and deliberate new
connections clear them. The provider retains the accepted host/self/session
identity separately from current roster availability. A contradictory identity
cannot silently replace that binding. Transport events carry the session in
which they were queued, including events whose live connection getters have
already been cleared.

The existing host lobby map-selection route deliberately starts a fresh
transport session; it now clears both provider and game identity first while
preserving the selected map. Existing peers must rejoin that new session.
Unexpected identity changes during an ordinary session remain rejected.
Readiness also requires a live transport; retained ENet IDs after a direct Stop
do not make a stopped endpoint usable. A zero-session connection refusal is
accepted only during the provider's active connection attempt.

The receive loop validates the complete packet framing before dispatch and
rejects an unknown sender, wrong session or unauthorized control before
liveness, overload warnings or corruption-triggered repair work. LOAD,
LOBBYINIT, KICK, SCORE, REQPING, STARTPROBLEM and ENDPROBLEM require the retained
host. PONG and repair requests remain peer messages with their own typed rules.
KICK's owner identifies the victim; accounting belongs to the sending host.
Existing cross-owner combat messages are not subject to a blanket owner-equality
rule. SETLEVEL and NEWLEADER have no located reachable producer in this version
and are consumed without application.

An accepted LOAD freezes the local participant/faction snapshot before any
station is created. Seating and the start barrier use that snapshot, so a
newcomer cannot acquire an owner or increase the waiting count while a level is
loading. Delayed departure events cannot clear a replacement in the same slot.
Directed WELCOME, ping and repair messages and explicit removal use player IDs.
Host loss remains terminal; the legacy host-election event grants no authority.
A terminal latch cancels a pending launch and prevents later queued packets or
stale load-entry calls from reopening the session.

This snapshot is local. It does **not** prove that all peers acknowledged the
same canonical launch configuration, and it is not a checkpoint or a reclaim
transaction. Transport BACK/SELFRESUMED do not hand a world owner to a controller.
Full typed combat authorization, host-authored seat incarnations and epochs are
still pending. The new event metadata changes no wire layout; it does not add
encryption or a cryptographic identity mechanism.

Windows regression evidence includes 38 exact-source handler-prefix cases,
25 actual outer-loop/lifecycle cases and 15 terminal-session cases. The latter
reproduced acceptance after terminal events on frozen pre-fix source before
passing against the corrected source. CMake extracts the production functions
and records source hashes; native fixtures supply clock/queue/world boundaries.
The full outer loop and corruption scan execute, while downstream NORMAL
gameplay handlers are represented by an entry marker. These results do not
establish actual multiplayer gameplay, atomic world repair or recovery success.
Earlier validation counts above describe earlier checkpoints, not this change.

The provider's 50 extracted production methods passed 94 cases with fake clock
and adapter boundaries. Separate native transport regressions passed 65 tests,
including real loopback endpoints for both adapters and enqueue-time session
metadata. Neither a method fixture nor local socket traffic is a remote match.
A full native Windows Release engine compile and link also passed with unchanged
recorded source/configuration/resource inputs. The remaining gameplay gates
below still apply to that executable.

## Remaining game integration and acceptance

- Define and enforce host-authored seat ownership and authority epochs for every
  owner-state mutation, including directed and broadcast traffic.
- Integrate incoming gameplay filtering, checkpoint capture during normal play,
  complete world restoration and a bounded synchronization transaction.
- Start host AI from a valid checkpoint and transfer control back to a human only
  after the host commits the new authority state. Aborted handoffs must have an
  explicit policy and cannot permit two controllers.
- Reconcile consolidated rosters, game watchdogs, destruction, content mismatch,
  kick/revocation and host loss with that transaction.
- Run real matches on both transports, including at least three peers and a full
  six-seat recovery case. Test process loss, network interruption, competing old
  clients, duplicate claims, active construction/projectiles and failed handoff.
  Compare world state and control after handoff, not only connection status.

The six-faction maps, bots and voices remain fork features. No upstream PR has
been prepared from this branch. Any general-purpose upstream candidate must be
isolated and checked against upstream's current main separately.
