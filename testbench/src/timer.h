#include <chrono>

class Timer {
private:
    using clock_t = std::chrono::steady_clock;
    using second_t = std::chrono::duration<double, std::ratio<1>>;
    using millisec_t = std::chrono::duration<double, std::milli>;
    std::chrono::time_point<clock_t> start_time_;
    std::chrono::time_point<clock_t> end_time_;


public:
    Timer() : start_time_(clock_t::now()) {}

    void Stop() {
        end_time_ = clock_t::now();
    }

    void Restart() {
        start_time_ = clock_t::now();
    }

    double ElapsedSec() {
        return std::chrono::duration_cast<second_t>(end_time_ - start_time_).count();
    }
    
    double ElapsedMillisec() {
        return std::chrono::duration_cast<millisec_t>(end_time_ - start_time_).count();
    }

};
