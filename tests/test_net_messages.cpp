// Network message structs (src/game/yw_net_msgs.h). Including the header
// also evaluates its wire-size static_asserts in this build. Every sender
// prepares its message with NetMsgClear(), which must leave no byte of the
// previous storage contents: padding goes onto the wire byte for byte.
#include "../src/game/yw_net_msgs.h"
#include "require.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>

namespace
{

template <typename T>
struct Storage
{
    typename std::aligned_storage<sizeof(T), alignof(T)>::type bytes;
};

// Default-constructs a T over storage filled with `dirt`, clears it, and
// reports whether every byte is zero (all default member values are zero).
template <typename T>
bool ClearedToZero(unsigned char dirt)
{
    std::unique_ptr<Storage<T>> storage(new Storage<T>);
    std::memset(&storage->bytes, dirt, sizeof(T));
    T *msg = ::new (static_cast<void *>(&storage->bytes)) T;
    NetMsgClear(*msg);
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(msg);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        if (bytes[i] != 0)
            return false;
    return true;
}

template <typename T>
void RequireClears(const char *name)
{
    if (!ClearedToZero<T>(0xFF) || !ClearedToZero<T>(0xA5))
    {
        std::cerr << name << ": ";
        Require(false, "NetMsgClear leaves a non-zero byte");
    }
}

// A message with a non-zero default and a padding hole after it.
struct Probe : uamessage_base
{
    std::uint8_t flag = 7;   // offset 16, padding 17..23
    double value = 0.0;      // offset 24
};

} // namespace

