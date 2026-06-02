#include "Sleep.h"

#include "tracy/Tracy.hpp"

#include <thread>
#include <chrono>

#ifdef _WIN32
  #include "Windows.h"
#endif

namespace Core::Platform
{
  // https://blog.bearcats.nl/perfect-sleep-function/
  // https://github.com/blat-blatnik/Snippets/blob/main/precise_sleep.c
  void PreciseSleep(double seconds)
  {
    ZoneScoped;
    const auto t0     = std::chrono::high_resolution_clock::now();
    const auto target = t0 + std::chrono::nanoseconds(int64_t(seconds * 1e9));

#ifdef _WIN32
    constexpr int PERIOD       = 1;
    constexpr double TOLERANCE = 0.02;

    // sleep
    const double ms  = seconds * 1000 - (PERIOD + TOLERANCE);
    const auto ticks = static_cast<int>(ms / PERIOD);
    if (ticks > 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(ticks * PERIOD));
    }
#endif

    // spin
    while (std::chrono::high_resolution_clock::now() < target)
    {
#ifdef _WIN32
      YieldProcessor();
#else
      // I am tempted to use this for all platforms, but the MS STL calls _Thrd_yield and it isn't clear what exactly it does.
      std::this_thread::yield();
#endif
    }
  }
} // namespace Core::Platform