#ifndef WORLD_SPEECH_H_INCLUDED
#define WORLD_SPEECH_H_INCLUDED

#include <cstddef>
#include <string>
#include <vector>

namespace World
{
namespace Speech
{
constexpr size_t EventCount = 16;

struct Variants
{
    std::vector<std::string> Files;
    size_t Last = static_cast<size_t>(-1);
};

int EventIndexFromMsgID(int message);
int EventIndexFromKey(const std::string &key);
const char *EventKey(size_t index);
std::string PackEventPath(const std::string &root, size_t event);
// Multiplayer default pack root for a faction owner, or "" when the faction
// keeps the original vehicle-class voices (Resistance 1, Mykonians 3).
const char *FactionPackRoot(int owner);
// Explicit prototype packs take precedence over the new multiplayer defaults.
// Owner IDs are faction IDs, not lobby slot indices.
std::vector<std::string> UnitEventPaths(const std::string &authoredRoot, size_t event,
                                      int owner, bool multiplayer);
bool IsVariantFilename(const std::string &filename, const std::string &stem);
const std::string &ChooseVariant(Variants &variants, unsigned int random);
}
}

#endif
