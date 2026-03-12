#pragma once
#include "Block.h"
#include "BlockFwd.h"
#include "ItemFwd.h"
#include "Core/ClassImplMacros.h"
#include "EntityPrefab.h"
#include "PCG.h"
#include "Game/Voxel/VoxelType.h"
#include "Client/Fvog/detail/Flags.h"
#include "MathUtilities.h"
#include "Networking/Interface.h"
#include "Head.h"
#include "CoreComponents.h"
#include "Game/IncompleteTypeDeleter.h"

#include "entt/entity/registry.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/handle.hpp"
#include "glm/vec3.hpp"
#include "glm/vec2.hpp"
#include "glm/gtc/quaternion.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>
#include <span>

namespace Physics
{
  struct CharacterController;
  struct CharacterControllerShrimple;
  struct RigidBody;
}

struct ItemState;
class World;
using namespace entt::literals;

struct Debugging
{
  bool showDebugGui        = false;
  bool forceShowCursor     = false;
  bool drawDebugProbe      = false;
  bool drawDebugNormal     = false;
  bool drawPhysicsShapes   = false;
  bool drawPhysicsVelocity = false;
  bool drawPathLines       = false;
  bool showFps             = true;
  bool disableAllUi        = false;
  bool infiniteItems       = false;
};

struct BlockEntity {};

// Similar to noclip character controller, but has inertia.
struct FlyingCharacterController
{
  float maxSpeed;
  float acceleration;
};

enum class TeamFlagBits
{
  NEUTRAL  = 0,
  FRIENDLY = 1 << 0,
  ENEMY    = 1 << 1,
  // TODO: mask and flags for PvP
};

FVOG_DECLARE_FLAG_TYPE(TeamFlags, TeamFlagBits, uint32_t);

void CreateContextVariablesAndObservers(World& world);

void SetVoxelAtRPC(World& world, glm::ivec3 voxelPosition, voxel_t voxel);

struct ItemIdAndCount
{
  ItemId item = entt::null;
  int count   = 1;
};

// Loot type for simple independent random drops.
struct RandomLootDrop
{
  // Use individual probabilities for spawning each of count items.
  [[nodiscard]] std::vector<ItemIdAndCount> Sample(PCG::Rng& rng) const;

  ItemId item = entt::null;
  int count = 1;
  float chanceForOne = 1;
  // TODO: distribution type (normal, uniform)
};

// Loot type that selects a single element from a pool of potential drops.
// Intended for allowing bosses to drop exactly one item or set of items.
struct PoolLootDrop
{
  [[nodiscard]] std::vector<ItemIdAndCount> Sample(PCG::Rng& rng) const;

  [[nodiscard]] int GetTotalWeight() const;

  struct ItemsAndWeight
  {
    std::vector<ItemIdAndCount> items; // The items given if selected.
    int weight = 1;                    // Chance to select this item from the pool.
  };

  // Probability that the pool will be sampled.
  float chance = 1;
  std::vector<ItemsAndWeight> pool;
};

// What a mob can drop when it dies.
struct LootDrops
{
  [[nodiscard]] std::vector<ItemIdAndCount> Collect(PCG::Rng& rng) const;

  std::vector<std::variant<RandomLootDrop, PoolLootDrop>> drops;
};

class LootRegistry
{
public:
  LootRegistry() = default;
  NO_COPY(LootRegistry);

  LootRegistry(LootRegistry&&) noexcept = default;
  LootRegistry& operator=(LootRegistry&&) noexcept = default;

  void Add(std::string name, std::unique_ptr<LootDrops>&& lootDrops);
  [[nodiscard]] const LootDrops* Get(const std::string& name);

private:
  std::unordered_map<std::string, std::unique_ptr<LootDrops>> nameToLoot_;
};

struct DroppedItem
{
  ItemState item;
};

// Used to look up what a mob drops when it dies.
struct Loot
{
  std::string name;
};

struct Crafting
{
  struct Recipe
  {
    std::vector<ItemIdAndCount> ingredients;
    std::vector<ItemIdAndCount> output;
    BlockId craftingStation = voxel_t(0);
    std::string name;
    std::string description;
  };

  std::vector<Recipe> recipes;
};

struct Inventory
{
  static constexpr size_t height = 4;
  static constexpr size_t width  = 8;

  // (row, col) of equipped slot
  glm::ivec2 activeSlotCoord    = {0, 0};

  bool canHaveActiveItem = true;

  // The held item
  entt::entity activeSlotEntity = entt::null;

  std::array<std::array<ItemState, width>, height> slots{};

  ItemState& ActiveSlot()
  {
    return slots[activeSlotCoord.x][activeSlotCoord.y];
  }

