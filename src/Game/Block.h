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
    bool operator==(const DropSelf&) const = default;
  };

  enum class Direction : uint32_t
  {
    North,
    South,
    East,
    West,
    Up,
    Down,
  };

  struct CubeFaceMaterial
  {
    // TODO: ability to specify an exact rotation.
    bool randomizeTexcoordRotation = false;
    uint32_t texcoordsQuarterTurns = 0; // Number of quarter turn rotations to apply to texture coordinates.
    std::optional<std::string> baseColorTexture;
    glm::vec3 baseColorFactor = {1, 1, 1};
    std::optional<std::string> emissionTexture;
    glm::vec3 emissionFactor = {0, 0, 0};
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

    // One material for every face.
    struct RenderAsTexturedCube
    {
      CubeFaceMaterial material;
    };

    // One material per face, in the order specified by Direction enum above.
    struct RenderAsTexturedCube2
    {
      std::array<CubeFaceMaterial, 6> faces;
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

    // Automatically added when Item::RegisterItemForBlock is called.
    struct CorrespondingItem
    {
      ItemId item;
    };

    struct Script
    {
      std::filesystem::path path;
    };

    struct RequiresSupport
    {
      Direction supportingSide = Direction::Down;
    };

    struct RequiresSupportByBlock
    {
      BlockId block;
    };

    // Automatically added when a block variant is made.
    // The base variant is the variant that will be dropped when the block is destroyed.
    struct BaseVariant
    {
      BlockId block;
    };

    // The base variant points north.
    struct StandardRotatedVariants
    {
      BlockId east;
      BlockId south;
      BlockId west;
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
  void OnUpdateBlock(World& world, glm::ivec3 voxelPosition);

  void SpawnLootDropFromBlock(World& world, glm::ivec3 voxelPos, BlockId block);

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
    std::optional<std::variant<Component::RenderAsTexturedCube, Component::RenderAsTexturedCube2, Component::RenderAsSubGrid>> render;
    Component::PhysicalProperties physicalProperties = {};
    std::optional<Component::Valuable> valuable;
    std::optional<Component::ExplodeWhenBroken> explode;
    std::optional<Component::SpawnDependentEntityPrefabWhenPlaced> entityPrefab;
    std::optional<Component::RequiresSupport> support;
    std::optional<Component::RequiresSupportByBlock> supportByBlock;
  };

  BlockId CreateStandardBlock(World& world, const CreateBlockParams& params);

  void CreateStandardRotatedVariants(World& world, BlockId base);

  [[nodiscard]] glm::ivec3 DirectionToNeighbor(Direction direction);
}
