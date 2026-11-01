#ifndef OPENNEOUA_NET_RESTORE_H
#define OPENNEOUA_NET_RESTORE_H

#include <cstddef>
#include <vector>

namespace World { namespace NetRestore {

template<class Record>
class SnapshotWriter
{
public:
    SnapshotWriter(Record *records, std::size_t capacity)
        : records_(records), capacity_(capacity) {}

    bool Append(const Record &record)
    {
        if (!records_ || failed_ || finished_ || capacity_ < 2 || count_ >= capacity_ - 1)
        {
            failed_ = true;
            return false;
        }
        records_[count_++] = record;
        return true;
    }

    bool Finish()
    {
        if (failed_ || finished_ || count_ == 0 || count_ >= capacity_)
            return false;
        records_[count_++].type = 0;
        finished_ = true;
        return true;
    }

    std::size_t Count() const { return count_; }

private:
    Record *records_;
    std::size_t capacity_;
    std::size_t count_ = 0;
    bool failed_ = false;
    bool finished_ = false;
};

struct Step
{
    std::size_t parent = 0;
    std::size_t gun = 0;
};

// The wire snapshot is host, then ordered guns/commanders/slaves, with each
// object's missiles immediately after it and one terminal record.
template<class Record>
bool BuildPlan(const Record *records, std::size_t count, std::size_t gunCapacity,
               std::vector<Step> &plan)
{
    plan.clear();
    if (!records || count < 2 || count > 1024 ||
        records[0].type != 1 || records[count - 1].type != 0)
        return false;

    std::vector<Step> candidate(count - 1);
    const std::size_t noCommander = count;
    std::size_t commander = noCommander;
    std::size_t launcher = 0;
    std::size_t gun = 0;
    for (std::size_t i = 1; i + 1 < count; ++i)
    {
        switch (records[i].type)
        {
        case 2: // Commander belongs to the station.
            commander = launcher = i;
            break;
        case 3: // Slaves belong to the current commander.
            if (commander == noCommander)
                return false;
            candidate[i].parent = commander;
            launcher = i;
            break;
        case 4: // Missiles do not themselves become launchers.
            candidate[i].parent = launcher;
            break;
        case 5:
            if (gun >= gunCapacity)
                return false;
            candidate[i].gun = gun++;
            commander = noCommander;
            launcher = i;
            break;
        default: // Duplicate host, early terminator or unknown record type.
            return false;
        }
    }
    plan.swap(candidate);
    return true;
}

} }
#endif
