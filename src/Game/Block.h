#pragma once
#include "Core/ClassImplMacros.h"
#include "BlockFwd.h"
#include "EntityPrefabFwd.h"
#include "ItemFwd.h"
#include "Core/ReflectionMacros.h"

#include "glm/vec3.hpp"

#include <array>
#include <variant>
#include <string>
#include <optional>
#include <map>
#include <unordered_map>
#include <filesystem>

class World;

namespace Voxel
{
  struct SubGrid;
}

namespace Block
{
  struct DropSelf
  {
    bool operator==(const DropSelf&) const = default;
  };

  enum class Direction : uint32_t
  {
    North, // -Z
    South, // +Z
    East,  // +X
    West,  // -X
    Up,    // +Y
    Down,  // -Y
  };

  R_STRUCT(CubeFaceMaterial)
    R_MEMBER(randomizeTexcoordRotation, bool, false);
    R_MEMBER(texcoordsQuarterTurns, uint32_t, 0);
    R_MEMBER(baseColorTexture, std::optional<std::string>, {});
    R_MEMBER(baseColorFactor, glm::vec3, (glm::vec3{1, 1, 1}));
    R_MEMBER(emissionTexture, std::optional<std::string>, {});
    R_MEMBER(emissionFactor, glm::vec3, (glm::vec3{0, 0, 0}));
  R_END();

  namespace Component
  {
    using LootType = std::variant<std::monostate, DropSelf, ItemState, std::string>;

    R_STRUCT(Breakable)
      R_MEMBER(initialHealth, float, 100, PROP_MIN(0.0f), PROP_MAX(100.0f));
      R_MEMBER(damageTier, int, {});
      R_MEMBER(damageFlags, BlockDamageFlags, BlockDamageFlagBit::ALL_TOOLS);
      R_MEMBER(fireDestroyChance, float, 0);
      R_MEMBER(dropWhenBroken, LootType, DropSelf{});
    R_END();

    R_DECLARE_COMPONENT(Breakable, BLOCK_COMPONENT | REPLICATED);

    // Blocks with this component are highlighted when using the spelunker potion.
    struct Valuable{};

    struct RenderAsSubGrid
    {
      std::shared_ptr<Voxel::SubGrid> subGrid;
    };

    R_STRUCT(RenderAsAnimatedSubGrid)
      R_MEMBER(frameDuration, float, 1.0f / 12);
      R_MEMBER(subGrids, std::vector<std::shared_ptr<Voxel::SubGrid>>, {});
    R_END();

    R_DECLARE_COMPONENT(RenderAsAnimatedSubGrid, BLOCK_COMPONENT);

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

    R_STRUCT(PhysicalProperties)
      R_MEMBER(isSolid, bool, true);
      R_MEMBER(flammability, float, 0);
    R_END();

    R_DECLARE_COMPONENT(PhysicalProperties, BLOCK_COMPONENT | REPLICATED);

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

    // Block must be supported by a solid block from the specified direction.
    // If not supported, the block breaks.
    struct RequiresSupport
    {
      Direction supportingSide = Direction::Down;
    };

    // Block must be supported by a specific block. Used in conjunction with RequiresSupport to specify a direction.
    struct RequiresSupportByBlock
    {
      BlockId block;
    };

    // Block must be supported by a block ID, specified for each side.
    struct RequiresSupportByBlocks
    {
      std::array<std::optional<BlockId>, 6> blocks;
    };

    // Block must be supported by one of the block IDs OR a solid block (if SolidSupport is used), specified for each side.
    struct RequiresSupportAdvanced
    {
      struct SolidSupport
      {
        bool operator==(const SolidSupport&) const noexcept = default;
      };
      using Support = std::vector<std::variant<SolidSupport, BlockId>>;
      std::array<std::optional<Support>, 6> supports;
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

    struct SpawnExtraBlockOnPlace
    {
      BlockId block;
      Direction direction;
    };

