#include "../src/netlib/ratelimit.h"
#include "require.h"
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
    return 0;
}
