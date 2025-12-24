#include "Prefab.h"
#include "World.h"
#include "Game/Globals.h"
#include "Block.h"
#include "Core/Serialization.h"

#include <fstream>

SimplePrefab::SimplePrefab(const World& world, const CreateInfo& info, const SerializableSimplePrefab& prefab)
  : PrefabDefinition(info)
{
  const auto& blockReg = *world.globals->blockRegistry;

  voxels.reserve(prefab.voxels.size());
  for (const auto& [pos, voxel] : prefab.voxels)
  {
    voxels.emplace_back(pos, blockReg.Get(prefab.voxelToName.at(uint32_t(voxel))));
  }
}

SimplePrefab::SimplePrefab(const World& world, const CreateInfo& info, std::string_view path)
  : PrefabDefinition(info)
{
  auto file = std::ifstream(std::string(path), std::fstream::in | std::fstream::binary);
  ASSERT(file);
  const auto raw     = std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  auto any           = Core::Serialization::DeserializeObject(raw, entt::resolve<SerializableSimplePrefab>());
  const auto* prefab = static_cast<const SerializableSimplePrefab*>(any.as_ref().base().data());
  ASSERT(prefab);
  
  *this = SimplePrefab(world, info, *prefab);
}

void SimplePrefab::Instantiate(World& world, glm::ivec3 worldPos) const
{
  for (const auto& [blockPos, block] : voxels)
  {
    Block::OnTryPlaceBlock(world, worldPos + blockPos, block);
  }
}

SerializableSimplePrefab::SerializableSimplePrefab(const World& world, std::span<const std::pair<glm::ivec3, voxel_t>> prefab)
{
  const auto& blockReg = *world.globals->blockRegistry;

  voxels.reserve(prefab.size());
  for (const auto& [pos, voxel] : prefab)
  {
    voxels.emplace_back(pos, voxel);
    voxelToName.try_emplace(uint32_t(voxel), blockReg.GetIdToTagMap().at(voxel));
  }
}