// Network message ids and wire structs of the multiplayer game.
//
// Every message travels as a raw copy of its struct, in host byte order with
// the compiler's layout, padding included, so the layout is the wire format.
// The checks at the end of this file pin the size of every message and
// record and the offset of every member, as GCC lays them out for x86-64
// Linux. MSYS2 MinGW should agree: every member is a fixed-width integer, a
// char, a double or an aggregate of those (no long, no bit-field), with the
// same size and alignment on both targets. Where a build disagrees, these
// checks fail it. The header is self-contained so the tests can include it.
#ifndef YWNET_MSGS_H_INCLUDED
#define YWNET_MSGS_H_INCLUDED

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include "common/matrix.h"
#include "common/types.h"
#include "common/vectors.h"
#include "world/consts.h"

enum UAMSG
{
    UAMSG_BASE = 1000,
    UAMSG_LOAD = UAMSG_BASE,
    UAMSG_NEWVHCL = UAMSG_BASE + 1,
    UAMSG_DESTROYVHCL = UAMSG_BASE + 2,
    UAMSG_NEWLEADER = UAMSG_BASE + 3,
    UAMSG_NEWWEAPON = UAMSG_BASE + 4,
    UAMSG_SETSTATE = UAMSG_BASE + 5,
    UAMSG_VHCLDATA_I = UAMSG_BASE + 6,
    UAMSG_VHCLDATA_E = UAMSG_BASE + 7,

    UAMSG_DEAD = UAMSG_BASE + 9,
    UAMSG_VHCLENERGY = UAMSG_BASE + 10,
    UAMSG_SECTORENERGY = UAMSG_BASE + 11,
    UAMSG_STARTBUILD = UAMSG_BASE + 12,
    UAMSG_BUILDINGVHCL = UAMSG_BASE + 13,
    UAMSG_VIEWER = UAMSG_BASE + 14,
    UAMSG_SYNCGM = UAMSG_BASE + 15,
    UAMSG_HOSTDIE = UAMSG_BASE + 16,
    // +17 DEBUG
    UAMSG_MESSAGE = UAMSG_BASE + 18,
    UAMSG_KICK = UAMSG_BASE + 19,
    UAMSG_UPGRADE = UAMSG_BASE + 20,
    UAMSG_FRACTION = UAMSG_BASE + 21,
    UAMSG_WELCOME = UAMSG_BASE + 22,
    UAMSG_READY = UAMSG_BASE + 23,
    UAMSG_REQUPDATE = UAMSG_BASE + 24,
    UAMSG_UPDATE = UAMSG_BASE + 25,
    UAMSG_IMPULSE = UAMSG_BASE + 26,
    UAMSG_LOGMSG = UAMSG_BASE + 27,
    UAMSG_REORDER = UAMSG_BASE + 28,
    UAMSG_LOBBYINIT = UAMSG_BASE + 29,
    UAMSG_STARTPLASMA = UAMSG_BASE + 30,
    UAMSG_ENDPLASMA = UAMSG_BASE + 31,
    // +32 SET VP
    UAMSG_STARTBEAM = UAMSG_BASE + 33,
    UAMSG_ENDBEAM = UAMSG_BASE + 34,
    UAMSG_EXIT = UAMSG_BASE + 35,
    UAMSG_SETLEVEL = UAMSG_BASE + 36,
    UAMSG_CRC = UAMSG_BASE + 37,
    UAMSG_REQPING = UAMSG_BASE + 38,
    UAMSG_PONG = UAMSG_BASE + 39,
    UAMSG_STARTPROBLEM = UAMSG_BASE + 40,
    UAMSG_ENDPROBLEM = UAMSG_BASE + 41,
    //UAMSG_CD = UAMSG_BASE + 42,
    UAMSG_SCORE = UAMSG_BASE + 43
};

struct uamessage_base
{
    uint32_t msgID = 0;
    uint32_t tstamp = 0;
    uint32_t msgCnt = 0;
    uint8_t owner = 0;
    uint8_t p[3] = {0};
};

