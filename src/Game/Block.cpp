#include "Block.h"

#include "Item.h"
#include "Core/Assert2.h"
#include "Networking/RPC.h"
#include "Audio.h"

Block::Registry::Registry()
{
  registry_ = std::make_unique<entt::registry>();

  [[maybe_unused]] const auto air = Create("air");
}

Block::Registry::~Registry() = default;

BlockId Block::Registry::Get(const std::string& tag) const
{
  return tagToId_.at(tag);
}

BlockId Block::Registry::Create(std::string tag)
{
  ASSERT(!tagToId_.contains(tag));
  const auto id = BlockId(registry_->create(entt::entity(nextBlockId)));
  ASSERT(id == nextBlockId);
  tagToId_.emplace(tag, id);
  idToTag_.emplace(id, tag);
  nextBlockId = BlockId(uint32_t(nextBlockId) + 1);
  return id;
}

bool Block::OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition, BlockId block)
{
  const auto& blockRegistry = world.GetRegistry().ctx().get<Block::Registry>().GetRegistry();
  auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
  if (grid.IsPositionInGrid(voxelPosition))
  {
    Networking::CallRPC("SetVoxelAtRPC"_hs, world, voxelPosition, block);

    if (const auto* p = blockRegistry.try_get<Component::SpawnDependentEntityPrefabWhenPlaced>(entt::entity(block)))
    {
      const auto worldPosition = glm::vec3(voxelPosition) + glm::vec3(0.5f);

      auto& entityPrefabs = world.GetRegistry().ctx().get<EntityPrefabRegistry>();
      auto spawnedEntity  = entityPrefabs.Get(p->id).Spawn(world, glm::vec3(0), glm::identity<glm::quat>());
      auto& registry      = world.GetRegistry();

      auto parent = registry.create();
      registry.emplace<BlockEntity>(parent);
      registry.emplace<Hierarchy>(parent);
      registry.emplace<LocalTransform>(parent, LocalTransform{worldPosition, glm::identity<glm::quat>(), 1});
      registry.emplace<GlobalTransform>(parent);
      registry.emplace<Name>(parent, "Block Entity");
      world.SetParent(spawnedEntity, parent);
      world.UpdateLocalTransform(parent);
    }

    return true;
  }
  return false;
}

void Block::OnDestroyBlock(World& world, glm::ivec3 voxelPosition, BlockId block)
{
  auto& registry = world.GetRegistry().ctx().get<Block::Registry>().GetRegistry();

  Networking::CallRPC("SetVoxelAtRPC"_hs, world, voxelPosition, voxel_t::Air);

  if (const auto* explode = registry.try_get<const Component::ExplodeWhenBroken>(entt::entity(block)))
  {
    world.GetAudio()->PlaySound({
      .name             = "shot",
      .volume           = 0.5f,
      .attenuationModel = Audio::Sound::AttenuationModel::Linear,
      .maxDistance      = 100,
      .pitch            = 0.5f,
      .position         = glm::vec3(voxelPosition) + 0.5f,
    });

    const auto radius2 = explode->radius * explode->radius;
    const auto cr      = (int)ceil(explode->radius);
    // Additionally damage all blocks in a radius.
    for (int z = -cr; z <= cr; z++)
    for (int y = -cr; y <= cr; y++)
    for (int x = -cr; x <= cr; x++)
    {
      const auto newPos = voxelPosition + glm::ivec3(x, y, z);
      if (Math::Distance2(voxelPosition, newPos) <= radius2 && newPos != voxelPosition)
      {
        world.DamageBlock(newPos, explode->damage, explode->damageTier, explode->damageFlags);
      }
    }

    // Push entities away from center of blast.
    const auto center = glm::vec3(voxelPosition) + 0.5f;
    for (auto entity : world.GetEntitiesInSphere(center, explode->radius))
    {
      if (auto* v = world.GetRegistry().try_get<LinearVelocity>(entity))
      {
        const auto& t = world.GetRegistry().get<const GlobalTransform>(entity);

        const auto force = explode->pushForce;
        v->v += force * glm::normalize(t.position - center);
      }
    }
  }

  if (const auto* p = registry.try_get<Component::SpawnDependentEntityPrefabWhenPlaced>(entt::entity(block)))
  {
    auto entity = world.GetBlockEntity(voxelPosition);
    ASSERT(entity != entt::null && "Block entity didn't exist!");
    world.GetRegistry().emplace<DeferredDelete>(entity);
  }
}

