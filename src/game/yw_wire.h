// Wire framing for the modernized netcode (design Phase 2): explicit
// little-endian envelope replacing raw struct memcpy on the wire. The UAMSG
// numeric registry (yw_net.h) stays as the type space; per-message
// Encode/Decode for uamessage_* structs lands with the transport cutover.
// This header is pure (no engine types) so framing is unit-tested standalone.
#ifndef YW_WIRE_H_INCLUDED
#define YW_WIRE_H_INCLUDED

#include <cstdint>
#include <cstring>

namespace Wire {

constexpr std::uint16_t Magic = 0x5541; // "UA"
constexpr std::size_t HeaderSize = 24;
constexpr std::size_t MaxPayload = 1376; // ZNDNET_MAX_MTU 1400 - HeaderSize

struct Header
{
    std::uint16_t protoVer = 0;
    std::uint16_t type = 0;     // UAMSG id (yw_net.h registry)
    std::uint32_t seq = 0;      // sender sequence (per channel)
    std::uint32_t ack = 0;      // highest sequence received from peer
    std::uint32_t ackBits = 0;  // bit i (0..31): seq ack-1-i also received
    std::uint32_t tick = 0;     // sender net tick (lag compensation)
    std::uint16_t payloadLen = 0;
};

inline void PutU16(std::uint8_t *dst, std::uint16_t v)
{
    dst[0] = (std::uint8_t)(v & 0xFF);
    dst[1] = (std::uint8_t)((v >> 8) & 0xFF);
}
inline void PutU32(std::uint8_t *dst, std::uint32_t v)
{
    dst[0] = (std::uint8_t)(v & 0xFF);
    dst[1] = (std::uint8_t)((v >> 8) & 0xFF);
    dst[2] = (std::uint8_t)((v >> 16) & 0xFF);
    dst[3] = (std::uint8_t)((v >> 24) & 0xFF);
}
inline std::uint16_t GetU16(const std::uint8_t *src)
{
    return (std::uint16_t)(src[0] | ((std::uint16_t)src[1] << 8));
}
inline std::uint32_t GetU32(const std::uint8_t *src)
{
    return (std::uint32_t)src[0] | ((std::uint32_t)src[1] << 8) |
           ((std::uint32_t)src[2] << 16) | ((std::uint32_t)src[3] << 24);
}

inline bool EncodeHeader(const Header &h, std::uint8_t *out, std::size_t cap)
{
    if (!out || cap < HeaderSize || h.payloadLen > MaxPayload)
        return false;
    PutU16(out + 0, Magic);
    PutU16(out + 2, h.protoVer);
    PutU16(out + 4, h.type);
    PutU32(out + 6, h.seq);
    PutU32(out + 10, h.ack);
    PutU32(out + 14, h.ackBits);
    PutU32(out + 18, h.tick);
    PutU16(out + 22, h.payloadLen);
    return true;
}

// Fails closed: bad magic, truncated header, or a payload length that
// overruns the supplied buffer are all rejected.
inline bool DecodeHeader(const std::uint8_t *in, std::size_t len, Header &out)
{
    if (!in || len < HeaderSize)
        return false;
    if (GetU16(in + 0) != Magic)
        return false;
    Header h;
    h.protoVer = GetU16(in + 2);
    h.type = GetU16(in + 4);
    h.seq = GetU32(in + 6);
    h.ack = GetU32(in + 10);
    h.ackBits = GetU32(in + 14);
    h.tick = GetU32(in + 18);
    h.payloadLen = GetU16(in + 22);
    if (h.payloadLen > MaxPayload || (std::size_t)h.payloadLen > len - HeaderSize)
        return false;
    out = h;
    return true;
}

// Selective acknowledgement over the 32-bit window behind `ack`.
inline std::uint32_t AckBitFor(std::uint32_t ack, std::uint32_t seq)
{
    const std::int32_t diff = (std::int32_t)(ack - seq);
    if (diff < 1 || diff > 32)
        return 0;
    return 1u << (diff - 1);
}
inline bool AckCovers(std::uint32_t ack, std::uint32_t ackBits, std::uint32_t seq)
{
    if (seq == ack)
        return true;
    return (ackBits & AckBitFor(ack, seq)) != 0;
}

} // namespace Wire
#endif
