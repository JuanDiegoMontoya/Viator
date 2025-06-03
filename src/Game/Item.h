#pragma once
#include "BlockFwd.h"
#include "ItemFwd.h"
#include "ClassImplMacros.h"
#include "VoxelType.h"
#include "entt/entity/fwd.hpp"
#include "shaders/Light.h.glsl" // "TEMP"

#include <span>

namespace Physics
{
  struct RigidBody;
}

class World;

class ItemDefinition
{
public:
  ItemDefinition(std::string_view name) : name_(name) {}
  virtual ~ItemDefinition() = default;

  virtual std::string GetName() const
  {
    return name_;
  }

  // Create an entity
  [[nodiscard]] virtual entt::entity Materialize(World&) const = 0;

  virtual void Dematerialize(World& world, entt::entity self) const;

  // Spawn the entity if necessary, give it physics, and unparent it from the player
  virtual Physics::RigidBody& GiveCollider(World& world, entt::entity self) const;

  // Perform an action with the entity
  virtual void UsePrimary([[maybe_unused]] float dt, [[maybe_unused]] World& world, [[maybe_unused]] entt::entity self, [[maybe_unused]] ItemState& state) const
  {
  }

  [[nodiscard]] virtual float GetUseDt() const
  {
    return 0.25f;
  }

  virtual void Update(float dt, World&, [[maybe_unused]] entt::entity self, ItemState& state) const
  {
    state.useAccum += dt;
  }

  [[nodiscard]] virtual int GetMaxStackSize() const
  {
    return 1;
  }

  [[nodiscard]] virtual glm::vec3 GetDroppedColliderSize() const
  {
    return glm::vec3{0.25f};
  }

protected:
  std::string name_;
};

class ItemRegistry
{
public:
  ItemRegistry() = default;

  // Even though this type is already noncopyable, this is required because C++ is goofy.
  // https://github.com/skypjack/entt/issues/1067
  NO_COPY(ItemRegistry);

  ItemRegistry(ItemRegistry&&) noexcept            = default;
  ItemRegistry& operator=(ItemRegistry&&) noexcept = default;

  const ItemDefinition& Get(const std::string& name) const;
  const ItemDefinition& Get(ItemId id) const;
  ItemId GetId(const std::string& name) const;

  ItemId Add(ItemDefinition* itemDefinition);

  std::span<const std::unique_ptr<ItemDefinition>> GetAllItemDefinitions() const
  {
    return idToDefinition_;
  }

private:
  std::unordered_map<std::string, ItemId> nameToId_;
  std::vector<std::unique_ptr<ItemDefinition>> idToDefinition_;
};

class SpriteItem : public ItemDefinition
{
public:
  SpriteItem(std::string_view name, std::string_view sprite, glm::vec3 tint = glm::vec3(1)) : ItemDefinition(name), sprite_(sprite), tint_(tint) {}

  entt::entity Materialize(World&) const override;

  glm::vec3 GetDroppedColliderSize() const override
  {
    return glm::vec3(0.125f);
  }

  int GetMaxStackSize() const override
  {
    return 100;
  }

protected:
  std::string sprite_;
  glm::vec3 tint_;
};

class Gun : public ItemDefinition
{
public:
  struct CreateInfo
  {
    std::string model = "ar15";
    glm::vec3 tint    = {1, 1, 1};
    float scale       = 1;
    float damage      = 20;
    float knockback   = 3;
    float fireRateRpm = 800;
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
  };

  explicit Gun(std::string_view name, const CreateInfo& createInfo) : ItemDefinition(name), createInfo_(createInfo) {}

  [[nodiscard]] entt::entity Materialize(World& world) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  [[nodiscard]] float GetUseDt() const override
  {
    return 1.0f / (createInfo_.fireRateRpm / 60.0f);
  }

private:
  CreateInfo createInfo_;
};

class ToolDefinition : public ItemDefinition
{
public:
  struct CreateInfo
  {
    std::optional<std::string> meshName;
    glm::vec3 meshTint;
    float blockDamage;
    int blockDamageTier;
    BlockDamageFlags blockDamageFlags;
    float useDt = 0.25f;
  };

  ToolDefinition(std::string_view name, const CreateInfo& createInfo) : ItemDefinition(name), createInfo_(createInfo) {}

  [[nodiscard]] entt::entity Materialize(World& world) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  float GetUseDt() const override
  {
    return createInfo_.useDt;
  }

protected:
  CreateInfo createInfo_;
};

class RainbowTool : public ToolDefinition
{
public:
  using ToolDefinition::ToolDefinition;

  void Update(float dt, World& world, entt::entity self, ItemState& state) const override;
};

class SpawnBossItemDefinition : public SpriteItem
{
public:
  using SpriteItem::SpriteItem;

  void UsePrimary(float, World& world, entt::entity self, ItemState& state) const override;
};

class HealingPotionDefinition : public SpriteItem
{
public:
  using SpriteItem::SpriteItem;

  void UsePrimary(float, World& world, entt::entity self, ItemState& state) const override;

private:
};

class Block : public ItemDefinition
{
public:
  Block(BlockId voxel, std::string_view name) : ItemDefinition(name), voxel(voxel) {}

  [[nodiscard]] int GetMaxStackSize() const override
  {
    return 100;
  }

  [[nodiscard]] glm::vec3 GetDroppedColliderSize() const override
  {
    return glm::vec3(0.125);
  }

  std::string GetName() const override
  {
    return name_;
  }

  [[nodiscard]] entt::entity Materialize(World& world) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  [[nodiscard]] float GetUseDt() const override
  {
    return 0.125f;
  }

  voxel_t voxel;
};

class Spear : public ItemDefinition
{
public:
  struct SpearCreateInfo
  {
    float useDt     = 0.55f;
    float damage    = 15;
    float knockback = 5;
    glm::vec3 tint  = {1, 1, 1};
  };
  Spear(std::string_view name, const SpearCreateInfo& info = {}) : ItemDefinition(name), createInfo_(info) {}

  void UsePrimary(float dt, World&, entt::entity, ItemState&) const override;

  [[nodiscard]] entt::entity Materialize(World& world) const override;

  float GetUseDt() const override
  {
    return createInfo_.useDt;
  }

private:
  SpearCreateInfo createInfo_;
};
