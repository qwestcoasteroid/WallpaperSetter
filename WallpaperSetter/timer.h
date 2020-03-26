#ifndef WALLPAPER_SETTER_TIMER_H_
#define WALLPAPER_SETTER_TIMER_H_

// Need to learn size of time points
class Timer {
public:
    Timer() noexcept;
    ~Timer() noexcept;

    void Start() &;
    void Stop() &;
    void Reset() & noexcept;

    long long ElapsedTime() & noexcept;

private:
    struct timer_implementation;

    timer_implementation* implementation_; 
};

#endif // WALLPAPER_SETTER_TIMER_H_