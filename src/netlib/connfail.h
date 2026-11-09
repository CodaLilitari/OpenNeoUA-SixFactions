// Why a client connection attempt failed, as windp::Connect records it.
// Maps the transport's EVENT_CONNERR codes (errcode.h; ZNDNet and the ENet
// adapter use the same values) to one reason with a stable name for logs and
// tests. SDL-free on purpose: tests/test_net_connfail.cpp.
#ifndef ZNDNET_CONNFAIL_H_INCLUDED
#define ZNDNET_CONNFAIL_H_INCLUDED

#include <cstdint>
#include "errcode.h"

namespace ZNDNet
{

enum ConnectFailure
{
    CONNFAIL_NONE = 0,
    CONNFAIL_ADDRESS,   // the host name or address does not resolve
    CONNFAIL_NO_ANSWER, // no CONNECTED within the wait, or the transport's own timeout
    CONNFAIL_FULL,      // the session has no free slot (or is closed to newcomers)
    CONNFAIL_NAME,      // the callsign is taken (ENet; ZNDNet renames instead)
    CONNFAIL_VERSION,   // another protocol version or service id
    CONNFAIL_RANDOM,
    CONNFAIL_RESUME,
    CONNFAIL_CLOSED,
    CONNFAIL_DROPPED,   // the transport disconnected during the handshake
};

inline ConnectFailure ConnectFailureFromRefusal(uint32_t code)
{
    switch (code)
    {
    case ERR_CONN_FULL:    return CONNFAIL_FULL;
    case ERR_CONN_NAME:    return CONNFAIL_NAME;
    case ERR_CONN_VERSION: return CONNFAIL_VERSION;
    case ERR_CONN_RANDOM: return CONNFAIL_RANDOM;
    case ERR_CONN_RESUME: return CONNFAIL_RESUME;
    case ERR_CONN_CLOSED: return CONNFAIL_CLOSED;
    default:               return CONNFAIL_NO_ANSWER; // ERR_CONN_TIMEOUT
    }
}

inline const char *ConnectFailureName(int failure)
{
    switch (failure)
    {
    case CONNFAIL_NONE:      return "none";
    case CONNFAIL_ADDRESS:   return "address";
    case CONNFAIL_NO_ANSWER: return "no_answer";
    case CONNFAIL_FULL:      return "full";
    case CONNFAIL_NAME:      return "name";
    case CONNFAIL_VERSION:   return "version";
    case CONNFAIL_RANDOM: return "random";
    case CONNFAIL_RESUME: return "resume";
    case CONNFAIL_CLOSED: return "closed";
    case CONNFAIL_DROPPED:   return "dropped";
    default:                 return "unknown";
    }
}

// A failure that a retry with the same settings cannot overcome. A taken
// name is not final: the host may hold an orphaned callsign until its
// resume grace expires.
inline bool ConnectFailureIsFinal(int failure)
{
    return failure == CONNFAIL_RANDOM || failure == CONNFAIL_ADDRESS || failure == CONNFAIL_VERSION || failure == CONNFAIL_RESUME || failure == CONNFAIL_CLOSED;
}

}

#endif