struct uamessage_load : uamessage_base
{
    uint32_t level = 0;
};

struct uamessage_newVhcl : uamessage_base
{
    vec3d pos;
    uint32_t parent = 0;
    uint32_t id = 0;
    uint32_t commandID = 0;
    uint8_t kind = 0;
    uint8_t type = 0;
    uint8_t np[2] = {0};
};

struct uamessage_destroyVhcl : uamessage_base
{
    uint32_t id = 0;
    uint8_t type = 0;
    uint8_t dp[3] = {0};
};

struct uamessage_newLeader : uamessage_base
{
    uint32_t id = 0;
    uint32_t newLeader = 0;
};

enum : uint8_t
{
    UAMSG_NEWWEAPON_FLAG_SILENT_SOUND_EVENTS = 1 << 0
};

struct uamessage_newWeapon : uamessage_base
{
    vec3d pos;
    uint32_t id = 0;
    uint32_t launcher = 0;
    vec3d dir;
    uint32_t target = 0;
    uint8_t flags = 0;
    uint8_t type = 0;
    uint8_t targetType = 0;
    uint8_t targetOwner = 0;
    vec3d targetPos;
};

struct uamessage_setState: uamessage_base
{
    uint32_t id = 0;
    uint32_t setFlags = 0;
    uint32_t unsetFlags = 0;
    uint8_t newStatus = 0;
    uint8_t classID = 0;
    uint8_t sp[2] = {0};
};


struct vhcldata
{
    int16_t pos_x = 0;
    int16_t pos_y = 0;
    int16_t pos_z = 0;
    int16_t vp = 0;
    int8_t roll = 0;
    int8_t pitch = 0;
    int8_t yaw = 0;
    uint8_t specialinfo = 0;
    uint32_t ident = 0;
    int32_t energy = 0;

    enum SI_FLAGS
    {
        SI_NORENDER = 1,
        SI_LAND = 2,
        SI_YPAGUN = 4,
        SI_UNK = 8,
        SI_ROBO = 0x10,
        SI_DSETTED = 0x20
    };
};

struct vhcldataE : vhcldata
{
    vec3d speed;
};

struct vhcldatahdr
{
    uint16_t number = 0;
    uint16_t size = 0;
    uint32_t diffTime = 0;
};

struct uamessage_vhclData : uamessage_base
{
    vhcldatahdr hdr;
};

struct uamessage_vhclDataI : uamessage_vhclData
{
    vhcldata data[1024];
};

struct uamessage_vhclDataE : uamessage_vhclData
{
    vhcldataE data[1024];
};

struct uamessage_dead : uamessage_base
{
    uint32_t id = 0;
    uint32_t newParent = 0;
    uint32_t killer = 0;
    uint8_t landed = 0;
    uint8_t classID = 0;
    uint8_t killerOwner = 0;
    uint8_t dp = 0;
};


struct uamessage_vhclEnergy : uamessage_base
{
    uint32_t id = 0;
    int32_t energy = 0;
    uint32_t killer = 0;
    uint8_t killerOwner = 0;
    uint8_t ep[3] = {0};
};

struct uamessage_sectorEnergy : uamessage_base
{
    vec3d pos;
    int32_t energy = 0;
    uint32_t whoHit = 0;
    uint8_t sectOwner = 0;
    uint8_t ep[3] = {0};
};

struct uamessage_startBuild : uamessage_base
{
    uint32_t bproto = 0;
    int16_t sec_x = 0;
    int16_t sec_y = 0;
};

struct uamessage_bldVhcl : uamessage_base
{
    struct bldVhcl
    {
        uint32_t id = 0;
        vec3d base;
        vec3d pos;
        int16_t protoID = 0;
        uint8_t bldp[2] = {0};
    };

    bldVhcl vhcl[8];
};

