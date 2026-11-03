// Network messages of the multiplayer game: receive kinds here, message ids
// and wire structs in yw_net_msgs.h.
#ifndef  YWNET_H_INCLUDED
#define YWNET_H_INCLUDED

#include "game/yw.h"
#include "game/yw_net_msgs.h"


enum RECVMSG
{
    RECVMSG_NONE   = 0,
    RECVMSG_NORMAL = 1,
    RECVMSG_CREATEPLAYER = 2,
    RECVMSG_DESTROYPLAYER = 3,
    RECVMSG_HOST = 4,
    RECVMSG_SYSUNK = 5,
    RECVMSG_SETSESSIONDESC = 6
};

#endif
