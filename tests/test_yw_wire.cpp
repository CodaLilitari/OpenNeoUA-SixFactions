#include "../src/game/yw_wire.h"
#include "../src/world/net_protocol.h"
#include "require.h"
#include <cstring>
int main() {
    using namespace Wire;
    Header in;
    in.protoVer = (std::uint16_t)World::NetProtocol::ProtoVer;
    in.type = 1000; in.seq = 0xDEADBEEF; in.ack = 77; in.ackBits = 0xA5A5A5A5;
    in.tick = 123456; in.payloadLen = 300;
    std::uint8_t buf[HeaderSize + 300];
    std::memset(buf, 0xEE, sizeof(buf));
    REQUIRE(EncodeHeader(in, buf, sizeof(buf)));
    REQUIRE(buf[0] == 0x41 && buf[1] == 0x55); // magic little-endian
    Header out;
    REQUIRE(DecodeHeader(buf, sizeof(buf), out));
    REQUIRE(out.protoVer == in.protoVer && out.type == in.type && out.seq == in.seq);
    REQUIRE(out.ack == in.ack && out.ackBits == in.ackBits && out.tick == in.tick);
    REQUIRE(out.payloadLen == in.payloadLen);
    // Fail closed: truncated, bad magic, payload overrun, oversize encode.
    REQUIRE(!DecodeHeader(buf, HeaderSize - 1, out));
    std::uint8_t bad[sizeof(buf)];
    std::memcpy(bad, buf, sizeof(buf)); bad[0] ^= 0xFF;
    REQUIRE(!DecodeHeader(bad, sizeof(bad), out));
    REQUIRE(!DecodeHeader(buf, HeaderSize + 299, out));
    Header big = in; big.payloadLen = (std::uint16_t)(MaxPayload + 1);
    REQUIRE(!EncodeHeader(big, buf, sizeof(buf)));
    Header maxOk = in; maxOk.payloadLen = (std::uint16_t)MaxPayload;
    std::uint8_t maxBuf[HeaderSize + MaxPayload];
    REQUIRE(EncodeHeader(maxOk, maxBuf, sizeof(maxBuf)));
    REQUIRE(DecodeHeader(maxBuf, sizeof(maxBuf), out) && out.payloadLen == MaxPayload);
    // Ack window: self, edges, outside, wraparound.
    REQUIRE(AckCovers(10, 0, 10));
    REQUIRE(AckCovers(10, AckBitFor(10, 9), 9));
    REQUIRE(AckCovers(10, AckBitFor(10, 10 - 32), 10 - 32));
    REQUIRE(AckBitFor(10, 10 - 33) == 0 && !AckCovers(10, 0xFFFFFFFFu, 10 - 33));
    REQUIRE(!AckCovers(10, 0, 11));
    REQUIRE(AckBitFor(2, 0xFFFFFFFEu) != 0 && AckCovers(2, AckBitFor(2, 0xFFFFFFFEu), 0xFFFFFFFEu));
    return 0;
}
