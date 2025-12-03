#include "Block.h"
#include "Voxel/Grid.h"
#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Globals.h"

#include "Item.h"
#include "Core/Assert2.h"
#include "Networking/RPC.h"
#include "Audio.h"
#include "Scripting.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"
#include "tracy/Tracy.hpp"
#include "entt/meta/resolve.hpp"

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

entt::registry& Block::Registry::GetRegistry()
{
  return *registry_;
}

const entt::registry& Block::Registry::GetRegistry() const
{
  return *registry_;
}

const std::unordered_map<std::string, BlockId>& Block::Registry::GetTagToIdMap() const
{
  return tagToId_;
}

const std::map<BlockId, std::string>& Block::Registry::GetIdToTagMap() const
{
  return idToTag_;
}

static bool OnTryPlaceBlockExt(World& world, glm::ivec3 voxelPosition, BlockId block, bool isBeingTransformed = false)
{
  const auto& blockRegistry = world.globals->blockRegistry->GetRegistry();
  auto& grid                = *world.globals->grid;
  if (grid.IsPositionInGrid(voxelPosition))
  {
    // Ensure the block is supported, if necessary.
    if (const auto* support = blockRegistry.try_get<const Block::Component::RequiresSupport>(entt::entity(block)))
    {
      const auto neighborPos = DirectionToNeighbor(support->supportingSide);
      const auto neighbor    = grid.GetVoxelAt(voxelPosition + neighborPos);
      if (const auto* pp = blockRegistry.try_get<const Block::Component::RequiresSupportByBlock>(entt::entity(block)))
      {
        if (neighbor != pp->block)
        {
          return false;
        }
      }
      else if (const auto* ppp = blockRegistry.try_get<const Block::Component::PhysicalProperties>(entt::entity(neighbor)))
      {
        if (!ppp->isSolid)
        {
          return false;
        }
      }
      else
      {
        return false;
      }
    }

    if (const auto* sp = blockRegistry.try_get<const Block::Component::SpawnExtraBlockOnPlace>(entt::entity(block)); sp && !isBeingTransformed)
    {
      const auto neighborPos = Block::DirectionToNeighbor(sp->direction);
      if (!grid.IsPositionInGrid(voxelPosition + neighborPos))
      {
        return false;
      }

      const auto neighbor = grid.GetVoxelAt(voxelPosition + neighborPos);
      if (neighbor != voxel_t::Air)
      {
        return false;
      }
    }

    if (!world.IsHosting())
    {
      SetVoxelAtRPC(world, voxelPosition, block);
    }
    else
    {
      Networking::CallRPC("SetVoxelAtRPC"_hs, world, voxelPosition, block);
    }

    // Defer the actual placement of blocks that potentially depend on the base to exist.
    if (const auto* sp = blockRegistry.try_get<const Block::Component::SpawnExtraBlockOnPlace>(entt::entity(block)); sp && !isBeingTransformed)
    {
      const auto neighborPos          = Block::DirectionToNeighbor(sp->direction);
      [[maybe_unused]] const bool res = Block::OnTryPlaceBlock(world, voxelPosition + neighborPos, sp->block);
      DEBUG_ASSERT(res);
    }

    if (const auto* p = blockRegistry.try_get<Block::Component::SpawnDependentEntityPrefabWhenPlaced>(entt::entity(block)); p && !isBeingTransformed)
    {
      const auto worldPosition = glm::vec3(voxelPosition) + glm::vec3(0.5f);

      auto& entityPrefabs = *world.globals->entityPrefabRegistry;
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

    if (blockRegistry.any_of<Block::Component::Flows, Block::Component::BaseFlow>(entt::entity(block)))
    {
      world.globals->waterQueue->push(voxelPosition);
    }

    return true;
  }
  return false;
}

bool Block::OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition, BlockId block)
{
  ZoneScoped;
  return OnTryPlaceBlockExt(world, voxelPosition, block);
}

