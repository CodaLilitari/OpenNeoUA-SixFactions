#ifndef WORLD_NET_BOT_H_INCLUDED
#define WORLD_NET_BOT_H_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "fork.h"
#include "net_faction_selection.h"
#include "net_factions.h"

namespace World { namespace NetBot {

// Multiplayer bots. A bot is a real client of the existing ZNDNet/windp
// protocol (same ServiceId handshake, same UAMSG lifecycle as a human); this
// header is its brain + lobby logic, kept SDL-free so it is unit-testable.
// The brain only ever reads fog-filtered observations (see Fork::Observe
// rules) and only issues human-equivalent orders; difficulty scales speed,
// sensor range, lookahead depth and noise — never information access.

enum class Difficulty : std::uint8_t { Cadet = 0, Normal = 1, Veteran = 2 };

struct DifficultyParams
{
    std::int32_t reactionMs;   // strategic re-think interval (AI gate is 250ms)
    float sensorRange;         // observation sensor envelope (1800..2160 cap)
    int lookaheadDepth;        // 0 = greedy, 1 = my move, 2 = + enemy reply
    std::uint32_t noisePct;    // % score jitter (higher = weaker play)
    std::uint32_t maxOrdersPerMinute;
};

inline DifficultyParams ParamsFor(Difficulty d)
{
    switch (d)
    {
    case Difficulty::Cadet:   return {1500, 1800.0f, 0, 30, 20};
    case Difficulty::Veteran: return {250, 2160.0f, 2, 0, 120};
    default:                  return {750, 2000.0f, 1, 10, 60};
    }
}

// Station AI gate (engine side: NC_STACK_yparobo::AI_layer3). A bot's
// station is marked as the player robo because it is the local station,
// but no human drives it: it must run the native station AI
// (AI_checkWorld/AI_doWish/AI_doMove) exactly like a computer station.
// Human player stations keep the player path.
inline bool StationUsesNativeAI(bool isPlayerRobo, bool botControlled)
{
    return !isPlayerRobo || botControlled;
}

struct BotProfile
{
    std::uint8_t owner = 0;
    const char *factionName = "";
    // Strategic wish weights (mirror NC_STACK_yparobo ROBO_ATT_EP_* wishes).
    int wConquer = 50, wDefense = 50, wRadar = 50, wPower = 50;
    int wSafety = 50, wRecon = 50, wRoboAttack = 50;
    int squadSize = 3;
    bool canConstruct = true; // Sulgogars: biological roster, no building
};

// One distinct personality per faction, in NetFactions::Owners order.
// Pairwise distinctness is asserted by tests/test_net_bot.cpp.
inline const BotProfile &ProfileForOwner(std::uint8_t owner)
{
    static const BotProfile kProfiles[] = {
        {1, "Resistance", 50, 50, 50, 50, 50, 50, 50, 3, true},
        {6, "Ghorkov",    90, 80, 30, 50, 30, 30, 85, 5, true},
        {3, "Mykonian",   55, 45, 95, 60, 45, 90, 60, 3, true},
        {4, "Taerkasten", 40, 95, 40, 70, 90, 35, 55, 4, true},
        {5, "Black Sect", 75, 40, 55, 45, 35, 60, 80, 3, true},
        {2, "Sulgogars",  70, 55, 20, 10, 40, 65, 70, 6, false},
    };
    for (const BotProfile &p : kProfiles)
        if (p.owner == owner)
            return p;
    return kProfiles[0];
}
inline const BotProfile *AllProfiles(std::size_t &count)
{
    static const BotProfile kAll[] = {
        ProfileForOwner(1), ProfileForOwner(6), ProfileForOwner(3),
        ProfileForOwner(4), ProfileForOwner(5), ProfileForOwner(2),
    };
    count = 6;
    return kAll;
}

struct BotSpec
{
    std::uint8_t owner = 0;
    Difficulty difficulty = Difficulty::Normal;
    bool valid = false;
};

// Parses "Ghorkov:Veteran", "black-sect:cadet", "2:0" (owner:difficulty-level).
// Faction names are case-insensitive; spaces/dashes are ignored.
inline BotSpec ParseBotSpec(const std::string &text)
{
    BotSpec spec;
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos)
        return spec;
    std::string faction = text.substr(0, colon);
    std::string level = text.substr(colon + 1);
    const auto norm = [](std::string s) {
        std::string out;
        for (char c : s)
        {
            if (c == ' ' || c == '-' || c == '_')
                continue;
            out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        return out;
    };
    faction = norm(faction);
    level = norm(level);
    static const char *kNames[] = {"resistance", "ghorkov", "mykonian",
                                   "taerkasten", "blacksect", "sulgogars"};
    static const std::uint8_t kOwners[] = {1, 6, 3, 4, 5, 2};
    for (int i = 0; i < 6; ++i)
        if (faction == kNames[i])
            spec.owner = kOwners[i];
    if (!spec.owner && !faction.empty() &&
        std::find(faction.begin(), faction.end(), ':') == faction.end())
    {
        // numeric owner id form ("6:veteran")
        bool digits = true;
        int v = 0;
        for (char c : faction)
        {
            if (c < '0' || c > '9') digits = false;
            else v = v * 10 + (c - '0');
        }
        if (digits)
            for (int o : NetFactions::Owners)
                if (v == o)
                    spec.owner = static_cast<std::uint8_t>(o);
    }
    if (!spec.owner)
        return spec;
    if (level == "cadet" || level == "0") spec.difficulty = Difficulty::Cadet;
    else if (level == "normal" || level == "1") spec.difficulty = Difficulty::Normal;
    else if (level == "veteran" || level == "2") spec.difficulty = Difficulty::Veteran;
    else return BotSpec();
    spec.valid = true;
    return spec;
}

// The lobby, not the command line, decides which faction a bot plays: a
// "-bot Ghorkov:Veteran" client can be seated as Taerkasten. The brain must
// then think as the faction it actually owns, both for its own-unit set and
// for the fog view; thinking as the requested owner would read another
// player's view (or none). Returns the spec bound to the seated owner and
// reports whether it differed from the request.
inline BotSpec BindSpecToOwner(const BotSpec &requested, std::uint8_t seatedOwner,
                               bool &mismatch)
{
    BotSpec bound = requested;
    mismatch = requested.valid && seatedOwner != requested.owner;
    if (requested.valid && seatedOwner)
        bound.owner = seatedOwner;
    return bound;
}

// Callsign for a bot: "Bot-<Faction>-<Cdt|Nrm|Vet>", letters and dashes
// only, at most MaxCallsignLength characters. The name screen refuses spaces
// and '*', and ZNDNet cuts names to 20 characters (ZNDNET_USER_NAME_MAX).
constexpr std::size_t MaxCallsignLength = 20;

inline std::string BotDisplayName(const BotSpec &spec)
{
    std::string name = "Bot-";
    for (const char *c = ProfileForOwner(spec.owner).factionName; *c; ++c)
        if (*c != ' ')
            name += *c;
    name += spec.difficulty == Difficulty::Cadet ? "-Cdt" :
            spec.difficulty == Difficulty::Veteran ? "-Vet" : "-Nrm";
    return name;
}

enum class OrderKind : std::uint8_t { None, Move, Attack, Build, Create, Hold };

struct BotOrder
{
    OrderKind kind = OrderKind::None;
    std::uint32_t gid = 0;
    std::uint32_t targetGid = 0;
    float x = 0.0f, y = 0.0f;
    std::int32_t protoId = 0;
    std::int32_t cost = 0;
    double score = 0.0;
};

// Station wish weights (NC_STACK_yparobo ROBO_ATT_EP_* setters).
struct WishWeights
{
    int conquer = 50, defense = 50, radar = 50, power = 50;
    int safety = 50, recon = 50, roboAttack = 50;
};

inline WishWeights BaseWeights(const BotProfile &p)
{
    WishWeights w;
    w.conquer = p.wConquer; w.defense = p.wDefense; w.radar = p.wRadar;
    w.power = p.wPower; w.safety = p.wSafety; w.recon = p.wRecon;
    w.roboAttack = p.wRoboAttack;
    return w;
}

// Stateless steering: every think starts from the faction profile and lifts
// only the wish matching the chosen order. Writing 100 into a wish and never
// restoring it would ratchet every faction toward the same all-100 station.
inline WishWeights WeightsForOrder(const BotProfile &p, OrderKind kind, int boost = 30)
{
    WishWeights w = BaseWeights(p);
    const auto lift = [boost](int base) { return std::min(100, base + boost); };
    switch (kind)
    {
    case OrderKind::Attack: w.roboAttack = lift(w.roboAttack); break;
    case OrderKind::Build:  w.power = lift(w.power); w.radar = lift(w.radar); break;
    case OrderKind::Move:   w.conquer = lift(w.conquer); break;
    default: break;
    }
    return w;
}

inline float SectorCenterX(const Fork::WorldState &s, std::int32_t sectorId)
{
    return (sectorId % s.mapW + 0.5f) * World::CVSectorLength;
}
inline float SectorCenterY(const Fork::WorldState &s, std::int32_t sectorId)
{
    return (sectorId / s.mapW + 0.5f) * World::CVSectorLength;
}

// Scores every legal order from fog-filtered observation only. Enemy state
// enters exclusively through Fork::UnitVisibleTo / visible sectors, exactly
// what a human client process could know. Deterministic for a given rng.
inline std::vector<BotOrder> CandidateOrders(const Fork::WorldState &state,
                                             const BotSpec &spec,
                                             Fork::ForkRng &rng)
{
    std::vector<BotOrder> orders;
    if (!spec.valid || state.mapW <= 0)
        return orders;
    const BotProfile &prof = ProfileForOwner(spec.owner);
    const DifficultyParams dp = ParamsFor(spec.difficulty);
    const auto jitter = [&](double base) {
        if (!dp.noisePct)
            return base;
        const double f = 1.0 + (static_cast<double>(rng.Below(200)) - 100.0) / 100.0 *
                                   (static_cast<double>(dp.noisePct) / 100.0);
        return base * f;
    };
    // Own combat units and visible enemies (legal observations only).
    std::vector<const Fork::UnitState *> own, foes;
    for (const Fork::UnitState &u : state.units)
    {
        if (u.owner == spec.owner)
            own.push_back(&u);
        else if (Fork::UnitVisibleTo(state, u, spec.owner, dp.sensorRange))
            foes.push_back(&u);
    }
    if (own.empty())
        return orders;
    // Attack: strongest own unit vs weakest visible foe.
    if (!foes.empty())
    {
        const Fork::UnitState *bestOwn = own[0];
        const Fork::UnitState *bestFoe = foes[0];
        for (const Fork::UnitState *u : own)
            if (u->energy > bestOwn->energy) bestOwn = u;
        for (const Fork::UnitState *u : foes)
            if (u->energy < bestFoe->energy) bestFoe = u;
        BotOrder o;
        o.kind = OrderKind::Attack;
        o.gid = bestOwn->gid;
        o.targetGid = bestFoe->gid;
        o.score = jitter(prof.wRoboAttack + (bestFoe->owner == 0 ? 0 : 20));
        orders.push_back(o);
    }
    // Conquer/defend/recon over visible sectors.
    for (const Fork::SectorState &s : state.sectors)
    {
        if (s.border)
            continue;
        const bool visible = s.owner == spec.owner || (s.viewMask & (1u << spec.owner));
        if (!visible)
            continue;
        const double value = 20.0 + s.energyPower * 0.05 +
                             (s.purpose == 2 ? 30.0 : 0.0); // power station bonus
        BotOrder o;
        o.x = SectorCenterX(state, s.id);
        o.y = SectorCenterY(state, s.id);
        o.gid = own[0]->gid;
        if (s.owner == spec.owner)
        {
            o.kind = OrderKind::Move; // defend: stage on own sector
            o.score = jitter(prof.wDefense * value / 100.0);
        }
        else if (s.owner == 0)
        {
            o.kind = OrderKind::Move; // recon/expand into neutral
            o.score = jitter((prof.wRecon + prof.wConquer) * value / 200.0);
        }
        else
        {
            o.kind = OrderKind::Move; // conquer enemy sector
            o.score = jitter(prof.wConquer * value / 100.0);
        }
        orders.push_back(o);
    }
    // Build: spend a rich own sector on construction (never for Sulgogars).
    if (prof.canConstruct)
    {
        for (const Fork::SectorState &s : state.sectors)
        {
            if (s.owner != spec.owner || s.border || s.energyPower < 100)
                continue;
            BotOrder o;
            o.kind = OrderKind::Build;
            o.x = SectorCenterX(state, s.id);
            o.y = SectorCenterY(state, s.id);
            o.protoId = 1;
            o.cost = 50;
            o.score = jitter(prof.wPower * 0.4 + prof.wRadar * 0.2);
            orders.push_back(o);
            break;
        }
    }
    std::sort(orders.begin(), orders.end(), [](const BotOrder &a, const BotOrder &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.gid != b.gid) return a.gid < b.gid;
        return a.targetGid < b.targetGid;
    });
    return orders;
}

inline Fork::ForkAction ToForkAction(const BotOrder &o)
{
    Fork::ForkAction a;
    a.gid = o.gid;
    a.targetGid = o.targetGid;
    a.x = o.x;
    a.y = o.y;
    a.protoId = o.protoId;
    a.cost = o.cost;
    switch (o.kind)
    {
    case OrderKind::Move: a.kind = Fork::ActionKind::Move; break;
    case OrderKind::Attack: a.kind = Fork::ActionKind::Attack; break;
    case OrderKind::Build: a.kind = Fork::ActionKind::Build; break;
    case OrderKind::Create: a.kind = Fork::ActionKind::Create; break;
    default: a.kind = Fork::ActionKind::Hold; break;
    }
    return a;
}

// Position evaluation from one owner's legal view: own energy/sectors minus
// visible enemy energy. Used only to rank forked rollouts.
inline double EvaluateFor(const Fork::WorldState &state, std::uint8_t owner,
                          float sensorRange)
{
    double score = 0.0;
    for (const Fork::SectorState &s : state.sectors)
        if (s.owner == owner)
            score += 25.0 + s.energyPower * 0.02;
    for (const Fork::UnitState &u : state.units)
    {
        if (u.owner == owner)
            score += u.energy * 0.1;
        else if (Fork::UnitVisibleTo(state, u, owner, sensorRange))
            score -= u.energy * 0.1;
    }
    return score;
}

// What Decide did with its lookahead, so the caller can report a lookahead
// that could not run instead of silently playing the greedy choice.
struct DecideReport
{
    std::uint32_t forksCreated = 0;  // candidate and reply forks stepped
    std::uint32_t forkFailures = 0;  // forks Fork::Create refused (state above the caps)
    bool greedyFallback = false;     // lookahead was due but scored nothing
};

// Decision with agentic-fork lookahead where difficulty calls for it:
// candidates are stepped in throwaway forks (same Fork::Step the local
// what-if path uses), Veteran additionally steps an assumed enemy reply.
// The live state is never mutated; only the winning order is returned.
inline BotOrder Decide(const Fork::WorldState &state, const BotSpec &spec,
                       Fork::ForkRng &rng, Fork::ForkId baseId,
                       DecideReport *report = nullptr)
{
    DecideReport local;
    DecideReport &out = report ? *report : local;
    out = DecideReport();
    BotOrder best;
    std::vector<BotOrder> candidates = CandidateOrders(state, spec, rng);
    if (candidates.empty())
        return best;
    const DifficultyParams dp = ParamsFor(spec.difficulty);
    if (dp.lookaheadDepth == 0 || !baseId.Valid())
    {
        best = candidates[0];
        return best;
    }
    Fork::ForkConfig cfg;
    cfg.controllerOwner = spec.owner;
    cfg.seed = rng.State();
    double bestScore = -1e100;
    bool scored = false;
    const std::size_t limit = std::min<std::size_t>(candidates.size(), 4);
    for (std::size_t i = 0; i < limit; ++i)
    {
        std::unique_ptr<Fork::Fork> fork = Fork::Fork::Create(state, cfg, baseId);
        if (!fork)
        {
            ++out.forkFailures;
            continue;
        }
        ++out.forksCreated;
        Fork::ForkActionBatch batch;
        batch.push_back(ToForkAction(candidates[i]));
        if (!fork->Step(batch, 25))
            continue;
        if (dp.lookaheadDepth >= 2)
        {
            // Assumed reply: strongest visible enemy faction attacks back.
            for (const Fork::UnitState &u : fork->State().units)
            {
                if (u.owner == spec.owner || u.owner == 0)
                    continue;
                BotSpec foe;
                foe.owner = u.owner;
                foe.difficulty = Difficulty::Normal;
                foe.valid = true;
                Fork::ForkRng foeRng(rng.State() ^ u.owner);
                std::vector<BotOrder> replies = CandidateOrders(fork->State(), foe, foeRng);
                if (!replies.empty())
                {
                    Fork::ForkConfig foeCfg = cfg;
                    foeCfg.controllerOwner = u.owner;
                    std::unique_ptr<Fork::Fork> replyFork =
                        Fork::Fork::Create(fork->State(), foeCfg, Fork::ForkId{baseId.v + 100 + i});
                    if (!replyFork)
                        ++out.forkFailures;
                    else
                    {
                        ++out.forksCreated;
                        Fork::ForkActionBatch rb;
                        rb.push_back(ToForkAction(replies[0]));
                        replyFork->Step(rb, 25);
                        scored = true;
                        const double s = EvaluateFor(replyFork->State(), spec.owner, dp.sensorRange);
                        if (s > bestScore) { bestScore = s; best = candidates[i]; }
                    }
                }
                break;
            }
            if (bestScore == -1e100)
            {
                scored = true;
                const double s = EvaluateFor(fork->State(), spec.owner, dp.sensorRange);
                if (s > bestScore) { bestScore = s; best = candidates[i]; }
            }
        }
        else
        {
            scored = true;
            const double s = EvaluateFor(fork->State(), spec.owner, dp.sensorRange);
            if (s > bestScore) { bestScore = s; best = candidates[i]; }
        }
    }
    if (best.kind == OrderKind::None)
        best = candidates[0];
    out.greedyFallback = !scored;
    return best;
}

// Lobby lifecycle for one bot slot. Mirrors the human flow: lobby roster ->
// join (WELCOME/FRACTION) -> READY -> in game; EXIT on leave; a disconnected
// human's slot may be taken over (same owner) and later reclaimed by a human.
enum class BotPhase : std::uint8_t
{
    Lobby, Joining, Ready, InGame, Leaving, Gone, Takeover, Reclaiming
};

struct BotLifecycle
{
    BotPhase phase = BotPhase::Lobby;
    BotSpec spec;
    bool humanWasDisconnected = false;

