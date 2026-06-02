#pragma once
#include "ClassImplMacros.h"
#include <utility>

template<typename Fn>
struct [[nodiscard]] DeferImpl
{
  NO_COPY_NO_MOVE(DeferImpl);

  template<typename Fn2>
  DeferImpl(Fn2&& fn) : f(std::forward<Fn2>(fn))
  {
  }

  ~DeferImpl()
  {
    if (!cancelled)
    {
      f();
    }
  }

  void Cancel()
  {
    cancelled = true;
  }

private:
  Fn f;
  bool cancelled = false;
};

template<typename Fn>
[[nodiscard]] DeferImpl<Fn> Defer(Fn&& fn)
{
  return {std::forward<Fn>(fn)};
}