void Block::OnDestroyBlock(World& world, glm::ivec3 voxelPosition, BlockId block)
{
  ZoneScoped;

  auto& registry = world.globals->blockRegistry->GetRegistry();

  Networking::CallRPC("SetVoxelAtRPC"_hs, world, voxelPosition, voxel_t::Air);

  if (const auto* p = registry.try_get<const Component::InterlinkedBlock>(entt::entity(block)))
  {
    const auto neighborPos = voxelPosition + DirectionToNeighbor(p->direction);
    const auto neighbor    = world.globals->grid->GetVoxelAt(neighborPos);
    SpawnLootDropFromBlock(world, neighborPos, neighbor);
    OnDestroyBlock(world, neighborPos, neighbor);
  }

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
      if (Math::Distance2(glm::vec3(voxelPosition), newPos) <= radius2 && newPos != voxelPosition)
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

  if (registry.all_of<Component::SpawnDependentEntityPrefabWhenPlaced>(entt::entity(block)))
  {
    auto entity = world.GetBlockEntity(voxelPosition);
    ASSERT(entity != entt::null && "Block entity didn't exist!");
    world.GetRegistry().emplace<DeferredDelete>(entity);
  }

  if (const auto* p = registry.try_get<Component::Script>(entt::entity(block)))
  {
    world.globals->scripting->ExecuteScript(p->path, "OnDestroyBlock", {&world, voxelPosition, block});
  }

  for (int i = 0; i < 6; i++)
  {
    const auto dir = static_cast<Direction>(i);
    OnUpdateBlock(world, voxelPosition + DirectionToNeighbor(dir));
  }
}