std::variant<std::monostate, ItemState, std::string> Block::GetLootDropType(const World& world, BlockId block)
{
  const auto& registry = world.GetRegistry().ctx().get<Block::Registry>().GetRegistry();
  if (const auto* b = registry.try_get<const Component::Breakable>(entt::entity(block)))
  {
    if (std::get_if<DropSelf>(&b->dropWhenBroken))
    {
      if (const auto* i = registry.try_get<const Component::CorrespondingItem>(entt::entity(block)))
      {
        return ItemState{.id = i->item};
      }
    }
    else if (auto* is = std::get_if<ItemState>(&b->dropWhenBroken))
    {
      return *is;
    }
    else if (auto* s = std::get_if<std::string>(&b->dropWhenBroken))
    {
      return *s;
    }
  }
  return std::monostate{};
}

bool Block::IsVisible(const World& world, BlockId block)
{
  const auto& bReg = world.GetRegistry().ctx().get<Registry>().GetRegistry();
  return bReg.any_of<Component::RenderAsTexturedCube, Component::RenderAsSubGrid>(entt::entity(block));
}

bool Block::IsSolid(const World& world, BlockId block)
{
  const auto& bReg = world.GetRegistry().ctx().get<Registry>().GetRegistry();
  if (const auto* p = bReg.try_get<const Component::PhysicalProperties>(entt::entity(block)))
  {
    return p->isSolid;
  }
  return false;
}

const TwoLevelGrid::SubGrid* Block::GetSubGrid(const World& world, BlockId block)
{
  const auto& bReg = world.GetRegistry().ctx().get<Registry>().GetRegistry();
  if (const auto* p = bReg.try_get<const Component::RenderAsSubGrid>(entt::entity(block)))
  {
    return p->subGrid.get();
  }
  return nullptr;
}

ItemId Block::GetItemId(const World& world, BlockId block)
{
  const auto& bReg = world.GetRegistry().ctx().get<Registry>().GetRegistry();
  if (const auto* p = bReg.try_get<const Component::CorrespondingItem>(entt::entity(block)))
  {
    return p->item;
  }
  return entt::null;
}

float Block::GetInitialHealth(const World& world,  BlockId block)
{
  const auto& bReg = world.GetRegistry().ctx().get<Registry>().GetRegistry();
  if (const auto* p = bReg.try_get<const Component::Breakable>(entt::entity(block)))
  {
    return p->initialHealth;
  }
  return 0;
}

BlockDamageFlags Block::GetDamageFlags(const World& world,  BlockId block)
{
  const auto& bReg = world.GetRegistry().ctx().get<Registry>().GetRegistry();
  if (const auto* p = bReg.try_get<const Component::Breakable>(entt::entity(block)))
  {
    return p->damageFlags;
  }
  return BlockDamageFlagBit::NONE;
}

int Block::GetDamageTier(const World& world,  BlockId block)
{
  const auto& bReg = world.GetRegistry().ctx().get<Registry>().GetRegistry();
  if (const auto* p = bReg.try_get<const Component::Breakable>(entt::entity(block)))
  {
    return p->damageTier;
  }
  return 0;
}

std::string Block::GetName(const World& world,  BlockId block)
{
  const auto& blocks = world.GetRegistry().ctx().get<Registry>();
  const auto& bReg = blocks.GetRegistry();
  if (const auto* p = bReg.try_get<const Name>(entt::entity(block)))
  {
    return p->name;
  }
  return blocks.GetIdToTagMap().at(block);
}

BlockId Block::CreateStandardBlock(World& world, const CreateBlockParams& params)
{
  auto& blocks = world.GetRegistry().ctx().get<Block::Registry>();
  auto& bReg   = blocks.GetRegistry();

  auto block = blocks.Create(std::move(params.tag));

  bReg.emplace<Name>(entt::entity(block), std::move(params.name));

  if (params.breakable.has_value())
  {
    bReg.emplace<Block::Component::Breakable>(entt::entity(block), *params.breakable);
  }

  if (params.render.has_value())
  {
    std::visit([&]<typename T>(T&& arg) -> void { bReg.emplace<std::remove_cvref_t<T>>(entt::entity(block), arg); }, *params.render);
  }

  bReg.emplace<Block::Component::PhysicalProperties>(entt::entity(block), params.physicalProperties);

  if (params.valuable.has_value())
  {
    bReg.emplace<Block::Component::Valuable>(entt::entity(block), *params.valuable);
  }

  if (params.explode.has_value())
  {
    bReg.emplace<Block::Component::ExplodeWhenBroken>(entt::entity(block), *params.explode);
  }

  if (params.entityPrefab.has_value())
  {
    bReg.emplace<Block::Component::SpawnDependentEntityPrefabWhenPlaced>(entt::entity(block), *params.entityPrefab);
  }

  Item::RegisterItemForBlock(world, block);

  return block;
}

