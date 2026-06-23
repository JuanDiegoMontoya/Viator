#include "BitArray.h"
#include "Core/Assert2.h"

#include <atomic>
#include <utility>
#include <cstring>

Core::BitArray::BitArray() = default;

Core::BitArray::BitArray(size_type size, bool value) : size_(size)
{
  const auto numChunks = IDivCeil(size, bits_per_chunk);
  chunks_              = new chunk_type[numChunks];

  const auto fillValue = static_cast<chunk_type>(value ? -1 : 0);
  for (size_type i = 0; i < numChunks; i++)
  {
    chunks_[i] = fillValue;
  }
}

Core::BitArray::BitArray(BitArray&& old) noexcept
{
  *this = std::move(old);
}

Core::BitArray& Core::BitArray::operator=(BitArray&& old) noexcept
{
  if (this != &old)
  {
    delete[] chunks_;
    size_   = std::exchange(old.size_, 0);
    chunks_ = std::exchange(old.chunks_, nullptr);
  }
  return *this;
}

Core::BitArray::~BitArray()
{
  delete[] chunks_;
}

void Core::BitArray::Set(size_type offset, bool value)
{
  DEBUG_ASSERT(offset < Size());
  const auto chunk = offset / bits_per_chunk;
  const auto bit   = offset % bits_per_chunk;
  auto ref         = std::atomic_ref(chunks_[chunk]);
  if (value)
  {
    ref.fetch_or(1 << bit);
  }
  else
  {
    ref.fetch_and(~(1 << bit));
  }
}

bool Core::BitArray::Get(size_type offset) const
{
  DEBUG_ASSERT(offset < Size());
  const auto chunk = offset / bits_per_chunk;
  const auto bit   = offset % bits_per_chunk;
  return std::atomic_ref(chunks_[chunk]).load() & (1 << bit);
}

void Core::BitArray::SetChunk(size_type chunkIndex, chunk_type chunk)
{
  DEBUG_ASSERT(chunkIndex < NumChunks());
  chunks_[chunkIndex] = chunk;
}

Core::BitArray::chunk_type Core::BitArray::GetChunk(size_type chunkIndex) const
{
  DEBUG_ASSERT(chunkIndex < NumChunks());
  return chunks_[chunkIndex];
}

void Core::BitArray::Resize(size_type newSize, bool value)
{
  auto newVec = BitArray(newSize, value);
  std::memcpy(newVec.chunks_, chunks_, IDivCeil(std::min(Size(), newSize), bits_per_chunk) * sizeof(chunk_type));
  *this = std::move(newVec);
}

Core::BitArray::size_type Core::BitArray::Size() const
{
  return size_;
}

Core::BitArray::size_type Core::BitArray::NumChunks() const
{
  return IDivCeil(Size(), bits_per_chunk);
}

#include "doctest.h"

TEST_CASE("BitArray")
{
  auto vec = Core::BitArray();

  SUBCASE("Constructors")
  {
    CHECK_EQ(vec.Size(), 0);

    auto vec2 = Core::BitArray(128, false);
    for (int i = 0; i < 128; i++)
    {
      CHECK_EQ(vec2.Get(i), false);
    }

    auto vec3 = Core::BitArray(128, true);
    for (int i = 0; i < 128; i++)
    {
      CHECK_EQ(vec3.Get(i), true);
    }
  }

  SUBCASE("Move semantics")
  {
    vec.Resize(128, true);
    vec = std::move(vec);
    CHECK_EQ(vec.Size(), 128);
    CHECK_EQ(vec.Get(100), true);

    auto vec2 = std::move(vec);
    CHECK_EQ(vec2.Size(), 128);
    CHECK_EQ(vec2.Get(100), true);
    CHECK_EQ(vec.Size(), 0);
  }

  SUBCASE("Get and set single")
  {
    for (int i = 0; i < 128; i++)
    {
      vec = Core::BitArray(128, false);
      vec.Set(i, true);
      CHECK_EQ(vec.Get(i), true);
    }

    for (int i = 0; i < 128; i++)
    {
      vec = Core::BitArray(128, true);
      vec.Set(i, false);
      CHECK_EQ(vec.Get(i), false);
    }
  }
}