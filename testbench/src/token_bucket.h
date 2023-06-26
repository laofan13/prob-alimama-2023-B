#include <chrono>
#include <thread>
#include <iostream>

class TokenBucket {
public:
    TokenBucket(uint64_t rate, uint64_t burstSize) {
      if (rate == 0) {
        enable_ = false;
        return;
      }
      enable_ = true;
      time_per_token_ = std::chrono::nanoseconds(1*1000*1000*1000) / rate;
      burst_time_ = burstSize * time_per_token_;
      last_consume_time_ = std::chrono::steady_clock::now();
    }

    bool Consume(uint64_t tokens) {
        if (!enable_) {
          return true;
        }
        auto now = std::chrono::steady_clock::now();
        auto time_needed = std::chrono::duration_cast<std::chrono::nanoseconds>(tokens * time_per_token_);
        auto earliest_time = std::max(last_consume_time_ + time_needed, now - burst_time_);
        if (earliest_time <= now) {
            last_consume_time_ = now;
            return true;
        } else {
            std::this_thread::sleep_until(earliest_time);
            last_consume_time_ = earliest_time;
            return true;
        }
    }

private:
    bool enable_;
    std::chrono::nanoseconds time_per_token_;
    std::chrono::nanoseconds burst_time_;
    std::chrono::steady_clock::time_point last_consume_time_;
};
