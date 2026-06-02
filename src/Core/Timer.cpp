#include "Timer.h"

#include <chrono>

namespace
{
  using second_t      = std::chrono::duration<double, std::ratio<1>>;
  using millisecond_t = std::chrono::duration<double, std::milli>;
  using microsecond_t = std::chrono::duration<double, std::micro>;
  using nanosecond_t  = std::chrono::nanoseconds;
  using myclock_t     = std::chrono::steady_clock;
  using timepoint_t   = std::chrono::time_point<myclock_t>;

  class TimerImpl : public Timer
  {
  public:
    TimerImpl() : timePoint(myclock_t::now()) {}

    void Reset() override
    {
      timePoint = myclock_t::now();
    }

    double Elapsed_s() const override
    {
      return std::chrono::duration_cast<second_t>(myclock_t::now() - timePoint).count();
    }

  private:
    timepoint_t timePoint;
  };
}

std::unique_ptr<Timer> Timer::Create()
{
  return std::make_unique<TimerImpl>();
}