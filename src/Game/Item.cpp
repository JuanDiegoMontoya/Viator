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
#include "spdlog/spdlog.h"

Item::Registry::Registry()
{
  registry_ = std::make_unique<entt::registry>();
}

Item::Registry::~Registry() = default;

ItemId Item::Registry::Get(const std::string& tag) const
{
  return nameToId_.at(tag);
}

ItemId Item::Registry::Create(std::string tag)
{
  ASSERT(!nameToId_.contains(tag));
  const auto id = registry_->create();
  nameToId_.emplace(tag, id);
  return id;
}

entt::entity Item::Materialize(World& world, ItemId item)
{
  const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();

  entt::entity self = entt::null;
  if (const auto* p = iReg.try_get<const Component::MaterializeAsMeshEntity>(item))
  {
    self = world.CreateRenderableEntity(p->position, p->rotation, p->scale);
    world.GetRegistry().emplace<Mesh>(self, p->mesh);
    world.GetRegistry().emplace<Tint>(self, p->tint);
  }

  if (const auto* p = iReg.try_get<const Component::MaterializeAsSprite>(item))
  {
    ASSERT(self == entt::null);
    self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>(), 0.25f);
    world.GetRegistry().emplace<Billboard>(self, p->tag);
    world.GetRegistry().emplace<Tint>(self, p->tint);
  }

  if (const auto* p = iReg.try_get<const Component::Block>(item))
  {
    auto& bReg = world.GetRegistry().ctx().get<Block::Registry>().GetRegistry();
    if (const auto* p2 = bReg.try_get<const Block::Component::SpawnDependentEntityPrefabWhenPlaced>(entt::entity(p->voxel)))
    {
      auto& entityPrefabs      = world.GetRegistry().ctx().get<EntityPrefabRegistry>();
      const auto& entityPrefab = entityPrefabs.Get(p2->id);
      if (entityPrefab.GetCreateInfo().isVisible)
      {
        return entityPrefab.Spawn(world, {0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>());
      }
    }
    ASSERT(self == entt::null);
    self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>(), 0.25f);

    auto& mesh = world.GetRegistry().emplace<Mesh>(self);
    mesh.name  = "cube";

    if (const auto* material = bReg.try_get<const Block::Component::RenderAsTexturedCube>(entt::entity(p->voxel)))
    {
      world.GetRegistry().emplace<Tint>(self, material->material.baseColorFactor);
      if (!material->material.emissionTexture && glm::length(material->material.emissionFactor) > 0.01f)
      {
        // TODO: Convert from luminance (cd/m^2) to luminous intensity (cd)
        auto light      = GpuLight();
        light.color     = material->material.emissionFactor;
        light.intensity = 1;
        light.type      = LIGHT_TYPE_POINT;
        light.range     = 100;
        world.GetRegistry().emplace<GpuLight>(self, light);
      }
    }
  }

  if (self != entt::null)
  {
    if (const auto* p = iReg.try_get<const Name>(item))
    {
      world.GetRegistry().emplace<Name>(self, *p);
    }
  }

  return self;
}

void Item::Dematerialize(World& world, [[maybe_unused]] ItemId item, entt::entity self)
{
  if (self == entt::null)
  {
    return;
  }

  [[maybe_unused]] const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();

  world.GetRegistry().emplace_or_replace<DeferredDelete>(self);
}

void Item::GiveCollider(World& world, ItemId item, entt::entity self)
{
  if (self == entt::null)
  {
    spdlog::warn("Attempted to give collider to null entity with item: {}", GetName(world, item));
    return;
  }

  const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();
  if (const auto* p = iReg.try_get<const Component::ColliderWhenDropped>(item))
  {
    world.GetRegistry().emplace<Friction>(self).axes = p->friction;
    world.GetRegistry().emplace<Physics::RigidBodySettings>(self,
      Physics::RigidBodySettings{
        .shape = {.shape = p->shape, .translation = p->translation, .rotation = p->rotation},
        .layer = Physics::Layers::DROPPED_ITEM,
      });
  }
  //return world.GetRegistry().get<Physics::RigidBody>(self);
}

