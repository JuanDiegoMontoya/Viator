#pragma once
#include "Core/ClassImplMacros.h"
#include <memory>

class Timer
{
public:
  [[nodiscard]] static std::unique_ptr<Timer> Create();

  NO_COPY_NO_MOVE(Timer);

  virtual ~Timer()                 = default;
  virtual void Reset()             = 0;
  virtual double Elapsed_s() const = 0;

protected:
  Timer() = default;
};