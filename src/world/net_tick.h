// Fixed-step net tick (design Phase 3): decouples the 20 Hz network
// simulation/snapshot cadence from the variable render frame time. Pure
// logic with injected time so cadence is deterministic in tests; the yw
// Process wiring (tick-gated snapshot emit) lands with the transport work.
#ifndef WORLD_NET_TICK_H_INCLUDED
#define WORLD_NET_TICK_H_INCLUDED

#include <cstdint>

namespace World { namespace Net {

class NetTick
{
public:
    static constexpr std::int32_t TickMs = 50; // 20 Hz

    explicit NetTick(std::int32_t tickMs = TickMs, std::uint32_t maxTicksPerFrame = 5)
        : tickMs_(tickMs > 0 ? tickMs : TickMs),
          maxTicksPerFrame_(maxTicksPerFrame ? maxTicksPerFrame : 1),
          accMs_(0), tick_(0)
    {
    }

    // Advances by one rendered frame; returns how many fixed ticks are due.
    // Backlog beyond maxTicksPerFrame is dropped (re-sync, no spiral of
    // death); the sub-tick remainder is preserved.
    std::uint32_t Advance(std::int32_t frameDtMs)
    {
        if (frameDtMs < 0)
            frameDtMs = 0;
        accMs_ += frameDtMs;
        std::uint32_t due = (std::uint32_t)(accMs_ / tickMs_);
        if (due > maxTicksPerFrame_)
        {
            due = maxTicksPerFrame_;
            accMs_ = accMs_ % tickMs_;
        }
        else
        {
            accMs_ -= (std::int64_t)due * tickMs_;
        }
        tick_ += due;
        return due;
    }

    // Retunes the step (e.g. from the configured flush period). The
    // remainder is kept below one step so a retune cannot burst.
    void SetTickMs(std::int32_t tickMs)
    {
        if (tickMs <= 0 || tickMs == tickMs_)
            return;
        tickMs_ = tickMs;
        if (accMs_ > tickMs_)
            accMs_ = tickMs_;
    }

    std::int32_t RemainderMs() const { return (std::int32_t)accMs_; }
    std::uint32_t Tick() const { return tick_; }
    void Reset() { accMs_ = 0; tick_ = 0; }

private:
    std::int32_t tickMs_;
    std::uint32_t maxTicksPerFrame_;
    std::int64_t accMs_;
    std::uint32_t tick_;
};

} }
#endif
