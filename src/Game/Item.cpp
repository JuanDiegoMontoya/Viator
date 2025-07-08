#include "Item.h"

#include "Block.h"
#include "EntityPrefab.h"
#include "World.h"
#include "Core/Assert2.h"
#include "Physics/Physics.h"
#include "Physics/PhysicsUtils.h"
#include "Audio.h"

#include "Jolt/Physics/Collision/CollisionCollectorImpl.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"

void SpawnBossItemDefinition::UsePrimary(float, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }

  auto& prefabs         = world.GetRegistry().ctx().get<EntityPrefabRegistry>();
  const auto& transform = world.GetRegistry().get<GlobalTransform>(self);
  prefabs.Get("Worm Boss").Spawn(world, transform.position + 20.0f * GetForward(transform.rotation));
  state.count -= 1;
  state.useAccum = 0;
}

void HealingPotionDefinition::UsePrimary(float, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }

  auto& registry = world.GetRegistry();
  auto parent    = registry.get<Hierarchy>(self).parent;
  ASSERT(registry.valid(parent));
  if (auto* health = registry.try_get<Health>(parent))
  {
    if (health->hp < health->maxHp)
    {
      health->hp = std::min(health->hp + 20, health->maxHp);
      state.count -= 1;
      state.useAccum = 0;
    }
  }
}

entt::entity Gun::Materialize(World& world) const
{
  auto self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f});
  world.SetLocalScale(self, createInfo_.scale);
  world.GetRegistry().emplace<Mesh>(self, createInfo_.model);
  world.GetRegistry().emplace<Tint>(self, createInfo_.tint);

  world.GetRegistry().emplace<Name>(self).name = name_;
  return self;
}

void Gun::UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const
{
  auto& registry = world.GetRegistry();
  // Only shoot if materialized
  if (!registry.valid(self))
  {
    return;
  }

  const auto& transform = registry.get<const GlobalTransform>(self);
  const auto shootDt    = GetUseDt();
  if (state.useAccum >= shootDt)
  {
    world.GetAudio()->PlaySound({
      .name  = "shot2",
      .volume = 0.25f,
      .pitch = world.Rng().RandFloat(0.9f, 1.1f),
      .reverb =
        Audio::Sound::ReverbInfo{
          .roomSize  = 0.5,
          .damping   = 0.5,
          .dryWetMix = 0.5,
        },
    });
    state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);

    for (int i = 0; i < createInfo_.bullets; i++)
    {
      const float bulletScale = 0.05f;
      auto bulletShape        = JPH::Ref(new JPH::SphereShape(.04f));
      bulletShape->SetDensity(11000);
      const auto dir =
        Math::RandVecInCone({world.Rng().RandFloat(), world.Rng().RandFloat()}, GetForward(transform.rotation), glm::radians(createInfo_.accuracyMoa / 60.0f));
      auto up = glm::vec3(0, 1, 0);
      if (glm::epsilonEqual(abs(dot(dir, glm::vec3(0, 1, 0))), 1.0f, 0.001f))
      {
        up = {0, 0, 1};
      }
      auto rot = glm::quatLookAtRH(dir, up);
      auto b   = world.CreateRenderableEntity(transform.position + glm::vec3(0, 0.1f, 0) + GetForward(transform.rotation) * 1.0f, rot, bulletScale);

      registry.emplace<Name>(b).name                 = "Bullet";
      registry.emplace<Mesh>(b).name                 = "frog";
      registry.emplace<Lifetime>(b).remainingSeconds = 8;

      if (createInfo_.light)
      {
        registry.emplace<GpuLight>(b, *createInfo_.light);
      }

      const auto inheritedVelocity = world.GetInheritedLinearVelocity(self);
      auto& projectile             = registry.emplace<Projectile>(b);
      projectile.initialSpeed      = createInfo_.velocity + glm::length(inheritedVelocity);
      projectile.drag              = 0.25f;
      projectile.restitution       = 0.25f;
      projectile.sticky            = createInfo_.sticky;
      projectile.stickyDist        = createInfo_.stickyDist;

      registry.emplace<LinearVelocity>(b, dir * createInfo_.velocity + inheritedVelocity);

      auto& contactDamage     = registry.emplace<ContactDamage>(b);
      contactDamage.damage    = createInfo_.damage;
      contactDamage.knockback = createInfo_.knockback;

      if (auto* team = world.GetTeamFlags(self))
      {
        registry.emplace<TeamFlags>(b, *team);
      }
    }

    // If parent is player, apply recoil
    if (auto* h = registry.try_get<const Hierarchy>(self); h && h->parent != entt::null)
    {
      const auto vr = glm::radians(createInfo_.vrecoil + world.Rng().RandFloat(-createInfo_.vrecoilDev, createInfo_.vrecoilDev));
      const auto hr = glm::radians(createInfo_.hrecoil + world.Rng().RandFloat(-createInfo_.hrecoilDev, createInfo_.hrecoilDev));
      if (auto* is = registry.try_get<InputLookState>(h->parent))
      {
        is->pitch += vr;
        is->yaw += hr;
        world.UpdateLocalTransform(h->parent);
      }
    }
  }
}

