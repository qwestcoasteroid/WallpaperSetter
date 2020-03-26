#include "timer.h"

#include <chrono>

struct Timer::timer_implementation {
    constexpr timer_implementation() noexcept : is_running_(false) {}

    std::chrono::time_point<std::chrono::system_clock> start_time_;
    std::chrono::time_point<std::chrono::system_clock> end_time_;
    bool is_running_;
};

Timer::Timer() noexcept : implementation_(new timer_implementation) {}

Timer::~Timer() noexcept {
    delete implementation_;
}

void Timer::Start() & {
    if (!implementation_->is_running_){
        implementation_->start_time_ = std::chrono::system_clock::now();
        implementation_->is_running_ = true;
    }
    else {
        // throw
    }
}

void Timer::Stop() & {
    if (implementation_->is_running_) {
        implementation_->end_time_ = std::chrono::system_clock::now();
        implementation_->is_running_ = false;
    }
    else {
        // throw
    }
}

void Timer::Reset() & noexcept {
    if (implementation_->is_running_) {
        implementation_->is_running_ = false;
    }

    implementation_->start_time_ =
        std::chrono::time_point<std::chrono::system_clock>();
    implementation_->end_time_ =
        std::chrono::time_point<std::chrono::system_clock>();
}

long long Timer::ElapsedTime() & noexcept {
    if (implementation_->is_running_) {
        implementation_->end_time_ = std::chrono::system_clock::now();
    }
        
    return std::chrono::duration_cast<std::chrono::milliseconds>
        (implementation_->end_time_ - implementation_->start_time_).count();
}