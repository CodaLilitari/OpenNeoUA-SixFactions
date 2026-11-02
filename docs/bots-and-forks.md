# Agentic forks and multiplayer bots

Two features share one deterministic core: a forkable world state that both
local what-if analysis and server-side bot planning step through the same
API (`World::Fork::Fork::Step`). Bots additionally join multiplayer games as
ordinary clients of the existing protocol — same `OpenNeoUA:six-factions:1`
handshake identity, same UAMSG lifecycle as a human player. No bot gets a
privileged identity, and no fork ever touches the net driver, renderer or
audio.

## Adding bots to a running game

Start one extra game process per bot with the `-bot` flag, then join (or
host) exactly as a human would — lobby, faction pick, READY, CRC, LOAD:

```text
OpenNeoUA -bot Ghorkov:Veteran
OpenNeoUA -bot black-sect:cadet
OpenNeoUA -bot 2:0
```

The spec is `Faction:Difficulty`. Factions: `Resistance`, `Ghorkov`,
`Mykonian`, `Taerkasten`, `BlackSect` (also `black-sect`), `Sulgogars`, or a
numeric owner id (`1, 6, 3, 4, 5, 2`). Difficulties: `Cadet`/`0`,
`Normal`/`1`, `Veteran`/`2`. Once the net game starts, the process drives its
own station: the faction profile is applied through the normal robo wish
setters (`setROBO_epConquer` and friends) and re-steered every reaction
interval toward the order chosen by fork lookahead. Bots appear in the lobby
as ordinary players (name them e.g. `BOT Ghorkov (Veteran)`).

Each faction plays differently (weights in `src/world/net_bot.h`):
Resistance balanced; Ghorkov heavy conquest squads; Mykonian radar/recon and
ranged pressure; Taerkasten fortified turtle-then-counter; Black Sect
opportunistic sector theft; Sulgogars swarm with their biological roster and
no construction wishes. Difficulty only scales reaction time (1500/750/250
ms), sensor range (1800/2000/2160), lookahead depth (0/1/2) and order noise —
never information access.

### Leave, takeover, reclaim

- Leave: `BotRuntime::RequestLeave` broadcasts `UAMSG_EXIT` (norm=1), the
  same quit path as a human; the departing player's units follow the normal
  `yw_cleanPlayer` fate.
- Disconnect takeover: run a standby bot process with the same faction spec.
  When the human drops, the bot adopts that owner slot
  (`BotLifecycle::OnTakeoverOffer`), resyncs from the host snapshot
  (REQUPDATE/UPDATE) and resumes play. The brain only accepts takeover for
  its own configured owner.
- Human reclaim: the human rejoins the same owner slot; the bot exits first
  (`OnHumanReclaim` → EXIT) and the human resyncs before READY. A faction
  that was not in the starting roster is never created mid-match.

Bots may send rate-limited (≥20 s), faction-flavoured chat through
`UAMSG_MESSAGE`. Lines are validated by `ValidBotChatLine`: at most 63 chars,
printable, and never starting with a digit (the receiver diverts
leading-digit text to a numeric command path).

## Agentic forks

Headers (all SDL-free, covered by standalone tests):

| File | Contents |
| --- | --- |
| [fork_rng.h](../src/world/fork_rng.h) | Per-world splitmix64 RNG; state is one `uint64_t`, serialized with the snapshot |
| [fork_snapshot.h](../src/world/fork_snapshot.h) | `WorldState` (sector grid + gid-sorted units), strict binary encode/decode, FNV-1a hash |
| [fork.h](../src/world/fork.h) | `Fork::Create/ForkFrom/Capture/Restore/Step/Observe/Diff/TryCommit/Discard`, `ForkConfig` limits, `ForkRegistry` budget |
| [fork_protocol.h](../src/agent/fork_protocol.h) | `fork_create/step/observe/commit/discard` on the existing bounded agent line protocol |
| [net_bot.h](../src/world/net_bot.h) | Faction profiles, difficulty, fog-filtered decisions, fork lookahead, roster fill, lifecycle, chat rules |

Semantics:

