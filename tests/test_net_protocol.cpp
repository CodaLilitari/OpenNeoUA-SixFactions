#include "../src/world/net_protocol.h"
#include "require.h"
int main() {
    using namespace World::NetProtocol;
    REQUIRE(ProtoCompatible(ProtoVer));
    REQUIRE(!ProtoCompatible(0));
    REQUIRE(!ProtoCompatible(ProtoVer + 1));
    ResumeToken z = {}; ResumeToken a = {}; a.b[0] = 7;
    REQUIRE(!TokenValid(z)); REQUIRE(TokenValid(a));
    REQUIRE(TokenEqual(a, a)); REQUIRE(!TokenEqual(a, z));
    unsigned char vb[2]; EncodeProtoVer(ProtoVer, vb);
    REQUIRE(DecodeProtoVer(vb) == ProtoVer);
    EncodeProtoVer(0x1234, vb); REQUIRE(vb[0] == 0x34 && vb[1] == 0x12);
    REQUIRE(DecodeProtoVer(vb) == 0x1234 && !ProtoCompatible(0x1234));
    return 0;
}
