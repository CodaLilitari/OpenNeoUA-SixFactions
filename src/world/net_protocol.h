#ifndef WORLD_NET_PROTOCOL_H_INCLUDED
#define WORLD_NET_PROTOCOL_H_INCLUDED

namespace World { namespace NetProtocol {
// Sent by the real transport handshake, before application packets are accepted.
// Change this identity whenever the multiplayer packet layout becomes incompatible.
constexpr char ServiceId[] = "OpenNeoUA:six-factions:1";
static_assert(sizeof(ServiceId) - 1 <= 255, "service identity fits handshake length");
// Wire protocol version carried in the handshake (design Phase 2). Bump on any
// incompatible wire change; hard-reject mismatches (closed 6-player groups).
constexpr unsigned ProtoVer = 2;
constexpr unsigned ProtoVerMin = 2;
inline bool ProtoCompatible(unsigned peer) { return peer >= ProtoVerMin && peer <= ProtoVer; }
// Handshake trailer: protocol version as little-endian U16 after the player name.
inline void EncodeProtoVer(unsigned ver, unsigned char out[2]) { out[0] = (unsigned char)(ver & 0xFF); out[1] = (unsigned char)((ver >> 8) & 0xFF); }
inline unsigned DecodeProtoVer(const unsigned char in[2]) { return (unsigned)in[0] | ((unsigned)in[1] << 8); }
// 128-bit resume token issued at CONNECTED (design 6). Zero token = no resume.
struct ResumeToken { unsigned char b[16]; };
inline bool TokenValid(const ResumeToken& t) { for (int i = 0; i < 16; ++i) if (t.b[i]) return true; return false; }
inline bool TokenEqual(const ResumeToken& a, const ResumeToken& c) { unsigned d = 0; for (int i = 0; i < 16; ++i) d |= (unsigned)(a.b[i] ^ c.b[i]); return d == 0; }
} }

#endif