struct uamessage_viewer : uamessage_base
{
    uint32_t id = 0;
    uint32_t launcher = 0;
    uint8_t classID = 0;
    uint8_t view = 0;
    uint8_t vp[2] = {0};
};

struct uamessage_syncgame : uamessage_base
{
    uint32_t hostID = 0;
    uint32_t gun[ 8 ] = {0};
};

struct uamessage_hostDie : uamessage_base
{
    uint32_t killer = 0;
    uint8_t killerOwner = 0;
    uint8_t hdp[3] = {0};
};

struct uamessage_message : uamessage_base
{
    char message[64] = {0};
};

struct uamessage_kick : uamessage_base
{
    char text[64] = {0};
};

struct uamessage_upgrade : uamessage_base
{
    int16_t upgradeID = 0;
    int16_t enable = 0;
};

struct uamessage_fraction : uamessage_base
{
    int16_t freefrac = 0;
    int16_t newfrac = 0;
};

struct uamessage_welcome : uamessage_base
{
    int16_t fraction = 0;
    uint8_t rdy = 0;
};

struct uamessage_ready : uamessage_base
{
    uint8_t rdy = 0;
    uint8_t rp[3] = {0};
};

typedef uamessage_base uamessage_requpdate;

struct vhclUpdData
{
    uint8_t type = 0;
    uint8_t status = 0;
    uint8_t vhclID = 0;
    int8_t rot_x = 0;
    int8_t rot_y = 0;
    int8_t rot_z = 0;
    uint8_t pp[2] = {0};
    uint32_t status_flg = 0;
    uint32_t gid = 0;
    int32_t engy = 0;
    vec3d pos;
};

struct uamessage_update : uamessage_base
{
    uint32_t sz = 0;
    uint32_t num = 0;
    vhclUpdData data[1024];
};

struct uamessage_impulse : uamessage_base
{
    uint32_t id = 0;
    vec3d pos;
    int32_t impulse = 0;
    float mass = 0.0;
    vec3d dir;
    float dir_len = 0.0;
};

struct uamessage_logmsg : uamessage_base
{
    uint32_t sender = 0;
    uint8_t senderOwner = 0;
    uint8_t lgp[3] = {0};
    int32_t pri = 0;
    int32_t id = 0;
    int32_t pr1 = 0;
    int32_t pr2 = 0;
    int32_t pr3 = 0;
};

struct uamessage_reorder : uamessage_base
{
    uint32_t mode = 0;
    uint32_t comm = 0;
    uint32_t commID = 0;
    uint32_t num = 0;
    uint32_t sz = 0;
    uint32_t units[500] = {0};
};

struct uamessage_startPlasma : uamessage_base
{
    uint32_t time = 0;
    float scale = 0.0;
    uint32_t id = 0;
    vec3d pos;
    mat3x3 dir;
};

struct uamessage_endPlasma : uamessage_base
{
    uint32_t id = 0;
};

struct uamessage_startBeam : uamessage_base
{
    vec3d pos;
    uint32_t id = 0;
};

struct uamessage_endBeam : uamessage_base
{
    uint32_t id = 0;
};

struct uamessage_exit : uamessage_base
{
    uint8_t norm = 0;
    uint8_t qp[3] = {0};
};

struct uamessage_setLevel : uamessage_base
{
    char hostName[64] = {0};
    uint32_t lvlID = 0;
    uint8_t fractions[World::CVMaxNetPlayers] = {0};
};

typedef uamessage_setLevel uamessage_lobbyInit;

struct uamessage_crc : uamessage_base
{
    uint32_t checksum = 0;
};

struct uamessage_ping: uamessage_base
{
    uint32_t timestamp = 0;
};

struct uamessage_startproblem: uamessage_base
{
    uint32_t problem = 0;
};

struct uamessage_endproblem: uamessage_base
{
    uint32_t solved = 0;
};

struct uamessage_cd : uamessage_base
{
    uint8_t cd = 0;
    int8_t rdy = 0;
    uint8_t cd_p[2] = {0};
};