  const ItemState& ActiveSlot() const
  {
    return slots[activeSlotCoord.x][activeSlotCoord.y];
  }


  // Completely deletes the old item, replacing it with the new. New item can be null.
  void OverwriteSlot(World& world, glm::ivec2 rowCol, ItemState itemState, entt::entity parent = entt::null);

  void TryStackItem(World& world, ItemState& item);
  std::optional<glm::ivec2> GetFirstEmptySlot() const;

  int CountItem(ItemId item) const;
  bool CanCraftRecipe(const Crafting::Recipe& recipe) const;
};

void SetActiveSlotRPC(World& world, entt::entity parent, glm::ivec2 rowCol);

void ScrollHotbarRPC(World& world, entt::entity parent, int32_t offset);

struct ArmorAndAccessories
{
  enum Slot : int32_t
  {
    SLOT_HEAD,
    SLOT_BODY,
    SLOT_LEGS,

    SLOT_ACCESSORY0,
    SLOT_ACCESSORY1,
    SLOT_ACCESSORY2,
    SLOT_ACCESSORY3,
    SLOT_ACCESSORY4,

    SLOT_COUNT,
  };

  std::array<ItemState, SLOT_COUNT> slots{};

  void OverwriteSlot(World& world, Slot slot, ItemState itemState);
};

struct TemporaryEffects
{
  std::vector<ItemState> effects;
};

// If necessary, materializes the item. Then, the item is given a RigidBody and is moved into the new entity.
entt::entity DropItemRPC(World& world, entt::entity parent, glm::ivec2 slot);
entt::entity DropItemFromArmorRPC(World& world, entt::entity parent, ArmorAndAccessories::Slot slot);
entt::entity ThrowItemRPC(World& world, entt::entity parent, entt::entity thrower, glm::ivec2 slot);
entt::entity ThrowItemFromArmorRPC(World& world, entt::entity parent, entt::entity thrower, ArmorAndAccessories::Slot slot);

void TryCraftRecipeRPC(World& world, entt::entity parent, Crafting::Recipe recipe);

// If parent1 and parent2 both have an inventory, swaps items between them.
bool SwapInventorySlotsRPC(World& world, entt::entity parent1, glm::ivec2 parent1Slot, entt::entity parent2, glm::ivec2 parent2Slot);

void TeleportPlayerRPC(World& world, entt::entity player, LocalTransform transform);

bool SwapInventorySlotAndArmorSlotRPC(World& world, entt::entity parent1, glm::ivec2 parent1Slot, entt::entity parent2, ArmorAndAccessories::Slot parent2Slot);
bool SwapArmorSlotsRPC(World& world, entt::entity parent1, ArmorAndAccessories::Slot parent1Slot, entt::entity parent2, ArmorAndAccessories::Slot parent2Slot);

void UpdateSimpleScriptableCodeRPC(World& world, entt::entity parent, std::string newCode);

// Return to desktop
struct CloseApplication {};

// Close server (if applicable), then return to main menu if head, or close app if headless
struct ReturnToMenu {};

enum class GameState
{
  MENU,
  GAME,
  PAUSED,
  WORLD_SELECT,
  LOADING_SP,
  LOADING_MP,
  MENU_SETTINGS,
  PAUSED_SETTINGS,
  SERVER_SELECT,
  SERVER_SELECT_ADD_SERVER,
};

enum class InputAxis
{
  STRAFE,
  FORWARD,
  SPRINT,
  WALK,
};

// Networked (client -> server)
// When a server receives these, they attach it to the corresponding player.
struct InputState
{
  float strafe      = 0;
  float forward     = 0;
  float elevate     = 0; // For flying controller
  bool jump         = false;
  bool sprint       = false;
  bool walk         = false;
  bool usePrimary   = false;
  bool useSecondary = false;
  bool interact     = false;
};

// Networked (client -> server)
struct InputLookState
{
  float pitch = 0;
  float yaw   = 0;
};

struct Player
{
  uint32_t id = 0;
  bool inventoryIsOpen = false;
  entt::entity openContainerId = entt::null;
  bool showInteractPrompt = false; // TODO: Move to LocalPlayer since this info is purely visual.
  bool attachedToRope = false;
};

// Tag for systems to exclude.
// Intended for preventing the player from influencing the world while dead.
struct GhostPlayer
{
  float remainingSeconds{};
};

struct Invulnerability
{
  float remainingSeconds{};
};

// Map of entities that cannot be damaged by this one to remaining time.
struct CannotDamageEntities
{
  std::unordered_map<entt::entity, float> entities;
};

struct Lifetime
{
  float remainingSeconds = 0;
};

glm::vec3 GetForward(glm::quat rotation);
glm::vec3 GetUp(glm::quat rotation);
glm::vec3 GetRight(glm::quat rotation);