void Item::Update(World& world, float dt, entt::entity self, ItemState& state)
{
  state.useAccum += dt;

  if (self == entt::null)
  {
    return;
  }

  const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();

  if (iReg.any_of<const Component::Rainbow>(state.id))
  {
    using namespace glm;
    auto hsv_to_rgb = [](vec3 hsv)
    {
      vec3 rgb = clamp(abs(mod(hsv.x * 6.0f + vec3(0.0, 4.0, 2.0), 6.0f) - 3.0f) - 1.0f, 0.0f, 1.0f);
      return hsv.z * mix(vec3(1.0), rgb, hsv.y);
    };

    auto& tint = world.GetRegistry().get<Tint>(self);
    tint.color = hsv_to_rgb({0.33f * world.GetRegistry().ctx().get<float>("time"_hs), 0.875f, 0.85f});
  }
}

void Item::UsePrimary(World& world, float dt, entt::entity self, ItemState& state)
{
  const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();
  auto& reg        = world.GetRegistry();
  if (const auto* usable = iReg.try_get<const Component::Usable>(state.id))
  {
    if (state.useAccum < usable->timeBetweenUses)
    {
      return;
    }

    state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);
    
    bool subtractCountFromState = false;

    if (const auto* p = iReg.try_get<const Component::SpawnEntityPrefabOnUse>(state.id))
    {
      auto& prefabs         = world.GetRegistry().ctx().get<EntityPrefabRegistry>();
      const auto& transform = world.GetRegistry().get<GlobalTransform>(self);
      prefabs.Get(p->tag).Spawn(world, transform.position + 20.0f * GetForward(transform.rotation));
      subtractCountFromState = true;
      state.useAccum = 0;
    }

    if (const auto* p = iReg.try_get<const Component::SpawnTempHurtboxOnUse>(state.id))
    {
      auto child = reg.create();
      
      reg.emplace<Name>(child).name                 = "Hurtbox";
      reg.emplace<LocalTransform>(child)            = {p->position, glm::identity<glm::quat>(), 1};
      reg.emplace<GlobalTransform>(child)           = {p->position, glm::identity<glm::quat>(), 1};
      reg.emplace<Lifetime>(child).remainingSeconds = p->duration.value_or(usable->timeBetweenUses);
      reg.emplace<Hierarchy>(child);
      reg.emplace<ContactDamage>(child) = {p->damage, p->knockback};
      world.SetParent(child, self);

      reg.emplace<Physics::RigidBodySettings>(child,
        Physics::RigidBodySettings{
          .shape      = p->shape,
          .isSensor   = true,
          .motionType = JPH::EMotionType::Kinematic,
          .layer      = Physics::Layers::PROJECTILE,
        });
    }

    if (const auto* p = iReg.try_get<const Component::HealUserOnUse>(state.id))
    {
      auto parent = reg.get<Hierarchy>(self).parent;
      ASSERT(reg.valid(parent));
      if (auto* health = reg.try_get<Health>(parent))
      {
        if (health->hp < health->maxHp)
        {
          health->hp = std::min(health->hp + p->amount, health->maxHp);
          subtractCountFromState = true;
        }
      }
    }

    if (const auto* p = iReg.try_get<const Component::Gun>(state.id))
    {
      const auto& transform = reg.get<const GlobalTransform>(self);
      world.GetAudio()->PlaySound({
        .name   = "shot2",
        .volume = 0.25f,
        .pitch  = world.Rng().RandFloat(0.9f, 1.1f),
        .reverb =
          Audio::Sound::ReverbInfo{
            .roomSize  = 0.5,
            .damping   = 0.5,
            .dryWetMix = 0.5,
          },
      });
      state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);

      for (int i = 0; i < p->bullets; i++)
      {
        const float bulletScale = 0.05f;
        auto bulletShape        = JPH::Ref(new JPH::SphereShape(.04f));
        bulletShape->SetDensity(11000);
        const auto dir =
          Math::RandVecInCone({world.Rng().RandFloat(), world.Rng().RandFloat()}, GetForward(transform.rotation), glm::radians(p->accuracyMoa / 60.0f));
        auto up = glm::vec3(0, 1, 0);
        if (glm::epsilonEqual(abs(dot(dir, glm::vec3(0, 1, 0))), 1.0f, 0.001f))
        {
          up = {0, 0, 1};
        }
        auto rot = glm::quatLookAtRH(dir, up);
        auto b   = world.CreateRenderableEntity(transform.position + glm::vec3(0, 0.1f, 0) + GetForward(transform.rotation) * 1.0f, rot, bulletScale);

        reg.emplace<Name>(b).name                 = "Bullet";
        reg.emplace<Mesh>(b).name                 = "frog";
        reg.emplace<Lifetime>(b).remainingSeconds = 8;
        reg.emplace<DespawnOnCollision>(b, p->maxBounces + 1);
        if (p->spawnBlockOnHit)
        {
          reg.emplace<SpawnBlockOnContact>(b, *p->spawnBlockOnHit);
        }

        if (p->light)
        {
          reg.emplace<GpuLight>(b, *p->light);
        }

        const auto inheritedVelocity = world.GetInheritedLinearVelocity(self);
        auto& projectile             = reg.emplace<Projectile>(b);
        projectile.initialSpeed      = p->velocity + glm::length(inheritedVelocity);
        projectile.drag              = 0.25f;
        projectile.restitution       = 0.25f;
        projectile.sticky            = p->sticky;
        projectile.stickyDist        = p->stickyDist;
        projectile.particles         = p->particles;

        reg.emplace<LinearVelocity>(b, dir * p->velocity + inheritedVelocity);

        auto& contactDamage     = reg.emplace<ContactDamage>(b);
        contactDamage.damage    = p->damage;
        contactDamage.knockback = p->knockback;

        if (auto* team = world.GetTeamFlags(self))
        {
          reg.emplace<TeamFlags>(b, *team);
        }
      }

      // If parent is player, apply recoil
      if (auto* h = reg.try_get<const Hierarchy>(self); h && h->parent != entt::null)
      {
        const auto vr = glm::radians(p->vrecoil + world.Rng().RandFloat(-p->vrecoilDev, p->vrecoilDev));
        const auto hr = glm::radians(p->hrecoil + world.Rng().RandFloat(-p->hrecoilDev, p->hrecoilDev));
        if (auto* is = reg.try_get<InputLookState>(h->parent))
        {
          is->pitch += vr;
          is->yaw += hr;
          world.UpdateLocalTransform(h->parent);
        }
      }
    }

    if (const auto* p = iReg.try_get<const Component::Tool>(state.id))
    {
      if (reg.all_of<LinearPath>(self))
      {
        reg.remove<LinearPath>(self);
      }
      auto& path = world.GetRegistry().emplace_or_replace<LinearPath>(self);
      path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, -0.25f, -0.25f},
        .rotation                                             = glm::angleAxis(glm::radians(-30.0f), glm::vec3(1, 0, 0)),
        .offsetSeconds                                        = usable->timeBetweenUses * 0.3f,
        .easing                                               = Math::Easing::EASE_OUT_CUBIC});
      path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, 0}, .offsetSeconds = usable->timeBetweenUses * 0.5f, .easing = Math::Easing::EASE_IN_CUBIC});
      
      const auto& h  = reg.get<const Hierarchy>(self);
      const auto par = h.parent;
      const auto& pt = reg.get<const GlobalTransform>(par);
      const auto pos = pt.position;
      const auto dir = GetForward(pt.rotation);

      // TODO: do not hit block if an entity is in the way.
      auto& grid = reg.ctx().get<Voxel::Grid>();
      auto hit   = Voxel::Grid::HitSurfaceParameters();
      if (grid.TraceRaySimple(pos, dir, 5, hit))
      {
        const auto damage = world.DamageBlock(glm::ivec3(hit.voxelPosition), p->blockDamage, p->blockDamageTier, p->blockDamageFlags);
        world.GetAudio()->PlaySound({.name = "hurt", .minDistance = 3, .pitch = 0.5f, .position = hit.positionWorld});

        // Make debris "particles"
        const auto numParticles = glm::clamp(glm::ceil(glm::mix(1.0f, 6.0f, damage / 20.0f)), 0.0f, 10.0f);
        world.SpawnHitParticles({
          .numParticles    = (uint32_t)numParticles,
          .position        = hit.positionWorld,
          .normal          = hit.flatNormalWorld,
          .spreadConeAngle = glm::quarter_pi<float>(),
        });
      }
    }

    if (const auto* p = iReg.try_get<const Component::Block>(state.id))
    {
      const auto& h  = reg.get<const Hierarchy>(self);
      const auto par = h.parent;
      const auto& pt = reg.get<const GlobalTransform>(par);
      const auto pos = pt.position;
      const auto dir = GetForward(pt.rotation);

      auto& grid = reg.ctx().get<Voxel::Grid>();
      auto hit   = Voxel::Grid::HitSurfaceParameters();
      if (GetMaxStackSize(world, state.id) > 0)
      {
        if (grid.TraceRaySimple(pos, dir, 5, hit))
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

            if (!collector.HadHit() && Block::OnTryPlaceBlock(world, newPos, Block::GetRotatedBlockVariant(world, p->voxel, dir, hit.flatNormalWorld)))
            {
              subtractCountFromState = true;
            }
          }
        }
      }
    }

    if (const auto* p = iReg.try_get<const Component::GiveEffectOnUse>(state.id))
    {
      subtractCountFromState = true;
      const auto& h = world.GetRegistry().get<const Hierarchy>(self);
      auto& effects = world.GetRegistry().get<TemporaryEffects>(h.parent).effects;
      // Update duration of the effect if it's already in the list of active effects.
      if (auto it = std::find_if(effects.begin(), effects.end(), [&](const ItemState& is) { return is.id == p->effectId; }); it != effects.end())
      {
        it->useAccum = glm::max(it->useAccum, p->duration);
      }
      else
      {
        effects.emplace_back(p->effectId, 1, p->duration);
      }
    }

    if (const auto* p = iReg.try_get<const Component::AnimatePathOnUse>(state.id))
    {
      reg.emplace_or_replace<LinearPath>(self, *p);
    }

    if (subtractCountFromState && !world.GetRegistry().ctx().get<Debugging>().infiniteItems)
    {
      state.count -= 1;
    }
  }
}

