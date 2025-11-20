#pragma once
#include <cstdint>

namespace Core
{
  class BitArray
  {
  public:
    using chunk_type = std::uint32_t;
    using size_type = std::size_t;
    static constexpr size_type bits_per_chunk = sizeof(chunk_type) * CHAR_BIT;

    BitArray();

    explicit BitArray(size_type size, bool value = false);

    BitArray(const BitArray&) = delete;
    BitArray& operator=(const BitArray&) = delete;

    BitArray(BitArray&& old) noexcept;

    BitArray& operator=(BitArray&& old) noexcept;

    ~BitArray();

    void Set(size_type offset, bool value);

    void Set(size_type offset, bool value, size_type count);

    [[nodiscard]] bool Get(size_type offset) const;

    [[nodiscard]] chunk_type Get(size_type offset, size_type count) const;

    void SetChunk(size_type chunkIndex, chunk_type chunk);

    [[nodiscard]] chunk_type GetChunk(size_type chunkIndex) const;

    void Resize(size_type newSize, bool value = false);

    [[nodiscard]] size_type Size() const;

    [[nodiscard]] size_type NumChunks() const;

  private:
    size_type size_     = 0;
    chunk_type* chunks_ = nullptr;

    static constexpr size_type IDivCeil(size_type dividend, size_type divisor)
    {
      return (dividend + divisor - 1) / divisor;
    }
  };
}