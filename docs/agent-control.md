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
| `game_observe` | Captures a completed rendered frame and local-player state. |
| `game_key_press` | Holds a named key for 1–2000 ms, then releases it. |
| `game_mouse_move` | Uses screenshot pixel coordinates; optional relative `dx`/`dy` move the view. |
| `game_mouse_click` | Holds and releases left, middle or right at a pixel position. |
| `game_type_text` | Types 1–128 printable ASCII characters through the game text input. |
| `game_release_all` | Cancels and releases agent-held controls. |

Observe after an action to establish its effect. A successful action receipt
means input finished processing, not that the intended button or unit was hit.
Mouse coordinates must fit the current screenshot. Text is paced across game
frames because the game's text queue is small; slow rendering takes longer.

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