entt::entity ToolDefinition::Materialize(World& world) const
{
  if (!createInfo_.meshName)
  {
    return entt::null;
  }

  auto self = world.CreateRenderableEntity({0.3f, -0.7f, -0.7f});
  world.GetRegistry().emplace<Mesh>(self, *createInfo_.meshName);
  world.GetRegistry().emplace<Tint>(self, createInfo_.meshTint);
  world.GetRegistry().emplace<Name>(self).name = GetName();
  return self;
}

void ToolDefinition::UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }
  auto& registry = world.GetRegistry();
  if (registry.all_of<LinearPath>(self))
  {
    registry.remove<LinearPath>(self);
  }
  auto& path = world.GetRegistry().emplace_or_replace<LinearPath>(self);
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, -0.25f, -0.25f},
    .rotation                                             = glm::angleAxis(glm::radians(-30.0f), glm::vec3(1, 0, 0)),
    .offsetSeconds                                        = GetUseDt() * 0.3f,
    .easing                                               = Math::Easing::EASE_OUT_CUBIC});
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, 0}, .offsetSeconds = GetUseDt() * 0.5f, .easing = Math::Easing::EASE_IN_CUBIC});

  state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);
  auto& reg      = world.GetRegistry();
  const auto& h  = reg.get<const Hierarchy>(self);
  const auto p   = h.parent;
  const auto& pt = reg.get<const GlobalTransform>(p);
  const auto pos = pt.position;
  const auto dir = GetForward(pt.rotation);

  auto& grid = reg.ctx().get<TwoLevelGrid>();
  auto hit   = TwoLevelGrid::HitSurfaceParameters();
  if (grid.TraceRaySimple(pos, dir, 10, hit))
  {
    const auto damage = world.DamageBlock(glm::ivec3(hit.voxelPosition), createInfo_.blockDamage, createInfo_.blockDamageTier, createInfo_.blockDamageFlags);
    world.GetAudio()->PlaySound({.name = "hurt", .minDistance = 3, .pitch = 0.5f, .position = hit.positionWorld});

    constexpr float debrisSize = 0.0525f;
    auto cube                  = Physics::Box({debrisSize, debrisSize, debrisSize});

    // Make debris "particles"
    const auto numParticles = glm::clamp(glm::ceil(glm::mix(1.0f, 6.0f, damage / 20.0f)), 0.0f, 10.0f);
    for (int i = 0; i < (int)numParticles; i++)
    {
      auto offset = glm::vec3(world.Rng().RandFloat(-0.125f, 0.125f), world.Rng().RandFloat(-0.125f, 0.125f), world.Rng().RandFloat(-0.125f, 0.125f));
      offset *= glm::equal(hit.flatNormalWorld, glm::vec3(0)); // Zero out the component of the normal.
      auto e =
        world.CreateRenderableEntityNoHashGrid(hit.positionWorld + offset + hit.flatNormalWorld * debrisSize / 2.0f, glm::identity<glm::quat>(), debrisSize);
      reg.emplace<Mesh>(e).name                 = "cube";
      reg.emplace<Name>(e).name                 = "Debris";
      reg.emplace<Lifetime>(e).remainingSeconds = 2;
      reg.emplace<Physics::RigidBodySettings>(e, Physics::RigidBodySettings{.shape = cube, .layer = Physics::Layers::DEBRIS});
      const auto velocity = Math::RandVecInCone({world.Rng().RandFloat(), world.Rng().RandFloat()}, hit.flatNormalWorld, glm::quarter_pi<float>()) * 3.0f;
      reg.emplace_or_replace<LinearVelocity>(e).v = velocity;
    }
  }
}

void RainbowTool::Update(float dt, World& world, entt::entity self, ItemState& state) const
{
  ToolDefinition::Update(dt, world, self, state);
  if (self == entt::null)
  {
    return;
  }

  using namespace glm;
  auto hsv_to_rgb = [](vec3 hsv)
  {
    vec3 rgb = clamp(abs(mod(hsv.x * 6.0f + vec3(0.0, 4.0, 2.0), 6.0f) - 3.0f) - 1.0f, 0.0f, 1.0f);
    return hsv.z * mix(vec3(1.0), rgb, hsv.y);
  };

  auto& tint = world.GetRegistry().get<Tint>(self);
  tint.color = hsv_to_rgb({0.33f * world.GetRegistry().ctx().get<float>("time"_hs), 0.875f, 0.85f});
}

