// Unattended lobby flags, engine side. Opt-in, next to -bot:
//   -net-name <callsign>   no spaces, at most 20 characters
//   -net-host <levelId>    host this network level
//   -net-join <address>    join a host (port 61234)
//   -net-faction <owner number or faction name>
//   -net-ready             client: send READY once every WELCOME is in
//   -net-start-when <n>    host: start once n players are in the lobby, all ready
// The flags walk the multiplayer screens through the code paths a player's
// clicks reach (provider choice, name, level list and AfterMapChoose,
// ConnectToServer, faction buttons, Ready 1208, Start 1200); only enabled
// widgets are "pressed". Bad input stops them with a netauto ERROR line in
// Data/Env/ypa_log.txt and on stderr. Without a -net-* flag nothing changes.
// The logic lives in world/net_lobby_flags.h (unit-tested).
#ifndef YW_NETAUTO_H_INCLUDED
#define YW_NETAUTO_H_INCLUDED

#include <cstdint>
#include <string>

class NC_STACK_ypaworld;
class NC_STACK_button;

namespace NetAuto {

bool InitFromCmdLine();            // after BotRuntime::InitFromCmdLine (uses the -bot faction)
void Tick(NC_STACK_ypaworld *world); // once per main-loop frame
void Shutdown();

// Called by the multiplayer screen's input handling when no widget was
// clicked this frame: the result code the flags press on this screen
// (netSelMode), or 0. A press made on another screen is never handed out, and
// none is left once the flags failed. Marks the Ready checkbox it presses.
std::uint16_t TakeNetworkCode(NC_STACK_button *networkButtons, int netSelMode);

bool Active();                     // -net-* flags were given
const char *StepName();
std::string Error();

} // namespace NetAuto
#endif