struct uamessage_score : uamessage_base
{
    std::array<World::TPlayerStatus, World::CVFractionsCount> status;
};

// Wire layout on x86-64: the size of every message and record, then the
// offset of every member. A change here breaks every peer that still uses
// the old layout, so it needs a protocol version bump (World::NetProtocol::
// ProtoVer and ProtoVerMin together), not just a recompile. The checks catch
// a member that moves, grows or shrinks, and padding that moves. They do not
// catch a member that changes type at the same size and offset (int32_t to
// float), which breaks the wire just as well.
#define YWNET_WIRE_SIZE(type, bytes) static_assert(sizeof(type) == (bytes), #type " changed size on the wire")
YWNET_WIRE_SIZE(uamessage_base, 16);
YWNET_WIRE_SIZE(uamessage_load, 20);
YWNET_WIRE_SIZE(uamessage_newVhcl, 56);
YWNET_WIRE_SIZE(uamessage_destroyVhcl, 24);
YWNET_WIRE_SIZE(uamessage_newLeader, 24);
YWNET_WIRE_SIZE(uamessage_newWeapon, 104);
YWNET_WIRE_SIZE(uamessage_setState, 32);
YWNET_WIRE_SIZE(vhcldata, 20);
YWNET_WIRE_SIZE(vhcldataE, 48);
YWNET_WIRE_SIZE(vhcldatahdr, 8);
YWNET_WIRE_SIZE(uamessage_vhclData, 24);                      // VHCLDATA_I/E header
YWNET_WIRE_SIZE(uamessage_vhclDataI, 24 + 20 * 1024);
YWNET_WIRE_SIZE(uamessage_vhclDataE, 24 + 48 * 1024);
YWNET_WIRE_SIZE(uamessage_dead, 32);
YWNET_WIRE_SIZE(uamessage_vhclEnergy, 32);
YWNET_WIRE_SIZE(uamessage_sectorEnergy, 56);
YWNET_WIRE_SIZE(uamessage_startBuild, 24);
YWNET_WIRE_SIZE(uamessage_bldVhcl::bldVhcl, 64);
YWNET_WIRE_SIZE(uamessage_bldVhcl, 16 + 8 * 64);
YWNET_WIRE_SIZE(uamessage_viewer, 28);
YWNET_WIRE_SIZE(uamessage_syncgame, 52);
YWNET_WIRE_SIZE(uamessage_hostDie, 24);
YWNET_WIRE_SIZE(uamessage_message, 80);
YWNET_WIRE_SIZE(uamessage_kick, 80);
YWNET_WIRE_SIZE(uamessage_upgrade, 20);
YWNET_WIRE_SIZE(uamessage_fraction, 20);
YWNET_WIRE_SIZE(uamessage_welcome, 20);
YWNET_WIRE_SIZE(uamessage_ready, 20);
YWNET_WIRE_SIZE(uamessage_requpdate, 16);
YWNET_WIRE_SIZE(vhclUpdData, 48);
YWNET_WIRE_SIZE(uamessage_update, 24 + 48 * 1024);             // header 24, up to 1024 records
YWNET_WIRE_SIZE(uamessage_impulse, 88);
YWNET_WIRE_SIZE(uamessage_logmsg, 44);
YWNET_WIRE_SIZE(uamessage_reorder, 36 + 4 * 500);              // header 36, up to 500 units
YWNET_WIRE_SIZE(uamessage_startPlasma, 128);
YWNET_WIRE_SIZE(uamessage_endPlasma, 20);
YWNET_WIRE_SIZE(uamessage_startBeam, 48);
YWNET_WIRE_SIZE(uamessage_endBeam, 20);
YWNET_WIRE_SIZE(uamessage_exit, 20);
YWNET_WIRE_SIZE(uamessage_setLevel, 92);
YWNET_WIRE_SIZE(uamessage_lobbyInit, 92);
YWNET_WIRE_SIZE(uamessage_crc, 20);
YWNET_WIRE_SIZE(uamessage_ping, 20);                           // REQPING and PONG
YWNET_WIRE_SIZE(uamessage_startproblem, 20);
YWNET_WIRE_SIZE(uamessage_endproblem, 20);
YWNET_WIRE_SIZE(uamessage_cd, 20);
YWNET_WIRE_SIZE(uamessage_score, 240);
#undef YWNET_WIRE_SIZE

