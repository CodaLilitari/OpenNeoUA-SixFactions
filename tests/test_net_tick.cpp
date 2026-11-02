#include "../src/world/net_tick.h"
#include <cassert>
int main() {
    using World::Net::NetTick;
    NetTick t;
    // Exact cadence: one tick per 50 ms frame.
    assert(t.Advance(50) == 1 && t.Tick() == 1 && t.RemainderMs() == 0);
    assert(t.Advance(50) == 1 && t.Tick() == 2);
    // Sub-tick frames accumulate: 16+16+16+2 = 50 -> dues 0,0,0,1.
    NetTick u;
    assert(u.Advance(16) == 0); assert(u.Advance(16) == 0);
    assert(u.Advance(16) == 0); assert(u.Advance(2) == 1);
    assert(u.RemainderMs() == 0 && u.Tick() == 1);
    // Remainder is preserved across frames.
    NetTick v;
    assert(v.Advance(30) == 0 && v.RemainderMs() == 30);
    assert(v.Advance(30) == 1 && v.RemainderMs() == 10);
    // Burst is clamped to maxTicksPerFrame and drops the backlog.
    NetTick w;
    assert(w.Advance(1000) == 5 && w.Tick() == 5);
    assert(w.RemainderMs() == 0); // 1000 % 50
    NetTick w2;
    assert(w2.Advance(1030) == 5 && w2.RemainderMs() == 30); // 1030 % 50
    assert(w2.Advance(20) == 1 && w2.RemainderMs() == 0);
    // Negative and zero frame times never produce ticks.
    NetTick x;
    assert(x.Advance(-100) == 0 && x.Advance(0) == 0 && x.Tick() == 0);
    // Reset returns to a clean cadence.
    x.Advance(50); x.Reset();
    assert(x.Tick() == 0 && x.RemainderMs() == 0 && x.Advance(50) == 1);
    // Custom step: 100 ms ticks.
    NetTick y(100, 2);
    assert(y.Advance(250) == 2 && y.RemainderMs() == 50);
    // Retune keeps the remainder below one step and never bursts.
    NetTick z;
    const std::uint32_t pre = z.Advance(30);
    assert(pre == 0 && z.RemainderMs() == 30);
    z.SetTickMs(10);
    assert(z.RemainderMs() <= 10);
    const std::uint32_t post = z.Advance(10);
    assert(post >= 1);
    z.SetTickMs(0);
    z.SetTickMs(-5);
    const std::uint32_t still = z.Advance(10);
    assert(still >= 1); // non-positive retune ignored (step stays 10 ms)
    return 0;
}
