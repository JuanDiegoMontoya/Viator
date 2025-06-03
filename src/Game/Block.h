#pragma once
#include "BlockFwd.h"
#include "EntityPrefabFwd.h"
#include "ItemFwd.h"
#include "TwoLevelGrid.h"

#include <variant>

class World;

struct VoxelMaterialDesc
{
  bool isInvisible               = false;
  bool randomizeTexcoordRotation = false;
  std::optional<std::string> baseColorTexture;
  glm::vec3 baseColorFactor = {1, 1, 1};
  std::optional<std::string> emissionTexture;
  glm::vec3 emissionFactor = {0, 0, 0};
  // This is only a shared ptr because I'm lazy and want this struct to remain copyable.
  std::shared_ptr<TwoLevelGrid::SubGrid> subGrid;
};

struct DropSelf
{
};

class BlockDefinition
{
public:
  struct CreateInfo
  {
    std::string name;
    float initialHealth = 100;
    int damageTier{};
    BlockDamageFlags damageFlags                                            = BlockDamageFlagBit::ALL_TOOLS;
    std::variant<std::monostate, DropSelf, ItemState, std::string> lootDrop = DropSelf{};
    // Giving every block a unique set of materials isn't ideal, but it will suffice in the short run.
    VoxelMaterialDesc voxelMaterialDesc;
    bool isSolid = true;
  };

  explicit BlockDefinition(const CreateInfo& info);
  virtual ~BlockDefinition() = default;

  NO_COPY_NO_MOVE(BlockDefinition);

  // Weakly attempt to place the block at the given position.
  // Returns whether the attempt succeeded (could fail due to
  // insufficient space, e.g. for multiblock structures or if
  // there's an entity in the way.
  virtual bool OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition) const;

  virtual void OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const;

  // What the block drops when it is destroyed. Options: nothing, an item,
  // or pick a loot drop from a string. By default, a block will drop the item associated
  // with itself.
  virtual std::variant<std::monostate, ItemState, std::string> GetLootDropType() const
  {
    if (std::get_if<DropSelf>(&createInfo_.lootDrop))
    {
      return ItemState{.id = itemId_};
    }
    if (auto* is = std::get_if<ItemState>(&createInfo_.lootDrop))
    {
      return *is;
    }
    if (auto* s = std::get_if<std::string>(&createInfo_.lootDrop))
    {
      return *s;
    }
    return std::monostate{};
  }

  [[nodiscard]] std::string GetName() const
  {
    return createInfo_.name;
  }

  [[nodiscard]] VoxelMaterialDesc GetMaterialDesc() const
  {
    return createInfo_.voxelMaterialDesc;
  }

  [[nodiscard]] float GetInitialHealth() const
  {
    return createInfo_.initialHealth;
  }

  [[nodiscard]] int GetDamageTier() const
  {
    return createInfo_.damageTier;
  }

  [[nodiscard]] BlockDamageFlags GetDamageFlags() const
  {
    return createInfo_.damageFlags;
  }

  [[nodiscard]] bool GetIsSolid() const
  {
    return createInfo_.isSolid;
  }

  [[nodiscard]] ItemId GetItemId() const
  {
    return itemId_;
  }

  [[nodiscard]] BlockId GetBlockId() const
  {
    return blockId_;
  }

  [[nodiscard]] const TwoLevelGrid::SubGrid* GetSubGrid() const
  {
    return createInfo_.voxelMaterialDesc.subGrid.get();
  }

protected:
  CreateInfo createInfo_;

  // Item and block IDs are set when added to registry
  friend class BlockRegistry;
  ItemId itemId_{};
  BlockId blockId_{};
};

class BlockRegistry
{
public:
  BlockRegistry(World& world) : world_(&world)
  {
    // Hardcode air as the first block.
    Add(new BlockDefinition({.name = "Air", .voxelMaterialDesc = {.isInvisible = true}, .isSolid = false}));
  }

  ~BlockRegistry() = default;

  NO_COPY(BlockRegistry);
  DEFAULT_MOVE(BlockRegistry);

  [[nodiscard]] const BlockDefinition& Get(const std::string& name) const;
  [[nodiscard]] const BlockDefinition& Get(BlockId id) const;
  [[nodiscard]] BlockId GetId(const std::string& name) const;

  BlockId Add(BlockDefinition* blockDefinition);

  std::span<const std::unique_ptr<BlockDefinition>> GetAllDefinitions() const
  {
    return std::span(idToDefinition_);
  }

private:
  World* world_;
  std::unordered_map<std::string, BlockId> nameToId_;
  std::vector<std::unique_ptr<BlockDefinition>> idToDefinition_;
};

class ExplodeyBlockDefinition : public BlockDefinition
{
public:
  struct ExplodeyCreateInfo
  {
    float radius{};
    float damage{};
    int damageTier{};
    float pushForce{};
    BlockDamageFlags damageFlags{};
  };

  explicit ExplodeyBlockDefinition(const CreateInfo& info, const ExplodeyCreateInfo& explodey) : BlockDefinition(info), explodeyInfo_(explodey) {}
  void OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const override;
  std::variant<std::monostate, ItemState, std::string> GetLootDropType() const override
  {
    return std::monostate{};
  }

private:
  ExplodeyCreateInfo explodeyInfo_;
};

class BlockEntityDefinition : public BlockDefinition
{
public:
  struct BlockEntityCreateInfo
  {
    EntityPrefabId id;
  };

  explicit BlockEntityDefinition(const CreateInfo& info, const BlockEntityCreateInfo& blockEntityInfo)
    : BlockDefinition(info), blockEntityInfo_(blockEntityInfo)
  {
  }

  bool OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition) const override;
  void OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const override;

  EntityPrefabId GetEntityPrefab() const
  {
    return blockEntityInfo_.id;
  }

private:
  BlockEntityCreateInfo blockEntityInfo_;
};
