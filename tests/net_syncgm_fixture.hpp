#include "net_identity_outer_fixture.hpp"
// Only the settings read by the exact loader tail are substituted. No load,
// render, socket or simulation is executed by this target.
namespace System { namespace IniConf {
struct Setting { int value; template<class T>T Get()const{return static_cast<T>(value);} };
static const Setting NetWaitStart{150000}, NetKickoff{20000};
} }