// Use with GlobalTransform and RenderTransform for smooth object movement.
struct PreviousGlobalTransform
{
  glm::vec3 position{};
  glm::quat rotation{};
  float scale{};
  bool teleported = true;
};

struct Health
{
  float hp = -1;
  float maxHp = -1;
};

struct RenderTransform
{
  GlobalTransform transform;
  GlobalTransform prevTransform;
};

struct NoclipCharacterController {};

struct Projectile
{
  float initialSpeed{}; // Used to calculate damage.
  float drag = 0; // TODO: remove (use Friction)
  float restitution = 0.25f;
  bool sticky       = false;
  float stickyDist  = 1e-3f;
  bool isStuck      = false;
  bool particles    = true;
};

struct LinearVelocity
{
  glm::vec3 v{};

  operator glm::vec3&()
  {
    return v;
  }
  operator const glm::vec3&() const
  {
    return v;
  }
};

// Velocity attenuation.
struct Friction
{
  glm::vec3 axes; // Amount to apply to each axis
};

struct IsInWater
{
};

struct TimeScale
{
  float scale = 1;
};

struct TickRate
{
  uint32_t hz;
};

struct Mesh
{
  std::string name;
};

struct DoNotRenderIfAncestorIsLocalPlayer {};

struct Tint
{
  glm::vec3 color = {1, 1, 1};
};

struct Billboard
{
  std::string name;
};

// Use when you want a child entity's collide events to be counted as the parent's.
struct ForwardCollisionsToParent {};

// For entities that deal damage to other entities they collide with.
struct ContactDamage
{
  float damage    = 0;
  float knockback = 5;
};

// Linearly interpolate between samples.
struct LinearPath
{
  // Defines a transform offset and duration.
  // Postfix sum of "offsetSeconds" defines timestamp on which the sample appears.
  // First keyframe is blended with identity transform if its position is not at 0.
  struct KeyFrame
  {
    glm::vec3 position = {};
    glm::quat rotation = glm::identity<glm::quat>();
    float scale = 1;
    float offsetSeconds;
    Math::Easing easing = Math::Easing::LINEAR;
  };
  std::vector<KeyFrame> frames;

  float secondsElapsed = 0;

  // Preserve local transform before this component was added.
  LocalTransform originalLocalTransform;
};

struct BlockHealth
{
  float health = 100;
};

// Placed on root entity belonging to this client's player.
struct LocalPlayer {};

struct SimpleEnemyBehavior {};
struct SimplePathfindingEnemyBehavior {};

struct AiWanderBehavior
{
  float minWanderDistance  = 3;
  float maxWanderDistance  = 6;
  float timeBetweenMoves   = 4;
  float accumulator        = 0;
  bool targetCanBeFloating = false;
};

struct AiTarget
{
  entt::entity currentTarget = entt::null;
};

struct AiVision
{
  // Spherical cone in which an AI actor can detect another entity.
  float coneAngleRad = glm::half_pi<float>();
  float distance     = 20;
  float invAcuity    = 1; // Time taken, at max distance, for target in cone to be spotted.
  float accumulator  = 0;
};

struct AiHearing
{
  // Absolute distance in which an AI actor can automatically detect another entity.
  float distance = 5;
};

struct PredatoryBirdBehavior
{
  enum class State
  {
    IDLE,
    CIRCLING,
    SWOOPING,
  };
  State state = State::IDLE;
  float accum = 0;
  entt::entity target = entt::null;
  glm::vec3 idlePosition{};
  float lineOfSightDuration = 0;
};

struct WormEnemyBehavior
{
  float maxTurnSpeedDegPerSec = 180;
};

struct WalkingMovementAttributes
{
  float walkModifier       = 0.35f;
  float runMaxSpeed        = 4.6f;
  float terminalVelocity   = -40;
  float gravity            = -20;
  float jumpInitialImpulse = 6;
  float jumpAcceleration   = 25;
  float timeSinceJumped    = 0;
  float jumpControlTime    = 0.15f;

  // Acceleration is applied when holding a movement key.
  // Deceleration is applied opposite of the current velocity and only when NOT pressing a movement key.
  float acceleration = 55;
  float deceleration = 55;
  float airAcceleration = 15;
  float airDeceleration = 3.5f;

  // Water modifiers.
  float waterMaxSpeed         = 3.5f;
  float waterAcceleration     = 15;
  float waterDeceleration     = 10;
  float waterTerminalVelocity = -8;
  float waterGravity          = -10;
  float waterJumpImpulse      = 4;
  float waterJumpControlTime  = 0.4f;
};

struct KnockbackMultiplier
{
  float factor = 1;
};