void Spear::UsePrimary([[maybe_unused]] float dt, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }

  state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);
  auto& path     = world.GetRegistry().emplace<LinearPath>(self);
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, -1}, .offsetSeconds = GetUseDt() * 0.45f, .easing = Math::Easing::EASE_IN_OUT_BACK});
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, 0}, .offsetSeconds = GetUseDt() * 0.45f, .easing = Math::Easing::EASE_IN_SINE});

  auto& reg                                     = world.GetRegistry();
  auto child                                    = reg.create();
  reg.emplace<Name>(child).name                 = "Hurtbox";
  reg.emplace<LocalTransform>(child)            = {{0, 0, -1}, glm::identity<glm::quat>(), 1};
  reg.emplace<GlobalTransform>(child)           = {{0, 0, -1}, glm::identity<glm::quat>(), 1};
  reg.emplace<Lifetime>(child).remainingSeconds = GetUseDt();
  reg.emplace<Hierarchy>(child);
  reg.emplace<ContactDamage>(child) = {createInfo_.damage, createInfo_.knockback};
  world.SetParent(child, self);

  reg.emplace<Physics::RigidBodySettings>(child,
    Physics::RigidBodySettings{
      .shape      = Physics::Sphere{0.125f},
      .isSensor   = true,
      .motionType = JPH::EMotionType::Kinematic,
      .layer      = Physics::Layers::PROJECTILE,
    });
}

entt::entity Spear::Materialize(World& world) const
{
  auto self                                    = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f});
  world.GetRegistry().emplace<Mesh>(self).name = "spear";
  world.GetRegistry().emplace<Name>(self).name = "Spear";
  world.GetRegistry().emplace<Tint>(self, createInfo_.tint);
  return self;
}

entt::entity Block::Materialize(World& world) const
{
  auto& blockDef = world.GetRegistry().ctx().get<BlockRegistry>().Get(voxel);
  if (auto* blockEntityDef = dynamic_cast<const BlockEntityDefinition*>(&blockDef))
  {
    auto& entityPrefabs      = world.GetRegistry().ctx().get<EntityPrefabRegistry>();
    const auto& entityPrefab = entityPrefabs.Get(blockEntityDef->GetEntityPrefab());
    if (entityPrefab.GetCreateInfo().isVisible)
    {
      return entityPrefab.Spawn(world, {0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>());
    }
  }
  auto self            = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>(), 0.25f);
  auto& mesh           = world.GetRegistry().emplace<Mesh>(self);
  mesh.name            = "cube";
  const auto& material = blockDef.GetMaterialDesc();
  world.GetRegistry().emplace<Tint>(self, material.baseColorFactor);
  if (!material.emissionTexture && glm::length(material.emissionFactor) > 0.01f)
  {
    // TODO: Convert from luminance (cd/m^2) to luminous intensity (cd)
    auto light      = GpuLight();
    light.color     = material.emissionFactor;
    light.intensity = 1;
    light.type      = LIGHT_TYPE_POINT;
    light.range     = 100;
    world.GetRegistry().emplace<GpuLight>(self, light);
  }

  world.GetRegistry().emplace<Name>(self).name = GetName();
  return self;
}

void Block::UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }

  state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);
  auto& reg      = world.GetRegistry();
  const auto& h  = reg.get<const Hierarchy>(self);
  const auto p   = h.parent;
  const auto& pt = reg.get<const GlobalTransform>(p);
  const auto pos = pt.position;
  const auto dir = GetForward(pt.rotation);

  auto& grid = reg.ctx().get<TwoLevelGrid>();
  auto hit   = TwoLevelGrid::HitSurfaceParameters();
  if (GetMaxStackSize() > 0)
  {
    if (grid.TraceRaySimple(pos, dir, 10, hit))
    {
      const auto newPos = glm::ivec3(hit.voxelPosition + hit.flatNormalWorld);
      if (grid.GetVoxelAt(newPos) == voxel_t::Air)
      {
        // Ensure area is clear of entities before placing
        const auto box = JPH::BoxShape(JPH::Vec3::sReplicate(0.45f));
        auto collector = JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector>();
        Physics::GetNarrowPhaseQuery().CollideShape(&box,
          JPH::Vec3::sReplicate(1),
          JPH::RMat44::sTranslation(Physics::ToJolt(glm::vec3(newPos) + glm::vec3(0.5f))),
          JPH::CollideShapeSettings(),
          JPH::RVec3::sZero(),
          collector,
          Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_PROJECTILE),
          Physics::GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_PROJECTILE));

        if (!collector.HadHit() && world.GetRegistry().ctx().get<BlockRegistry>().Get(voxel).OnTryPlaceBlock(world, newPos))
        {
          state.count--;
        }
      }
    }
  }
}

