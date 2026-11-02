#include "../src/world/speech.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void Require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

static bool HasFactionPack(int owner)
{
    // Sulgogars 2, Taerkasten 4, Black Sect 5, Ghorkov 6. Resistance (1) and
    // Mykonians (3) keep the original vehicle-class voices.
    return owner == 2 || owner == 4 || owner == 5 || owner == 6;
}

int main()
{
    using namespace World::Speech;
    const int ready = EventIndexFromMsgID(14);
    Require(ready >= 0, "unit-ready event must resolve");
    for (int owner = 0; owner <= 8; ++owner)
    {
        Require(UnitEventPaths("", ready, owner, false).empty(),
                "single-player must not acquire multiplayer defaults");
        const auto paths = UnitEventPaths("0", ready, owner, true);
        Require(paths.size() == (HasFactionPack(owner) ? 1u : 0u),
                "only Sulgogars, Taerkasten, Black Sect and Ghorkov receive defaults");
        Require(std::string(FactionPackRoot(owner)).empty() == !HasFactionPack(owner),
                "pack root table matches the routing rule");
    }
    const struct { int owner; const char *path; const char *message; } expected[] = {
        {2, "sounds/speech/voicepacks/sulgogars-mp/unit_ready", "Sulgogar must resolve by faction owner"},
        {4, "sounds/speech/voicepacks/taerkasten-mp/unit_ready", "Taerkasten must resolve by faction owner"},
        {5, "sounds/speech/voicepacks/black-sect-mp/unit_ready", "Black Sect must resolve by faction owner"},
        {6, "sounds/speech/voicepacks/ghorkov-mp/unit_ready", "Ghorkov must resolve by faction owner"},
    };
    for (const auto &e : expected)
    {
        const auto paths = UnitEventPaths("", ready, e.owner, true);
        Require(paths.size() == 1 && paths[0] == e.path, e.message);
    }
    const auto black = UnitEventPaths("", ready, 5, true);
    const auto custom = UnitEventPaths("sounds/custom/", ready, 5, true);
    Require(custom.size() == 2 && custom[0] == "sounds/custom/unit_ready" && custom[1] == black[0],
            "explicit prototype pack must precede the faction fallback");
    const auto ghorkovCustom = UnitEventPaths("sounds/custom/", ready, 6, true);
    Require(ghorkovCustom.size() == 2 && ghorkovCustom[0] == "sounds/custom/unit_ready" &&
            ghorkovCustom[1] == "sounds/speech/voicepacks/ghorkov-mp/unit_ready",
            "explicit prototype pack must precede the Ghorkov fallback too");
    const auto same = UnitEventPaths("sounds/speech/voicepacks/black-sect-mp/", ready, 5, true);
    Require(same.size() == 1, "identical packs must not be attempted twice");
    const auto sameTaer = UnitEventPaths("sounds/speech/voicepacks/taerkasten-mp", ready, 4, true);
    Require(sameTaer.size() == 1, "identical Taerkasten packs must not be attempted twice");
    Require(UnitEventPaths("sounds/custom", EventCount, 2, true).empty(),
            "unknown events must not resolve to arbitrary files");
    Require(EventIndexFromMsgID(23) == -1,
            "strategic announcer events must stay outside unit voice routing");
    for (size_t event = 0; event < EventCount; ++event)
    {
        Require(!std::string(EventKey(event)).empty(), "every supported event needs an asset key");
        for (int owner : {2, 4, 5, 6})
            Require(UnitEventPaths("", event, owner, true).size() == 1,
                    "each faction default must cover every supported unit event");
        for (int owner : {1, 3})
            Require(UnitEventPaths("", event, owner, true).empty(),
                    "Resistance and Mykonians keep the original voices");
    }
    std::cout << "Faction speech routing passed\n";
}
