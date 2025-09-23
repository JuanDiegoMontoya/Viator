#pragma once
#include "entt/core/fwd.hpp"
#include "entt/entity/entity.hpp"

namespace entt
{
  enum class entity : id_type;
}

using ItemId = entt::entity;

struct ItemState
{
  bool operator==(const ItemState&) const = default;
  //ItemId id      = ItemId(0xDEADBEEF);
  ItemId id      = entt::null;
  int count      = 1;
  float useAccum = 1000;
};
