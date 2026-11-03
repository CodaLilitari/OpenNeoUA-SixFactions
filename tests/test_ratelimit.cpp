#include "../src/netlib/ratelimit.h"
#include "require.h"
#include <algorithm>
#include <cstdint>
#include <initializer_list>
int main() {
    using ZNDNet::TokenBucket;
    // Burst up to capacity, then denied.
    TokenBucket b(4, 2);
    REQUIRE(b.Allow(1000)); REQUIRE(b.Allow(1000));
    REQUIRE(b.Allow(1000)); REQUIRE(b.Allow(1000));
    REQUIRE(!b.Allow(1000)); REQUIRE(b.Tokens() == 0);
    // Refill: 1s at 2/s restores exactly 2.
    REQUIRE(b.Allow(2000)); REQUIRE(b.Allow(2000));
    REQUIRE(!b.Allow(2000));
    // Long idle caps at capacity, never above.
    TokenBucket c(4, 2);
    REQUIRE(c.Allow(0)); REQUIRE(c.Allow(0));
    REQUIRE(c.Tokens() == 2);
    for (int i = 0; i < 4; ++i) REQUIRE(c.Allow(3600000));
    REQUIRE(!c.Allow(3600000)); REQUIRE(c.Tokens() == 0);
    // Oversized cost is denied without consuming.
    TokenBucket d(4, 2);
    REQUIRE(!d.Allow(500, 5)); REQUIRE(d.Tokens() == 4);
    REQUIRE(d.Allow(500, 4)); REQUIRE(d.Tokens() == 0);
    // Backwards clock never refills.
    TokenBucket e(4, 1000);
    REQUIRE(e.Allow(5000)); REQUIRE(e.Allow(4000));
    REQUIRE(e.Tokens() == 2);
    // Default bucket: 400 burst.
    TokenBucket f;
    for (int i = 0; i < 400; ++i) REQUIRE(f.Allow(100));
    REQUIRE(!f.Allow(100));

    // Arrivals closer together than one token's worth of time (5 ms at
    // 200/s) still refill: the fraction is carried, not dropped. A source
    // sending every 1-4 ms used to get nothing after its burst.
    for (std::uint64_t gap : {1u, 2u, 3u, 4u, 5u, 7u})
    {
        TokenBucket g;
        for (int i = 0; i < 400; ++i) REQUIRE(g.Allow(0));
        REQUIRE(!g.Allow(0));
        std::uint64_t allowed = 0, arrivals = 0, last = 0;
        for (std::uint64_t now = gap; now <= 10000; now += gap)
        {
            if (g.Allow(now)) ++allowed;
            ++arrivals;
            last = now;
        }
        // Exactly 200/s when arrivals outpace the refill, every arrival when
        // they do not (7 ms gaps).
        REQUIRE(allowed == std::min(arrivals, last * 200 / 1000));
    }

    // A full bucket keeps no fraction: time spent at capacity does not bank
    // part of a token for later.
    TokenBucket h(4, 200);
    REQUIRE(h.Allow(0, 0) && h.Tokens() == 4);   // starts the clock, full
    for (int i = 0; i < 4; ++i) REQUIRE(h.Allow(3));  // 0.6 token over capacity: dropped
    REQUIRE(!h.Allow(5));                         // 0.4 token since then
    REQUIRE(h.Allow(8));                          // 1.0 token since then
    REQUIRE(!h.Allow(8));
    return 0;
}
