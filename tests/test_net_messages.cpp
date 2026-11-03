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

    std::cout << "every wire message clears to zero bytes, padding included\n";
    return 0;
}
