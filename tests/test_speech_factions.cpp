#include "../src/world/speech.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void Require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
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
        Require(paths.size() == (owner == 2 || owner == 5 ? 1u : 0u),
                "only the two newly playable factions receive defaults");
    }
    const auto black = UnitEventPaths("", ready, 5, true);
    Require(black[0] == "sounds/speech/voicepacks/black-sect-mp/unit_ready",
            "Black Sect must resolve by faction owner");
    const auto sulg = UnitEventPaths("", ready, 2, true);
    Require(sulg[0] == "sounds/speech/voicepacks/sulgogars-mp/unit_ready",
            "Sulgogar must resolve by faction owner");
    const auto custom = UnitEventPaths("sounds/custom/", ready, 5, true);
    Require(custom.size() == 2 && custom[0] == "sounds/custom/unit_ready" && custom[1] == black[0],
            "explicit prototype pack must precede the faction fallback");
    const auto same = UnitEventPaths("sounds/speech/voicepacks/black-sect-mp/", ready, 5, true);
    Require(same.size() == 1, "identical packs must not be attempted twice");
    Require(UnitEventPaths("sounds/custom", EventCount, 2, true).empty(),
            "unknown events must not resolve to arbitrary files");
    Require(EventIndexFromMsgID(23) == -1,
            "strategic announcer events must stay outside unit voice routing");
    for (size_t event = 0; event < EventCount; ++event)
    {
        Require(!std::string(EventKey(event)).empty(), "every supported event needs an asset key");
        Require(UnitEventPaths("", event, 2, true).size() == 1,
                "Sulgogar default must cover every supported unit event");
        Require(UnitEventPaths("", event, 5, true).size() == 1,
                "Black Sect default must cover every supported unit event");
    }
    std::cout << "Faction speech routing passed\n";
}