bool Item::ItemIsCompatibleWithSlot(World& world, ItemId item, Component::AllowedSlots slot)
{
  const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();
  if (const auto* p = iReg.try_get<const Component::AllowedSlots>(item))
  {
    return slot == *p;
  }

  return slot == Component::AllowedSlots::Normal;
}

int Item::GetMaxStackSize(World& world, ItemId item)
{
  const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();
  if (const auto* p = iReg.try_get<const Component::Stackable>(item))
  {
    return p->maxStackSize;
  }

  return 1;
}

std::string Item::GetName(World& world, ItemId item)
{
  const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();
  if (const auto* p = iReg.try_get<const Name>(item))
  {
    return p->name;
  }

  return std::to_string(entt::to_integral(item));
}

float Item::GetEffect(World& world, ItemId item, [[maybe_unused]] entt::entity parent, EffectCondition condition, EffectQuantityType quantityType, EffectType type)
{
  const auto& iReg = world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();
  if (const auto* p = iReg.try_get<const Component::StaticEffects>(item))
  {
    for (const auto& effect : p->effects)
    {
      if (effect.condition == condition && effect.quantityType == quantityType && effect.type == type)
      {
        return effect.amount;
      }
    }
  }

  if (quantityType == EffectQuantityType::Additive)
  {
    return 0;
  }

  if (quantityType == EffectQuantityType::Multiplicative)
  {
    return 1;
  }

  UNREACHABLE;
  return -1;
}

