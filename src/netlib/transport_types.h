#ifndef ZND_TRANSPORT_TYPES_H
#define ZND_TRANSPORT_TYPES_H
#include <cstdint>
#include <string>
namespace ZNDNet
{
enum RosterStatus : std::uint8_t
{
    RosterOnline = 1,
    RosterAway = 2
};
enum LeaveReason : std::uint8_t
{
    LeaveLeft = 0,
    LeaveAway = 1,
    LeaveKicked = 2,
    LeaveExpired = 3
};
enum JoinReason : std::uint8_t
{
    JoinFresh = 0,
    JoinResumed = 1
};
enum class Transition : std::uint8_t
{
    Fresh,
    Resumed,
    Left,
    Away,
    Kicked,
    Expired
};
inline bool CallsignEqual(const std::string &a,const std::string &b)
{
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){auto fold=[](unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;};if(fold(a[i])!=fold(b[i]))return false;}
    return true;
}
}
#endif
