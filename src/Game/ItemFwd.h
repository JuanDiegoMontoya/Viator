#pragma once
#include <cstdint>

using ItemId              = uint32_t;
constexpr ItemId nullItem = ~0u;

struct ItemState
{
  ItemId id      = nullItem;
  int count      = 1;
  float useAccum = 1000;
};