// Provider events retain their original numeric values. Recovery events are
// distinct from a terminal departure, so an outage cannot destroy a seat.
#ifndef WINDP_EVENTS_H_INCLUDED
#define WINDP_EVENTS_H_INCLUDED

enum RECVMSG
{
    RECVMSG_NONE = 0,
    RECVMSG_NORMAL = 1,
    RECVMSG_CREATEPLAYER = 2,
    RECVMSG_DESTROYPLAYER = 3,
    RECVMSG_HOST = 4,
    RECVMSG_SYSUNK = 5,
    RECVMSG_SETSESSIONDESC = 6,
    RECVMSG_PLAYERAWAY = 7,
    RECVMSG_PLAYERBACK = 8,
    RECVMSG_PLAYERGONE = 9,
    RECVMSG_SELFLOST = 10,
    RECVMSG_SELFRESUMED = 11,
    RECVMSG_SESSIONEND = 12,
    RECVMSG_ROSTERRESET = 13,
    RECVMSG_RECOVERYFAILED = 14
};

#endif
