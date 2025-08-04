#pragma once
#include "entt/core/fwd.hpp"
#include "entt/entity/entity.hpp"

#include <cstdint>

namespace entt
{
  enum class entity : id_type;
}

using ItemId = entt::entity;

struct ItemState
{
  //ItemId id      = ItemId(0xDEADBEEF);
  ItemId id      = entt::null;
  int count      = 1;
  float useAccum = 1000;
};
