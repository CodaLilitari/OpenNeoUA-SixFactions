#include "../src/world/net_protocol.h"
#include <cassert>
int main() {
    using namespace World::NetProtocol;
    assert(ProtoCompatible(ProtoVer));
    assert(!ProtoCompatible(0));
    assert(!ProtoCompatible(ProtoVer + 1));
    ResumeToken z = {}; ResumeToken a = {}; a.b[0] = 7;
    assert(!TokenValid(z)); assert(TokenValid(a));
    assert(TokenEqual(a, a)); assert(!TokenEqual(a, z));
    unsigned char vb[2]; EncodeProtoVer(ProtoVer, vb);
    assert(DecodeProtoVer(vb) == ProtoVer);
    EncodeProtoVer(0x1234, vb); assert(vb[0] == 0x34 && vb[1] == 0x12);
    assert(DecodeProtoVer(vb) == 0x1234 && !ProtoCompatible(0x1234));
    return 0;
}
