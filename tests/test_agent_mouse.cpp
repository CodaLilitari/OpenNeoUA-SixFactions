// Optional native OpenGL test. On Linux run under Xvfb; the window stays hidden.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#undef main
#include "../src/agent/bridge.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

static void check(bool value, const char *message) {
    if (!value) { std::cerr << message << ": " << SDL_GetError() << '\n'; std::exit(1); }
}
static std::string read(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
int main(int argc, char **argv) {
    check(argc == 2, "external fixture directory required");
    SDL_SetMainReady();
    check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0, "SDL init failed");
    SDL_Window *window = SDL_CreateWindow("OpenNeoUA agent mouse regression", 0, 0, 160, 120,
                                         SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    check(window != nullptr, "hidden window failed");
    SDL_GLContext context = SDL_GL_CreateContext(window);
    check(context != nullptr, "OpenGL context failed");
    SDL_Event event; while (SDL_PollEvent(&event)) {}
    SDL_setenv("OPENNEOUA_AGENT_DIR", argv[1], 1);
    Agent::Init();
    const std::string root = argv[1], meta = read(root + "/session.json");
    const auto marker = meta.find("\"session\":\"");
    check(marker != std::string::npos, "session metadata missing");
    const auto token = meta.substr(marker + 11, 32);
    auto request = [&](int id, const char *body) {
        std::ofstream file(root + "/request.txt", std::ios::binary | std::ios::trunc);
        file << "1 " << token << ' ' << id << " 9999999999999 " << body << '\n';
        file.close(); SDL_Delay(30); Agent::BeforeEvents();
    };
    Agent::Snapshot state; state.screen = "menu";
    // Legacy lists read the position last supplied by motion, not button.x/y.
    // A single click must therefore position the pointer before its press.
    request(1, "click 1 64 48 1");
    int x = -1, y = -1; std::string order;
    while (SDL_PollEvent(&event)) if (Agent::EventFilter(nullptr, &event)) {
        if (event.type == SDL_MOUSEMOTION) {
            x = event.motion.x; y = event.motion.y; order += 'm';
            check(event.motion.xrel == 0 && event.motion.yrel == 0, "click invented relative movement");
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            order += 'd';
            check(x == 64 && y == 48, "click used stale legacy list position");
            check(event.button.x == x && event.button.y == y, "press and motion positions differ");
        }
    }
    check(order == "md", "click must send one motion followed by one press");
    Agent::AfterFrame(state); SDL_Delay(3); Agent::BeforeEvents();
    order.clear();
    while (SDL_PollEvent(&event)) if (Agent::EventFilter(nullptr, &event)) {
        if (event.type == SDL_MOUSEBUTTONUP) {
            check(event.button.x == 64 && event.button.y == 48, "release position changed");
            order += 'u';
        }
        if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN) order += '!';
    }
    check(order == "u", "click release missing or repeated motion/press");
    Agent::AfterFrame(state);
    check(read(root + "/response.json").find("\"id\":1,\"ok\":true") != std::string::npos,
          "click did not complete after release");
    request(2, "click 1 160 0 1");
    int emitted = 0;
    while (SDL_PollEvent(&event)) if (Agent::EventFilter(nullptr, &event) &&
        (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN)) ++emitted;
    check(emitted == 0, "out-of-bounds click moved or pressed");
    check(read(root + "/response.json").find("input_dispatch_failed") != std::string::npos,
          "out-of-bounds click lacked failure receipt");
    Agent::Shutdown(); SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::cout << "PASS: hidden native window, positioned click, ordered SDL events, release and bounds\n";
}
