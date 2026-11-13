// Deterministic scheduling regressions for the real ZNDNet idle/wait methods.
// No sockets or transport workers are started: fixtures hold the same locks
// and retry custody used by those workers, without adding production hooks.
#include "../src/netlib/zndNet.h"
#include "../src/world/net_protocol.h"
#include <atomic>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace ZNDNet;

static void Check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

class Semaphore
{
    SDL_sem *value_ = SDL_CreateSemaphore(0);
public:
    Semaphore() { Check(value_ != nullptr, "semaphore allocation failed"); }
    ~Semaphore() { SDL_DestroySemaphore(value_); }
    void Post() { Check(SDL_SemPost(value_) == 0, "semaphore signal failed"); }
    bool Wait(unsigned milliseconds) { return SDL_SemWaitTimeout(value_, milliseconds) == 0; }
};

class HeldMutex
{
    SDL_mutex *value_;
public:
    explicit HeldMutex(SDL_mutex *value) : value_(value)
    {
        Check(SDL_LockMutex(value_) == 0, "cannot hold fixture mutex");
    }
    ~HeldMutex() { Release(); }
    void Release()
    {
        if (value_) { SDL_UnlockMutex(value_); value_ = nullptr; }
    }
};

class Worker
{
    std::function<void()> action_;
    SDL_Thread *thread_ = nullptr;
    std::exception_ptr failure_;
    static int Run(void *raw)
    {
        Worker &self = *static_cast<Worker *>(raw);
        try { self.action_(); }
        catch (...) { self.failure_ = std::current_exception(); }
        return 0;
    }
public:
    explicit Worker(std::function<void()> action) : action_(std::move(action))
    {
        thread_ = SDL_CreateThread(&Run, "drain-regression", this);
        Check(thread_ != nullptr, "fixture thread creation failed");
    }
    ~Worker() { if (thread_) SDL_WaitThread(thread_, nullptr); }
    void Join()
    {
        if (thread_) { SDL_WaitThread(thread_, nullptr); thread_ = nullptr; }
        if (failure_) std::rethrow_exception(failure_);
    }
};

class ProbeClient : public ZNDClient
{
public:
    using ZNDClient::ZNDClient;
    SDL_mutex *Mutex(const std::string &name)
    {
        if (name == "sender") return sendModifyMutex;
        if (name == "update") return eSyncMutex;
        if (name == "send-queue") return sendPktListMutex;
        if (name == "confirm-queue") return confirmQueueMutex;
        throw std::runtime_error("unknown fixture mutex");
    }
    void SeedUnacknowledged()
    {
        uint8_t text[] = "pending-retry";
        auto *data = new SendingData(IPaddress{}, 17,
            RefDataStatic::create(text, sizeof(text) - 1), PKT_FLAG_GARANT);
        HeldMutex lock(confirmQueueMutex);
        confirmQueue.push_back(data);
    }
    void PauseWithRetryCustody(Semaphore &paused, Semaphore &resume)
    {
        // Models the exact observable interval in ConfirmQueueCheck and
        // ConfirmRetry: updater owns eSync and a packet, both queues empty.
        // It deliberately does not claim to execute those dispatch paths.
        HeldMutex update(eSyncMutex);
        std::unique_ptr<SendingData> retry;
        {
            HeldMutex confirm(confirmQueueMutex);
            Check(confirmQueue.size() == 1, "missing pending retry fixture");
            retry.reset(confirmQueue.front());
            confirmQueue.pop_front();
        }
        paused.Post();
        Check(resume.Wait(3000), "controller did not release retry custody");
        Send_RetryData(retry.release());
    }
    void ClearQueued() { Send_Clear(); }
};

static void Contended(const std::string &lockName, unsigned timeout)
{
    // Bug caught: any blocking lock in an idle probe can defeat its deadline.
    ProbeClient client(World::NetProtocol::ServiceId);
    Semaphore entered, done;
    std::atomic<bool> result{true};
    HeldMutex held(client.Mutex(lockName));
    Worker waiter([&] {
        entered.Post();
        result = client.WaitSendIdle(timeout);
        done.Post();
    });
    const bool started = entered.Wait(1000);
    const bool completedWhileHeld = started && done.Wait(250);
    // Release before reporting failure so the old blocking implementation
    // exits cleanly. This gate, not a sleep, determines the contested window.
    held.Release();
    waiter.Join();
    Check(started, "waiter did not start");
    Check(completedWhileHeld, "WaitSendIdle blocked until its local mutex was released");
    Check(!result, "WaitSendIdle reported idle while the local transport was busy");
    Check(client.WaitSendIdle(0), "endpoint did not become idle after lock release");
}

static void RetryCustody(unsigned timeout)
{
    // Bug caught: an updater-owned retry must not look successfully drained
    // merely because it is temporarily absent from both visible queues.
    ProbeClient client(World::NetProtocol::ServiceId);
    client.SeedUnacknowledged();
    Semaphore paused, resume, entered, done;
    std::atomic<bool> result{true};
    Worker updater([&] { client.PauseWithRetryCustody(paused, resume); });
    const bool transferPaused = paused.Wait(1000);
    Worker waiter([&] {
        entered.Post();
        result = client.WaitSendIdle(timeout);
        done.Post();
    });
    const bool started = entered.Wait(1000);
    const bool completedDuringTransfer = started && done.Wait(250);
    resume.Post();
    waiter.Join();
    updater.Join();
    Check(transferPaused, "retry fixture never entered transfer custody");
    Check(started, "waiter did not start");
    Check(completedDuringTransfer, "drain blocked on a paused updater");
    Check(!result, "unacknowledged retry custody was falsely reported drained");
    Check(!client.WaitSendIdle(0), "requeued retry disappeared from idle accounting");
    client.ClearQueued();
    Check(client.WaitSendIdle(0), "cleared fixture remained busy");
}

int main(int argc, char **argv)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) || SDLNet_Init()) return 1;
    int result = 0;
    try
    {
        Check(argc == 3, "select sender/update/send-queue/confirm-queue/retry and 0 or 20 ms");
        const std::string scenario = argv[1];
        const std::string timeoutText = argv[2];
        Check(timeoutText == "0" || timeoutText == "20", "unexpected timeout");
        const unsigned timeout = timeoutText == "0" ? 0 : 20;
        if (scenario == "retry") RetryCustody(timeout);
        else Contended(scenario, timeout);
        std::cout << "PASS " << scenario << " timeout=" << timeout << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    SDLNet_Quit();
    SDL_Quit();
    return result;
}