float Item::GetTotalEffectOnEntity(World& world, entt::entity entity, EffectType effect, float base)
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
        if (slot.id != entt::null && inv->activeSlotCoord == glm::ivec2(rowIdx, colIdx))
        {
          sum += GetEffect(world, slot.id, entity, EffectCondition::OnHeld, EffectQuantityType::Additive, effect);
          product *= GetEffect(world, slot.id, entity, EffectCondition::OnHeld, EffectQuantityType::Multiplicative, effect);
        }
      }
    }
  }

  if (auto* armor = world.GetRegistry().try_get<const ArmorAndAccessories>(entity))
  {
    for (int i = 0; i < ArmorAndAccessories::SLOT_COUNT; i++)
    {
      const auto& slot = armor->slots[i];
      if (slot.id != entt::null)
      {
        sum += GetEffect(world, slot.id, entity, EffectCondition::OnWorn, EffectQuantityType::Additive, effect);
        product *= GetEffect(world, slot.id, entity, EffectCondition::OnWorn, EffectQuantityType::Multiplicative, effect);
      }
    }
  }

  if (auto* effects = world.GetRegistry().try_get<const TemporaryEffects>(entity))
  {
    for (const auto& effect2 : effects->effects)
    {
      DEBUG_ASSERT(effect2.id != entt::null);
      sum += GetEffect(world, effect2.id, entity, EffectCondition::OnWorn, EffectQuantityType::Additive, effect);
      product *= GetEffect(world, effect2.id, entity, EffectCondition::OnWorn, EffectQuantityType::Multiplicative, effect);
    }
  }

  return (base + sum) * product;
}