std::variant<std::monostate, ItemState, std::string> Block::GetLootDropType(const World& world, BlockId block)
{
  const auto& registry = world.globals->blockRegistry->GetRegistry();
  if (const auto* b = registry.try_get<const Component::Breakable>(entt::entity(block)))
  {
    if (std::get_if<DropSelf>(&b->dropWhenBroken))
    {
      if (const auto* bv = registry.try_get<const Component::BaseVariant>(entt::entity(block)))
      {
        return GetLootDropType(world, bv->block);
      }
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

void Block::OnUpdateBlock(World& world, glm::ivec3 voxelPosition)
{
  ZoneScoped;

  auto& grid = *world.globals->grid;
  auto block = grid.GetVoxelAt(voxelPosition);
  auto& reg  = world.globals->blockRegistry->GetRegistry();

  // Check whether block requires support from a specific side.
  bool isSupported = true;
  if (const auto* p = reg.try_get<const Block::Component::RequiresSupport>(entt::entity(block)))
  {
    const auto neighborPos = voxelPosition + DirectionToNeighbor(p->supportingSide);
    const auto neighbor = grid.GetVoxelAt(neighborPos);
    if (const auto* b = reg.try_get<const Block::Component::RequiresSupportByBlock>(entt::entity(block)))
    {
      if (b->block != neighbor)
      {
        isSupported = false;
      }
    }
    else
    {
      if (const auto* pp = reg.try_get<const Block::Component::PhysicalProperties>(entt::entity(neighbor)))
      {
        if (!pp->isSolid)
        {
          isSupported = false;
        }
      }
      else
      {
        isSupported = false;
      }
    }
  }

  if (const auto* p = reg.try_get<const Block::Component::RequiresSupportByBlocks>(entt::entity(block)))
  {
    for (int i = 0; i < 6; i++)
    {
      const auto neighborPos = voxelPosition + DirectionToNeighbor(Direction(i));
      const auto neighbor    = grid.GetVoxelAt(neighborPos);
      if (p->blocks[i] && *p->blocks[i] != neighbor)
      {
        isSupported = false;
        break;
      }
    }
  }

  if (!isSupported)
  {
    SpawnLootDropFromBlock(world, voxelPosition, block);
    OnDestroyBlock(world, voxelPosition, block);
  }

  if (const auto* p = reg.try_get<const Block::Component::Flows>(entt::entity(block)))
  {
    ZoneScopedN("Component::Flows");

    // TODO: Cache result of this function to improve performance.
    const auto GetFlowIndex = [&reg](BlockId block) -> int
    {
      if (block == voxel_t::Air)
      {
        return 0;
      }

      if (const auto* p = reg.try_get<const Block::Component::Flows>(entt::entity(block)))
      {
        if (const auto* baseFlow = reg.try_get<const Block::Component::BaseFlow>(entt::entity(p->base)))
        {
          for (int i = 0; i < baseFlow->blocks.size(); i++)
          {
            if (block == baseFlow->blocks[i])
            {
              return i + 1;
            }
          }
        }
      }

      return -1;
    };

    const auto GetBlockFromFlowIndex = [&reg](BlockId block, int flowIdx) -> BlockId
    {
      if (flowIdx == 0)
      {
        return voxel_t::Air;
      }

      if (const auto* p = reg.try_get<const Block::Component::Flows>(entt::entity(block)))
      {
        if (const auto* baseFlow = reg.try_get<const Block::Component::BaseFlow>(entt::entity(p->base)))
        {
          return baseFlow->blocks.at(flowIdx - 1);
        }
      }

      PANIC;
    };

    const auto TransferFlowTo = [&](Direction direction)
    {
      ZoneScopedN("TransferFlowTo");
      const auto neighborDir   = DirectionToNeighbor(direction);
      const auto neighborPos   = voxelPosition + neighborDir;
      if (!grid.IsPositionInGrid(neighborPos))
      {
        return;
      }
      const auto neighborBlock = grid.GetVoxelAt(neighborPos);
      const auto neighborFlow  = GetFlowIndex(neighborBlock);
      const auto myFlow        = GetFlowIndex(block);
      if (neighborFlow < 0)
      {
        return;
      }

      // Transfer as much flow downwards as possible.
      if (direction == Direction::Down)
      {
        const auto newBlock = GetBlockFromFlowIndex(block, (myFlow + neighborFlow <= 8) ? 0 : ((myFlow + neighborFlow) - 8));
        if (newBlock == block)
        {
          return;
        }
        OnTryPlaceBlock(world, voxelPosition, newBlock);
        OnTryPlaceBlock(world, neighborPos, GetBlockFromFlowIndex(block, glm::min(myFlow + neighborFlow, 8)));
        world.globals->waterQueue->push(voxelPosition);
        world.globals->waterQueue->push(neighborPos);
        QueueUpdateNeighbors(world, voxelPosition);
        QueueUpdateNeighbors(world, neighborPos);
        block = newBlock;
      }
      else if (myFlow > neighborFlow)
      {
        // Stochastically stop updates when gradient is small (prevents some endless update patterns).
        if (myFlow == neighborFlow + 1 && world.Rng().RandFloat() < 0.75f)
        {
          world.globals->waterSet->emplace(voxelPosition);
          return;
        }

        // "Evaporate" small gradients.
        if (myFlow == 1 && world.Rng().RandFloat() < 0.01f)
        {
          OnTryPlaceBlock(world, voxelPosition, voxel_t::Air);
          QueueUpdateNeighbors(world, voxelPosition);
          return;
        }
        const auto newBlock = GetBlockFromFlowIndex(block, myFlow - 1);
        OnTryPlaceBlock(world, voxelPosition, newBlock);
        OnTryPlaceBlock(world, neighborPos, GetBlockFromFlowIndex(block, neighborFlow + 1));
        world.globals->waterQueue->push(voxelPosition);
        world.globals->waterQueue->push(neighborPos);
        QueueUpdateNeighbors(world, voxelPosition);
        QueueUpdateNeighbors(world, neighborPos);
        block = newBlock;
      }
    };

    TransferFlowTo(Direction::Down);
#if 1
    auto dirs = std::vector{Direction::North, Direction::West, Direction::South, Direction::East};
    while (!dirs.empty())
    {
      const auto idx = dirs.size() > 1 ? world.Rng().RandU32(0, (uint32_t)dirs.size() - 1) : 0;
      TransferFlowTo(dirs[idx]);
      std::swap(dirs[idx], dirs.back());
      dirs.pop_back();
    }
#else
    TransferFlowTo(Direction::North);
    TransferFlowTo(Direction::West);
    TransferFlowTo(Direction::South);
    TransferFlowTo(Direction::East);
#endif
  }
}

void Block::QueueUpdateNeighbors(World& world, glm::ivec3 position)
{
  auto& queue = *world.globals->waterQueue;
  queue.push(position + DirectionToNeighbor(Direction::Up));
  queue.push(position + DirectionToNeighbor(Direction::North));
  queue.push(position + DirectionToNeighbor(Direction::East));
  queue.push(position + DirectionToNeighbor(Direction::South));
  queue.push(position + DirectionToNeighbor(Direction::West));
}

void OnUseBlockHelper(World& world, glm::ivec3 voxelPosition, BlockId block, int remainingDepth)
{
  ZoneScoped;

  auto& grid = *world.globals->grid;
  auto& reg  = world.globals->blockRegistry->GetRegistry();

  if (const auto* p = reg.try_get<const Block::Component::TransformWhenUsed>(entt::entity(block)))
  {
    OnTryPlaceBlockExt(world, voxelPosition, p->block, true);
  }

  if (const auto* p = reg.try_get<const Block::Component::InterlinkedBlock>(entt::entity(block)); p && remainingDepth > 0)
  {
    const auto neighborPos = voxelPosition + Block::DirectionToNeighbor(p->direction);
    OnUseBlockHelper(world, neighborPos, grid.GetVoxelAt(neighborPos), remainingDepth - 1);
  }
}

void Block::OnUseBlock(World& world, glm::ivec3 voxelPosition, BlockId block)
{
  ZoneScoped;
  OnUseBlockHelper(world, voxelPosition, block, 1);
}

void Block::SpawnLootDropFromBlock(World& world, glm::ivec3 voxelPos, BlockId block)
{
  auto& registry = world.GetRegistry();
  const auto worldPos = glm::vec3(voxelPos) + 0.5f;

  const auto dropType = Block::GetLootDropType(world, block);
  if (auto* ip = std::get_if<ItemState>(&dropType))
  {
    auto itemSelf = Item::Materialize(world, ip->id);

    registry.get<LocalTransform>(itemSelf).position = worldPos;
    world.UpdateLocalTransform(itemSelf);
    Item::GiveCollider(world, ip->id, itemSelf);
    registry.emplace<DroppedItem>(itemSelf).item = *ip;

    const auto throwdir                                  = glm::vec3(world.Rng().RandFloat(-0.25f, 0.25f), 1, world.Rng().RandFloat(-0.25f, 0.25f));
    registry.get_or_emplace<LinearVelocity>(itemSelf).v = throwdir * 2.0f;
  }
  else if (auto* lp = std::get_if<std::string>(&dropType))
  {
    auto* table = world.globals->game->lootRegistry.Get(*lp);
    ASSERT(table);
    for (auto drop : table->Collect(world.Rng()))
    {
      auto droppedEntity = Item::Materialize(world, drop.item);
      Item::GiveCollider(world, drop.item, droppedEntity);
      registry.get<LocalTransform>(droppedEntity).position = worldPos;
      world.UpdateLocalTransform(droppedEntity);
      registry.emplace<DroppedItem>(droppedEntity, DroppedItem{{.id = drop.item, .count = drop.count}});
      auto velocity = glm::vec3(0);
      const auto newEntityVelocity =
        velocity +
        world.Rng().RandFloat(1, 3) * Math::RandVecInCone({world.Rng().RandFloat(), world.Rng().RandFloat()}, glm::vec3(0, 1, 0), glm::half_pi<float>());
      registry.emplace_or_replace<LinearVelocity>(droppedEntity, newEntityVelocity);
    }
  }
}

bool Block::IsVisible(const World& world, BlockId block)
{
  const auto& bReg = world.globals->blockRegistry->GetRegistry();
  return bReg.any_of<Component::RenderAsTexturedCube, Component::RenderAsTexturedCube2, Component::RenderAsSubGrid>(entt::entity(block));
}

bool Block::IsSolid(const World& world, BlockId block)
{
  const auto& bReg = world.globals->blockRegistry->GetRegistry();
  if (const auto* p = bReg.try_get<const Component::PhysicalProperties>(entt::entity(block)))
  {
    return p->isSolid;
  }
  return false;
}

const Voxel::SubGrid* Block::GetSubGrid(const World& world, BlockId block)
{
  const auto& bReg = world.globals->blockRegistry->GetRegistry();
  if (const auto* p = bReg.try_get<const Component::RenderAsSubGrid>(entt::entity(block)))
  {
    return p->subGrid.get();
  }
  return nullptr;
}

ItemId Block::GetItemId(const World& world, BlockId block)
{
  const auto& bReg = world.globals->blockRegistry->GetRegistry();
  if (const auto* p = bReg.try_get<const Component::CorrespondingItem>(entt::entity(block)))
  {
    return p->item;
  }
  return entt::null;
}

float Block::GetInitialHealth(const World& world,  BlockId block)
{
  const auto& bReg = world.globals->blockRegistry->GetRegistry();
  if (const auto* p = bReg.try_get<const Component::Breakable>(entt::entity(block)))
  {
    return p->initialHealth;
  }
  return 0;
}

BlockDamageFlags Block::GetDamageFlags(const World& world,  BlockId block)
{
  const auto& bReg = world.globals->blockRegistry->GetRegistry();
  if (const auto* p = bReg.try_get<const Component::Breakable>(entt::entity(block)))
  {
    return p->damageFlags;
  }
  return BlockDamageFlagBit::NONE;
}

int Block::GetDamageTier(const World& world,  BlockId block)
{
  const auto& bReg = world.globals->blockRegistry->GetRegistry();
  if (const auto* p = bReg.try_get<const Component::Breakable>(entt::entity(block)))
  {
    return p->damageTier;
  }
  return 0;
}

std::string Block::GetName(const World& world,  BlockId block)
{
  const auto& blocks = *world.globals->blockRegistry;
  const auto& bReg = blocks.GetRegistry();
  if (const auto* p = bReg.try_get<const Name>(entt::entity(block)))
  {
    return p->name;
  }
  return blocks.GetIdToTagMap().at(block);
}

BlockId Block::GetRotatedBlockVariant(const World& world, BlockId block, glm::vec3 viewDir, [[maybe_unused]] glm::vec3 normal)
{
  const auto& blocks = *world.globals->blockRegistry;
  const auto& bReg   = blocks.GetRegistry();

  if (const auto* p = bReg.try_get<const Component::StandardRotatedVariants>(entt::entity(block)))
  {
    float* big = &viewDir.x;
    // if (abs(viewDir.y) > abs(*big))
    //   big = &viewDir.y;
    if (abs(viewDir.z) > abs(*big))
    {
      big = &viewDir.z;
    }

    if (big == &viewDir.x)
    {
      if (*big < 0)
      {
        return p->west;
      }
      return p->east;
    }
    if (*big > 0)
    {
      return p->south;
    }
  }

  return block;
}

BlockId Block::GetRotatedBlockVariant(const World& world, BlockId block, Direction direction)
{
  auto& blocks = *world.globals->blockRegistry;
  auto& bReg   = blocks.GetRegistry();

  const auto dir = WhichRotatedVariantAmI(world, block);
  if (dir == direction)
  {
    return block;
  }

  auto rotated = Component::StandardRotatedVariants{};
  if (const auto* p = bReg.try_get<const Component::StandardRotatedVariants>(entt::entity(block))) // Only the North variant (base) should have this.
  {
    rotated = *p;
  }

  if (const auto* bp = bReg.try_get<const Component::BaseVariant>(entt::entity(block)))
  {
    ASSERT(bReg.all_of<Component::StandardRotatedVariants>(entt::entity(bp->block)));
    rotated = bReg.get<const Component::StandardRotatedVariants>(entt::entity(bp->block));
  }

  if (direction == Direction::East)
  {
    return rotated.east;
  }

  if (direction == Direction::South)
  {
    return rotated.south;
  }

  if (direction == Direction::West)
  {
    return rotated.west;
  }

  return block;
}

BlockId Block::CreateStandardBlock(World& world, const CreateBlockParams& params)
{
  ZoneScoped;

  auto& blocks = *world.globals->blockRegistry;
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
    bReg.emplace<Block::Component::Valuable>(entt::entity(block));
  }

  if (params.explode.has_value())
  {
    bReg.emplace<Block::Component::ExplodeWhenBroken>(entt::entity(block), *params.explode);
  }

  if (params.entityPrefab.has_value())
  {
    bReg.emplace<Block::Component::SpawnDependentEntityPrefabWhenPlaced>(entt::entity(block), *params.entityPrefab);
  }

  if (params.support.has_value())
  {
    bReg.emplace<Block::Component::RequiresSupport>(entt::entity(block), *params.support);
  }

  if (params.supportByBlock.has_value())
  {
    bReg.emplace<Block::Component::RequiresSupportByBlock>(entt::entity(block), *params.supportByBlock);
  }

  Item::RegisterItemForBlock(world, block);

  return block;
}

static Voxel::SubGrid MakeRotatedSubGrid(const Voxel::SubGrid& subGrid, const glm::mat3& rotation)
{
  auto newGrid            = Voxel::SubGrid{};
  newGrid.dimensions      = subGrid.dimensions;
  const auto numSubVoxels = newGrid.dimensions.x * newGrid.dimensions.y * newGrid.dimensions.z;
  newGrid.grid            = std::make_unique<Voxel::SubVoxel[]>(numSubVoxels);
  std::ranges::copy(subGrid.materials, newGrid.materials);

  for (int z = 0; z < newGrid.dimensions.z; z++)
  for (int y = 0; y < newGrid.dimensions.y; y++)
  for (int x = 0; x < newGrid.dimensions.x; x++)
  {
    const auto centeredCoord = glm::vec3(x, y, z) - (0.5f * (glm::vec3(newGrid.dimensions) - 1.0f));
    const auto rotated       = rotation * centeredCoord;
    const auto rotatedCoord  = glm::ivec3(glm::round(rotated + (0.5f * (glm::vec3(newGrid.dimensions) - 1.0f))));
    const auto outIndex      = Voxel::Grid::FlattenGenericCoord(glm::ivec3(newGrid.dimensions), rotatedCoord);
    const auto inIndex       = Voxel::Grid::FlattenGenericCoord(glm::ivec3(subGrid.dimensions), {x, y, z});
    DEBUG_ASSERT(outIndex < numSubVoxels);
    DEBUG_ASSERT(inIndex < numSubVoxels);
    newGrid.grid[outIndex] = subGrid.grid[inIndex];
  }

  return newGrid;
}

// Reference orientation: North.
static Block::Component::RenderAsTexturedCube2 MakeRotatedCubeFaceMaterials(const Block::Component::RenderAsTexturedCube2& cube, Block::Direction newDir)
{
  ASSERT(newDir == Block::Direction::East || newDir == Block::Direction::South || newDir == Block::Direction::West);
  auto ret = Block::Component::RenderAsTexturedCube2(cube);

  using Block::Direction;
  if (newDir == Block::Direction::East)
  {
    ret.faces[int(Direction::Up)].texcoordsQuarterTurns   = 1;
    ret.faces[int(Direction::Down)].texcoordsQuarterTurns = 1;
    ret.faces[int(Direction::East)]  = cube.faces[int(Direction::North)];
    ret.faces[int(Direction::South)] = cube.faces[int(Direction::East)];
    ret.faces[int(Direction::West)]  = cube.faces[int(Direction::South)];
    ret.faces[int(Direction::North)] = cube.faces[int(Direction::West)];
  }

  if (newDir == Block::Direction::South)
  {
    ret.faces[int(Direction::Up)].texcoordsQuarterTurns   = 2;
    ret.faces[int(Direction::Down)].texcoordsQuarterTurns = 2;
    ret.faces[int(Direction::East)]  = cube.faces[int(Direction::West)];
    ret.faces[int(Direction::South)] = cube.faces[int(Direction::North)];
    ret.faces[int(Direction::West)]  = cube.faces[int(Direction::East)];
    ret.faces[int(Direction::North)] = cube.faces[int(Direction::South)];
  }

  if (newDir == Block::Direction::West)
  {
    ret.faces[int(Direction::Up)].texcoordsQuarterTurns   = 3;
    ret.faces[int(Direction::Down)].texcoordsQuarterTurns = 3;
    ret.faces[int(Direction::East)]  = cube.faces[int(Direction::South)];
    ret.faces[int(Direction::South)] = cube.faces[int(Direction::West)];
    ret.faces[int(Direction::West)]  = cube.faces[int(Direction::North)];
    ret.faces[int(Direction::North)] = cube.faces[int(Direction::East)];
  }

  return ret;
}

#include "Core/Reflection.h"
#include "entt/meta/meta.hpp"
static void CopyEntity(entt::registry& registry, entt::entity srcEntity, entt::entity dstEntity)
{
  for (auto&& [type, storage] : registry.storage())
  {
    if (storage.contains(srcEntity))
    {
      const auto meta         = entt::resolve(type);
      const bool isEmptyType  = meta.traits<Core::Reflection::Traits>() & Core::Reflection::Traits::EMPTY;
      auto emplaceDefaultFunc = meta.func("EmplaceDefault"_hs);
      DEBUG_ASSERT(emplaceDefaultFunc);
      emplaceDefaultFunc.invoke({}, &registry, dstEntity);
      if (!isEmptyType)
      {
        auto oldComp = meta.from_void(storage.value(srcEntity));
        auto newComp = meta.from_void(storage.value(dstEntity));
        newComp.assign(oldComp);
      }
    }
  }
}

Block::Component::StandardRotatedVariants& Block::CreateStandardRotatedVariants(World& world, BlockId base)
{
  auto& blocks = *world.globals->blockRegistry;
  auto& bReg   = blocks.GetRegistry();

  auto rotatedVariants = Component::StandardRotatedVariants{};

  ASSERT(!bReg.any_of<Component::RenderAsTexturedCube>(entt::entity(base)));

  constexpr Direction directions[] = {Direction::East, Direction::South, Direction::West};
  const glm::mat3 rotateFromNorth[] = {
    glm::mat3_cast(glm::angleAxis(glm::half_pi<float>(), glm::vec3{0, -1, 0})),
    glm::mat3_cast(glm::angleAxis(glm::pi<float>(), glm::vec3{0, -1, 0})),
    glm::mat3_cast(glm::angleAxis(glm::three_over_two_pi<float>(), glm::vec3{0, -1, 0})),
  };
  constexpr const char* suffixes[] = {"_east", "_south", "_west"};

  for (int i = 0; i < 3; i++)
  {
    const auto dir    = directions[i];
    const auto tag    = blocks.GetIdToTagMap().at(base);
    const auto newTag = tag + suffixes[i];
    const auto block  = blocks.Create(newTag);

    CopyEntity(bReg, entt::entity(base), entt::entity(block));

    bReg.emplace_or_replace<Name>(entt::entity(block), newTag);

    if (const auto* p = bReg.try_get<const Component::RenderAsSubGrid>(entt::entity(base)))
    {
      const auto rot = rotateFromNorth[i];
      bReg.emplace_or_replace<Component::RenderAsSubGrid>(entt::entity(block), std::make_shared<Voxel::SubGrid>(MakeRotatedSubGrid(*p->subGrid, rot)));
    }

    if (const auto* p = bReg.try_get<const Component::RenderAsTexturedCube2>(entt::entity(base)))
    {
      bReg.emplace_or_replace<Component::RenderAsTexturedCube2>(entt::entity(block), MakeRotatedCubeFaceMaterials(*p, dir));
    }

    bReg.emplace<Component::BaseVariant>(entt::entity(block), base);

    Item::RegisterItemForBlock(world, block);

    if (dir == Direction::East)
    {
      rotatedVariants.east = block;
    }
    if (dir == Direction::South)
    {
      rotatedVariants.south = block;
    }
    if (dir == Direction::West)
    {
      rotatedVariants.west = block;
    }
  }

  return bReg.emplace<Component::StandardRotatedVariants>(entt::entity(base), rotatedVariants);
}

void Block::UpdateTransformedForRotatedVariants(World& world, BlockId base)
{
  auto& blocks = *world.globals->blockRegistry;
  auto& bReg   = blocks.GetRegistry();

  const auto* rotated = bReg.try_get<const Component::StandardRotatedVariants>(entt::entity(base));
  ASSERT(rotated);

  // Update transformed variants of this block.
  {
    auto& eTransformed = bReg.get<Component::TransformWhenUsed>(entt::entity(rotated->east));
    eTransformed.block = GetRotatedBlockVariant(world, eTransformed.block, Direction::East);

    auto& sTransformed = bReg.get<Component::TransformWhenUsed>(entt::entity(rotated->south));
    sTransformed.block = GetRotatedBlockVariant(world, sTransformed.block, Direction::South);

    auto& wTransformed = bReg.get<Component::TransformWhenUsed>(entt::entity(rotated->west));
    wTransformed.block = GetRotatedBlockVariant(world, wTransformed.block, Direction::West);
  }

  if (bReg.all_of<Component::SpawnExtraBlockOnPlace>(entt::entity(base)))
  {
    auto& eSpawn = bReg.get<Component::SpawnExtraBlockOnPlace>(entt::entity(rotated->east));
    eSpawn.block = GetRotatedBlockVariant(world, eSpawn.block, Direction::East);

    auto& sSpawn = bReg.get<Component::SpawnExtraBlockOnPlace>(entt::entity(rotated->south));
    sSpawn.block = GetRotatedBlockVariant(world, sSpawn.block, Direction::South);

    auto& wSpawn = bReg.get<Component::SpawnExtraBlockOnPlace>(entt::entity(rotated->west));
    wSpawn.block = GetRotatedBlockVariant(world, wSpawn.block, Direction::West);
  }
}

glm::ivec3 Block::DirectionToNeighbor(Direction direction)
{
  switch (direction)
  {
  case Direction::North: return {0, 0, -1};
  case Direction::South: return {0, 0, 1};
  case Direction::East: return {1, 0, 0};
  case Direction::West: return {-1, 0, 0};
  case Direction::Up: return {0, 1, 0};
  case Direction::Down: return {0, -1, 0};
  default: UNREACHABLE;
  }
}

Block::Direction Block::NormalToDirection(glm::vec3 normal)
{
  if (normal.z < 0)
    return Direction::North;
  if (normal.z > 0)
    return Direction::South;
  if (normal.x > 0)
    return Direction::East;
  if (normal.x < 0)
    return Direction::West;
  if (normal.y > 0)
    return Direction::Up;
  return Direction::Down;
}

Block::Direction Block::WhichRotatedVariantAmI(const World& world, BlockId block)
{
  auto& blocks = *world.globals->blockRegistry;
  auto& bReg   = blocks.GetRegistry();

  if (const auto* base = bReg.try_get<const Component::BaseVariant>(entt::entity(block)))
  {
    if (const auto* baseRotated = bReg.try_get<const Component::StandardRotatedVariants>(entt::entity(base->block)))
    {
      if (baseRotated->east == block)
      {
        return Direction::East;
      }
      if (baseRotated->south == block)
      {
        return Direction::South;
      }
      if (baseRotated->west == block)
      {
        return Direction::West;
      }
      PANIC;
    }
  }

  return Direction::North;
}

