# Local agent control

The Windows and Linux builds use the same opt-in gameplay interface. It is off
unless `OPENNEOUA_AGENT_DIR` names an existing absolute directory outside a Git
checkout. Use a separate directory for every running game. This is a local
inspect/act/observe interface; model latency still limits real-time play.

The Python 3.9+ adapter in `tools/agent/openneoua_mcp.py` exposes MCP over stdio.
It uses only the Python standard library. It has no HTTP listener, shell tool,
Lua evaluator, unit-spawning command or resource editor. An agent can operate
the ordinary game menus and controls and receive a rendered PNG plus local
player state. Enemy state is not enumerated. Use `game.new.debug = no` for normal
play: debug configuration changes some keyboard bindings.

## Start a game and connect

Windows PowerShell, after creating the chosen external directory:

```powershell
$env:OPENNEOUA_AGENT_DIR = 'F:\OpenNeoUA-Agent\session-one'
Set-Location 'F:\OpenNeoUA-BlackSect-Lab\six-factions'
.\OpenNeoUA.exe
```

Linux, with the directory already created:

```sh
export OPENNEOUA_AGENT_DIR=/var/tmp/openneoua-agent/session-one
cd /path/to/complete/game
./run-openneoua.sh
```

For several instances on one machine (one agent directory, virtual display
and runtime copy each, in a private network namespace), use
`tools/linux/live/drive.py`; see
[Linux build](linux-build.md#several-instances-on-one-machine).

Configure the MCP client to run the adapter on the same machine/filesystem:

```json
{
  "mcpServers": {
    "openneoua": {
      "command": "python",
      "args": ["/absolute/path/to/tools/agent/openneoua_mcp.py", "--directory", "/absolute/path/to/session-one"]
    }
  }
}
```

On Windows, use `python.exe` and Windows absolute paths. The exact client
configuration shape depends on the MCP host. No automatic registration or
permission change is performed by the game or adapter.

## Tools

| Tool | Behavior |
|---|---|
| `game_capabilities` | Reads the explicit engine session and supported operations. |
| `game_observe` | Captures a completed rendered frame and local-player, lobby and network state. |
| `game_status` | Returns the same state as `game_observe` without a screenshot, for cheap polling. |
| `game_key_press` | Holds a named key for 1–2000 ms, then releases it. |
| `game_mouse_move` | Uses screenshot pixel coordinates; optional relative `dx`/`dy` move the view. |
| `game_mouse_click` | Positions the pointer, then holds and releases left, middle or right at that pixel position. |
| `game_type_text` | Types 1–128 printable ASCII characters through the game text input. |
| `game_release_all` | Cancels and releases agent-held controls. |
| `game_fork_create` | Creates an isolated simulation fork of the live world for one owner. |
| `game_fork_step` | Steps a fork with one action (hold/move/attack/create/build). |
| `game_fork_observe` | Observes a fork, fog-filtered for one owner. In a network match the engine substitutes the local player's owner, so an agent cannot observe through another player's view. |
| `game_fork_commit` | Commits a fork action log into another fork by replay. |
| `game_fork_clone` | Clones a fork (same base state, new controller) so a commit into the clone can succeed. |
| `game_fork_discard` | Discards a fork without touching the live world. |

Fork tools are advertised in `session.json` only when the engine has a fork
session bound to the live world; they step the deterministic fork model, never
the live simulation, and commit only by replaying an action log fork-to-fork.

Observe after an action to establish its effect. A successful action receipt
means input finished processing, not that the intended button or unit was hit.
Mouse coordinates must fit the current screenshot. Text is paced across game
frames because the game's text queue is small; slow rendering takes longer.
Clicks emit an absolute motion event before the button press so legacy list
selection sees the requested position. This motion has zero relative movement;
use `game_mouse_move` with `dx`/`dy` when a camera turn is intended.

The optional `OPENNEOUA_AGENT_MOUSE_TEST=ON` CMake test (together with
`OPENNEOUA_AGENT_BRIDGE_TEST=ON`) checks this event order, release and bounds in
a hidden native OpenGL window. It requires a working video driver; on Linux,
run its CTest invocation under Xvfb. The ordinary file/queue tests do not require
a display.

## Lobby and network state

Since observation schema 2 (`observation_schema` in `session.json`), every
`observe` and `status` receipt carries a `net` object once the game world
exists. It is read-only and mirrors what the multiplayer screens and the
netlog show, so lobby, drop and rejoin checks need neither screenshots nor
log scraping:

| Field | Meaning |
|---|---|
| `lobby_screen` | Multiplayer screen (`netSelMode`): `provider`, `name`, `address`, `map`, `session`, `session_select`. |
| `menu_open` | The multiplayer menu is the active shell screen. |
| `role`, `transport` | `host`, `client` or `none`; `zndnet`, `enet` or `none`. Both are `none` without a transport object (before the host/join choice and after leaving a session). |
| `name` | Own callsign; once connected, the name the transport assigned (ZNDNet renames a duplicate to `name.NNNN`). |
| `is_host`, `level_id` | Lobby host flag and the chosen network level (0 = none). |
| `net_game`, `started` | `_isNetGame` and `_isNetGameStarted` (start barrier passed). |
| `local_owner`, `player_index` | Owner seated for this process at level load (0 before), and own transport roster index (-1 outside a session). |
| `can_start`, `problem` | Host: the roster check allows a launch. `netProblem` (0 = none). |
| `lobby` | Lobby rows in roster order: `slot`, `name`, `faction`, `ready`, `me`, `welcomed`, `conflict`. |
| `seats` | Per owner of a loaded level: `owner`, `name`, `ready`, `destroyed` (destroy flags), `status` (3 after an explicit quit), `latency_ms`, `silent_ms` (game time since the owner's last message; -1 for the own seat and before the start). |
| `bots` | Bots run by this process (`-bot`): requested `faction`, `difficulty`, lifecycle `phase` (`lobby`, `joining`, `ready`, `in_game`, `leaving`, `gone`, `takeover`, `reclaiming`) and the `owner` played (0 before the start). |

Factions are owner ids: 1 Resistance, 2 Sulgogars, 3 Mykonian, 4 Taerkasten,
5 Black Sect, 6 Ghorkov. Callsigns are escaped to plain ASCII JSON.

With unattended lobby flags (next section), `auto` adds their `step` and, once
they failed, the `error`.

## Unattended lobby flags

Opt-in command-line flags, next to `-bot`, walk the multiplayer screens
without clicks, so drills with three to six instances are scriptable. They
work without the agent bridge; with it, `net.auto` reports their progress.

| Flag | Effect |
|---|---|
| `-net-host <level>` | Choose Host, enter the callsign, pick the network level by id and open the lobby. |
| `-net-join <address>` | Choose Join, enter the callsign and connect to the host name or IPv4 address (port 61234). Failed attempts are retried every 2 s for 120 s; an address that does not resolve or a version refusal stops at once. |
| `-net-name <callsign>` | The callsign: no spaces, no `*`, at most 20 characters. Without it the profile's callsign is used and checked the same way. |
| `-net-faction <faction>` | Owner number 1-6 or `resistance`, `sulgogars`, `mykonian`, `taerkasten`, `black-sect`, `ghorkov`. A client chooses only after every other player's WELCOME arrived. With `-bot` and no `-net-faction`, the bot's faction is chosen when it is on the level and free; otherwise the lobby's choice stays. |
| `-net-ready` | Client: tick Ready once the faction has settled and the Ready box is enabled. |
| `-net-start-when <n>` | Host: press Start once at least n players are in the lobby, all of them ready, and the roster check has passed for 2 s. With n = 1 the host starts alone. |

```text
OpenNeoUA -net-host 132 -net-name HOST1 -net-faction resistance -net-start-when 3
OpenNeoUA -net-join 192.168.1.10 -net-name C1 -net-faction taerkasten -net-ready
OpenNeoUA -bot Ghorkov:Veteran -net-join 192.168.1.10 -net-name BOT1 -net-ready
```

The flags press only what a player could press at that moment (enabled
widgets) and go through the same handlers: the host/join choice, the name and
address fields' OK, the level list and `AfterMapChoose`, `ConnectToServer`, the
faction buttons, Ready (code 1208) and Start (code 1200). Each step is logged
as `netauto: ...` in `Data/Env/ypa_log.txt`; `net.auto.step` is one of
`wait_menu`, `provider`, `name`, `address`, `map`, `faction`, `ready`,
`in_session`, `wait_start`, `in_game` or `failed`. Bad input stops them with a
`netauto: ERROR:` line in the log and on stderr, and the game stays
interactive: an unknown, repeated or value-less flag, a callsign with a space
or more than 20 characters, an unknown faction or level, a faction that is not
on the level or already taken, or a level with fewer starts than
`-net-start-when`. A step that does not take effect stops them the same way,
for example a widget press that the screen has not taken within 15 s, or a
screen that changed before it took the press; once they have stopped they
press nothing more. They act once; after the match has started nothing more
happens. Without a `-net-*` flag nothing changes.

A failed connection attempt is logged with its reason in the netlog:
`Connection to ... failed: no_answer`, `full`, `name`, `version`, `dropped`
or `address`.

## Transport and failure handling

The engine creates `session.json` with a fresh session nonce. The adapter holds
an exclusive `adapter.lock`; the engine also prevents two active engines using
one directory. One request is in flight at a time. A request uses the fixed
`request.txt` path and this bounded ASCII line:

```text
1 SESSION ID EXPIRY_MS OP ARGS
```

`ID` is a positive increasing integer up to 2147483647. `EXPIRY_MS` is an absolute
Unix UTC timestamp in milliseconds. Expired requests do not start. Key and mouse
holds use an independent monotonic duration limit. The `text` operation encodes
printable ASCII as hexadecimal; filenames and arbitrary paths are not operands.

Same-directory temporary writes are renamed into place. Responses use
`response.json`, including session and request ID; screenshots use `frame.ppm`.
The adapter only accepts a matching receipt and converts the bounded PPM to
native PNG content. It does not automatically retry a timed-out action. An
unacknowledged request blocks the next ordinary command; `game_release_all`
can preempt it. A restart changes the nonce and never replays the old command.

This fixed-file design deliberately retains only the latest receipt. Archive
receipts externally if a replayable experiment log is required. Keep the session
directory private to its operator. If an adapter crashes, inspect its process
before manually removing a stale `adapter.lock`; the adapter never steals a lock.

The protocol follows the MCP [stdio transport](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports)
and [tool message](https://modelcontextprotocol.io/specification/2025-06-18/server/tools)
contracts. Protocol tests, engine integration, actual play, and multiplayer
replication are distinct validation stages; see the current validation notes.