ItemId Item::CreateGun(Registry& registry, std::string tag, std::string name, float rateOfFireRPM, const Component::Gun& gun)
{
  const auto id = registry.Create(std::move(tag));
  registry.GetRegistry().emplace<Name>(id, std::move(name));
  registry.GetRegistry().emplace<Component::Usable>(id).timeBetweenUses = 1.0f / (rateOfFireRPM / 60.0f);
  registry.GetRegistry().emplace<Component::Gun>(id, gun);
  registry.GetRegistry().emplace<Component::MaterializeAsMeshEntity>(id) = {.mesh = gun.model, .tint = gun.tint, .position = {0.2f, -0.2f, -0.5f}};
  registry.GetRegistry().emplace<Component::ColliderWhenDropped>(id);
  return id;
}

ItemId Item::CreateTool(Registry& registry,
  std::string tag,
  std::string name,
  std::string model,
  glm::vec3 tint,
  float timeBetweenUses,
  const Component::Tool& tool)
{
  const auto id = registry.Create(std::move(tag));
  registry.GetRegistry().emplace<Name>(id, std::move(name));
  registry.GetRegistry().emplace<Component::Usable>(id).timeBetweenUses  = timeBetweenUses;
  registry.GetRegistry().emplace<Component::MaterializeAsMeshEntity>(id) = {.mesh = std::move(model), .tint = tint, .position = {0.3f, -0.7f, -0.7f}};
  registry.GetRegistry().emplace<Component::ColliderWhenDropped>(id);
  auto& path = registry.GetRegistry().emplace<Item::Component::AnimatePathOnUse>(id);
  path.frames.emplace_back(LinearPath::KeyFrame{
    .position      = {0, -0.25f, -0.25f},
    .rotation      = glm::angleAxis(glm::radians(-30.0f), glm::vec3(1, 0, 0)),
    .offsetSeconds = timeBetweenUses * 0.3f,
    .easing        = Math::Easing::EASE_OUT_CUBIC,
  });
  path.frames.emplace_back(LinearPath::KeyFrame{
    .position      = {0, 0, 0},
    .offsetSeconds = timeBetweenUses * 0.5f,
    .easing        = Math::Easing::EASE_IN_CUBIC,
  });

  registry.GetRegistry().emplace<Component::Tool>(id, tool);
  registry.GetRegistry().emplace<Component::SpawnTempHurtboxOnUse>(id) = {.position = {0, .7f, -.4f}, .damage = 10, .knockback = 5, .duration = timeBetweenUses};
  return id;
}

ItemId Item::CreateSpear(Registry& registry, std::string tag, std::string name, std::string model, glm::vec3 tint, float timeBetweenUses, float damage, float knockback)
{
  const auto id = registry.Create(std::move(tag));
  registry.GetRegistry().emplace<Name>(id, std::move(name));
  registry.GetRegistry().emplace<Component::Usable>(id, timeBetweenUses);
  registry.GetRegistry().emplace<Component::MaterializeAsMeshEntity>(id) = {.mesh = std::move(model), .tint = tint, .position = {0.2f, -0.2f, -0.5f}};
  registry.GetRegistry().emplace<Component::ColliderWhenDropped>(id);
  registry.GetRegistry().emplace<Component::SpawnTempHurtboxOnUse>(id) = {.position = {0, 0, -1}, .damage = damage, .knockback = knockback, .duration = timeBetweenUses};
  auto& path = registry.GetRegistry().emplace<Component::AnimatePathOnUse>(id);
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, -1}, .offsetSeconds = timeBetweenUses * 0.45f, .easing = Math::Easing::EASE_IN_OUT_BACK});
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, 0}, .offsetSeconds = timeBetweenUses * 0.45f, .easing = Math::Easing::EASE_IN_SINE});
  return id;
}