    // Whatever happens to this block will also happen to the interlinked block.
    // e.g. being destroyed or transformed.
    struct InterlinkedBlock
    {
      Direction direction;
    };

    struct TransformWhenUsed
    {
      BlockId block;
    };

    // List of blocks participating in flow behavior for a particular kind of liquid.
    struct BaseFlow
    {
      // Blocks containing the liquid from lowest to highest quantity.
      std::vector<BlockId> blocks;
    };

    // Block has water flowing-like behavior.
    struct Flows
    {
      // Refers to the block that contains the BaseFlow component.
      BlockId base;
    };

    R_STRUCT(Fire)
      R_MEMBER(blockToSpawnOnPropagate, BlockId, entt::null);
      R_MEMBER(chanceToPropagateOnRandomUpdate, float, 0);
      R_MEMBER(chanceToDespawnOnRandomUpdate, float, 0);
    R_END();

    R_DECLARE_COMPONENT(Fire, BLOCK_COMPONENT | REPLICATED);
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

    [[nodiscard]] entt::registry& GetRegistry();
    [[nodiscard]] const entt::registry& GetRegistry() const;
    [[nodiscard]] const std::unordered_map<std::string, BlockId>& GetTagToIdMap() const;
    [[nodiscard]] const std::map<BlockId, std::string>& GetIdToTagMap() const;

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
  void OnRandomUpdateBlock(World& world, glm::ivec3 voxelPosition);
  void OnUseBlock(World& world, glm::ivec3 voxelPosition, BlockId block);

  void SpawnLootDropFromBlock(World& world, glm::ivec3 voxelPos, BlockId block);

  [[nodiscard]] bool IsVisible(const World& world, BlockId block);
  [[nodiscard]] bool IsSolid(const World& world, BlockId block);
  [[nodiscard]] std::vector<const Voxel::SubGrid*> GetSubGrids(const World& world, BlockId block);
  [[nodiscard]] ItemId GetItemId(const World& world, BlockId block);
  [[nodiscard]] float GetInitialHealth(const World& world, BlockId block);
  [[nodiscard]] BlockDamageFlags GetDamageFlags(const World& world, BlockId block);
  [[nodiscard]] int GetDamageTier(const World& world, BlockId block);
  [[nodiscard]] std::string GetName(const World& world, BlockId block);
  [[nodiscard]] BlockId GetRotatedBlockVariant(const World& world, BlockId block, glm::vec3 viewDir, glm::vec3 normal);
  [[nodiscard]] BlockId GetRotatedBlockVariant(const World& world, BlockId block, Direction direction);

  struct CreateBlockParams
  {
    std::string tag;
    std::string name;
    std::optional<Component::Breakable> breakable;
    std::optional<std::variant<Component::RenderAsTexturedCube, Component::RenderAsTexturedCube2, Component::RenderAsSubGrid, Component::RenderAsAnimatedSubGrid>> render;
    Component::PhysicalProperties physicalProperties = {};
    std::optional<Component::Valuable> valuable;
    std::optional<Component::ExplodeWhenBroken> explode;
    std::optional<Component::SpawnDependentEntityPrefabWhenPlaced> entityPrefab;
    std::optional<Component::RequiresSupport> support;
    std::optional<Component::RequiresSupportByBlock> supportByBlock;
    std::optional<Component::RequiresSupportByBlocks> supportByBlocks;
  };

  BlockId CreateStandardBlock(World& world, const CreateBlockParams& params);

  Component::StandardRotatedVariants& CreateStandardRotatedVariants(World& world, BlockId base);
  void UpdateTransformedForRotatedVariants(World& world, BlockId base);

  [[nodiscard]] glm::ivec3 DirectionToNeighbor(Direction direction);
  [[nodiscard]] Direction NormalToDirection(glm::vec3 normal);
  [[nodiscard]] Direction WhichRotatedVariantAmI(const World& world, BlockId block);
  void QueueUpdateNeighbors(World& world, glm::ivec3 position);
}
