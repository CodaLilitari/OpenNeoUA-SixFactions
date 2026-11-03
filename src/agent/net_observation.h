// Lobby and network state reported by the agent "observe" and "status"
// operations (observation schema 2). Plain data plus a JSON writer, with no
// SDL or engine dependency, so the serializer is unit-testable on its own.
// The engine fills NetState in src/game/yw_netobserve.cpp.
#ifndef OPENNEOUA_AGENT_NET_OBSERVATION_H
#define OPENNEOUA_AGENT_NET_OBSERVATION_H
#include <cstdio>
#include <string>
#include <vector>

namespace Agent {
// One row of the multiplayer lobby, in transport roster order (the order the
// lobby screen lists players in).
struct LobbyRow {
    int slot = 0;            // transport roster index (lobby row)
    std::string name;        // callsign as the transport knows it
    int faction = 0;         // owner id: 1 Resistance, 2 Sulgogars, 3 Mykonian,
                             // 4 Taerkasten, 5 Black Sect, 6 Ghorkov; 0 none
    bool ready = false;
    bool me = false;         // this process's own row
    bool welcomed = false;   // a WELCOME from this player has been received
    bool conflict = false;   // the roster check flags this row (faction clash)
};
// Per-owner seat of a loaded network level (UserData::netPlayers).
struct Seat {
    int owner = 0;
    std::string name;        // callsign bound to the owner at level load
    bool ready = false;      // SYNCGM received (start barrier)
    int destroyFlags = 0;    // non-zero once the owner's station was removed
    int status = 0;          // 3 after an explicit quit (EXIT)
    int latencyMs = 0;
    int silentMs = -1;       // game ms since the owner's last message; -1 before start
};
// One bot controller run by this process.
struct BotInfo {
    int faction = 0;         // owner id requested with -bot
    std::string difficulty;  // cadet, normal, veteran
    std::string phase;       // BotLifecycle phase, snake_case
    int owner = 0;           // owner played in the running match; 0 before start
};
struct NetState {
    bool valid = false;               // false: no world yet, nothing is reported
    std::string lobbyScreen = "provider"; // UserData::netSelMode as a name
    bool menuOpen = false;            // the multiplayer menu is the active screen
    std::string role = "none";        // host, client or none (transport endpoint)
    std::string transport = "none";   // zndnet, enet or none
    std::string name;                 // own callsign (assigned by the transport once connected)
    bool isHost = false;              // UserData::isHost
    int levelId = 0;                  // chosen network level; 0 = none
    bool netGame = false;             // NC_STACK_ypaworld::_isNetGame
    bool started = false;             // NC_STACK_ypaworld::_isNetGameStarted
    int localOwner = 0;               // owner seated for this process (set at level load)
    int playerIndex = -1;             // own transport roster index; -1 outside a session
    bool canStart = false;            // host: the roster check allows a launch
    int problem = 0;                  // UserData::netProblem (0 = no problem)
    std::vector<LobbyRow> lobby;
    std::vector<Seat> seats;
    std::vector<BotInfo> bots;
};

// JSON string literal for arbitrary bytes. Callsigns come from other peers, so
// every byte outside printable ASCII is escaped and the output is plain ASCII.
inline std::string JsonString(const std::string &value) {
    std::string out = "\"";
    for (const char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c >= 0x20 && c <= 0x7e) out += static_cast<char>(c);
        else {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
            out += escaped;
        }
    }
    return out + "\"";
}

inline std::string NetStateJson(const NetState &s) {
    const auto flag = [](bool value) { return value ? "true" : "false"; };
    std::string out = "{\"lobby_screen\":" + JsonString(s.lobbyScreen) +
        ",\"menu_open\":" + flag(s.menuOpen) + ",\"role\":" + JsonString(s.role) +
        ",\"transport\":" + JsonString(s.transport) + ",\"name\":" + JsonString(s.name) +
        ",\"is_host\":" + flag(s.isHost) + ",\"level_id\":" + std::to_string(s.levelId) +
        ",\"net_game\":" + flag(s.netGame) + ",\"started\":" + flag(s.started) +
        ",\"local_owner\":" + std::to_string(s.localOwner) +
        ",\"player_index\":" + std::to_string(s.playerIndex) +
        ",\"can_start\":" + flag(s.canStart) + ",\"problem\":" + std::to_string(s.problem) + ",\"lobby\":[";
    for (size_t i = 0; i < s.lobby.size(); ++i) {
        const LobbyRow &r = s.lobby[i];
        out += (i ? ",{" : "{");
        out += "\"slot\":" + std::to_string(r.slot) + ",\"name\":" + JsonString(r.name) +
            ",\"faction\":" + std::to_string(r.faction) + ",\"ready\":" + flag(r.ready) +
            ",\"me\":" + flag(r.me) + ",\"welcomed\":" + flag(r.welcomed) +
            ",\"conflict\":" + flag(r.conflict) + "}";
    }
    out += "],\"seats\":[";
    for (size_t i = 0; i < s.seats.size(); ++i) {
        const Seat &seat = s.seats[i];
        out += (i ? ",{" : "{");
        out += "\"owner\":" + std::to_string(seat.owner) + ",\"name\":" + JsonString(seat.name) +
            ",\"ready\":" + flag(seat.ready) + ",\"destroyed\":" + std::to_string(seat.destroyFlags) +
            ",\"status\":" + std::to_string(seat.status) + ",\"latency_ms\":" + std::to_string(seat.latencyMs) +
            ",\"silent_ms\":" + std::to_string(seat.silentMs) + "}";
    }
    out += "],\"bots\":[";
    for (size_t i = 0; i < s.bots.size(); ++i) {
        const BotInfo &bot = s.bots[i];
        out += (i ? ",{" : "{");
        out += "\"faction\":" + std::to_string(bot.faction) + ",\"difficulty\":" + JsonString(bot.difficulty) +
            ",\"phase\":" + JsonString(bot.phase) + ",\"owner\":" + std::to_string(bot.owner) + "}";
    }
    return out + "]}";
}
} // namespace Agent
#endif
