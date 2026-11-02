#ifndef OPENNEOUA_AGENT_FORK_PROTOCOL_H
#define OPENNEOUA_AGENT_FORK_PROTOCOL_H

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "protocol.h" // Agent::Integer (bounded line format, 4096-byte cap)

namespace Agent {

// Fork operations layered on the existing bounded line protocol
// ("1 SESSION ID EXPIRY_MS OP ARGS"). They drive World::Fork objects only;
// the pixel/key ops in protocol.h remain the human-speed outer loop and
// never enter fork stepping. Idempotency reuses Agent::Sequence unchanged.
enum class ForkOp { Create, Step, Observe, Commit, Discard, Clone };

struct ForkCommand
{
    std::uint32_t id = 0;
    std::uint64_t expires = 0;
    ForkOp op = ForkOp::Observe;
    std::uint64_t fork = 0; // fork id acted on (all ops except Create)
    std::uint64_t dst = 0;  // Commit destination fork id
    std::uint8_t owner = 0; // controller owner for Create/Observe
    std::uint32_t ticks = 0;
    std::uint32_t gid = 0;    // Step actor unit
    std::uint32_t target = 0; // Step attack target
    int x = 0, y = 0;         // Step move goal / create position (map units)
    int proto = 0;            // Step create/build prototype
    int cost = 0;             // Step create/build energy cost
    int action = 0;           // 0 hold 1 move 2 attack 3 create 4 build
};

struct ForkParsed
{
    bool valid = false;
    bool addressed = false;
    ForkCommand command;
    std::string error = "malformed_fork_request";
};

// Grammar (single newline-terminated line, printable ASCII):
//   fork_create OWNER
//   fork_step FORK ACTION GID TARGET X Y PROTO COST TICKS
//   fork_observe FORK OWNER
//   fork_commit FORK DST
//   fork_clone FORK OWNER
//   fork_discard FORK
inline ForkParsed ParseFork(const std::string &line, const std::string &session)
{
    ForkParsed p;
    if (line.empty() || line.size() > 4096 || line.back() != '\n')
        return p;
    for (std::size_t i = 0; i + 1 < line.size(); ++i)
        if (line[i] < 32 || line[i] > 126)
            return p;
    std::istringstream input(line);
    std::vector<std::string> tokens;
    for (std::string token; input >> token;)
        tokens.push_back(token);
    if (tokens.size() < 5 || tokens[0] != "1" || tokens[1] != session)
        return p;
    p.addressed = true;
    int id = 0;
    if (!Integer(tokens[2], 1, 2147483647, id))
        return p;
    if (tokens[3].empty() || tokens[3].size() > 13)
        return p;
    for (char c : tokens[3])
        if (c < '0' || c > '9')
            return p;

    const auto num = [&](std::size_t i, int lo, int hi, int &out) {
        return i < tokens.size() && Integer(tokens[i], lo, hi, out);
    };
    const auto big = [&](std::size_t i, std::uint64_t &out) {
        if (i >= tokens.size() || tokens[i].empty() || tokens[i].size() > 20)
            return false;
        std::uint64_t v = 0;
        for (char c : tokens[i])
        {
            if (c < '0' || c > '9')
                return false;
            if (v > (0xFFFFFFFFFFFFFFFFull - 9) / 10)
                return false;
            v = v * 10 + static_cast<unsigned>(c - '0');
        }
        out = v;
        return true;
    };

    ForkCommand &c = p.command;
    c.id = static_cast<std::uint32_t>(id);
    for (char ch : tokens[3])
        c.expires = c.expires * 10 + static_cast<unsigned>(ch - '0');
    int owner = 0, ticks = 0, value = 0;
    if (tokens[4] == "fork_create" && tokens.size() == 6 && num(5, 0, 255, owner))
    {
        c.op = ForkOp::Create;
        c.owner = static_cast<std::uint8_t>(owner);
    }
    else if (tokens[4] == "fork_step" && tokens.size() == 14 && big(5, c.fork) &&
             num(6, 0, 4, c.action) &&
             num(7, 0, 2147483647, value) && (c.gid = static_cast<std::uint32_t>(value), true) &&
             num(8, 0, 2147483647, value) && (c.target = static_cast<std::uint32_t>(value), true) &&
             num(9, -1000000, 1000000, c.x) && num(10, -1000000, 1000000, c.y) &&
             num(11, 0, 100000, c.proto) && num(12, 0, 1000000, c.cost) &&
             num(13, 1, 100000, ticks))
    {
        c.op = ForkOp::Step;
        c.ticks = static_cast<std::uint32_t>(ticks);
    }
    else if (tokens[4] == "fork_observe" && tokens.size() == 7 && big(5, c.fork) &&
             num(6, 0, 255, owner))
    {
        c.op = ForkOp::Observe;
        c.owner = static_cast<std::uint8_t>(owner);
    }
    else if (tokens[4] == "fork_commit" && tokens.size() == 7 && big(5, c.fork) &&
             big(6, c.dst))
    {
        c.op = ForkOp::Commit;
    }
    else if (tokens[4] == "fork_discard" && tokens.size() == 6 && big(5, c.fork))
    {
        c.op = ForkOp::Discard;
    }
    else if (tokens[4] == "fork_clone" && tokens.size() == 7 && big(5, c.fork) &&
             num(6, 0, 255, owner))
    {
        c.op = ForkOp::Clone;
        c.owner = static_cast<std::uint8_t>(owner);
    }
    else
    {
        return p;
    }
    p.valid = true;
    p.error.clear();
    return p;
}

// Maps a parsed Step command onto the fork action vocabulary. Returns false
// for Hold-with-payload and other nonsense combinations.
inline bool ToForkAction(const ForkCommand &c, int &kind, std::uint32_t &gid,
                         std::uint32_t &target, float &x, float &y, int &proto, int &cost)
{
    kind = c.action;
    gid = c.gid;
    target = c.target;
    x = static_cast<float>(c.x);
    y = static_cast<float>(c.y);
    proto = c.proto;
    cost = c.cost;
    switch (c.action)
    {
    case 0: return gid == 0 && target == 0;                    // hold
    case 1: return gid != 0;                                   // move
    case 2: return gid != 0 && target != 0 && gid != target;   // attack
    case 3: case 4: return proto > 0 && cost >= 0;             // create/build
    default: return false;
    }
}

} // namespace Agent
#endif
