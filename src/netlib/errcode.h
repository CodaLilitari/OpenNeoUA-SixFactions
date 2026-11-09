#ifndef ZNDNET_ERRCODE_H_INCLUDED
#define ZNDNET_ERRCODE_H_INCLUDED

namespace ZNDNet
{

enum ERR_SES
{
    ERR_SES_CREATE = 0x00,
    ERR_SES_JOIN   = 0x01,
};

enum
{
    ERR_CONN_TIMEOUT    = 0x00,
    ERR_CONN_FULL       = 0x01,
    ERR_CONN_NAME       = 0x02,
    ERR_CONN_VERSION    = 0x03,
    ERR_CONN_RESUME     = 0x04,
    ERR_CONN_CLOSED     = 0x05,
    ERR_CONN_RANDOM     = 0x06,

    ERR_DISCONNECT_TIMEOUT = 0x00,
    ERR_DISCONNECT_SERVER  = 0x01,
};

}


#endif
