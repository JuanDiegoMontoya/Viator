#include "Prefab.h"
#include "World.h"
#include "Block.h"

void SimplePrefab::Instantiate(World& world, glm::ivec3 worldPos) const
{
  for (const auto& [blockPos, block] : voxels)
  {
    Block::OnTryPlaceBlock(world, worldPos + blockPos, block);
  }
}
