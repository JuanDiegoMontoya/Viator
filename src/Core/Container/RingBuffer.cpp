#include "RingBuffer.h"
#include "PCG.h"

#include "doctest.h"

#include <ranges>
#include <queue>

namespace
{
  template<typename T>
  void CompareRingBufferAndVector(RingBuffer<T> ring, const std::vector<T>& values)
  {
    REQUIRE(ring.size() == values.size());
    for (size_t i = 0; i < values.size(); i++)
    {
      CHECK(ring.front() == values[i]);
      CHECK_NOTHROW(ring.pop());
    }
  }

  template<typename T>
  void CompareRingBufferAndQueue(RingBuffer<T> ring, std::queue<T> ring2)
  {
    REQUIRE(ring.size() == ring2.size());
    while (!ring.empty())
    {
      CHECK(ring.front() == ring2.front());
      CHECK_NOTHROW(ring.pop());
      CHECK_NOTHROW(ring2.pop());
    }
  }

  template<typename T>
  void CompareRingBuffersDestructive(RingBuffer<T>& ring, RingBuffer<T>& ring2)
  {
    REQUIRE(ring.size() == ring2.size());
    while (!ring.empty())
    {
      CHECK(ring.front() == ring2.front());
      CHECK_NOTHROW(ring.pop());
      CHECK_NOTHROW(ring2.pop());
    }
  }
}

TEST_CASE("RingBuffer")
{
  auto ring = RingBuffer<int>();

  ring.push(1);
  ring.push(2);
  ring.push(3);
  ring.push(4);

  SUBCASE("Basic (front() and pop())")
  {
    CHECK(!ring.empty());
    CompareRingBufferAndVector(ring, {1, 2, 3, 4});
    ring.pop();
    ring.pop();
    ring.pop();
    ring.pop();
    CHECK_THROWS_AS(ring.pop(), std::logic_error);
    CHECK_THROWS_AS([[maybe_unused]] auto _ = ring.front(), std::logic_error);
  }

  SUBCASE("Copyability")
  {
    auto ring2 = ring;
    CompareRingBuffersDestructive(ring, ring2);
  }

  SUBCASE("Movability")
  {
    auto ring2 = ring;
    auto ring3 = std::move(ring2);
    CompareRingBuffersDestructive(ring, ring3);
    CHECK(ring2.empty());
  }

  SUBCASE("Pop some, then push some")
  {
    ring.pop();
    ring.pop();
    CompareRingBufferAndVector(ring, {3, 4});

    ring.push(5);
    ring.push(6);
    CompareRingBufferAndVector(ring, {3, 4, 5, 6});
  }

  SUBCASE("Random pushing and popping behavior versus std::queue")
  {
    auto rng = PCG::Rng(0);

    const auto PushSome = [](PCG::Rng& rng, int count, RingBuffer<int>& testRing, std::queue<int>& testQueue)
    {
      for (int i = 0; i < count; i++)
      {
        const auto value = static_cast<int>(rng.RandU32(0, 1000));
        testRing.push(value);
        testQueue.push(value);
      }
    };

    const auto PopSome = [](PCG::Rng&, int count, RingBuffer<int>& testRing, std::queue<int>& testQueue)
    {
      for (int i = 0; i < count && !testRing.empty(); i++)
      {
        CHECK_NOTHROW(testRing.pop());
        CHECK_NOTHROW(testQueue.pop());
      }
    };

    using Action = void(*)(PCG::Rng&, int, RingBuffer<int>&, std::queue<int>&);
    Action actions[] = {PushSome, PopSome};

    for (int i = 0; i < 10; i++)
    {
      auto testRing = RingBuffer<int>();
      auto testQueue = std::queue<int>();
      PushSome(rng, 100, testRing, testQueue);
      CompareRingBufferAndQueue(testRing, testQueue);

      // Do some random actions.
      for (int j = 0; j < 10; j++)
      {
        const int action = rng.RandU32(0, 1);
        const int count = rng.RandU32(0, 100);
        actions[action](rng, count, testRing, testQueue);
        CompareRingBufferAndQueue(testRing, testQueue);
      }
    }
  }
}