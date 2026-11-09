#include "../src/netlib/transport_state.h"
#include "require.h"
using namespace ZNDNet;
int
main()
{
    // Literal schema vector, independent of either serializer. The row is host
    // id4, slot0, online; session3, binding2, attempt1 and no transitions.
    const std::vector<uint8_t> golden = { 0x55, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 3,   0,
                                          0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   1,
                                          'x',  1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 'h', 0 };
    RosterState s;
    uint64_t attempt = 0;
    uint32_t generation = 0;
    REQUIRE(DecodeRoster(golden.data(), golden.size(), s, attempt, generation));
    REQUIRE(attempt == 1 && generation == 2 && s.session == 3 && s.rows.size() == 1 &&
            s.rows[0].id == 4 && s.rows[0].slot == 0 && s.rows[0].status == RosterOnline);
    REQUIRE(EncodeRoster(s, 1, 2) == golden);
    for (size_t n = 0; n < golden.size(); ++n)
    {
        RosterState unchanged;
        unchanged.name = "keep";
        REQUIRE(!DecodeRoster(golden.data(), n, unchanged, attempt, generation));
        REQUIRE(unchanged.name == "keep");
    }
    auto invalid = golden;
    invalid[41] = RosterAway;
    REQUIRE(!DecodeRoster(invalid.data(), invalid.size(), s, attempt, generation));
    RosterState host;
    host.session = 3;
    host.name = "x";
    host.rows.push_back({ 4, 0, RosterOnline, "h" });
    RosterState receiver;
    std::vector<RosterChange> delivered;
    bool consolidated = false;
    REQUIRE(ApplyRosterState(receiver, host, delivered, consolidated) && consolidated &&
            delivered.empty());
    host.rows.push_back({ 5, 1, RosterOnline, "one" });
    host.Changed(host.rows.back(), Transition::Fresh);
    host.rows.back().status = RosterAway;
    host.Changed(host.rows.back(), Transition::Away);
    REQUIRE(ApplyRosterState(receiver, host, delivered, consolidated) && !consolidated &&
            delivered.size() == 2);
    REQUIRE(delivered[0].transition == Transition::Fresh &&
            delivered[1].transition == Transition::Away);
    REQUIRE(!ApplyRosterState(receiver, host, delivered, consolidated) && delivered.empty());
    for (unsigned i = 0; i < 65; ++i)
        host.Changed(host.rows.back(), i % 2 ? Transition::Away : Transition::Resumed);
    REQUIRE(host.history.size() == 64);
    REQUIRE(ApplyRosterState(receiver, host, delivered, consolidated) && consolidated &&
            delivered.empty());
    return 0;
}
