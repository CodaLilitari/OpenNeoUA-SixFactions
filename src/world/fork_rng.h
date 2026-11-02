#ifndef WORLD_FORK_RNG_H_INCLUDED
#define WORLD_FORK_RNG_H_INCLUDED

#include <cstdint>

namespace World { namespace Fork {

// Per-world deterministic RNG for forkable simulation. Replaces the global
// C rand() call sites inside fork stepping: the whole generator state is one
// uint64_t, so Capture/Restore can serialize it and two forks seeded alike
// stay bit-identical under the same action stream (splitmix64).
class ForkRng
{
public:
    explicit ForkRng(std::uint64_t seed = 0x9E3779B97F4A7C15ull) : state_(seed) {}

    std::uint64_t Next()
    {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Uniform integer in [0, bound); bound==0 yields 0. Modulo reduction is
    // intentional: fork hashes only require same-binary determinism.
    std::uint32_t Below(std::uint32_t bound)
    {
        return bound ? static_cast<std::uint32_t>(Next() % bound) : 0;
    }

    // Uniform float in [0, 1) with 24-bit resolution.
    float Unit()
    {
        return static_cast<float>(Next() >> 40) * (1.0f / 16777216.0f);
    }

    std::uint64_t State() const { return state_; }
    void SetState(std::uint64_t state) { state_ = state; }

private:
    std::uint64_t state_;
};

} }
#endif
