// Test plumbing only. Never supplies a replacement provider/recovery algorithm.
#ifndef PROVIDER_TEST_FAKE_PLATFORM_H
#define PROVIDER_TEST_FAKE_PLATFORM_H
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct IPaddress { std::uint32_t host = 0; std::uint16_t port = 0; };
struct IDVPair {};
using IDVList = std::vector<IDVPair>;
struct TInputState {};
struct GuiBase {};
struct NC_STACK_network {};

namespace ProviderFixture {
static std::uint32_t now = 10000;
static unsigned delayCalls = 0, resolveCalls = 0;
static std::string lastResolved;
static IPaddress resolvedEndpoint{0x04030201u, 0x32efu};
static bool resolveFails = false;
inline void ClearClockCounters() { delayCalls = resolveCalls = 0; lastResolved.clear(); }
}
inline std::uint32_t SDL_GetTicks() { return ProviderFixture::now; }
inline void SDL_Delay(std::uint32_t milliseconds) {
    ++ProviderFixture::delayCalls;
    ProviderFixture::now += milliseconds;
}
inline int SDLNet_ResolveHost(IPaddress *destination, const char *name, std::uint16_t) {
    ++ProviderFixture::resolveCalls;
    ProviderFixture::lastResolved = name ? name : "";
    if (ProviderFixture::resolveFails) return -1;
    *destination = ProviderFixture::resolvedEndpoint;
    return 0;
}
inline void log_netlog(const char *, ...) {}
inline int StriCmp(const std::string &a, const std::string &b) {
    const auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    if (a.size() != b.size()) return 1;
    for (std::size_t i = 0; i < a.size(); ++i) if (fold(a[i]) != fold(b[i])) return 1;
    return 0;
}
namespace World { constexpr int CVMaxNetPlayers = 6; }
#endif
