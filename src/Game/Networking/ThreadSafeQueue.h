#pragma once
#include "Core/ClassImplMacros.h"

#include <shared_mutex>
#include <mutex>
#include <queue>

template<typename T>
class ThreadSafeQueue
{
public:
  explicit ThreadSafeQueue() = default;
  NO_COPY_NO_MOVE(ThreadSafeQueue);

  void push_back(const T& value)
  {
    auto lock = std::lock_guard(mutex_);
    queue_.push(value);
  }

  void push_back(T&& value)
  {
    auto lock = std::lock_guard(mutex_);
    queue_.push(std::move(value));
  }

  T pop_front()
  {
    auto lock = std::lock_guard(mutex_);
    auto front = std::move(queue_.front());
    queue_.pop();
    return front;
  }

  bool empty() const
  {
    auto lock = std::shared_lock(mutex_);
    return queue_.empty();
  }

  std::size_t size() const
  {
    auto lock = std::shared_lock(mutex_);
    return queue_.size();
  }

  void clear()
  {
    auto lock = std::lock_guard(mutex_);
    queue_ = {};
  }

private:
  mutable std::shared_mutex mutex_;
  std::queue<T> queue_;
};
