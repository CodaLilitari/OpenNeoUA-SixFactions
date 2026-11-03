// REQUPDATE/UPDATE pacing (src/world/net_update_gate.h).
#include "../src/world/net_update_gate.h"
#include "require.h"
#include <iostream>

using World::NetUpdate::PeerGate;
using World::NetUpdate::PendingHoldMs;
using World::NetUpdate::RequestIntervalMs;
using World::NetUpdate::AnswerIntervalMs;

int main()
{
    // A fresh peer: nothing held, a request and an answer are allowed.
    PeerGate fresh;
    REQUIRE(!fresh.HoldsEvents(0) && fresh.MayRequest(0) && fresh.MayAnswer(0));

    // The hold lasts PendingHoldMs. The legacy countdown was zeroed by the
    // sender's next message; here the peer's traffic does not touch the gate,
    // so a stream of messages (one every 10 ms) keeps finding it held.
    PeerGate gate;
    const std::uint32_t t = 100000;
    gate.Requested(t);
    for (std::uint32_t ms = 0; ms < PendingHoldMs; ms += 10)
        REQUIRE(gate.HoldsEvents(t + ms) && !gate.MayRequest(t + ms));
    REQUIRE(!gate.HoldsEvents(t + PendingHoldMs));

    // After an unanswered hold, the next request still waits for the interval.
    REQUIRE(!gate.MayRequest(t + PendingHoldMs));
    REQUIRE(!gate.MayRequest(t + RequestIntervalMs - 1));
    REQUIRE(gate.MayRequest(t + RequestIntervalMs));

    // The update arriving ends the hold at once but not the request interval:
    // errors right after a restore cannot ask for another full tree.
    PeerGate answered;
    answered.Requested(t);
    answered.UpdateArrived();
    REQUIRE(!answered.HoldsEvents(t + 1));
    REQUIRE(!answered.MayRequest(t + 1) && !answered.MayRequest(t + RequestIntervalMs - 1));
    REQUIRE(answered.MayRequest(t + RequestIntervalMs));

    // At most one REQUPDATE per interval, however many errors a frame reports.
    PeerGate busy;
    int sent = 0;
    for (std::uint32_t now = t; now < t + 60000; now += 16)
        for (int error = 0; error < 5; ++error)
            if (busy.MayRequest(now))
            {
                busy.Requested(now);
                ++sent;
            }
    REQUIRE(sent == 12); // 60 s / 5 s

    // Responder side: at most one UPDATE per AnswerIntervalMs to one peer.
    PeerGate responder;
    REQUIRE(responder.MayAnswer(t));
    responder.Answered(t);
    REQUIRE(!responder.MayAnswer(t + AnswerIntervalMs - 1));
    REQUIRE(responder.MayAnswer(t + AnswerIntervalMs));

    // A requester that keeps to RequestIntervalMs is never refused, even when
    // one request takes a second longer to arrive than the next.
    REQUIRE(AnswerIntervalMs + 1000 <= RequestIntervalMs);
    PeerGate jitter;
    std::uint32_t sendAt = t;
    for (int k = 0; k < 20; ++k, sendAt += RequestIntervalMs)
    {
        const std::uint32_t arrival = sendAt + (k % 2 == 0 ? 1000 : 0);
        REQUIRE(jitter.MayAnswer(arrival));
        jitter.Answered(arrival);
    }

    // The clock may wrap or restart (a level load resets the world clock).
    PeerGate wrap;
    wrap.Requested(0xFFFFFF00u);
    REQUIRE(wrap.HoldsEvents(0x00000100u));           // 512 ms after the stamp
    REQUIRE(!wrap.HoldsEvents(0x00000100u + PendingHoldMs));
    PeerGate restarted;
    restarted.Requested(500000);
    restarted.Answered(500000);
    REQUIRE(!restarted.HoldsEvents(0) && restarted.MayRequest(0) && restarted.MayAnswer(0));

    std::cout << "update gate holds, paces requests and answers\n";
    return 0;
}
