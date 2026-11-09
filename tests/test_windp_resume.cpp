#include "../src/platform/windp_resume.h"
#include "require.h"
#include <cstdint>
#include <iostream>
#include <limits>

static WindpResume::Record Saved()
{
    WindpResume::Record r;
    r.kind = WindpResume::Transport::Enet;
    r.addressHost = 123;
    r.addressPort = 456;
    r.addressText = "example-host";
    r.name = "PLAYER";
    r.id = 30;
    r.session = 99;
    r.graceMs = 60000;
    r.token.b[0] = 7;
    return r;
}

int main()
{
    using namespace WindpResume;
    State s;
    REQUIRE(!s.CanSendGameplay());
    REQUIRE(!s.Remember(Record{}));
    REQUIRE(s.Remember(Saved()));
    REQUIRE(s.CanSendGameplay());
    REQUIRE(!s.Enabled());
    // Recovery has to be deliberately armed by the game ownership integration.
    REQUIRE(!s.Lost(10));
    REQUIRE(s.GetPhase() == Phase::Failed);
    REQUIRE(!World::NetProtocol::TokenValid(s.Saved().token));
    REQUIRE(!s.CanSendGameplay());

    REQUIRE(s.Remember(Saved()));
    s.Enable(true);
    REQUIRE(s.Lost(100));
    REQUIRE(s.GetPhase() == Phase::WaitingRetry);
    REQUIRE(!s.CanSendGameplay());
    s.Enable(true); // idempotent during a pending retry
    REQUIRE(s.Enabled());
    REQUIRE(s.Poll(100) == Action::StartAttempt);
    REQUIRE(s.GetPhase() == Phase::Connecting);
    s.Enable(true); // idempotent while the transport attempt is running
    REQUIRE(s.Enabled());
    REQUIRE(s.Poll(5099) == Action::None);
    REQUIRE(s.Poll(5100) == Action::StopAttempt);
    REQUIRE(s.Poll(6099) == Action::None);
    REQUIRE(s.Poll(6100) == Action::StartAttempt);
    // A transport return does not authorize stale gameplay or extend recovery.
    REQUIRE(s.Accepted(30, 99));
    REQUIRE(s.GetPhase() == Phase::AwaitingReclaim);
    REQUIRE(!s.CanSendGameplay());
    REQUIRE(s.Poll(60099) == Action::None);
    REQUIRE(s.Poll(60100) == Action::Fail);
    REQUIRE(!World::NetProtocol::TokenValid(s.Saved().token));
    REQUIRE(!s.Reclaimed());

    REQUIRE(s.Remember(Saved()));
    s.Enable(true);
    REQUIRE(s.Lost(200));
    REQUIRE(s.Poll(200) == Action::StartAttempt);
    REQUIRE(s.Accepted(30, 99));
    REQUIRE(s.Reclaimed());
    REQUIRE(s.CanSendGameplay());
    REQUIRE(s.Lost(50000));
    REQUIRE(s.Poll(50000) == Action::StartAttempt);
    REQUIRE(s.Poll(55000) == Action::StopAttempt); // fresh loss gets a fresh budget
    REQUIRE(s.Poll(56000) == Action::StartAttempt);
    REQUIRE(!s.Accepted(31, 99));
    REQUIRE(s.GetPhase() == Phase::Failed);
    REQUIRE(!World::NetProtocol::TokenValid(s.Saved().token));

    REQUIRE(s.Remember(Saved()));
    s.Enable(true);
    REQUIRE(s.Lost(1000));
    REQUIRE(s.Poll(1000) == Action::StartAttempt);
    REQUIRE(s.AttemptFailed(2000, ZNDNet::CONNFAIL_NO_ANSWER));
    REQUIRE(s.Poll(2999) == Action::None);
    REQUIRE(s.Poll(3000) == Action::StartAttempt);
    REQUIRE(!s.AttemptFailed(3001, ZNDNet::CONNFAIL_RESUME));
    REQUIRE(s.GetPhase() == Phase::Failed);
    REQUIRE(s.Failure() == ZNDNet::CONNFAIL_RESUME);
    REQUIRE(s.Poll(6000) == Action::None);

    // Monotonic uint32 elapsed arithmetic remains correct across SDL tick wrap.
    const auto edge = std::numeric_limits<std::uint32_t>::max() - 50;
    auto shortRecord = Saved(); shortRecord.graceMs = 100;
    REQUIRE(s.Remember(shortRecord));
    s.Enable(true);
    REQUIRE(s.Lost(edge));
    REQUIRE(s.Poll(edge) == Action::StartAttempt);
    REQUIRE(s.Poll(48) == Action::None);
    REQUIRE(s.Poll(49) == Action::Fail);

    // Repeated losses during reclaim cannot replenish the original grace.
    REQUIRE(s.Remember(shortRecord));
    s.Enable(true);
    REQUIRE(s.Lost(100));
    REQUIRE(s.Poll(100) == Action::StartAttempt);
    REQUIRE(s.Accepted(30, 99));
    REQUIRE(s.Lost(190));
    REQUIRE(s.Poll(200) == Action::Fail);
    s.Clear();
    REQUIRE(s.GetPhase() == Phase::Idle);
    REQUIRE(!s.Enabled());
    REQUIRE(s.Saved().name.empty());
    REQUIRE(s.Saved().id == 0);
    REQUIRE(!s.CanSendGameplay());
    std::cout << "provider recovery preserves identity, fixed deadlines and reclaim send barrier\n";
}