// So recently-dropped items do not get magnetized to players.
struct CannotBePickedUp
{
  float remainingSeconds = 0.5f;
};

struct NoHashGrid {};

struct DespawnWhenFarFromPlayer
{
  float maxDistance = 60;
  float gracePeriod = 10;
};

struct DespawnOnCollision
{
  // Entity will be deleted after reaching this many collisions.
  int count = 1;
};

struct DespawnWhenFarFromEntity
{
  entt::entity entity = entt::null;
  float maxDistance = 10;
};

struct SpawnBlockOnContact
{
  BlockId block = voxel_t::Null;
};

struct ShortenConstraintsOverTime
{
  float velocity             = 0;
  float maxVelocity          = 10;
  float acceleration         = 0;
  float maxAbsLambdaPosition = 5; // The constraint will not shorten if abs(JPH::DistanceConstraint::GetTotalLambdaPosition()) exceeds this value.

  float springFrequency         = 5.0f;
  float maxSpringFrequency      = 20.0f;
  float springFrequencyVelocity = 8.0f;
  float springDamping           = 0.05f;
};

struct Grapple
{
  entt::entity shooter = entt::null;
  std::optional<ShortenConstraintsOverTime> shortenConstraints; // Apply to spawned entity.
};

struct RopeAttachmentPoint
{
  float distanceFromBase = 0;
};

// Intended for child entities that have physics that you want to keep synchronized with the parent.
// This is used for giving the player pseudo-rigid body physics for grappling hooks to work.
struct SyncWithParentPosition {};

// If this entity has no constraints, it is deleted.
struct DestroyWhenConstraintsBroken {};

class NpcSpawnDirector
{
public:
  void Update(World& world, float dt);

  int maxEnemiesBase                = 20;
  int maxEnemiesPerAdditionalPlayer = 10;
  float timeBetweenSpawns           = 1;
  float accumulator                 = 0;
};

struct Enemy {};

// This component exists solely to check if a physics ray hit the voxel world.
struct VoxelsComponent {};

struct NetworkNeedUpdateLocalTransform {};

struct FogEmitter
{
  float radiusInner = 1;
  float radiusOuter = 1;
  float density     = 10; // Negative values are allowed and will remove fog.
  glm::vec3 color   = {1, 1, 1};
};

struct SimpleScriptable
{
  bool interactable = true;
  bool playersCanWrite = true;
  bool playersCanExecute = true;
  std::string code;
};

struct AlwaysOrientTowardsVelocity {};

struct SunInfo
{
  float timeOfDay = 0.667f; // 0 = midnight, 1 = midday, 2 = midnight
  float azimuth   = 0.3f;
  float dayLength = 1200; // Time, in seconds, it takes for a full day cycle to complete.
  bool pauseDayNightCycle = false;
};

struct GameParams
{
  class Scripting* scripting;
  std::optional<std::filesystem::path> worldToLoad = std::nullopt;
  std::optional<std::uint16_t> port                = std::nullopt;
  Head* head = nullptr;
};

// Game class used for client and server
class Game
{
public:
  NO_COPY_NO_MOVE(Game);
  explicit Game(const GameParams& params);
  ~Game();
  void Run();
  void Tick(float dt);

private:
  friend class TestGameImpl;

  bool isRunning_ = false;
  Head* head_;
  std::unique_ptr<Networking::Interface> networking_;
  std::unique_ptr<World> world_;
};

namespace Voxel
{
  struct SubGrid;
}

namespace Vox
{
  struct Chunk;
}
std::vector<std::shared_ptr<Voxel::SubGrid>> VoxToSubGrids(const Vox::Chunk& root);
std::shared_ptr<Voxel::SubGrid> VoxToSubGrid(const Vox::Chunk& root);

class HashGrid;
namespace Pathfinding
{
  class PathCache;
}
struct GameGlobals
{
private:
  template<typename T>
  using unique_ptr = std::unique_ptr<T, IncompleteTypeDeleter<T>>;

public:
  GameGlobals();
  DEFAULT_MOVE(GameGlobals);
  // Gameplay stuff, probably needs serialization
  GameState gameState = GameState::MENU;
  PCG::Rng rng;
  Debugging debugging;
  TimeScale timeScale;
  TickRate tickRate{.hz = 30};
  float time = 0;
  unique_ptr<HashGrid> hashGrid;
  NpcSpawnDirector npcSpawnDirector;
  bool updateNpcSpawnDirector = true;
  bool npcsIgnorePlayers      = false;
  bool disableNpcPathfinding  = false;
  SunInfo sunInfo;
  unique_ptr<Pathfinding::PathCache> pathCache;
  LootRegistry lootRegistry;
  Crafting crafting;
};