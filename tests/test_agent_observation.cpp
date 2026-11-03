// Observation schema 2: the lobby/network JSON written under "net".
// Explicit checks (no assert), so the test also fails in NDEBUG builds.
#include "../src/agent/net_observation.h"
#include <cstdlib>
#include <iostream>
#include <string>

static void Require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main() {
    Require(Agent::JsonString("HOST1") == "\"HOST1\"", "plain callsign changed");
    Require(Agent::JsonString("a\"b\\c") == "\"a\\\"b\\\\c\"", "quote or backslash not escaped");
    Require(Agent::JsonString(std::string("x\ny\x01", 4)) == "\"x\\u000ay\\u0001\"", "control bytes not escaped");
    Require(Agent::JsonString("\xc3\xa9\x7f") == "\"\\u00c3\\u00a9\\u007f\"", "non-ASCII bytes not escaped");
    Require(Agent::JsonString(std::string("a\0b", 3)) == "\"a\\u0000b\"", "embedded NUL dropped");

    Agent::NetState empty;
    Require(Agent::NetStateJson(empty) ==
        "{\"lobby_screen\":\"provider\",\"menu_open\":false,\"role\":\"none\",\"transport\":\"none\","
        "\"name\":\"\",\"is_host\":false,\"level_id\":0,\"net_game\":false,\"started\":false,"
        "\"local_owner\":0,\"player_index\":-1,\"can_start\":false,\"problem\":0,"
        "\"lobby\":[],\"seats\":[],\"bots\":[]}", "empty state layout changed");

    Agent::NetState s;
    s.valid = true;
    s.lobbyScreen = "session";
    s.menuOpen = true;
    s.role = "client";
    s.transport = "enet";
    s.name = "C1";
    s.levelId = 200;
    s.netGame = true;
    s.playerIndex = 1;
    s.problem = 5;
    Agent::LobbyRow host; host.slot = 0; host.name = "HOST1"; host.faction = 1; host.ready = true; host.welcomed = true;
    Agent::LobbyRow me; me.slot = 1; me.name = "C\"1"; me.faction = 6; me.me = true; me.conflict = true;
    s.lobby.push_back(host);
    s.lobby.push_back(me);
    Agent::Seat seat; seat.owner = 6; seat.name = "C1"; seat.ready = true; seat.destroyFlags = 3; seat.status = 3;
    seat.latencyMs = 40; seat.silentMs = 1500;
    s.seats.push_back(seat);
    Agent::BotInfo bot; bot.faction = 6; bot.difficulty = "veteran"; bot.phase = "in_game"; bot.owner = 6;
    s.bots.push_back(bot);
    const std::string json = Agent::NetStateJson(s);
    Require(json ==
        "{\"lobby_screen\":\"session\",\"menu_open\":true,\"role\":\"client\",\"transport\":\"enet\","
        "\"name\":\"C1\",\"is_host\":false,\"level_id\":200,\"net_game\":true,\"started\":false,"
        "\"local_owner\":0,\"player_index\":1,\"can_start\":false,\"problem\":5,"
        "\"lobby\":[{\"slot\":0,\"name\":\"HOST1\",\"faction\":1,\"ready\":true,\"me\":false,\"welcomed\":true,\"conflict\":false},"
        "{\"slot\":1,\"name\":\"C\\\"1\",\"faction\":6,\"ready\":false,\"me\":true,\"welcomed\":false,\"conflict\":true}],"
        "\"seats\":[{\"owner\":6,\"name\":\"C1\",\"ready\":true,\"destroyed\":3,\"status\":3,\"latency_ms\":40,\"silent_ms\":1500}],"
        "\"bots\":[{\"faction\":6,\"difficulty\":\"veteran\",\"phase\":\"in_game\",\"owner\":6}]}",
        "populated state layout changed");
    for (const char c : json)
        Require(c >= 0x20 && c <= 0x7e, "observation JSON is not printable ASCII");
    std::cout << "PASS: net observation JSON layout and byte-safe escaping\n";
    return 0;
}
