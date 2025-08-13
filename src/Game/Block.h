#pragma once
#include "BlockFwd.h"
#include "EntityPrefabFwd.h"
#include "ItemFwd.h"
#include "TwoLevelGrid.h"

#include "glm/vec3.hpp"

#include <variant>
#include <string>
#include <map>
#include <unordered_map>

class World;

namespace Block
{
  struct DropSelf
  {
  };

  namespace Component
  {
    struct Breakable
    {
      float initialHealth = 100;
      int damageTier{};
      BlockDamageFlags damageFlags = BlockDamageFlagBit::ALL_TOOLS;
      std::variant<std::monostate, DropSelf, ItemState, std::string> dropWhenBroken = DropSelf{};
    };

    // Blocks with this component are highlighted when using the spelunker potion.
    struct Valuable{};

    struct RenderAsSubGrid
    {
      std::shared_ptr<TwoLevelGrid::SubGrid> subGrid;
    };

    struct RenderAsTexturedCube
    {
      bool randomizeTexcoordRotation = false;
      std::optional<std::string> baseColorTexture;
      glm::vec3 baseColorFactor = {1, 1, 1};
      std::optional<std::string> emissionTexture;
      glm::vec3 emissionFactor = {0, 0, 0};
    };

    struct PhysicalProperties
    {
      bool isSolid = true;
    };

    struct ExplodeWhenBroken
    {
      float radius{};
      float damage{};
      int damageTier{};
      float pushForce{};
      BlockDamageFlags damageFlags{};
    };

    struct SpawnDependentEntityPrefabWhenPlaced
    {
      EntityPrefabId id;
    };

    // Automatically added when 
    struct CorrespondingItem
    {
      ItemId item;
    };
  }

  class Registry
  {
  public:
    Registry();
    ~Registry();

    // Even though this type is already noncopyable, this is required because C++ is goofy.
    // https://github.com/skypjack/entt/issues/1067
    NO_COPY(Registry);

    Registry(Registry&&) noexcept            = default;
    Registry& operator=(Registry&&) noexcept = default;

    BlockId Get(const std::string& tag) const;

    // Use this instead of GetRegistry()->create(). This one will make a map entry.
    [[nodiscard]] BlockId Create(std::string tag);

    [[nodiscard]] entt::registry& GetRegistry()
    {
      return *registry_;
    }

    [[nodiscard]] const entt::registry& GetRegistry() const
    {
      return *registry_;
    }

    [[nodiscard]] const auto& GetTagToIdMap() const
    {
      return tagToId_;
    }

    [[nodiscard]] const auto& GetIdToTagMap() const
    {
      return idToTag_;
    }

  private:
    BlockId nextBlockId = BlockId(0);
    std::unique_ptr<entt::registry> registry_;
    std::unordered_map<std::string, BlockId> tagToId_;
    std::map<BlockId, std::string> idToTag_;
  };

  bool OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition, BlockId block);
  void OnDestroyBlock(World& world, glm::ivec3 voxelPosition, BlockId block);
  [[nodiscard]] std::variant<std::monostate, ItemState, std::string> GetLootDropType(const World& world, BlockId block);

  [[nodiscard]] bool IsVisible(const World& world, BlockId block);
  [[nodiscard]] bool IsSolid(const World& world, BlockId block);
  [[nodiscard]] const TwoLevelGrid::SubGrid* GetSubGrid(const World& world, BlockId block);
  [[nodiscard]] ItemId GetItemId(const World& world, BlockId block);
  [[nodiscard]] float GetInitialHealth(const World& world, BlockId block);
  [[nodiscard]] BlockDamageFlags GetDamageFlags(const World& world, BlockId block);
  [[nodiscard]] int GetDamageTier(const World& world, BlockId block);
  [[nodiscard]] std::string GetName(const World& world, BlockId block);

  struct CreateBlockParams
  {
    std::string tag;
    std::string name;
    std::optional<Component::Breakable> breakable;
    std::optional<std::variant<Component::RenderAsTexturedCube, Component::RenderAsSubGrid>> render;
    const Component::PhysicalProperties& physicalProperties = {};
    std::optional<Component::Valuable> valuable;
    std::optional<Component::ExplodeWhenBroken> explode;
    std::optional<Component::SpawnDependentEntityPrefabWhenPlaced> entityPrefab;
  };

  BlockId CreateStandardBlock(World& world, const CreateBlockParams& params);
}
