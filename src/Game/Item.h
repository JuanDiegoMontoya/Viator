#pragma once
#include "BlockFwd.h"
#include "Game.h"
#include "ItemFwd.h"
#include "Core/ClassImplMacros.h"
#include "Game/Voxel/VoxelType.h"
#include "entt/entity/fwd.hpp"
#include "shaders/Light.h.glsl" // "TEMP"
#include "Physics/Shape.h"

#include <vector>
#include <string>

namespace Physics
{
  struct RigidBody;
}

class World;
struct LinearPath;

namespace Item
{
  enum class EffectType : int32_t
  {
    ArmorModifier,
    MovementSpeedModifier,
    JumpImpulseModifier,
    WaterJumpControlTimeModifier,
    WaterAccelerationModifier,
    WaterMaxSpeedModifier,
    WaterGravityModifier,
    BaseDamage,
    Knockback,
    HealthRegeneration,
    Shine,     // User becomes a light source
    Spelunker, // User gains X-ray vision that makes ore and other valuables visible through walls
    EFFECT_COUNT,
  };

  enum class EffectCondition : int32_t
  {
    OnUse,
    OnHeld,
    OnWorn,
  };

  enum class EffectQuantityType : int32_t
  {
    Additive,
    Multiplicative,
  };

  namespace Component
  {
    struct MaterializeAsMeshEntity
    {
      std::string mesh;
      glm::vec3 tint = {1, 1, 1};
      glm::vec3 position = {0, 0, 0};
      float scale = 1;
      glm::quat rotation = glm::identity<glm::quat>();
    };

    struct Usable
    {
      float timeBetweenUses = 0.25f;
    };

    struct Stackable
    {
      int maxStackSize = 999;
    };

    struct ColliderWhenDropped
    {
      Physics::PolyShape shape = Physics::Box{glm::vec3{0.125f}};
      glm::vec3 translation{};
      glm::quat rotation = glm::identity<glm::quat>();
      glm::vec3 friction = {.2f, .1f, .2f};
    };

    enum class AllowedSlots
    {
      Normal,    // The item can only be placed in normal inventory slots.
      Head,      // Head slot or normal slots.
      Body,      // Body slot or normal slots.
      Legs,      // Leg slot or normal slots.
      Accessory, // Accessory slots or normal slots.
      Hidden,    // Can ONLY appear in hidden inventory slots (used for temporary effects).
    };

    struct StaticEffect
    {
      EffectCondition condition;
      EffectQuantityType quantityType;
      EffectType type;
      float amount;
    };

    struct StaticEffects
    {
      std::vector<StaticEffect> effects;
    };

    struct Gun
    {
      std::string model = "ar15";
      glm::vec3 tint    = {1, 1, 1};
      float scale       = 1;
      float damage      = 20;
      float knockback   = 3;
      float bullets     = 1;
      float velocity    = 300;
      float accuracyMoa = 4;
      float vrecoil     = 1.0f; // Degrees
      float vrecoilDev  = 0.25f;
      float hrecoil     = 0.0f;
      float hrecoilDev  = 0.25f;
      std::optional<GpuLight> light;
      bool sticky      = false;
      float stickyDist = 1e-3f;
      int maxBounces   = 0;
      std::optional<BlockId> spawnBlockOnHit;
      bool particles = true;
      std::optional<FogEmitter> fogEmitter;
    };

    struct MaterializeAsSprite
    {
      std::string tag;
      glm::vec3 tint;
    };

    struct Rainbow
    {
    };

    struct Tool
    {
      float blockDamage;
      int blockDamageTier;
      BlockDamageFlags blockDamageFlags;
    };

    struct SpawnEntityPrefabOnUse
    {
      std::string tag;
    };

    struct HealUserOnUse
    {
      float amount;
    };

    struct Block
    {
      voxel_t voxel;
    };

    struct SpawnTempHurtboxOnUse
    {
      Physics::PolyShape shape = Physics::Sphere{0.125f};
      glm::vec3 position;
      float damage;
      float knockback;
      std::optional<float> duration; // If not specified, uses Usable::timeBetweenUses
    };

    using AnimatePathOnUse = LinearPath;

    struct GiveEffectOnUse
    {
      ItemId effectId;
      float duration;
    };

    struct Rope
    {
      float length = 5;
    };

    struct GrapplingHookLauncher
    {
      float maxDistance         = 10;
      float launchVelocity      = 30;
      float pullAcceleration    = 50;
      float pullMaxVelocity     = 15;
      float pullInitialVelocity = 5;

      float initialSpringFrequency  = 5.0f;
      float maxSpringFrequency      = 20.0f;
      float springFrequencyVelocity = 8.0f;
      float springDamping           = 0.05f;
    };

    struct AbsorbFogOnUse {};
    struct EmitFogOnUse {};
  } // namespace Component

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

    ItemId Get(const std::string& tag) const;

    // Use this instead of GetRegistry()->create(). This one will make a map entry.
    [[nodiscard]] ItemId Create(std::string tag);

    entt::registry& GetRegistry()
    {
      return *registry_;
    }

    const entt::registry& GetRegistry() const
    {
      return *registry_;
    }

    const auto& GetNameToIdMap() const
    {
      return nameToId_;
    }

  private:
    std::unique_ptr<entt::registry> registry_;
    std::unordered_map<std::string, ItemId> nameToId_;
  };

  // Common functions
  [[nodiscard]] entt::entity Materialize(World& world, ItemId item);
  void Dematerialize(World& world, ItemId item, entt::entity self);
  void GiveCollider(World& world, ItemId item, entt::entity self);
  void Update(World& world, float dt, entt::entity self, ItemState& state);
  void UsePrimary(World& world, float dt, entt::entity self, ItemState& state);
  [[nodiscard]] bool ItemIsCompatibleWithSlot(World& world, ItemId item, Component::AllowedSlots slot);
  [[nodiscard]] int GetMaxStackSize(World& world, ItemId item);
  [[nodiscard]] std::string GetName(World& world, ItemId item);
  [[nodiscard]] float GetEffect(World& world, ItemId item, entt::entity parent, EffectCondition condition, EffectQuantityType quantityType, EffectType type);

  [[nodiscard]] float GetTotalEffectOnEntity(World& world, entt::entity entity, EffectType effect, float base);

  // Helpers for defining items
  ItemId CreateGun(Registry& registry, std::string tag, std::string name, float rateOfFireRPM, const Component::Gun& gun);
  ItemId CreateTool(Registry& registry, std::string tag, std::string name, std::string model, glm::vec3 tint, float timeBetweenUses, const Component::Tool& tool);
  ItemId CreateSpear(Registry& registry, std::string tag, std::string name, std::string model, glm::vec3 tint, float timeBetweenUses, float damage, float knockback);
  ItemId CreateSimpleSpriteItem(Registry& registry, std::string tag, std::string name, std::string sprite, int maxStackSize = 1, glm::vec3 tint = {1, 1, 1});
  ItemId CreateEffector(Registry& registry, std::string tag, std::string name, EffectType type, float additive = 1, float multiplicative = 1);
  ItemId CreateEffectGranter(Registry& registry, std::string tag, std::string name, ItemId effector, float duration, std::string sprite, glm::vec3 tint = {1, 1, 1});
  ItemId CreateArmor(Registry& registry,
    std::string tag,
    std::string name,
    Component::AllowedSlots slot,
    float armorModifier,
    std::string sprite,
    glm::vec3 tint = {1, 1, 1});

  ItemId RegisterItemForBlock(World& world, BlockId block);
} // namespace Item
