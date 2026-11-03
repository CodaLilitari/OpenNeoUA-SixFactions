#ifndef ZNDNET_RATELIMIT_H_INCLUDED
#define ZNDNET_RATELIMIT_H_INCLUDED

#include <cstdint>

namespace ZNDNet
{

// Per-source token bucket limiting inbound data packets. System packets are
// exempt at the call site. Time is injected in milliseconds so behaviour is
// deterministic in tests; callers pass Tick64::GetTicks().
//
// The refill keeps thousandths of a token, so arrivals closer together than
// one token's worth of time still add up: at 200/s, a packet every 1 ms adds
// 0.2 token. (Adding only whole tokens per call and dropping the rest gave a
// source that sent every 1-4 ms no refill at all after its burst.) A full
// bucket keeps no fraction.
class TokenBucket
{
public:
    TokenBucket() : TokenBucket(400, 200) {}
    TokenBucket(std::uint32_t capacity, std::uint32_t refillPerSecond)
        : capacity_(capacity), refillPerSecond_(refillPerSecond),
          tokens_(capacity), milliTokens_(0), lastMs_(0), started_(false)
    {
        if (refillPerSecond_ > 1000000)
            refillPerSecond_ = 1000000;
    }

    bool Allow(std::uint64_t nowMs, std::uint32_t cost = 1)
    {
        if (!started_)
        {
            started_ = true;
            lastMs_ = nowMs;
        }
        else if (nowMs > lastMs_)
        {
            const std::uint64_t elapsed = nowMs - lastMs_;
            lastMs_ = nowMs;
            if (elapsed > 60000)
            {
                tokens_ = capacity_;
                milliTokens_ = 0;
            }
            else if (refillPerSecond_)
            {
                milliTokens_ += elapsed * refillPerSecond_;
                tokens_ += milliTokens_ / 1000;
                milliTokens_ %= 1000;
                if (tokens_ >= capacity_)
                {
                    tokens_ = capacity_;
                    milliTokens_ = 0;
                }
            }
        }
        if (cost > tokens_)
            return false;
        tokens_ -= cost;
        return true;
    }

    std::uint64_t Tokens() const { return tokens_; }

private:
    std::uint64_t capacity_;
    std::uint64_t refillPerSecond_;
    std::uint64_t tokens_;
    std::uint64_t milliTokens_; // refill below one token, in 1/1000 token
    std::uint64_t lastMs_;
    bool started_;
};

}

#endif