void ItemDefinition::Dematerialize(World& world, entt::entity self) const
{
  if (self != entt::null)
  {
    world.GetRegistry().emplace_or_replace<DeferredDelete>(self);
  }
}

Physics::RigidBody& ItemDefinition::GiveCollider(World& world, entt::entity self) const
{
  ASSERT(self != entt::null);
  world.GetRegistry().emplace<Friction>(self).axes = {.2, .1, .2};
  world.GetRegistry().emplace<Physics::RigidBodySettings>(self,
    Physics::RigidBodySettings{
      .shape = {Physics::Box{GetDroppedColliderSize()}},
      .layer = Physics::Layers::DROPPED_ITEM,
    });
  return world.GetRegistry().get<Physics::RigidBody>(self);
}

const ItemDefinition& ItemRegistry::Get(const std::string& name) const
{
  const auto id = GetId(name);
  return Get(id);
}

const ItemDefinition& ItemRegistry::Get(ItemId id) const
{
  return *idToDefinition_.at(id);
}

ItemId ItemRegistry::GetId(const std::string& name) const
{
  return nameToId_.at(name);
}

ItemId ItemRegistry::Add(ItemDefinition* itemDefinition)
{
  const auto id = (ItemId)idToDefinition_.size();
  nameToId_.try_emplace(itemDefinition->GetName(), id);
  idToDefinition_.emplace_back(itemDefinition);
  return id;
}

entt::entity SpriteItem::Materialize(World& world) const
{
  auto self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>(), 0.25f);
  world.GetRegistry().emplace<Billboard>(self, sprite_);
  world.GetRegistry().emplace<Tint>(self, tint_);
  world.GetRegistry().emplace<Name>(self, GetName());
  return self;
}

entt::entity Effector::Materialize(World&) const
{
  return entt::null;
}

void EffectGrantingPotion::UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }

  state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);
  state.count--;

  const auto& h = world.GetRegistry().get<const Hierarchy>(self);
  auto& effects = world.GetRegistry().get<TemporaryEffects>(h.parent).effects;
  // Update duration of the effect if it's already in the list of active effects.
  if (auto it = std::find_if(effects.begin(), effects.end(), [&](const ItemState& is) { return is.id == effectorId;}); it != effects.end())
  {
    it->useAccum = glm::max(it->useAccum, duration);
  }
  else
  {
    effects.emplace_back(effectorId, 1, duration);
  }
}

float GetTotalEffectOnEntity(World& world, entt::entity entity, ItemDefinition::EffectType effect, float base)
{
  float sum     = 0;
  float product = 1;

  if (auto* inv = world.GetRegistry().try_get<const Inventory>(entity))
  {
    for (size_t rowIdx = 0; rowIdx < inv->slots.size(); rowIdx++)
    {
      for (size_t colIdx = 0; colIdx < inv->slots[rowIdx].size(); colIdx++)
      {
        const auto& slot = inv->slots[rowIdx][colIdx];
        if (slot.id != nullItem && inv->activeSlotCoord == glm::ivec2(rowIdx, colIdx))
        {
          sum += world.GetRegistry().ctx().get<ItemRegistry>().Get(slot.id).GetHeldEffectAdditive(world, entity, effect);
          product *= world.GetRegistry().ctx().get<ItemRegistry>().Get(slot.id).GetHeldEffectMultiplicative(world, entity, effect);
        }
      }
    }
  }

  if (auto* armor = world.GetRegistry().try_get<const ArmorAndAccessories>(entity))
  {
    for (int i = 0; i < ArmorAndAccessories::SLOT_COUNT; i++)
    {
      const auto& slot = armor->slots[i];
      if (slot.id != nullItem)
      {
        sum += world.GetRegistry().ctx().get<ItemRegistry>().Get(slot.id).GetWornEffectAdditive(world, entity, effect);
        product *= world.GetRegistry().ctx().get<ItemRegistry>().Get(slot.id).GetWornEffectMultiplicative(world, entity, effect);
      }
    }
  }

  if (auto* effects = world.GetRegistry().try_get<const TemporaryEffects>(entity))
  {
    for (const auto& effect2 : effects->effects)
    {
      DEBUG_ASSERT(effect2.id != nullItem);
      sum += world.GetRegistry().ctx().get<ItemRegistry>().Get(effect2.id).GetWornEffectAdditive(world, entity, effect);
      product *= world.GetRegistry().ctx().get<ItemRegistry>().Get(effect2.id).GetWornEffectMultiplicative(world, entity, effect);
    }
  }

  return (base + sum) * product;
}
