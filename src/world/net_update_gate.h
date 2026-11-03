// REQUPDATE/UPDATE pacing between this process and one remote peer.
//
// A peer that receives events it cannot apply (a message naming a unit it
// does not know, or a unit it has not heard of for minutes) asks the sender
// for its whole station tree with REQUPDATE. The answer is an UPDATE of up to
// 49 176 bytes that replaces that tree, so while it is pending the sender's
// incremental unit events are ignored.
//
// The legacy countdown for that hold was zeroed by the sender's next message,
// so the hold never held and every missing-unit error could send another
// REQUPDATE, and get another full UPDATE, at once. One gate per remote peer
// now keeps the hold until the UPDATE arrives or PendingHoldMs passes, sends
// that peer at most one REQUPDATE per RequestIntervalMs, and answers its
// REQUPDATEs at most once per AnswerIntervalMs. The answer interval is shorter
// than the request interval, so latency jitter never makes a responder refuse
// a requester that keeps to its interval.
//
// Times are the caller's millisecond clock (the world timestamp); unsigned
// differences keep the gate correct across a wrap, and a clock that restarts
// below an old stamp reads as expired, never as a hold that does not end.
#ifndef WORLD_NET_UPDATE_GATE_H_INCLUDED
#define WORLD_NET_UPDATE_GATE_H_INCLUDED

#include <cstdint>

namespace World { namespace NetUpdate {

constexpr std::uint32_t PendingHoldMs = 2000;     // legacy hold after a REQUPDATE
constexpr std::uint32_t RequestIntervalMs = 5000; // between REQUPDATEs to one peer
constexpr std::uint32_t AnswerIntervalMs = 4000;  // between UPDATEs to one peer

class PeerGate
{
public:
    // Requester side. While an update is pending, the peer's unit events are
    // ignored because the update will replace the tree they apply to.
    bool HoldsEvents(std::uint32_t nowMs) const
    {
        return pending_ && Within(requestedAtMs_, nowMs, PendingHoldMs);
    }
    bool MayRequest(std::uint32_t nowMs) const
    {
        return !HoldsEvents(nowMs) && !(requested_ && Within(requestedAtMs_, nowMs, RequestIntervalMs));
    }
    void Requested(std::uint32_t nowMs)
    {
        requested_ = true;
        pending_ = true;
        requestedAtMs_ = nowMs;
    }
    // The requested UPDATE (or any UPDATE from this peer) arrived.
    void UpdateArrived() { pending_ = false; }

    // Responder side: this peer asked us for our tree.
    bool MayAnswer(std::uint32_t nowMs) const
    {
        return !(answered_ && Within(answeredAtMs_, nowMs, AnswerIntervalMs));
    }
    void Answered(std::uint32_t nowMs)
    {
        answered_ = true;
        answeredAtMs_ = nowMs;
    }

private:
    static bool Within(std::uint32_t sinceMs, std::uint32_t nowMs, std::uint32_t spanMs)
    {
        return static_cast<std::uint32_t>(nowMs - sinceMs) < spanMs;
    }

    bool requested_ = false;
    bool pending_ = false;
    bool answered_ = false;
    std::uint32_t requestedAtMs_ = 0;
    std::uint32_t answeredAtMs_ = 0;
};

} }
#endif
