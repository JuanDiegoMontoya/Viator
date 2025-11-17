#pragma once
#include <vector>
#include <utility>
#include <stdexcept>

// Efficient implementation of queue semantics.
template<typename T>
class RingBuffer
{
public:
  constexpr RingBuffer()
  {
    data_.resize(4);
  }

  constexpr RingBuffer(const RingBuffer&) = default;

  constexpr RingBuffer& operator=(const RingBuffer&) = default;

  constexpr RingBuffer(RingBuffer&& old) noexcept
  {
    *this = std::move(old);
  }

  constexpr RingBuffer& operator=(RingBuffer&& old) noexcept
  {
    size_  = std::exchange(old.size_, 0);
    read_  = std::exchange(old.read_, 0);
    write_ = std::exchange(old.write_, 0);
    data_  = std::exchange(old.data_, std::vector<T>(4));
    return *this;
  }

  constexpr void push(T value)
  {
    const auto oldSize = size();
    if (oldSize == capacity())
    {
      resize(oldSize + oldSize / 2); // Geometric growth.
    }

    data_[write_] = std::move(value);

    write_ = (write_ + 1) % capacity();
    ++size_;
  }

  constexpr void pop()
  {
    if (empty())
    {
      throw std::logic_error("Called pop() on empty RingBuffer.");
    }

    read_ = (read_ + 1) % capacity();
    --size_;
  }

  template<typename Self>
  [[nodiscard]] constexpr auto&& front(this Self&& self)
  {
    if (self.empty())
    {
      throw std::logic_error("Called front() on empty RingBuffer.");
    }

    const auto read = self.read_;
    return std::forward<Self>(self).data_[read];
  }

  constexpr void clear()
  {
    size_  = 0;
    read_  = 0;
    write_ = 0;
  }

  constexpr void resize(size_t newSize)
  {
    auto newData = std::vector<T>(newSize);

    // TODO: Add optimized path that uses memcpy for trivially copyable types (which implies trivial relocatability).
    if (write_ > read_)
    {
      for (size_t i = read_; i < write_ && i - read_ < newData.size(); i++)
      {
        newData[i - read_] = std::move(data_[i]);
      }
    }
    else
    {
      for (size_t i = write_; i < data_.size() && i - write_ < newData.size(); i++)
      {
        newData[i - write_] = std::move(data_[i]);
      }

      for (size_t i = 0; i < read_ && i + write_ < newData.size(); i++)
      {
        newData[i + write_] = std::move(data_[i]);
      }
    }

    write_ = size();
    read_  = 0;
    std::swap(data_, newData);
  }

  [[nodiscard]] constexpr size_t size() const noexcept
  {
    return size_;
  }

  [[nodiscard]] constexpr size_t capacity() const noexcept
  {
    return data_.size();
  }

  [[nodiscard]] constexpr bool empty() const noexcept
  {
    return size_ == 0;
  }

private:
  size_t size_  = 0; // Used to disambiguate between empty and full buffers.
  size_t read_  = 0;
  size_t write_ = 0;
  std::vector<T> data_;
};
