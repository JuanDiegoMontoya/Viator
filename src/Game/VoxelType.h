#pragma once
#include <cstdint>

enum class voxel_t : std::uint32_t
{
  Air = 0,
  Null = ~0u,
};