// Offsets of the members each struct declares; inherited members are
// pinned on the struct that declares them. A struct that extends
// uamessage_base is not standard-layout, and offsetof on it is not portable
// C++. GCC and Clang compute it as for any struct without virtual bases and
// only warn about it, so that warning is off for these checks.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#define YWNET_WIRE_AT(type, member, bytes) \
    static_assert(offsetof(type, member) == (bytes), #type "::" #member " moved on the wire")
YWNET_WIRE_AT(uamessage_base, msgID, 0);
YWNET_WIRE_AT(uamessage_base, tstamp, 4);
YWNET_WIRE_AT(uamessage_base, msgCnt, 8);
YWNET_WIRE_AT(uamessage_base, owner, 12);
YWNET_WIRE_AT(uamessage_base, p, 13);
YWNET_WIRE_AT(uamessage_load, level, 16);
YWNET_WIRE_AT(uamessage_newVhcl, pos, 16);
YWNET_WIRE_AT(uamessage_newVhcl, parent, 40);
YWNET_WIRE_AT(uamessage_newVhcl, id, 44);
YWNET_WIRE_AT(uamessage_newVhcl, commandID, 48);
YWNET_WIRE_AT(uamessage_newVhcl, kind, 52);
YWNET_WIRE_AT(uamessage_newVhcl, type, 53);
YWNET_WIRE_AT(uamessage_newVhcl, np, 54);
YWNET_WIRE_AT(uamessage_destroyVhcl, id, 16);
YWNET_WIRE_AT(uamessage_destroyVhcl, type, 20);
YWNET_WIRE_AT(uamessage_destroyVhcl, dp, 21);
YWNET_WIRE_AT(uamessage_newLeader, id, 16);
YWNET_WIRE_AT(uamessage_newLeader, newLeader, 20);
YWNET_WIRE_AT(uamessage_newWeapon, pos, 16);
YWNET_WIRE_AT(uamessage_newWeapon, id, 40);
YWNET_WIRE_AT(uamessage_newWeapon, launcher, 44);
YWNET_WIRE_AT(uamessage_newWeapon, dir, 48);
YWNET_WIRE_AT(uamessage_newWeapon, target, 72);
YWNET_WIRE_AT(uamessage_newWeapon, flags, 76);
YWNET_WIRE_AT(uamessage_newWeapon, type, 77);
YWNET_WIRE_AT(uamessage_newWeapon, targetType, 78);
YWNET_WIRE_AT(uamessage_newWeapon, targetOwner, 79);
YWNET_WIRE_AT(uamessage_newWeapon, targetPos, 80);
YWNET_WIRE_AT(uamessage_setState, id, 16);
YWNET_WIRE_AT(uamessage_setState, setFlags, 20);
YWNET_WIRE_AT(uamessage_setState, unsetFlags, 24);
YWNET_WIRE_AT(uamessage_setState, newStatus, 28);
YWNET_WIRE_AT(uamessage_setState, classID, 29);
YWNET_WIRE_AT(uamessage_setState, sp, 30);
YWNET_WIRE_AT(vhcldata, pos_x, 0);
YWNET_WIRE_AT(vhcldata, pos_y, 2);
YWNET_WIRE_AT(vhcldata, pos_z, 4);
YWNET_WIRE_AT(vhcldata, vp, 6);
YWNET_WIRE_AT(vhcldata, roll, 8);
YWNET_WIRE_AT(vhcldata, pitch, 9);
YWNET_WIRE_AT(vhcldata, yaw, 10);
YWNET_WIRE_AT(vhcldata, specialinfo, 11);
YWNET_WIRE_AT(vhcldata, ident, 12);
YWNET_WIRE_AT(vhcldata, energy, 16);
YWNET_WIRE_AT(vhcldataE, speed, 24);
YWNET_WIRE_AT(vhcldatahdr, number, 0);
YWNET_WIRE_AT(vhcldatahdr, size, 2);
YWNET_WIRE_AT(vhcldatahdr, diffTime, 4);
YWNET_WIRE_AT(uamessage_vhclData, hdr, 16);
YWNET_WIRE_AT(uamessage_vhclDataI, data, 24);
YWNET_WIRE_AT(uamessage_vhclDataE, data, 24);
YWNET_WIRE_AT(uamessage_dead, id, 16);
YWNET_WIRE_AT(uamessage_dead, newParent, 20);
YWNET_WIRE_AT(uamessage_dead, killer, 24);
YWNET_WIRE_AT(uamessage_dead, landed, 28);
YWNET_WIRE_AT(uamessage_dead, classID, 29);
YWNET_WIRE_AT(uamessage_dead, killerOwner, 30);
YWNET_WIRE_AT(uamessage_dead, dp, 31);
YWNET_WIRE_AT(uamessage_vhclEnergy, id, 16);
YWNET_WIRE_AT(uamessage_vhclEnergy, energy, 20);
YWNET_WIRE_AT(uamessage_vhclEnergy, killer, 24);
YWNET_WIRE_AT(uamessage_vhclEnergy, killerOwner, 28);
YWNET_WIRE_AT(uamessage_vhclEnergy, ep, 29);
YWNET_WIRE_AT(uamessage_sectorEnergy, pos, 16);
YWNET_WIRE_AT(uamessage_sectorEnergy, energy, 40);
YWNET_WIRE_AT(uamessage_sectorEnergy, whoHit, 44);
YWNET_WIRE_AT(uamessage_sectorEnergy, sectOwner, 48);
YWNET_WIRE_AT(uamessage_sectorEnergy, ep, 49);
YWNET_WIRE_AT(uamessage_startBuild, bproto, 16);
YWNET_WIRE_AT(uamessage_startBuild, sec_x, 20);
YWNET_WIRE_AT(uamessage_startBuild, sec_y, 22);
YWNET_WIRE_AT(uamessage_bldVhcl, vhcl, 16);
YWNET_WIRE_AT(uamessage_bldVhcl::bldVhcl, id, 0);
YWNET_WIRE_AT(uamessage_bldVhcl::bldVhcl, base, 8);
YWNET_WIRE_AT(uamessage_bldVhcl::bldVhcl, pos, 32);
YWNET_WIRE_AT(uamessage_bldVhcl::bldVhcl, protoID, 56);
YWNET_WIRE_AT(uamessage_bldVhcl::bldVhcl, bldp, 58);
YWNET_WIRE_AT(uamessage_viewer, id, 16);
YWNET_WIRE_AT(uamessage_viewer, launcher, 20);
YWNET_WIRE_AT(uamessage_viewer, classID, 24);
YWNET_WIRE_AT(uamessage_viewer, view, 25);
YWNET_WIRE_AT(uamessage_viewer, vp, 26);
YWNET_WIRE_AT(uamessage_syncgame, hostID, 16);
YWNET_WIRE_AT(uamessage_syncgame, gun, 20);
YWNET_WIRE_AT(uamessage_hostDie, killer, 16);
YWNET_WIRE_AT(uamessage_hostDie, killerOwner, 20);
YWNET_WIRE_AT(uamessage_hostDie, hdp, 21);
YWNET_WIRE_AT(uamessage_message, message, 16);
YWNET_WIRE_AT(uamessage_kick, text, 16);
YWNET_WIRE_AT(uamessage_upgrade, upgradeID, 16);
YWNET_WIRE_AT(uamessage_upgrade, enable, 18);
YWNET_WIRE_AT(uamessage_fraction, freefrac, 16);
YWNET_WIRE_AT(uamessage_fraction, newfrac, 18);
YWNET_WIRE_AT(uamessage_welcome, fraction, 16);
YWNET_WIRE_AT(uamessage_welcome, rdy, 18);
YWNET_WIRE_AT(uamessage_ready, rdy, 16);
YWNET_WIRE_AT(uamessage_ready, rp, 17);
YWNET_WIRE_AT(vhclUpdData, type, 0);
YWNET_WIRE_AT(vhclUpdData, status, 1);
YWNET_WIRE_AT(vhclUpdData, vhclID, 2);
YWNET_WIRE_AT(vhclUpdData, rot_x, 3);
YWNET_WIRE_AT(vhclUpdData, rot_y, 4);
YWNET_WIRE_AT(vhclUpdData, rot_z, 5);
YWNET_WIRE_AT(vhclUpdData, pp, 6);
YWNET_WIRE_AT(vhclUpdData, status_flg, 8);
YWNET_WIRE_AT(vhclUpdData, gid, 12);
YWNET_WIRE_AT(vhclUpdData, engy, 16);
YWNET_WIRE_AT(vhclUpdData, pos, 24);
YWNET_WIRE_AT(uamessage_update, sz, 16);
YWNET_WIRE_AT(uamessage_update, num, 20);
YWNET_WIRE_AT(uamessage_update, data, 24);
YWNET_WIRE_AT(uamessage_impulse, id, 16);
YWNET_WIRE_AT(uamessage_impulse, pos, 24);
YWNET_WIRE_AT(uamessage_impulse, impulse, 48);
YWNET_WIRE_AT(uamessage_impulse, mass, 52);
YWNET_WIRE_AT(uamessage_impulse, dir, 56);
YWNET_WIRE_AT(uamessage_impulse, dir_len, 80);
YWNET_WIRE_AT(uamessage_logmsg, sender, 16);
YWNET_WIRE_AT(uamessage_logmsg, senderOwner, 20);
YWNET_WIRE_AT(uamessage_logmsg, lgp, 21);
YWNET_WIRE_AT(uamessage_logmsg, pri, 24);
YWNET_WIRE_AT(uamessage_logmsg, id, 28);
YWNET_WIRE_AT(uamessage_logmsg, pr1, 32);
YWNET_WIRE_AT(uamessage_logmsg, pr2, 36);
YWNET_WIRE_AT(uamessage_logmsg, pr3, 40);
YWNET_WIRE_AT(uamessage_reorder, mode, 16);
YWNET_WIRE_AT(uamessage_reorder, comm, 20);
YWNET_WIRE_AT(uamessage_reorder, commID, 24);
YWNET_WIRE_AT(uamessage_reorder, num, 28);
YWNET_WIRE_AT(uamessage_reorder, sz, 32);
YWNET_WIRE_AT(uamessage_reorder, units, 36);
YWNET_WIRE_AT(uamessage_startPlasma, time, 16);
YWNET_WIRE_AT(uamessage_startPlasma, scale, 20);
YWNET_WIRE_AT(uamessage_startPlasma, id, 24);
YWNET_WIRE_AT(uamessage_startPlasma, pos, 32);
YWNET_WIRE_AT(uamessage_startPlasma, dir, 56);
YWNET_WIRE_AT(uamessage_endPlasma, id, 16);
YWNET_WIRE_AT(uamessage_startBeam, pos, 16);
YWNET_WIRE_AT(uamessage_startBeam, id, 40);
YWNET_WIRE_AT(uamessage_endBeam, id, 16);
YWNET_WIRE_AT(uamessage_exit, norm, 16);
YWNET_WIRE_AT(uamessage_exit, qp, 17);
YWNET_WIRE_AT(uamessage_setLevel, hostName, 16);
YWNET_WIRE_AT(uamessage_setLevel, lvlID, 80);
YWNET_WIRE_AT(uamessage_setLevel, fractions, 84);
YWNET_WIRE_AT(uamessage_crc, checksum, 16);
YWNET_WIRE_AT(uamessage_ping, timestamp, 16);
YWNET_WIRE_AT(uamessage_startproblem, problem, 16);
YWNET_WIRE_AT(uamessage_endproblem, solved, 16);
YWNET_WIRE_AT(uamessage_cd, cd, 16);
YWNET_WIRE_AT(uamessage_cd, rdy, 17);
YWNET_WIRE_AT(uamessage_cd, cd_p, 18);
YWNET_WIRE_AT(uamessage_score, status, 16);
#undef YWNET_WIRE_AT
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Outgoing messages go onto the wire byte for byte, padding included, and
// default member values do not touch padding: IMPULSE, STARTPLASMA,
// SECTORENERGY, STARTBEAM, WELCOME, SETLEVEL, the BUILDINGVHCL records and the
// UPDATE and VHCLDATA_E records all have holes that would carry whatever the
// stack held. NetMsgClear() fills a message with zero bytes and then applies
// its default member values again. Every sender calls it before setting
// fields, also on the static buffers that are reused for each send.
template <typename T>
inline void NetMsgClear(T &msg)
{
    static_assert(std::is_trivially_copyable<T>::value && std::is_trivially_destructible<T>::value,
                  "network messages are raw byte copies");
    std::memset(static_cast<void *>(&msg), 0, sizeof(T));
    ::new (static_cast<void *>(&msg)) T;
}

// The STARTBUILD a station's process broadcasts once its own ypaworld_func148
// has started building `buildingID` on cell (sectorX, sectorY) for `owner`
// (NC_STACK_yparobo::StartBuilding). Every peer starts the same construction
// from it (yw_net.cpp).
inline void NetMsgFillStartBuild(uamessage_startBuild &msg, std::uint8_t owner,
                                 std::uint32_t buildingID, std::int16_t sectorX,
                                 std::int16_t sectorY)
{
    NetMsgClear(msg);
    msg.msgID = UAMSG_STARTBUILD;
    msg.owner = owner;
    msg.bproto = buildingID;
    msg.sec_x = sectorX;
    msg.sec_y = sectorY;
}

// When a message can be applied. Lobby messages describe the lobby and mean
// nothing once a level is loaded: the stations are seated by then. Level
// messages act on the loaded world and cannot be applied without one.
// Chat works in both. Unknown ids count as level messages.
enum class NetMsgScope { Lobby, Level, Any };

inline NetMsgScope NetMsgScopeOf(std::uint32_t msgID)
{
    switch (msgID)
    {
    case UAMSG_LOAD:
    case UAMSG_FRACTION:
    case UAMSG_WELCOME:
    case UAMSG_READY:
    case UAMSG_LOBBYINIT:
    case UAMSG_SETLEVEL:
    case UAMSG_CRC:
        return NetMsgScope::Lobby;
    case UAMSG_MESSAGE:
        return NetMsgScope::Any;
    default:
        return NetMsgScope::Level;
    }
}

// Whether a world position lies on a map of mapW x mapH sectors, read
// exactly the way World::PositionToSectorID reads it: column x / length and
// row -z / length, each truncated toward zero by the conversion to int. So a
// position less than one sector west or north of the map lands in column or
// row 0, as it does on the sender. Coordinates that are not finite are off
// the map: converting them to int is undefined.
inline bool NetPositionOnMap(double x, double y, double z, int mapW, int mapH)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    const double east = x / World::CVSectorLength;
    const double south = -z / World::CVSectorLength;
    // Truncated toward zero, v gives a cell in [0, n) exactly when -1 < v < n.
    return east > -1.0 && east < mapW && south > -1.0 && south < mapH;
}

#endif
