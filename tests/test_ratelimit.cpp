#include "../src/netlib/ratelimit.h"
#include <cassert>
int main() {
    using ZNDNet::TokenBucket;
    // Burst up to capacity, then denied.
    TokenBucket b(4, 2);
    assert(b.Allow(1000)); assert(b.Allow(1000));
    assert(b.Allow(1000)); assert(b.Allow(1000));
    assert(!b.Allow(1000)); assert(b.Tokens() == 0);
    // Refill: 1s at 2/s restores exactly 2.
    assert(b.Allow(2000)); assert(b.Allow(2000));
    assert(!b.Allow(2000));
    // Long idle caps at capacity, never above.
    TokenBucket c(4, 2);
    assert(c.Allow(0)); assert(c.Allow(0));
    assert(c.Tokens() == 2);
    for (int i = 0; i < 4; ++i) assert(c.Allow(3600000));
    assert(!c.Allow(3600000)); assert(c.Tokens() == 0);
    // Oversized cost is denied without consuming.
    TokenBucket d(4, 2);
    assert(!d.Allow(500, 5)); assert(d.Tokens() == 4);
    assert(d.Allow(500, 4)); assert(d.Tokens() == 0);
    // Backwards clock never refills.
    TokenBucket e(4, 1000);
    assert(e.Allow(5000)); assert(e.Allow(4000));
    assert(e.Tokens() == 2);
    // Default bucket: 400 burst.
    TokenBucket f;
    for (int i = 0; i < 400; ++i) assert(f.Allow(100));
    assert(!f.Allow(100));
    return 0;
}
