// Only the lock/wait algorithms are extracted from production. The transport
// is explicitly empty; this complements, not replaces, the full ENet tests.
#include <SDL.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include "enet_drain_probe.inc"

static void Check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

struct WaitState
{
    ZNDNet::ZNDEnet &endpoint;
    SDL_sem *entered;
    SDL_sem *done;
    uint32_t timeout;
    std::atomic<bool> idle{true};
};

static int WaitMain(void *raw)
{
    auto &state = *static_cast<WaitState *>(raw);
    SDL_SemPost(state.entered);
    state.idle = state.endpoint.WaitSendIdle(state.timeout);
    SDL_SemPost(state.done);
    return 0;
}

static void Contended(uint32_t timeout)
{
    ZNDNet::ZNDEnet endpoint;
    Check(endpoint.netMutex_ != nullptr, "adapter fixture mutex allocation failed");
    using Sem = std::unique_ptr<SDL_sem, decltype(&SDL_DestroySemaphore)>;
    Sem entered(SDL_CreateSemaphore(0), SDL_DestroySemaphore);
    Sem done(SDL_CreateSemaphore(0), SDL_DestroySemaphore);
    Check(entered && done, "semaphore allocation failed");
    WaitState state{endpoint, entered.get(), done.get(), timeout};
    Check(SDL_LockMutex(endpoint.netMutex_) == 0, "cannot hold actual adapter-lock fixture");
    SDL_Thread *waiter = SDL_CreateThread(WaitMain, "enet-drain-locking", &state);
    if (!waiter)
    {
        SDL_UnlockMutex(endpoint.netMutex_);
        throw std::runtime_error("waiter creation failed");
    }
    const bool started = SDL_SemWaitTimeout(entered.get(), 1000) == 0;
    const bool completedWhileHeld = started && SDL_SemWaitTimeout(done.get(), 250) == 0;
    // Always release and join before an assertion: the old blocking method
    // must produce a useful failure, not leave a hung test process.
    SDL_UnlockMutex(endpoint.netMutex_);
    SDL_WaitThread(waiter, nullptr);
    Check(started, "waiter did not enter");
    Check(completedWhileHeld, "ENet idle probe blocked until its adapter mutex was released");
    Check(!state.idle, "ENet idle probe reported idle while its adapter lock was held");
    Check(endpoint.WaitSendIdle(0), "empty transport did not become idle after lock release");
}

int main(int argc, char **argv)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER)) return 1;
    int result = 0;
    try
    {
        Check(argc == 2, "select timeout 0 or 20");
        const std::string value = argv[1];
        Check(value == "0" || value == "20", "unexpected timeout");
        Contended(value == "0" ? 0 : 20);
        std::cout << "PASS ENet exact-method lock fixture timeout=" << value << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
