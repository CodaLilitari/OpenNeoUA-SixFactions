// Frame-driven provider recovery policy. This authorizes a transport attempt,
// never game ownership: the game must explicitly finish its reclaim barrier.
#ifndef WINDP_RESUME_H_INCLUDED
#define WINDP_RESUME_H_INCLUDED

#include <cstdint>
#include <string>
#include "../netlib/connfail.h"
#include "../world/net_protocol.h"

namespace WindpResume {

enum class Transport { Znd, Enet };
enum class Phase { Idle, Connected, WaitingRetry, Connecting, AwaitingReclaim, Failed };
enum class Action { None, StartAttempt, StopAttempt, Fail };

struct Record
{
    Transport kind = Transport::Znd;
    // SDLNet's resolved endpoint bytes, retained to avoid DNS in retry frames.
    std::uint32_t addressHost = 0;
    std::uint16_t addressPort = 0;
    std::string addressText;
    std::string name;
    std::uint64_t id = 0;
    std::uint64_t session = 0;
    std::uint32_t graceMs = 60000;
    World::NetProtocol::ResumeToken token{};

    bool Valid() const
    {
        return (kind == Transport::Znd || kind == Transport::Enet) && addressHost &&
            addressPort && !name.empty() && id && session && graceMs >= 100 &&
            graceMs <= 600000 && World::NetProtocol::TokenValid(token);
    }
};

class State
{
public:
    static constexpr std::uint32_t AttemptMs = 5000;
    static constexpr std::uint32_t RetryMs = 1000;

    void Clear()
    {
        saved_ = Record{};
        phase_ = Phase::Idle;
        enabled_ = false;
        lossAt_ = attemptAt_ = retryAt_ = 0;
        failure_ = ZNDNet::CONNFAIL_NONE;
    }

    bool Remember(const Record &record)
    {
        Clear();
        if (!record.Valid())
            return false;
        saved_ = record;
        phase_ = Phase::Connected;
        return true;
    }

    void Enable(bool enabled)
    {
        if (!enabled && Recovering())
            Terminal(ZNDNet::CONNFAIL_DROPPED);
        // Setting an already armed policy to true must not disarm an active
        // retry/reclaim interval or replenish its original grace deadline.
        enabled_ = enabled && saved_.Valid() &&
                   (phase_ == Phase::Connected || Recovering());
    }

    // The first loss starts the one fixed grace window. Further losses during
    // retry or world restore must not replenish it.
    bool Lost(std::uint32_t now)
    {
        if (!enabled_ || !saved_.Valid() || phase_ == Phase::Idle || phase_ == Phase::Failed)
        {
            Terminal(ZNDNet::CONNFAIL_DROPPED);
            return false;
        }
        if (!Recovering())
        {
            lossAt_ = now;
            retryAt_ = now;
        }
        else
            retryAt_ = now + RetryMs;
        phase_ = Phase::WaitingRetry;
        failure_ = ZNDNet::CONNFAIL_DROPPED;
        return true;
    }

    Action Poll(std::uint32_t now)
    {
        if (!Recovering())
            return Action::None;
        if (std::uint32_t(now - lossAt_) >= saved_.graceMs)
        {
            Terminal(ZNDNet::CONNFAIL_NO_ANSWER);
            return Action::Fail;
        }
        if (phase_ == Phase::Connecting && std::uint32_t(now - attemptAt_) >= AttemptMs)
        {
            phase_ = Phase::WaitingRetry;
            retryAt_ = now + RetryMs;
            return Action::StopAttempt;
        }
        if (phase_ == Phase::WaitingRetry && static_cast<std::int32_t>(now - retryAt_) >= 0)
        {
            phase_ = Phase::Connecting;
            attemptAt_ = now;
            return Action::StartAttempt;
        }
        return Action::None;
    }

    bool AttemptFailed(std::uint32_t now, int failure)
    {
        if (!Recovering() || ZNDNet::ConnectFailureIsFinal(failure))
        {
            Terminal(failure);
            return false;
        }
        failure_ = failure;
        phase_ = Phase::WaitingRetry;
        retryAt_ = now + RetryMs;
        return true;
    }

    bool Accepted(std::uint64_t id, std::uint64_t session)
    {
        if (phase_ != Phase::Connecting || id != saved_.id || session != saved_.session)
        {
            Terminal(ZNDNet::CONNFAIL_RESUME);
            return false;
        }
        phase_ = Phase::AwaitingReclaim;
        return true;
    }

    // Only call after the host-authored world/owner-epoch hand-back is complete.
    bool Reclaimed()
    {
        if (phase_ != Phase::AwaitingReclaim || !saved_.Valid())
            return false;
        phase_ = Phase::Connected;
        failure_ = ZNDNet::CONNFAIL_NONE;
        return true;
    }

    void Terminal(int failure)
    {
        saved_ = Record{};
        enabled_ = false;
        phase_ = Phase::Failed;
        failure_ = failure;
    }

    bool Recovering() const
    {
        return phase_ == Phase::WaitingRetry || phase_ == Phase::Connecting ||
               phase_ == Phase::AwaitingReclaim;
    }
    bool CanSendGameplay() const { return phase_ == Phase::Connected; }
    bool Enabled() const { return enabled_; }
    Phase GetPhase() const { return phase_; }
    int Failure() const { return failure_; }
    const Record &Saved() const { return saved_; }

private:
    Record saved_;
    Phase phase_ = Phase::Idle;
    bool enabled_ = false;
    std::uint32_t lossAt_ = 0, attemptAt_ = 0, retryAt_ = 0;
    int failure_ = ZNDNet::CONNFAIL_NONE;
};

inline const char *PhaseName(Phase phase)
{
    switch (phase)
    {
    case Phase::Idle: return "idle";
    case Phase::Connected: return "connected";
    case Phase::WaitingRetry: return "waiting_retry";
    case Phase::Connecting: return "connecting";
    case Phase::AwaitingReclaim: return "awaiting_reclaim";
    case Phase::Failed: return "failed";
    }
    return "unknown";
}
} // namespace WindpResume
#endif
