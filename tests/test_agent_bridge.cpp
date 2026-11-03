#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#undef main
#include "../src/agent/bridge.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static void check(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
static std::string read(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
int main(int argc, char **argv) {
    check(argc == 2, "external fixture directory required");
    SDL_SetMainReady();
    check(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL event initialization failed");
    SDL_setenv("OPENNEOUA_AGENT_DIR", argv[1], 1);
    const std::string root = argv[1];
    Agent::Init();
    const auto meta = read(root + "/session.json");
    const auto marker = meta.find("\"session\":\"");
    check(marker != std::string::npos, "session metadata missing");
    const std::string session = meta.substr(marker + 11, 32);
    check(session.size() == 32, "invalid session token");
    const auto request = [&](const std::string &body, const std::string &token) {
        std::ofstream file(root + "/request.txt", std::ios::binary | std::ios::trunc);
        const auto space = body.find(' ');
        file << "1 " << token << ' ' << body.substr(0, space) << " 9999999999999" << body.substr(space) << '\n'; file.close();
        SDL_Delay(30); Agent::BeforeEvents();
    };
    const auto count = [](Uint32 kind) {
        SDL_Event event; int matches = 0;
        while (SDL_PollEvent(&event)) if (Agent::EventFilter(nullptr, &event) && event.type == kind) ++matches;
        return matches;
    };
    Agent::Snapshot state; state.screen = "menu";
    request("1 key 4 1", session);
    check(count(SDL_KEYDOWN) == 1, "key down not delivered through SDL");
    check(read(root + "/response.json").empty(), "hold acknowledged before processing");
    Agent::AfterFrame(state); SDL_Delay(3); Agent::BeforeEvents();
    check(count(SDL_KEYUP) == 1, "bounded hold did not automatically release");
    check(read(root + "/response.json").empty(), "release acknowledged before next processed frame");
    Agent::AfterFrame(state);
    const auto first = read(root + "/response.json");
    check(first.find("\"id\":1,\"ok\":true") != std::string::npos, "completed key not acknowledged");
    check(first.find("\"applied_frame\":1,\"completed_frame\":2") != std::string::npos, "receipt frame boundary is incorrect");
    request("7 key 4 1", "ffffffffffffffffffffffffffffffff");
    check(count(SDL_KEYDOWN) == 0 && read(root + "/response.json") == first, "wrong session executed or overwrote response");
    request("1 key 4 1", session);
    check(count(SDL_KEYDOWN) == 0 && read(root + "/response.json") == first, "duplicate key reexecuted");
    request("2 text 414243", session);
    std::string actual;
    for (int i = 0; i < 3; ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (Agent::EventFilter(nullptr, &event) && event.type == SDL_TEXTINPUT) actual += event.text.text;
        check(actual.size() == static_cast<size_t>(i + 1), "text was not paced by rendered frames");
        Agent::AfterFrame(state); Agent::BeforeEvents();
    }
    check(actual == "ABC", "text characters lost or reordered");
    check(read(root + "/response.json").find("\"id\":2,\"ok\":true") != std::string::npos, "text completed before or after incorrect frame");
    request("3 key 5 2000", session); check(count(SDL_KEYDOWN) == 1, "second hold missing");
    request("4 release", session); check(count(SDL_KEYUP) == 1, "release did not preempt held input");
    Agent::AfterFrame(state);
    check(read(root + "/response.json").find("\"id\":4,\"ok\":true") != std::string::npos, "release acknowledgment missing");
    request("5 observe", session); Agent::AfterFrame(state);
    check(read(root + "/response.json").find("screenshot_failed") != std::string::npos, "headless observation invented screenshot success");
    request("6 key 6 2000", session); check(count(SDL_KEYDOWN) == 1, "retry test hold missing");
    SDL_SetEventFilter([](void *, SDL_Event *event) { return event->type >= SDL_USEREVENT ? 0 : 1; }, nullptr);
    request("7 release", session);
    SDL_SetEventFilter(nullptr, nullptr);
    Agent::AfterFrame(state); Agent::BeforeEvents();
    check(count(SDL_KEYUP) == 1, "failed release was not retried after dispatch became available");
    Agent::AfterFrame(state);
    check(read(root + "/response.json").find("\"id\":7,\"ok\":true") != std::string::npos, "retried release missing acknowledgment");
    request("8 key 7 2000", session); check(count(SDL_KEYDOWN) == 1, "override test hold missing");
    SDL_Event human = {}; human.type = SDL_KEYDOWN; human.key.keysym.scancode = SDL_SCANCODE_A;
    check(SDL_PushEvent(&human) == 1, "physical-event fixture failed");
    std::string order;
    while (SDL_PollEvent(&human)) if (Agent::EventFilter(nullptr, &human)) {
        if (human.type == SDL_KEYUP) order += 'u';
        if (human.type == SDL_KEYDOWN) order += 'd';
    }
    // SDL's poll sentinel may defer an event queued during dispatch to the next drain.
    while (SDL_PollEvent(&human)) if (Agent::EventFilter(nullptr, &human)) {
        if (human.type == SDL_KEYUP) order += 'u';
        if (human.type == SDL_KEYDOWN) order += 'd';
    }
    if (order != "ud") std::cerr << "observed input order=" << order << '\n';
    check(order == "ud", "human override did not release before physical input");
    Agent::AfterFrame(state);
    check(read(root + "/response.json").find("human_override") != std::string::npos, "human override falsely reported success");
    {
        std::ofstream file(root + "/request.txt", std::ios::trunc | std::ios::binary);
        file << "1 " << session << " 9 1 key 4 2000\n";
    }
    SDL_Delay(30); Agent::BeforeEvents();
    check(count(SDL_KEYDOWN) == 0, "expired command executed");
    check(read(root + "/response.json").find("expired") != std::string::npos, "expired command lacked rejection receipt");
    check(std::rename((root + "/response.json").c_str(), (root + "/prior-response.json").c_str()) == 0, "response fixture rename failed");
#ifdef _WIN32
    check(_mkdir((root + "/response.json").c_str()) == 0, "response blocker failed");
#else
    check(mkdir((root + "/response.json").c_str(), 0700) == 0, "response blocker failed");
#endif
    request("10 key 4 1", session); check(count(SDL_KEYDOWN) == 1, "I/O retry key not applied");
    Agent::AfterFrame(state); SDL_Delay(3); Agent::BeforeEvents();
    check(count(SDL_KEYUP) == 1, "I/O failure left input held");
    Agent::AfterFrame(state);
#ifdef _WIN32
    check(_rmdir((root + "/response.json").c_str()) == 0, "response unblock failed");
#else
    check(rmdir((root + "/response.json").c_str()) == 0, "response unblock failed");
#endif
    SDL_Delay(30); Agent::BeforeEvents();
    check(count(SDL_KEYDOWN) == 0 && read(root + "/response.json").find("\"id\":10,\"ok\":true") != std::string::npos,
        "failed response publication was not retried without reexecution");
    request("11 key 4 2000", session); check(count(SDL_KEYDOWN) == 1, "focus fixture key missing");
    SDL_Event focus = {}; focus.type = SDL_WINDOWEVENT; focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    SDL_PushEvent(&focus); check(count(SDL_KEYUP) == 1, "focus loss did not release input");
    count(SDL_WINDOWEVENT); Agent::AfterFrame(state);
    check(read(root + "/response.json").find("focus_lost") != std::string::npos, "focus loss reported success");
    request("12 key 4 2000", session); check(count(SDL_KEYDOWN) == 1, "I/O cancel fixture key missing");
    check(std::rename((root + "/request.txt").c_str(), (root + "/removed-request.txt").c_str()) == 0, "request removal fixture failed");
    SDL_Delay(30); Agent::BeforeEvents(); check(count(SDL_KEYUP) == 1, "input not released after request I/O failure");
    Agent::AfterFrame(state);
    check(read(root + "/response.json").find("agent_io_failure") != std::string::npos, "I/O failure reported success");
    request("13 key 4 2000", session); check(count(SDL_KEYDOWN) == 1, "shutdown fixture key missing");
    Agent::Shutdown(); check(count(SDL_KEYUP) == 1, "shutdown did not release input through ordinary dispatch");
    Agent::Shutdown();
    check(!Agent::Enabled(), "bridge still enabled after shutdown");

    // Observation schema 2: a fresh engine session reports lobby/network state
    // under "net"; "status" answers without a screenshot and is not cancelled by input.
    Agent::Init();
    check(Agent::Enabled(), "bridge not enabled after a second init");
    const auto meta2 = read(root + "/session.json");
    check(meta2.find("\"observation_schema\":2") != std::string::npos, "observation schema not bumped");
    check(meta2.find("\"protocol_minor\":1") != std::string::npos, "protocol minor not bumped for status");
    check(meta2.find("\"observe\",\"status\",") != std::string::npos, "status operation not advertised");
    const auto marker2 = meta2.find("\"session\":\"");
    check(marker2 != std::string::npos, "second session metadata missing");
    const std::string session2 = meta2.substr(marker2 + 11, 32);
    check(session2 != session, "second engine session reused the nonce");
    Agent::Snapshot lobby; lobby.screen = "menu";
    lobby.net.valid = true; lobby.net.lobbyScreen = "session"; lobby.net.role = "host";
    lobby.net.transport = "zndnet"; lobby.net.name = "HOST1"; lobby.net.levelId = 200;
    Agent::LobbyRow row; row.slot = 0; row.name = "H\"1"; row.faction = 1; row.ready = true; row.me = true;
    lobby.net.lobby.push_back(row);
    request("1 status", session2);
    check(read(root + "/response.json").find(session2) == std::string::npos, "status acknowledged before a frame");
    SDL_Event press = {}; press.type = SDL_KEYDOWN; press.key.keysym.scancode = SDL_SCANCODE_B;
    check(SDL_PushEvent(&press) == 1, "status override fixture failed");
    count(SDL_KEYDOWN);
    Agent::AfterFrame(lobby);
    const auto status = read(root + "/response.json");
    check(status.find("\"session\":\"" + session2 + "\",\"id\":1,\"ok\":true") != std::string::npos,
        "status not acknowledged, or cancelled by physical input");
    check(status.find("screenshot") == std::string::npos, "status captured or failed a screenshot");
    check(status.find(",\"net\":{\"lobby_screen\":\"session\",\"menu_open\":false,\"role\":\"host\","
        "\"transport\":\"zndnet\",\"name\":\"HOST1\"") != std::string::npos, "status lacks the net state");
    check(status.find("\"lobby\":[{\"slot\":0,\"name\":\"H\\\"1\",\"faction\":1,\"ready\":true,\"me\":true") !=
        std::string::npos, "lobby row missing or callsign not escaped");
    request("2 observe", session2); Agent::AfterFrame(lobby);
    const auto observed = read(root + "/response.json");
    check(observed.find("\"id\":2,") != std::string::npos && observed.find("screenshot_failed") != std::string::npos,
        "headless observe did not report its capture failure");
    check(observed.find(",\"net\":{\"lobby_screen\":\"session\"") != std::string::npos, "observe lacks the net state");
    Agent::Snapshot plain; plain.screen = "menu";
    request("3 status", session2); Agent::AfterFrame(plain);
    check(read(root + "/response.json").find("\"net\"") == std::string::npos, "net reported without a world");
    Agent::Shutdown(); SDL_Quit();
    std::cout << "PASS: actual SDL queue, native files, post-release acknowledgment, session/replay isolation, paced text, cancellation, honest capture failure and net status\n";
    return 0;
}
