#ifndef WORLD_NET_PROTOCOL_H_INCLUDED
#define WORLD_NET_PROTOCOL_H_INCLUDED

namespace World { namespace NetProtocol {
// Sent by the real transport handshake, before application packets are accepted.
// Change this identity whenever the multiplayer packet layout becomes incompatible.
constexpr char ServiceId[] = "OpenNeoUA:six-factions:1";
static_assert(sizeof(ServiceId) - 1 <= 255, "service identity fits handshake length");
} }

#endif
