#ifndef WALLPAPER_SETTER_TIMER_H_
#define WALLPAPER_SETTER_TIMER_H_

#include <chrono>

// Need to learn size of time points
class Timer {
public:
    constexpr Timer() noexcept : count_(), start_time_(),
        end_time_(), is_running(false) {}

    void Start() &;
    void Stop() &;
    void Reset() & noexcept;

    std::chrono::milliseconds ElapsedTime() & noexcept;

private:
    std::chrono::milliseconds count_;
    std::chrono::time_point<std::chrono::system_clock> start_time_;
    std::chrono::time_point<std::chrono::system_clock> end_time_;
    bool is_running;
};

#endif // WALLPAPER_SETTER_TIMER_H_