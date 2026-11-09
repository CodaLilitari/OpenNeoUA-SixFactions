#ifndef ZND_TRANSPORT_STATE_H
#define ZND_TRANSPORT_STATE_H

#include "transport_types.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ZNDNet
{
inline std::uint64_t
TransportNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
// Fixed storage and a per-source-IP work budget. Five concurrent legitimate
// joins fit within the burst, while ports cannot multiply one source's budget.
class HandshakeBudget
{
    struct Entry
    {
        std::uint64_t source = 0, start = 0;
        unsigned used = 0;
        bool set = false;
    };
    std::array<Entry, 128> entries{};

  public:
    bool Allow(std::uint64_t source)
    {
        const auto now = TransportNow();
        Entry* candidate = nullptr;
        for (auto& entry : entries)
        {
            if (entry.set && entry.source == source)
            {
                if (now - entry.start >= 1000)
                {
                    entry.start = now;
                    entry.used = 0;
                }
                if (entry.used >= 16)
                    return false;
                ++entry.used;
                return true;
            }
            if (!entry.set || now - entry.start >= 1000)
                candidate = &entry;
        }
        if (!candidate)
            return false;
        *candidate = { source, now, 1, true };
        return true;
    }
};
struct RosterRow
{
    std::uint64_t id = 0;
    std::uint8_t slot = 0, status = RosterOnline;
    std::string name;
};
struct RosterChange
{
    std::uint64_t revision = 0;
    RosterRow row;
    Transition transition = Transition::Fresh;
};
struct RosterState
{
    std::uint64_t session = 0, revision = 0;
    std::string name;
    std::vector<RosterRow> rows;
    std::deque<RosterChange> history;
    void Changed(const RosterRow& row, Transition transition)
    {
        history.push_back({ ++revision, row, transition });
        if (history.size() > 64)
            history.pop_front();
    }
};
// Explicit little-endian control serialization. Never serialize C++ structs.
struct ControlWriter
{
    std::vector<std::uint8_t> b;
    void U(std::uint64_t v, unsigned n)
    {
        for (unsigned i = 0; i < n; ++i)
            b.push_back((v >> (8 * i)) & 255);
    }
    void Str(const std::string& s)
    {
        U(s.size(), 1);
        b.insert(b.end(), s.begin(), s.end());
    }
};
struct ControlReader
{
    const std::uint8_t* p;
    std::size_t n, pos = 0;
    bool ok = true;
    ControlReader(const void* data, std::size_t len)
      : p(static_cast<const std::uint8_t*>(data))
      , n(len)
    {
    }
    std::uint64_t U(unsigned bytes)
    {
        if (!ok || bytes > 8 || bytes > n - pos)
        {
            ok = false;
            return 0;
        }
        std::uint64_t v = 0;
        for (unsigned i = 0; i < bytes; ++i)
            v |= std::uint64_t(p[pos++]) << (8 * i);
        return v;
    }
    std::string Str(std::size_t max)
    {
        const auto size = U(1);
        if (!ok || size > max || size > n - pos)
        {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p + pos), size);
        pos += size;
        if (s.find('\0') != std::string::npos)
            ok = false;
        return s;
    }
    bool End() const
    {
        return ok && pos == n;
    }
};
inline void
WriteRow(ControlWriter& w, const RosterRow& r)
{
    w.U(r.id, 8);
    w.U(r.slot, 1);
    w.U(r.status, 1);
    w.Str(r.name);
}
inline RosterRow
ReadRow(ControlReader& r)
{
    RosterRow x;
    x.id = r.U(8);
    x.slot = r.U(1);
    x.status = r.U(1);
    x.name = r.Str(20);
    if (!x.id || x.slot >= 6 || (x.status != RosterOnline && x.status != RosterAway) ||
        x.name.empty())
        r.ok = false;
    return x;
}
inline std::vector<std::uint8_t>
EncodeRoster(const RosterState& s, std::uint64_t attempt, std::uint32_t generation)
{
    ControlWriter w;
    w.U('U', 1);
    w.U(attempt, 8);
    w.U(generation, 4);
    w.U(s.session, 8);
    w.U(s.revision, 8);
    w.Str(s.name);
    w.U(s.rows.size(), 1);
    for (const auto& r : s.rows)
        WriteRow(w, r);
    w.U(s.history.size(), 1);
    for (const auto& c : s.history)
    {
        w.U(c.revision, 8);
        w.U(static_cast<unsigned>(c.transition), 1);
        WriteRow(w, c.row);
    }
    return w.b;
}
inline bool
DecodeRoster(const void* data,
             std::size_t len,
             RosterState& out,
             std::uint64_t& attempt,
             std::uint32_t& generation)
{
    if (len > 8192)
        return false;
    ControlReader r(data, len);
    if (r.U(1) != 'U')
        return false;
    auto a = r.U(8);
    auto g = r.U(4);
    RosterState s;
    s.session = r.U(8);
    s.revision = r.U(8);
    s.name = r.Str(32);
    const auto count = r.U(1);
    if (!a || !g || !s.session || count < 1 || count > 6)
        return false;
    std::array<bool, 6> slots{};
    for (unsigned i = 0; i < count && r.ok; ++i)
    {
        auto row = ReadRow(r);
        if (!r.ok || slots[row.slot])
            return false;
        slots[row.slot] = true;
        for (const auto& prev : s.rows)
            if (prev.id == row.id || CallsignEqual(prev.name,row.name))
                return false;
        s.rows.push_back(row);
    }
    if (!slots[0])
        return false;
    for (const auto& row : s.rows)
        if (row.slot == 0 && row.status != RosterOnline)
            return false;
    const auto changes = r.U(1);
    if (changes > 64)
        return false;
    std::uint64_t previous = 0;
    for (unsigned i = 0; i < changes && r.ok; ++i)
    {
        RosterChange c;
        c.revision = r.U(8);
        auto t = r.U(1);
        if (t > 5)
            return false;
        c.transition = static_cast<Transition>(t);
        c.row = ReadRow(r);
        if (!c.revision || c.revision > s.revision || (i && c.revision != previous + 1))
            return false;
        previous = c.revision;
        s.history.push_back(c);
    }
    if (!r.End() || (changes && previous != s.revision))
        return false;
    std::sort(s.rows.begin(),
              s.rows.end(),
              [](const RosterRow& a, const RosterRow& b) { return a.slot < b.slot; });
    out = std::move(s);
    attempt = a;
    generation = g;
    return true;
}
// Initial snapshots and history overflow are explicitly consolidated. Otherwise
// every transition after the applied revision is emitted exactly once.
inline bool
ApplyRosterState(RosterState& current,
                 const RosterState& next,
                 std::vector<RosterChange>& changes,
                 bool& consolidated)
{
    changes.clear();
    consolidated = false;
    if (current.session && current.session != next.session)
        return false;
    if (current.session && next.revision <= current.revision)
        return false;
    if (!current.session || next.history.empty() ||
        next.history.front().revision > current.revision + 1)
        consolidated = true;
    else
        for (const auto& c : next.history)
            if (c.revision > current.revision)
                changes.push_back(c);
    current = next;
    return true;
}
}
#endif
