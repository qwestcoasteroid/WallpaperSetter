#include "timer.h"

void Timer::Start() & {
    if (!is_running){
        start_time_ = std::chrono::system_clock::now();
        is_running = true;
    }
    else {
        // throw
    }
}

void Timer::Stop() & {
    if (is_running) {
        end_time_ = std::chrono::system_clock::now();
        is_running = false;
    }
    else {
        // throw
    }
}

void Timer::Reset() & noexcept {
    if (is_running) {
        is_running = false;
    }

    count_ = std::chrono::milliseconds();
    start_time_ = std::chrono::time_point<std::chrono::system_clock>();
    end_time_ = std::chrono::time_point<std::chrono::system_clock>();
}

std::chrono::milliseconds Timer::ElapsedTime() & noexcept {
    if (is_running) {
        end_time_ = std::chrono::system_clock::now();
    }
        
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time_ - start_time_);
}