int main()
{
#define CLEARS(type) RequireClears<type>(#type)
    CLEARS(uamessage_base);
    CLEARS(uamessage_load);
    CLEARS(uamessage_newVhcl);
    CLEARS(uamessage_destroyVhcl);
    CLEARS(uamessage_newLeader);
    CLEARS(uamessage_newWeapon);
    CLEARS(uamessage_setState);
    CLEARS(uamessage_vhclDataI);
    CLEARS(uamessage_vhclDataE);
    CLEARS(uamessage_dead);
    CLEARS(uamessage_vhclEnergy);
    CLEARS(uamessage_sectorEnergy);
    CLEARS(uamessage_startBuild);
    CLEARS(uamessage_bldVhcl);
    CLEARS(uamessage_viewer);
    CLEARS(uamessage_syncgame);
    CLEARS(uamessage_hostDie);
    CLEARS(uamessage_message);
    CLEARS(uamessage_kick);
    CLEARS(uamessage_upgrade);
    CLEARS(uamessage_fraction);
    CLEARS(uamessage_welcome);
    CLEARS(uamessage_ready);
    CLEARS(uamessage_requpdate);
    CLEARS(vhclUpdData);
    CLEARS(uamessage_update);
    CLEARS(uamessage_impulse);
    CLEARS(uamessage_logmsg);
    CLEARS(uamessage_reorder);
    CLEARS(uamessage_startPlasma);
    CLEARS(uamessage_endPlasma);
    CLEARS(uamessage_startBeam);
    CLEARS(uamessage_endBeam);
    CLEARS(uamessage_exit);
    CLEARS(uamessage_setLevel);
    CLEARS(uamessage_crc);
    CLEARS(uamessage_ping);
    CLEARS(uamessage_startproblem);
    CLEARS(uamessage_endproblem);
    CLEARS(uamessage_score);
#undef CLEARS

    // NetMsgClear applies default member values again after zeroing, so a
    // non-zero default survives and the padding beside it is zero.
    std::unique_ptr<Storage<Probe>> storage(new Storage<Probe>);
    std::memset(&storage->bytes, 0xFF, sizeof(Probe));
    Probe *probe = ::new (static_cast<void *>(&storage->bytes)) Probe;
    probe->msgID = 1234;
    NetMsgClear(*probe);
    REQUIRE(probe->flag == 7 && probe->msgID == 0 && probe->value == 0.0);
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(probe);
    for (std::size_t i = 17; i < 24; ++i)
        REQUIRE(bytes[i] == 0);

    // Scopes: the lobby set is ignored once a level is loaded, everything
    // else except chat needs a loaded level.
    for (std::uint32_t id : {UAMSG_LOAD, UAMSG_FRACTION, UAMSG_WELCOME, UAMSG_READY,
                             UAMSG_LOBBYINIT, UAMSG_SETLEVEL, UAMSG_CRC})
        REQUIRE(NetMsgScopeOf(id) == NetMsgScope::Lobby);
    REQUIRE(NetMsgScopeOf(UAMSG_MESSAGE) == NetMsgScope::Any);
    for (std::uint32_t id : {UAMSG_NEWVHCL, UAMSG_DESTROYVHCL, UAMSG_NEWWEAPON, UAMSG_SETSTATE,
                             UAMSG_VHCLDATA_I, UAMSG_VHCLDATA_E, UAMSG_DEAD, UAMSG_VHCLENERGY,
                             UAMSG_SECTORENERGY, UAMSG_STARTBUILD, UAMSG_BUILDINGVHCL, UAMSG_VIEWER,
                             UAMSG_SYNCGM, UAMSG_HOSTDIE, UAMSG_KICK, UAMSG_UPGRADE, UAMSG_REQUPDATE,
                             UAMSG_UPDATE, UAMSG_IMPULSE, UAMSG_LOGMSG, UAMSG_REORDER,
                             UAMSG_STARTPLASMA, UAMSG_ENDPLASMA, UAMSG_STARTBEAM, UAMSG_ENDBEAM,
                             UAMSG_EXIT, UAMSG_REQPING, UAMSG_PONG, UAMSG_STARTPROBLEM,
                             UAMSG_ENDPROBLEM, UAMSG_SCORE})
        REQUIRE(NetMsgScopeOf(id) == NetMsgScope::Level);
    REQUIRE(NetMsgScopeOf(UAMSG_BASE + 99) == NetMsgScope::Level);

    // SECTORENERGY positions on a 4 x 3 sector map, read like
    // World::PositionToSectorID: column x / length, row -z / length, each
    // truncated toward zero. A position the sender's own lookup puts on the
    // map must pass, including those less than a sector west or north of it,
    // which truncate to column or row 0.
    const double len = World::CVSectorLength;
    const auto engineCellOnMap = [len](double x, double z, int w, int h) {
        const int column = static_cast<int>(x / len), row = static_cast<int>(-z / len);
        return column >= 0 && column < w && row >= 0 && row < h;
    };
    REQUIRE(NetPositionOnMap(0.0, 0.0, 0.0, 4, 3));
    REQUIRE(NetPositionOnMap(4 * len - 0.5, -300.0, -(3 * len - 0.5), 4, 3));
    REQUIRE(NetPositionOnMap(len * 1.5, 0.0, -0.0, 4, 3));
    REQUIRE(NetPositionOnMap(-0.5, 0.0, -len, 4, 3));         // column 0 on the sender too
    REQUIRE(NetPositionOnMap(-(len - 1.0), 0.0, -len, 4, 3)); // still column 0
    REQUIRE(NetPositionOnMap(len, 0.0, 0.5, 4, 3));           // row 0
    REQUIRE(NetPositionOnMap(len, 0.0, len - 1.0, 4, 3));     // still row 0
    REQUIRE(!NetPositionOnMap(-len, 0.0, -len, 4, 3));        // column -1
    REQUIRE(!NetPositionOnMap(len, 0.0, len, 4, 3));          // row -1
    REQUIRE(!NetPositionOnMap(4 * len, 0.0, -len, 4, 3));     // one past the east edge
    REQUIRE(!NetPositionOnMap(len, 0.0, -3 * len, 4, 3));     // one past the south edge
    REQUIRE(!NetPositionOnMap(len, 0.0, -len, 0, 0));         // no map loaded
    for (double x = -2.5 * len; x <= 5.5 * len; x += len / 8)
        for (double z = 1.5 * len; z >= -4.5 * len; z -= len / 8)
            for (double nudge : {0.0, 1e-9, -1e-9})
                REQUIRE(NetPositionOnMap(x + nudge, 0.0, z + nudge, 4, 3) ==
                        engineCellOnMap(x + nudge, z + nudge, 4, 3));
    const double nan = std::nan("");
    const double inf = HUGE_VAL;
    REQUIRE(!NetPositionOnMap(nan, 0.0, -len, 4, 3));
    REQUIRE(!NetPositionOnMap(len, nan, -len, 4, 3));
    REQUIRE(!NetPositionOnMap(len, 0.0, -inf, 4, 3));
    REQUIRE(!NetPositionOnMap(1e300, 0.0, -len, 4, 3));
    REQUIRE(!NetPositionOnMap(-1e300, 0.0, -len, 4, 3));
    REQUIRE(!NetPositionOnMap(len, 0.0, 1e300, 4, 3));

    std::cout << "every wire message clears to zero bytes, padding included; "
                 "scopes and map positions checked\n";
    return 0;
}
