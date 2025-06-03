#pragma once
#include "Client/Fvog/detail/Flags.h"
#include "Game/VoxelType.h"

using BlockId               = voxel_t;
constexpr BlockId nullBlock = voxel_t::Null;

enum class BlockDamageFlagBit
{
  NONE               = 0,
  PICKAXE            = 1 << 0,
  AXE                = 1 << 1,
  ALL_TOOLS          = PICKAXE | AXE,
  NO_LOOT            = 1 << 2,
  NO_LOOT_95_PERCENT = 1 << 3,
};

FVOG_DECLARE_FLAG_TYPE(BlockDamageFlags, BlockDamageFlagBit, uint32_t);