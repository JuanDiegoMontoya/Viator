#include "Block.h"

#include "Item.h"
#include "Core/Assert2.h"
#include "Networking/RPC.h"
#include "Audio.h"

BlockDefinition::BlockDefinition(const CreateInfo& info) : createInfo_(info) {}

bool BlockDefinition::OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition) const
{
  auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
  if (grid.IsPositionInGrid(voxelPosition))
  {
    Networking::CallRPC("SetVoxelAtRPC"_hs, world, voxelPosition, GetBlockId());
    return true;
  }
  return false;
}

void BlockDefinition::OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const
{
  Networking::CallRPC("SetVoxelAtRPC"_hs, world, voxelPosition, voxel_t::Air);
}

const BlockDefinition& BlockRegistry::Get(const std::string& name) const
{
  return *idToDefinition_.at((uint32_t)nameToId_.at(name));
}

const BlockDefinition& BlockRegistry::Get(BlockId id) const
{
  return *idToDefinition_.at((uint32_t)id);
}

BlockId BlockRegistry::Add(BlockDefinition* blockDefinition)
{
  ASSERT(!nameToId_.contains(blockDefinition->GetName()));
  ASSERT(world_->GetRegistry().ctx().contains<Item::Registry>());

  const auto myBlockId      = (BlockId)idToDefinition_.size();
  blockDefinition->blockId_ = myBlockId;

  nameToId_.emplace(blockDefinition->GetName(), myBlockId);
  idToDefinition_.emplace_back(blockDefinition);

  auto& itemRegistry = world_->GetRegistry().ctx().get<Item::Registry>();
  auto& reg          = itemRegistry.GetRegistry();
  const auto e = itemRegistry.Create(blockDefinition->GetName());
  reg.emplace<Name>(e).name = blockDefinition->GetName();
  reg.emplace<Item::Component::MaterializeAsMeshEntity>(e) = {.mesh = "cube", .position = {0.2f, -0.2f, -0.5f}};
  reg.emplace<Item::Component::Usable>(e).timeBetweenUses  = 0.5f;
  reg.emplace<Item::Component::Stackable>(e);
  reg.emplace<Item::Component::ColliderWhenDropped>(e);
  reg.emplace<Item::Component::AllowedSlots>(e, Item::Component::AllowedSlots::Normal);
  reg.emplace<Item::Component::Block>(e).voxel = myBlockId;

  blockDefinition->itemId_ = e;

  return myBlockId;
}

void ExplodeyBlockDefinition::OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const
{
  BlockDefinition::OnDestroyBlock(world, voxelPosition);

  world.GetAudio()->PlaySound({
    .name             = "shot",
    .volume = 0.5f,
    .attenuationModel = Audio::Sound::AttenuationModel::Linear,
    .maxDistance      = 100,
    .pitch            = 0.5f,
    .position = glm::vec3(voxelPosition) + 0.5f,
  });

  const auto radius2 = explodeyInfo_.radius * explodeyInfo_.radius;
  const auto cr      = (int)ceil(explodeyInfo_.radius);
  // Additionally damage all blocks in a radius.
  for (int z = -cr; z <= cr; z++)
    for (int y = -cr; y <= cr; y++)
      for (int x = -cr; x <= cr; x++)
      {
        const auto newPos = voxelPosition + glm::ivec3(x, y, z);
        if (Math::Distance2(voxelPosition, newPos) <= radius2 && newPos != voxelPosition)
        {
          world.DamageBlock(newPos, explodeyInfo_.damage, explodeyInfo_.damageTier, explodeyInfo_.damageFlags);
        }
      }

  // Push entities away from center of blast.
  const auto center = glm::vec3(voxelPosition) + 0.5f;
  for (auto entity : world.GetEntitiesInSphere(center, explodeyInfo_.radius))
  {
    if (auto* v = world.GetRegistry().try_get<LinearVelocity>(entity))
    {
      const auto& t = world.GetRegistry().get<const GlobalTransform>(entity);

      const auto force = explodeyInfo_.pushForce;
      v->v += force * glm::normalize(t.position - center);
    }
  }
}

bool BlockEntityDefinition::OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition) const
{
  if (BlockDefinition::OnTryPlaceBlock(world, voxelPosition))
  {
    const auto worldPosition = glm::vec3(voxelPosition) + glm::vec3(0.5f);

    auto& entityPrefabs = world.GetRegistry().ctx().get<EntityPrefabRegistry>();
    auto spawnedEntity  = entityPrefabs.Get(blockEntityInfo_.id).Spawn(world, glm::vec3(0), glm::identity<glm::quat>());
    auto& registry      = world.GetRegistry();

    auto parent = registry.create();
    registry.emplace<BlockEntity>(parent);
    registry.emplace<Hierarchy>(parent);
    registry.emplace<LocalTransform>(parent, LocalTransform{worldPosition, glm::identity<glm::quat>(), 1});
    registry.emplace<GlobalTransform>(parent);
    registry.emplace<Name>(parent, "Block Entity");
    world.SetParent(spawnedEntity, parent);
    world.UpdateLocalTransform(parent);
    return true;
  }
  return false;
}

void BlockEntityDefinition::OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const
{
  BlockDefinition::OnDestroyBlock(world, voxelPosition);
  auto entity = world.GetBlockEntity(voxelPosition);
  ASSERT(entity != entt::null && "Block entity didn't exist!");
  world.GetRegistry().emplace<DeferredDelete>(entity);
}