    bool OnRosterAccepted() // CheckRoster CanStart with this bot Ready
    {
        if (phase != BotPhase::Lobby) return false;
        phase = BotPhase::Joining;
        return true;
    }
    bool OnReadySent()
    {
        if (phase != BotPhase::Joining) return false;
        phase = BotPhase::Ready;
        return true;
    }
    bool OnGameLoaded()
    {
        if (phase != BotPhase::Ready && phase != BotPhase::Takeover) return false;
        phase = BotPhase::InGame;
        return true;
    }
    bool OnLeaveRequested()
    {
        if (phase != BotPhase::InGame && phase != BotPhase::Ready) return false;
        phase = BotPhase::Leaving; // sends UAMSG_EXIT, then DESTROYPLAYER
        return true;
    }
    bool OnDestroyed()
    {
        if (phase != BotPhase::Leaving) return false;
        phase = BotPhase::Gone;
        return true;
    }
    // Standby bot adopts a disconnected human's owner slot (snapshot resync
    // via REQUPDATE is the engine's job; brain only gates the transition).
    bool OnTakeoverOffer(std::uint8_t disconnectedOwner)
    {
        if (phase != BotPhase::Lobby && phase != BotPhase::Gone) return false;
        if (spec.owner != disconnectedOwner) return false;
        humanWasDisconnected = true;
        phase = BotPhase::Takeover;
        return true;
    }
    bool OnHumanReclaim() // human rejoins same owner: bot exits first
    {
        if (phase != BotPhase::InGame || !humanWasDisconnected) return false;
        phase = BotPhase::Reclaiming;
        return true;
    }
    bool OnReclaimComplete()
    {
        if (phase != BotPhase::Reclaiming) return false;
        phase = BotPhase::Gone;
        humanWasDisconnected = false;
        return true;
    }
    // Back in the shell after a match, however it ended: the next match
    // starts from the lobby. Without this a bot stayed InGame (or Gone)
    // after its first match.
    void ResetForNextMatch()
    {
        phase = BotPhase::Lobby;
        humanWasDisconnected = false;
    }
};

// Fills empty lobby slots with bots. Human entries keep their slots; each bot
// takes the lowest free slot with its requested faction (or the first
// available faction when the request is taken/unavailable). The combined
// roster must pass NetFactions::CheckRoster (bots are Ready).
inline bool BuildRosterWithBots(std::uint32_t mapMask,
                                const NetFactions::RosterEntry *humans, std::size_t humanCount,
                                const BotSpec *bots, std::size_t botCount,
                                NetFactions::RosterEntry *out, std::size_t &outCount)
{
    outCount = 0;
    if (!out || humanCount + botCount > static_cast<std::size_t>(NetFactions::MaxPlayers))
        return false;
    std::uint32_t takenMask = 0;
    bool slotUsed[NetFactions::MaxPlayers] = {};
    for (std::size_t i = 0; i < humanCount; ++i)
    {
        if (!humans) return false;
        out[outCount++] = humans[i];
        takenMask |= humans[i].Faction;
        if (humans[i].Slot >= 0 && humans[i].Slot < NetFactions::MaxPlayers)
            slotUsed[humans[i].Slot] = true;
    }
    for (std::size_t b = 0; b < botCount; ++b)
    {
        if (!bots[b].valid)
            return false;
        std::uint32_t faction = 1u << bots[b].owner;
        if (!NetFactions::IsAvailable(faction, mapMask, takenMask))
            faction = NetFactions::FirstAvailable(mapMask & ~takenMask);
        if (!faction)
            return false;
        int slot = -1;
        for (int s = 0; s < NetFactions::MaxPlayers; ++s)
            if (!slotUsed[s]) { slot = s; break; }
        if (slot < 0)
            return false;
        slotUsed[slot] = true;
        takenMask |= faction;
        out[outCount++] = NetFactions::RosterEntry{slot, faction, true};
    }
    return NetFactions::CheckRoster(mapMask, out, outCount).Valid;
}

// Chat safety: UAMSG_MESSAGE payloads are char[64], and the receiver plays
// speech sample 9<n> instead of showing the text whenever
// int(strtol(text, 0, 0)) > 0 (yw_net.cpp). strtol skips leading white space
// and takes a sign and 0x or 0 prefixes, its long is 32 bits on Windows and
// 64 on Linux, and the int conversion can turn a large negative value
// positive. So a bot line must be printable, at most 63 chars + NUL, and must
// not start with anything strtoll reads as a non-zero number.
inline bool ValidBotChatLine(const std::string &line)
{
    if (line.empty() || line.size() > 63)
        return false;
    for (char c : line)
        if (c < 32 || c > 126)
            return false;
    return std::strtoll(line.c_str(), nullptr, 0) == 0;
}

inline const char *TauntFor(std::uint8_t owner, std::uint32_t pick)
{
    static const char *kLines[][3] = {
        {"Holding the line.", "For the Resistance!", "Push forward, stay sharp."},
        {"Ghorkov strength prevails.", "Crush them.", "Advance without mercy."},
        {"Superior technology wins.", "Your position is obsolete.", "Calculated. Executed."},
        {"The fortress endures.", "You shall not pass.", "Counterattack ready."},
        {"The Sect provides.", "Your sector looks useful.", "Anvil falls where it wishes."},
        {"The swarm hungers.", "More wings, more teeth.", "The Queen provides."},
    };
    std::size_t idx = 0;
    for (int o : NetFactions::Owners)
    {
        if (o == owner)
            break;
        ++idx;
    }
    if (idx > 5)
        idx = 0;
    return kLines[idx][pick % 3];
}

inline bool TauntDue(std::int32_t lastMs, std::int32_t nowMs, std::int32_t minGapMs = 20000)
{
    return lastMs < 0 || nowMs - lastMs >= minGapMs;
}

} }
#endif