ItemId Item::CreateSimpleSpriteItem(Registry& registry, std::string tag, std::string name, std::string sprite, int maxStackSize, glm::vec3 tint)
{
  const auto id = registry.Create(std::move(tag));
  registry.GetRegistry().emplace<Name>(id, std::move(name));
  registry.GetRegistry().emplace<Component::MaterializeAsSprite>(id) = {.tag = std::move(sprite), .tint = tint};
  registry.GetRegistry().emplace<Component::ColliderWhenDropped>(id);
  registry.GetRegistry().emplace<Component::Stackable>(id, maxStackSize);
  return id;
}

ItemId Item::CreateEffector(Registry& registry, std::string tag, std::string name, EffectType type, float additive, float multiplicative)
{
  const auto id = registry.Create(std::move(tag));
  registry.GetRegistry().emplace<Name>(id, std::move(name));
  auto& effects = registry.GetRegistry().emplace<Component::StaticEffects>(id).effects;
  effects.push_back({
    .condition    = EffectCondition::OnWorn,
    .quantityType = EffectQuantityType::Additive,
    .type         = type,
    .amount       = additive,
  });
  effects.push_back({
    .condition    = EffectCondition::OnWorn,
    .quantityType = EffectQuantityType::Multiplicative,
    .type         = type,
    .amount       = multiplicative,
  });
  registry.GetRegistry().emplace<Component::AllowedSlots>(id, Component::AllowedSlots::Hidden);
  return id;
}

ItemId Item::CreateEffectGranter(Registry& registry, std::string tag, std::string name, ItemId effector, float duration, std::string sprite, glm::vec3 tint)
{
  const auto id = CreateSimpleSpriteItem(registry, std::move(tag), std::move(name), std::move(sprite), 999, tint);
  registry.GetRegistry().emplace<Component::GiveEffectOnUse>(id) = {.effectId = effector, .duration = duration};
  registry.GetRegistry().emplace<Component::Usable>(id, 0.5f);
  return id;
}

ItemId Item::CreateArmor(Registry& registry,
  std::string tag,
  std::string name,
  Component::AllowedSlots slot,
  float armorModifier,
  std::string sprite,
  glm::vec3 tint)
{
  const auto id = CreateSimpleSpriteItem(registry, std::move(tag), std::move(name), std::move(sprite), 1, tint);
  registry.GetRegistry().emplace<Component::AllowedSlots>(id, slot);
  registry.GetRegistry().emplace<Component::StaticEffects>(id).effects.push_back({
    .condition    = EffectCondition::OnWorn,
    .quantityType = EffectQuantityType::Additive,
    .type         = EffectType::ArmorModifier,
    .amount       = armorModifier,
  });
  return id;
}

ItemId Item::RegisterItemForBlock(World& world, BlockId block)
{
  auto& itemRegistry = world.GetRegistry().ctx().get<Item::Registry>();
  auto& reg          = itemRegistry.GetRegistry();

  auto& blockRegistry = world.GetRegistry().ctx().get<Block::Registry>();
  auto name           = blockRegistry.GetIdToTagMap().at(block);

  const auto e = itemRegistry.Create(name);
  if (auto* n = blockRegistry.GetRegistry().try_get<Name>(entt::entity(block)))
  {
    name = n->name;
  }
  reg.emplace<Name>(e, name);
  reg.emplace<Item::Component::Usable>(e).timeBetweenUses = 0.25f;
  reg.emplace<Item::Component::Stackable>(e);
  reg.emplace<Item::Component::ColliderWhenDropped>(e);
  reg.emplace<Item::Component::AllowedSlots>(e, Item::Component::AllowedSlots::Normal);
  reg.emplace<Item::Component::Block>(e).voxel = block;

  blockRegistry.GetRegistry().emplace_or_replace<Block::Component::CorrespondingItem>(entt::entity(block), e);
  return e;
}