- **Snapshot/restore**: `Capture()` returns hash + timestamps; `Restore()`
  only accepts states with the same map dimensions and clears the action
  log. Encoded snapshots round-trip bit-exactly (`EncodeState`/`DecodeState`).
- **Fork/step**: children are deep copies — stepping a child never disturbs
  the parent. `Step` validates the whole batch first (controller may only
  order its own units; create/build must be affordable in an owned,
  non-border sector) and applies nothing when any action is illegal.
- **Diff**: gid-merged created/destroyed/changed units plus sector
  owner/energy deltas, in wire-friendly shape.
- **Merge/commit**: commit-by-replay only. `TryCommit` re-validates the
  fork's recorded action log against the destination (on a copy first) and
  replays it; with the default `RejectIfStale` policy a destination whose
  hash moved on is refused — state bytes are never copied into an
  authoritative world, so net CRC checks still guard divergence.
- **Observe**: fog-filtered per owner — own units in full; enemy units only
  inside a visible sector or within sensor range of an own unit. Bots use
  exactly this view, so a bot can never attack what it cannot see.
- **Limits**: fixed step (40 ms default), per-call tick budget, unit cap
  (1024, matching the net snapshot caps), snapshot byte ceiling, and a live
  fork budget (8 per registry; discarded forks free their slot).

Engine bridge: `ForkBridge::CaptureWorldState` / `ValidateActions`
(`src/game/yw_fork.cpp`) translate the live `NC_STACK_ypaworld` (sectors via
`Sectors()`, unit hierarchy via `_unitsList`/`_kidList`) into fork state.
Bot lookahead (`NetBot::Decide`) forks that state, steps candidates, and at
Veteran depth also steps an assumed enemy reply before picking an order.

## Tests

```text
cmake -S tests -B <external-build-directory>
cmake --build <external-build-directory>
ctest --test-dir <external-build-directory> --output-on-failure
```

`world_fork` covers RNG/snapshot determinism, fork isolation, atomic illegal
batches, tick/unit limits, fog observation, diff, commit/stale rejection and
the agent fork protocol. `net_bot` covers pairwise-distinct faction
personalities, spec parsing, bot roster fill against `CheckRoster`, the
no-see-no-attack rule, deterministic decisions that never mutate the input
state, lifecycle/takeover/reclaim transitions and wire-safe chat.

Runtime acceptance still required (same gap as the six-client matrix in
[six-faction-multiplayer.md](six-faction-multiplayer.md)): a mixed
human+bot match with replicated combat/capture/completion, a mid-match
takeover drill and a human-reclaim drill on real builds.

## Bot station dispatch (engine integration note)

A bot process's station is the local station, so it carries the player-robo
and inputting flags. Without an explicit gate that silently routed it to the
human `User_layer` and skipped the native station AI entirely: fork
lookahead could steer wish weights forever and nothing would ever build,
move or fight.

Bot processes now set `NC_STACK_yparobo::_botControlled` from
`BotRuntime::Tick`. `NC_STACK_ypabact::UsesAIControl()` defaults to the
old behaviour (`!getBACT_inputting()`); the robo override returns true for
bot-controlled stations, and both the generic `AI_layer1`/`AI_layer2`
dispatch and robo `AI_layer1`/`AI_layer3` honour it
(`World::NetBot::StationUsesNativeAI` in `src/world/net_bot.h`).
Human-controlled stations keep the exact original player path; the only
behaviour change is inside `-bot` processes.

## Runtime acceptance status

Live-proven on real builds (see `docs/validation-status.md`):

- Bot client joins a hosted lobby, readies and enters a match as an
  ordinary player; disconnect of a client mid-match is handled by the
  host (waiting -> destroy player) while the match continues.
- Dual-bot headless match (Resistance:Veteran host vs Ghorkov:Veteran
  client on UNDER): both stations produce armies, conquer sectors, meet,
  damage and destroy each other's units with energies replicated on both
  clients; the losing station's destruction and the sector-control
  collapse were observed identically from both processes.
- Live fork create/step (Move)/clone/commit/discard against a running
  match.

Still open: a full six-bot roster match on the generated six-faction map,
a mid-match bot takeover drill and a human-reclaim drill on real builds.
Fork Attack/Create/Build steps remain covered by unit tests rather than
live commit drives.

