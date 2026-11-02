#pragma once
#include "entt/core/fwd.hpp"
#include "entt/entity/entity.hpp"

namespace entt
{
  enum class entity : id_type;
}

struct ItemId
{
  bool operator==(const ItemId&) const = default;
  uint32_t id;
};

constexpr ItemId NullItem = {uint32_t(entt::null)};

struct ItemIdAndCount
{
  ItemId item = NullItem;
  int count   = 1;
};

struct ItemState
{
  bool operator==(const ItemState&) const = default;
  //ItemId id      = ItemId(0xDEADBEEF);
  ItemId id      = NullItem;
  int count      = 1;
  float useAccum = 1000;
};
