#include "World.h"
#include "Game.h"
#include "Game/Globals.h"

#include "HashGrid.h"
#include "Item.h"
#include "Pathfinding.h"
#include "Prefab.h"
#include "VoxLoader.h"
#include "Client/debug/Shapes.h"
#include "Core/Assert2.h"
#include "Physics/Physics.h"
#include "Physics/PhysicsUtils.h"
#include "Networking/Client.h"
#include "Networking/RPC.h"
#include "Networking/Server.h"
#include "Audio.h"
#include "shaders/Light.h.glsl"
#include "Voxel/Grid.h"

#include "Jolt/Physics/Constraints/SwingTwistConstraint.h"
#include "tracy/Tracy.hpp"
#include "spdlog/spdlog.h"

#include <execution>
#include <stack>

World::World() : registry_(registryOld_)
{
}

std::optional<glm::vec3> SampleWalkablePosition(const Voxel::Grid& grid, PCG::Rng& rng, glm::vec3 origin, float minDistance, float maxDistance, bool isAirWalkable)
{
  // Pick a random position in the sphere with a minimum distance from the player.
  const float r     = rng.RandFloat(minDistance, maxDistance);
  const float theta = rng.RandFloat(0, glm::two_pi<float>());
  const float phi   = rng.RandFloat(0, glm::two_pi<float>());
  const auto pos    = origin + Math::SphericalToCartesian(theta, phi, r); // Totally not uniform, but fine for testing.

  // Validate the position
  if (!grid.IsPositionInGrid(pos) || grid.GetVoxelAt(pos) != voxel_t::Air)
  {
    return std::nullopt;
  }

  auto lastValidPos      = pos;
  bool foundSolidSurface = false;
  if (!isAirWalkable)
  {
    // Slide down until on solid surface.
    for (int i = 0; i < 20; i++)
    {
      const auto nextPos = lastValidPos - glm::vec3(0, i, 0);
      if (!grid.IsPositionInGrid(nextPos) || grid.GetVoxelAt(nextPos) != voxel_t::Air)
      {
        foundSolidSurface = true;
        break;
      }
      lastValidPos = nextPos;
    }
  }

  const auto realPos = floor(lastValidPos) + glm::vec3(0.5f);
  // Re-validate distance as it may have changed.
  const auto dist2 = Math::Distance2(realPos, origin);
  if ((isAirWalkable || foundSolidSurface) && dist2 > minDistance * minDistance && dist2 < maxDistance * maxDistance)
  {
    return realPos;
  }

  return std::nullopt;
}

entt::entity CreateSnake(World& world, glm::vec3, glm::quat)
{
  auto& registry          = world.GetRegistry();
  entt::entity head       = entt::null;
  auto prevBody2          = std::optional<JPH::BodyID>();
  entt::entity prevEntity = entt::null;
  for (int i = 0; i < 15; i++)
  {
    // const auto position             = glm::vec3{cos(glm::two_pi<float>() * i / 15.0f) * 4.0f, 50 + i / 5.0f, sin(glm::two_pi<float>() * i / 15.0f) * 4.0f};
    const auto position            = glm::vec3{20, 75, i / 0.8f};
    auto a                         = world.CreateRenderableEntity(position, glm::identity<glm::quat>(), i == 0 ? 0.5f : 1.0f);
    registry.emplace<Name>(a).name = i == 0 ? "Worm head" : "Worm body";
    registry.emplace<Mesh>(a).name = "frog";
    auto body                      = JPH::BodyID();
    if (i != 0)
    {
      registry.emplace<Physics::RigidBodySettings>(a,
        Physics::RigidBodySettings{
          .shape      = Physics::ShapeSettings{Physics::Sphere{0.5f}, 35.0f - i * 2},
          .activate   = true,
          .isSensor   = false,
          .motionType = JPH::EMotionType::Dynamic,
          .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
        });

      const auto& rb = registry.get<Physics::RigidBody>(a);
      body           = rb.body;

      world.GetPhysicsEngine().GetBodyInterface().SetGravityFactor(rb.body, 1);
      world.GetPhysicsEngine().GetBodyInterface().SetMotionQuality(rb.body, JPH::EMotionQuality::LinearCast);
    }

    if (i == 0)
    {
      head = a;
      registry.emplace<Physics::CharacterControllerShrimpleSettings>(a, Physics::CharacterControllerShrimpleSettings{.shape = Physics::Sphere{0.5f}});
      body = registry.get<Physics::CharacterControllerShrimple>(a).character->GetBodyID();
      // registry.emplace<NoclipCharacterController>(a);
      registry.emplace<SimplePathfindingEnemyBehavior>(a);
      registry.emplace<Pathfinding::CachedPath>(a).timeBetweenUpdates = 1;
      // registry.emplace<SimpleEnemyBehavior>(a);
      registry.emplace<InputState>(a);
      registry.emplace<TeamFlags>(a, TeamFlagBits::ENEMY);
      registry.emplace<Health>(a)    = {100, 100};
      registry.emplace<Loot>(a).name = "standard";
      registry.emplace<DespawnWhenFarFromPlayer>(a);
      registry.emplace<Enemy>(a);
    }

    if (prevBody2)
    {
      registry.emplace<ForwardCollisionsToParent>(a);
      auto prevPos = registry.get<GlobalTransform>(a).position;
      // if (i == 1)
      //{
      //   //SetLocalPosition(a, prevPos);
      //   //auto settings = JPH::Ref(new JPH::FixedConstraintSettings());
      //   //settings->mAutoDetectPoint = true;
      //   //auto constraint            = Physics::GetBodyInterface().CreateConstraint(settings, *prevBody2, rb.body);
      //   //Physics::GetPhysicsSystem().AddConstraint(constraint);
      //    auto settings   = JPH::Ref(new JPH::DistanceConstraintSettings());
      //    settings->mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
      //    settings->mMinDistance = 0.95f;
      //    settings->mMaxDistance = 1;
      //    settings->mConstraintPriority = 1'000'000'000 - i * 1000;
      //    auto constraint               = Physics::GetBodyInterface().CreateConstraint(settings, *prevBody2, body);
      //    Physics::GetPhysicsSystem().AddConstraint(constraint);
      // }
      // else
      {
        auto settings = JPH::Ref(new JPH::SwingTwistConstraintSettings);
        // settings->mPosition1           = settings->mPosition2  = Physics::ToJolt(position) + JPH::Vec3(-0.5f, 0, 0);
        settings->mPosition1 = settings->mPosition2 = Physics::ToJolt((prevPos + position) / 2.0f);
        settings->mTwistAxis1 = settings->mTwistAxis2 = JPH::Vec3::sAxisX();
        settings->mPlaneAxis1 = settings->mPlaneAxis2 = JPH::Vec3::sAxisY();
        settings->mNormalHalfConeAngle                = JPH::DegreesToRadians(60);
        settings->mPlaneHalfConeAngle                 = JPH::DegreesToRadians(60);
        settings->mTwistMinAngle                      = JPH::DegreesToRadians(-20);
        settings->mTwistMaxAngle                      = JPH::DegreesToRadians(20);
        // auto settings   = JPH::Ref(new JPH::DistanceConstraintSettings());
        // settings->mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
        // settings->mMinDistance = 0.95f;
        // settings->mMaxDistance = 1;
        auto constraint = world.GetPhysicsEngine().GetBodyInterface().CreateConstraint(settings, *prevBody2, body);
        // constraint->SetNumPositionStepsOverride(100);
        world.GetPhysicsEngine().RegisterConstraint(constraint);
      }
      auto& h                    = registry.get<Hierarchy>(a);
      h.useLocalPositionAsGlobal = true;
      h.useLocalRotationAsGlobal = true;
      world.SetParent(a, prevEntity);

      auto eHitbox                         = registry.create();
      registry.emplace<Name>(eHitbox).name = "Worm hitbox";
      registry.emplace<ForwardCollisionsToParent>(eHitbox);
      registry.emplace<PreviousGlobalTransform>(eHitbox);
      auto& tpHitbox                             = registry.emplace<LocalTransform>(eHitbox);
      tpHitbox.position                          = {};
      tpHitbox.rotation                          = glm::identity<glm::quat>();
      tpHitbox.scale                             = 1;
      registry.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
      registry.emplace<Hierarchy>(eHitbox);
      registry.emplace<Physics::RigidBodySettings>(eHitbox,
        Physics::RigidBodySettings{
          .shape      = Physics::Sphere{0.5f},
          .isSensor   = true,
          .motionType = JPH::EMotionType::Kinematic,
          .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
        });
      world.SetParent(eHitbox, a);
    }
    prevBody2  = body;
    prevEntity = a;
  }
  return head;
}

entt::entity World::CreateRenderableEntityNoHashGrid(glm::vec3 position, glm::quat rotation, float scale)
{
  auto e     = registry_.create();
  auto& t    = registry_.emplace<LocalTransform>(e);
  t.position = position;
  t.rotation = rotation;
  t.scale    = scale;

  registry_.emplace<GlobalTransform>(e) = {t.position, t.rotation, t.scale};

  auto& it    = registry_.emplace<PreviousGlobalTransform>(e);
  it.position = t.position;
  it.rotation = t.rotation;
  it.scale    = t.scale;
  registry_.emplace<RenderTransform>(e);
  registry_.emplace<Hierarchy>(e);

  registry_.emplace<NoHashGrid>(e);
  return e;
}

entt::entity World::CreateRenderableEntity(glm::vec3 position, glm::quat rotation, float scale)
{
  auto e = CreateRenderableEntityNoHashGrid(position, rotation, scale);
  registry_.remove<NoHashGrid>(e);
  globals->game->hashGrid->set(position, e);
  return e;
}

entt::entity World::CreateDroppedItem(ItemState item, glm::vec3 position, glm::quat rotation, float scale)
{
  auto entity = Item::Materialize(*this, item.id);

  auto& t    = registry_.get<LocalTransform>(entity);
  t.position = position;
  t.rotation = rotation;
  t.scale    = scale;
  UpdateLocalTransform(entity);
  Item::GiveCollider(*this, item.id, entity);
  registry_.emplace<DroppedItem>(entity).item = ItemState{item.id};
  return entity;
}

entt::entity World::TryGetLocalPlayer()
{
  auto view = registry_.view<const GlobalTransform, const LocalPlayer>();
  auto e    = view.front();
  if (e != entt::null)
  {
    return e;
  }
  return entt::null;
}

const GlobalTransform* World::TryGetLocalPlayerTransform()
{
  auto view = registry_.view<const GlobalTransform, const LocalPlayer>();
  auto e    = view.front();
  if (e != entt::null)
  {
    return &view.get<const GlobalTransform>(e);
  }
  return nullptr;
}

void World::SetLocalScale(entt::entity entity, float scale)
{
  auto& lt = registry_.get<LocalTransform>(entity);
  lt.scale = scale;
  UpdateLocalTransform(entity);
}

entt::entity World::GetChildNamed(entt::entity entity, std::string_view name) const
{
  for (auto child : registry_.get<const Hierarchy>(entity).children)
  {
    if (auto* n = registry_.try_get<Name>(child); n && n->name == name)
    {
      return child;
    }
  }
  return entt::null;
}

glm::vec3 World::GetInheritedLinearVelocity(entt::entity entity)
{
  ASSERT(registry_.valid(entity));
  if (auto* vel = registry_.try_get<LinearVelocity>(entity))
  {
    return vel->v;
  }
  if (auto* h = registry_.try_get<const Hierarchy>(entity); h && h->parent != entt::null)
  {
    return GetInheritedLinearVelocity(h->parent);
  }
  return {0, 0, 0};
}

const TeamFlags* World::GetTeamFlags(entt::entity entity) const
{
  ASSERT(registry_.valid(entity));
  if (auto* teamFlags = registry_.try_get<const TeamFlags>(entity))
  {
    return teamFlags;
  }
  if (auto* h = registry_.try_get<const Hierarchy>(entity); h && h->parent != entt::null)
  {
    return GetTeamFlags(h->parent);
  }
  return nullptr;
}

entt::entity World::CreatePlayer()
{
  auto p = registry_.create();

  registry_.emplace<Name>(p, "Player");
  registry_.emplace<Player>(p);
  registry_.emplace<InputState>(p);
  registry_.emplace<InputLookState>(p);
  registry_.emplace<Hierarchy>(p);
  registry_.emplace<Health>(p) = {100, 100};
  registry_.emplace<TeamFlags>(p, TeamFlagBits::FRIENDLY);

  auto meshE = CreateRenderableEntity({0, 0, 0});
  registry_.emplace<Name>(meshE, "Mesh");
  registry_.emplace<Mesh>(meshE).name = "player";
  SetParent(meshE, p);
  registry_.get<Hierarchy>(meshE).useLocalRotationAsGlobal = true;
  registry_.emplace<DoNotRenderIfAncestorIsLocalPlayer>(meshE);

  auto& tp    = registry_.emplace<LocalTransform>(p);
  tp.position = {60, 410, 60};
  tp.rotation = glm::identity<glm::quat>();
  tp.scale    = 1;

  registry_.emplace<GlobalTransform>(p) = {tp.position, tp.rotation, tp.scale};
  registry_.emplace<PreviousGlobalTransform>(p);
  registry_.emplace<RenderTransform>(p);
  registry_.emplace<WalkingMovementAttributes>(p);
  GivePlayerCharacterController(p);
  // GivePlayerFlyingCharacterController(p);
  // registry_.emplace<NoclipCharacterController>(p);
  registry_.emplace_or_replace<LinearVelocity>(p);
  // cc.character->SetMaxStrength(10000000);

  auto& items     = *globals->itemRegistry;
  auto& inventory = registry_.emplace<Inventory>(p);
  inventory.OverwriteSlot(*this, {0, 0}, {items.Get("weapon_stone_spear")}, p);
  inventory.OverwriteSlot(*this, {0, 1}, {items.Get("tool_stone_pickaxe")}, p);
  inventory.OverwriteSlot(*this, {0, 2}, {items.Get("tool_stone_axe")}, p);
  registry_.emplace<ArmorAndAccessories>(p);
  registry_.emplace<TemporaryEffects>(p);

  GivePlayerColliders(p);

  return p;
}

Physics::CharacterController& World::GivePlayerCharacterController(entt::entity playerEntity)
{
  constexpr float playerHalfHeight = 0.8f;
  constexpr float playerHalfWidth  = 0.3f;
  // auto playerCapsule = JPH::Ref(new JPH::CapsuleShape(playerHalfHeight - playerHalfWidth, playerHalfWidth));
  auto playerShape = Physics::Box({playerHalfWidth, playerHalfHeight, playerHalfWidth});

  registry_.emplace_or_replace<Physics::CharacterControllerSettings>(playerEntity,
    Physics::CharacterControllerSettings{{.shape = playerShape, .translation = {0, -playerHalfHeight * 0.875f, 0}}});

  // Adding a CharacterControllerSettings will also add a CharacterController.
  return registry_.get<Physics::CharacterController>(playerEntity);
}

Physics::CharacterControllerShrimple& World::GivePlayerCharacterControllerShrimple(entt::entity playerEntity)
{
  constexpr float playerHalfHeight = 0.8f;
  constexpr float playerHalfWidth  = 0.3f;
  // auto playerCapsule = JPH::Ref(new JPH::CapsuleShape(playerHalfHeight - playerHalfWidth, playerHalfWidth));
  auto playerShape = Physics::Box({playerHalfWidth, playerHalfHeight, playerHalfWidth});

  registry_.emplace<Physics::CharacterControllerShrimpleSettings>(playerEntity,
    Physics::CharacterControllerShrimpleSettings{{.shape = playerShape, .translation = {0, -playerHalfHeight * 0.875f, 0}}});

  return registry_.get<Physics::CharacterControllerShrimple>(playerEntity);
}

FlyingCharacterController& World::GivePlayerFlyingCharacterController(entt::entity playerEntity)
{
  registry_.emplace<Friction>(playerEntity, glm::vec3(5.0f));
  return registry_.emplace<FlyingCharacterController>(playerEntity) = {.maxSpeed = 9, .acceleration = 35.0f};
}

void World::GivePlayerColliders(entt::entity playerEntity)
{
  constexpr float playerHalfHeight = 0.8f * 1.0f;
  constexpr float playerHalfWidth  = 0.3f * 1.0f;
  ASSERT(playerHalfHeight - playerHalfWidth >= 0);
  auto playerHitbox = Physics::Capsule(playerHalfHeight - playerHalfWidth, playerHalfWidth);

  auto pHitbox                          = registry_.create();
  registry_.emplace<Name>(pHitbox).name = "Player hitbox";
  registry_.emplace<ForwardCollisionsToParent>(pHitbox);
  auto& tpHitbox    = registry_.emplace<LocalTransform>(pHitbox);
  tpHitbox.position = {};
  tpHitbox.rotation = glm::identity<glm::quat>();
  tpHitbox.scale    = 1;

  registry_.emplace<GlobalTransform>(pHitbox) = {{}, glm::identity<glm::quat>(), 1};

  registry_.emplace<Hierarchy>(pHitbox).useLocalRotationAsGlobal = true; // Stay upright
  registry_.emplace<Physics::RigidBodySettings>(pHitbox,
    Physics::RigidBodySettings{
      .shape      = Physics::ShapeSettings{.shape = playerHitbox, .translation = {0, -0.8f * 0.875f, 0}},
      .isSensor   = true,
      .motionType = JPH::EMotionType::Kinematic,
      .layer      = Physics::Layers::HITBOX,
    });
  SetParent(pHitbox, playerEntity);

  // Rigid body at the player's location that's used for placing contraints on the player.
  // This body also serves as a "real" (not CharacterVirtual) collider to prevent the player from sliding through walls when being pulled by a rope.
  // TODO: The latter functionality should be a separate, temporary body that only exists when the player is attached to a rope, as it may cause minor issues with movement otherwise.
  auto pFakePhysics = registry_.create();
  registry_.emplace<Name>(pFakePhysics).name = "Player fake physics";
  registry_.emplace<SyncWithParentPosition>(pFakePhysics);
  auto& tpFakePhysics = registry_.emplace<LocalTransform>(pFakePhysics);
  tpFakePhysics.position = {};
  tpFakePhysics.rotation = glm::identity<glm::quat>();
  tpFakePhysics.scale    = 1;
  registry_.emplace<GlobalTransform>(pFakePhysics) = {{}, glm::identity<glm::quat>(), 1};
  auto& hierarchy = registry_.emplace<Hierarchy>(pFakePhysics);
  hierarchy.useLocalPositionAsGlobal = true;
  hierarchy.useLocalRotationAsGlobal = true;
  registry_.emplace<Physics::RigidBodySettings>(pFakePhysics,
    Physics::RigidBodySettings{
      .shape =
        Physics::ShapeSettings{
          .shape       = Physics::Capsule(.25f, 0.50f),
          .density     = 1000,
          .translation = {0, -0.8f * 0.875f, 0},
        },
      .isSensor      = false,
      .gravityFactor = 0,
      .motionType    = JPH::EMotionType::Dynamic,
      //.motionQuality = JPH::EMotionQuality::LinearCast,
      .layer            = Physics::Layers::CHARACTER,
      .degreesOfFreedom = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ,
    });
  SetParent(pFakePhysics, playerEntity);
}

void World::KillPlayer(entt::entity playerEntity)
{
  registry_.remove<NoclipCharacterController, FlyingCharacterController, Physics::CharacterController, Physics::CharacterControllerShrimple>(playerEntity);
  registry_.emplace<GhostPlayer>(playerEntity).remainingSeconds = 5;

  if (auto e = GetChildNamed(playerEntity, "Player hitbox"); e != entt::null)
  {
    registry_.destroy(e);
    registry_.get<Hierarchy>(playerEntity).RemoveChild(e);
  }

  auto& inventory = registry_.get<const Inventory>(playerEntity);
  if (inventory.ActiveSlot().id != entt::null)
  {
    Item::Dematerialize(*this, inventory.ActiveSlot().id, inventory.activeSlotEntity);
  }
}

void World::RespawnPlayer(entt::entity playerEntity)
{
  registry_.remove<GhostPlayer>(playerEntity);
  Networking::CallRPC2("TeleportPlayerRPC"_hs, playerEntity, *this, playerEntity, LocalTransform{{60, 410, 60}, glm::identity<glm::quat>(), 1});
  UpdateLocalTransform(playerEntity);

  registry_.get_or_emplace<Health>(playerEntity)                           = {100, 100};
  registry_.get_or_emplace<Invulnerability>(playerEntity).remainingSeconds = 5;

  GivePlayerCharacterController(playerEntity);
  GivePlayerColliders(playerEntity);

  auto& inventory = registry_.get<Inventory>(playerEntity);
  if (inventory.ActiveSlot().id != entt::null)
  {
    inventory.activeSlotEntity = Item::Materialize(*this, inventory.ActiveSlot().id);
    SetParent(inventory.activeSlotEntity, playerEntity);
  }
}

float World::DamageEntity(entt::entity entity, float damage)
{
  if (registry_.any_of<Invulnerability>(entity))
  {
    return 0;
  }

  const auto armor = Item::GetTotalEffectOnEntity(*this, entity, Item::EffectType::ArmorModifier, 0);
  damage           = glm::max(1.0f, damage - armor / 2);
  auto& h          = registry_.get<Health>(entity);
  h.hp -= damage;
  return damage;
}

bool World::CanEntityDamageEntity(entt::entity entitySource, entt::entity entityTarget) const
{
  if (const auto* cd = registry_.try_get<const CannotDamageEntities>(entitySource))
  {
    if (cd->entities.contains(entityTarget))
    {
      return false;
    }
  }

  return true;
}

bool World::AreEntitiesEnemies(entt::entity entity1, entt::entity entity2) const
{
  auto* team1 = GetTeamFlags(entity1);
  auto* team2 = GetTeamFlags(entity2);
  if (!team1 || !team2 || !(*team1 & *team2))
  {
    return true;
  }
  return false;
}

std::vector<entt::entity> World::GetEntitiesInSphere(glm::vec3 center, float radius) const
{
  ZoneScoped;
  const float radius2 = radius * radius;
  const auto& grid    = globals->game->hashGrid;

  auto entities = std::vector<entt::entity>();

  const auto lower = grid->QuantizeKey(center - radius);
  const auto upper = grid->QuantizeKey(center + radius);

  // Broadphase: iterate over all chunks touched by sphere.
  for (int z = lower.z; z <= upper.z; z++)
  {
    for (int y = lower.y; y <= upper.y; y++)
    {
      for (int x = lower.x; x <= upper.x; x++)
      {
        const auto [begin, end] = grid->equal_range_chunk({x, y, z});
        for (auto it = begin; it != end; ++it)
        {
          // Narrowphase: distance check.
          const auto entity    = it->second;
          const auto& position = registry_.get<const GlobalTransform>(entity).position;

          const auto vec       = position - center;
          const auto distance2 = glm::dot(vec, vec);
          if (distance2 <= radius2)
          {
            entities.emplace_back(entity);
          }
        }
      }
    }
  }

  return entities;
}

std::vector<entt::entity> World::GetEntitiesInCapsule(glm::vec3 start, glm::vec3 end, float radius)
{
  ZoneScoped;
  const auto& grid = globals->game->hashGrid;

  auto entities = std::vector<entt::entity>();

  const auto lower = grid->QuantizeKey(glm::min(start - radius, end - radius));
  const auto upper = grid->QuantizeKey(glm::max(start + radius, end + radius));

  for (int z = lower.z; z <= upper.z; z++)
  {
    for (int y = lower.y; y <= upper.y; y++)
    {
      for (int x = lower.x; x <= upper.x; x++)
      {
        const auto [beginIt, endIt] = grid->equal_range_chunk({x, y, z});
        for (auto it = beginIt; it != endIt; ++it)
        {
          // Narrowphase: distance check.
          const auto entity    = it->second;
          const auto& position = registry_.get<const GlobalTransform>(entity).position;

          if (Math::PointLineSegmentDistance(position, start, end) <= radius)
          {
            entities.emplace_back(entity);
          }
        }
      }
    }
  }

  return entities;
}

entt::entity World::GetNearestPlayer(glm::vec3 position)
{
  entt::entity nearestPlayer = entt::null;
  float nearestDistance2     = HUGE_VALF;

  for (auto [entity, transform, player] : registry_.view<const GlobalTransform, const Player>(entt::exclude<GhostPlayer>).each())
  {
    if (const auto dist2 = Math::Distance2(position, transform.position); dist2 < nearestDistance2)
    {
      nearestPlayer    = entity;
      nearestDistance2 = dist2;
    }
  }

  return nearestPlayer;
}

float World::DamageBlock(glm::ivec3 voxelPos, float damage, int damageTier, BlockDamageFlags damageType)
{
  ZoneScoped;
  auto& grid = *globals->grid;
  const auto prevVoxel = grid.GetVoxelAt(voxelPos);
  if (prevVoxel == voxel_t::Air)
  {
    return 0;
  }

  entt::entity foundEntity = entt::null;
  BlockHealth* hp          = nullptr;

  const auto worldPos = glm::vec3(voxelPos) + 0.5f;
  for (auto entity : GetEntitiesInSphere(worldPos, 0.125f))
  {
    hp = registry_.try_get<BlockHealth>(entity);
    if (hp)
    {
      foundEntity = entity;
      break;
    }
  }

  if (foundEntity == entt::null)
  {
    foundEntity = this->CreateRenderableEntity(worldPos);
    hp          = &registry_.emplace<BlockHealth>(foundEntity, Block::GetInitialHealth(*this, prevVoxel));
  }

  registry_.emplace_or_replace<Lifetime>(foundEntity).remainingSeconds = 5;

  if ((damageType & Block::GetDamageFlags(*this, prevVoxel)).flags == 0 || damageTier < Block::GetDamageTier(*this, prevVoxel))
  {
    return 0;
  }

  const auto initialHealth = hp->health;
  hp->health -= damage;
  if (hp->health <= 0)
  {
    Block::OnDestroyBlock(*this, voxelPos, prevVoxel);

    const auto hasNoLoot95 = damageType & BlockDamageFlagBit::NO_LOOT_95_PERCENT;
    if ((!hasNoLoot95 || Rng().RandFloat() >= 0.95) && !(damageType & BlockDamageFlagBit::NO_LOOT))
    {
      Block::SpawnLootDropFromBlock(*this, voxelPos, prevVoxel);
    }

    // Awaken bodies that are adjacent to destroyed voxel in case they were resting on it.
    // TODO: This doesn't seem to be robust. Setting mTimeBeforeSleep to 0 in PhysicsSettings seems to disable sleeping, which fixes this issue.
    GetPhysicsEngine().GetBodyInterface().ActivateBodiesInAABox({Physics::ToJolt(worldPos), 2.0f}, {}, {});

    registry_.destroy(foundEntity);
  }

  return initialHealth - hp->health;
}

entt::entity World::GetBlockEntity(glm::ivec3 voxelPosition)
{
  for (auto entity : GetEntitiesInSphere(glm::vec3(voxelPosition) + glm::vec3(0.5f), 0.25f))
  {
    if (registry_.all_of<BlockEntity>(entity))
    {
      return entity;
    }
  }
  return entt::null;
}

entt::entity World::GetRootEntityOfHierarchy(entt::entity entity) const
{
  if (auto* h = registry_.try_get<const Hierarchy>(entity); h && h->parent != entt::null)
  {
    return GetRootEntityOfHierarchy(h->parent);
  }
  return entity;
}

bool World::IsClient() const
{
  const auto* networking = globals->networking;
  return networking->get() && dynamic_cast<Networking::Client*>(networking->get());
}

bool World::IsServer() const
{
  return !IsClient();
}

bool World::IsHosting() const
{
  const auto* networking = globals->networking;
  const auto* server     = dynamic_cast<const Networking::Server*>(networking->get());
  return networking->get() && server && server->GetNumberOfConnections();
}

Audio* World::GetAudio()
{
  return globals->head->GetAudio();
}

glm::vec3 World::GetFootPosition(entt::entity entity)
{
  const auto* t = registry_.try_get<const GlobalTransform>(entity);
  ASSERT(t);

  if (const auto* s = registry_.try_get<const Physics::Shape>(entity))
  {
    const auto floorOffsetY = -s->shape->GetLocalBounds().GetExtent().GetY();
    return t->position + glm::vec3(0, floorOffsetY + 1e-1f, 0); // Needs fairly large epsilon because feet can penetrate ground in physics sim.
  }

  return t->position - glm::vec3(0, t->scale, 0);
}

float World::GetHeight(entt::entity entity)
{
  if (const auto* s = registry_.try_get<const Physics::Shape>(entity))
  {
    return s->shape->GetLocalBounds().GetExtent().GetY() * 2.0f;
  }

  const auto& t = registry_.get<const GlobalTransform>(entity);
  return t.scale * 2.0f;
}

void World::UpdateLocalTransform(entt::entity entity, int depth)
{
  ASSERT(depth < 256, "Stack overflow");
  ASSERT(registry_.valid(entity));
  if (!registry_.valid(entity))
  {
    return;
  }

  // parent_from_local, world_from_local
  auto [h, plt, pgt] = registry_.try_get<const Hierarchy, const LocalTransform, GlobalTransform>(entity);
  if (!plt || !pgt)
  {
    return;
  }

  const auto& lt = *plt;
  auto& gt       = *pgt;

  if (!h || !registry_.valid(h->parent))
  {
    gt = {lt.position, lt.rotation, lt.scale};
  }
  else if (registry_.valid(h->parent) && registry_.all_of<GlobalTransform>(h->parent))
  {
    const auto& pt = registry_.get<const GlobalTransform>(h->parent);
    gt.position    = pt.position + lt.position * pt.scale;

    gt.scale = lt.scale * pt.scale;

    gt.position -= pt.position;
    gt.position = glm::mat3_cast(pt.rotation) * gt.position;
    gt.position += pt.position;

    if (h->useLocalRotationAsGlobal)
    {
      gt.rotation = lt.rotation;
    }
    else
    {
      gt.rotation = pt.rotation * lt.rotation;
    }

    if (h->useLocalPositionAsGlobal)
    {
      gt.position = lt.position;
    }
  }

  if (!registry_.any_of<NoHashGrid>(entity))
  {
    globals->game->hashGrid->set(gt.position, entity);
  }

  if (!h)
  {
    return;
  }

  for (auto child : h->children)
  {
    UpdateLocalTransform(child, depth + 1);
  }
}

void World::SetParent(entt::entity child, entt::entity parent)
{
  ASSERT(registry_.valid(child));
  ASSERT(registry_.valid(parent));
  ASSERT(child != parent);
  SPDLOG_TRACE("[SetParent] Setting parent of {} to {}", entt::to_integral(child), entt::to_integral(parent));

  auto& h        = registry_.get<Hierarchy>(child);
  auto oldParent = h.parent;

  // Remove self from old parent
  if (h.parent != entt::null)
  {
    auto& ph = registry_.get<Hierarchy>(h.parent);
    ph.RemoveChild(child);
  }

  // Handle case of removing parent
  if (parent == entt::null)
  {
    h.parent = entt::null;
    if (parent != oldParent)
    {
      auto&& [gt, lt] = registry_.get<const GlobalTransform, LocalTransform>(child);
      lt.position     = gt.position;
      lt.rotation     = gt.rotation;
      lt.scale        = gt.scale;
      UpdateLocalTransform(child);
    }
    return;
  }

  // Add self to new parent
  h.parent = parent;
  auto& ph = registry_.get<Hierarchy>(parent);
  ph.AddChild(child);

  // Detect cycles in debug mode
  for ([[maybe_unused]] entt::entity cParent = parent; cParent != entt::null; cParent = registry_.get<const Hierarchy>(cParent).parent)
  {
    DEBUG_ASSERT(cParent != child);
  }

  UpdateLocalTransform(child);
}

void World::SpawnHitParticles(const SpawnHitParticlesParams& p)
{
  auto& reg = registry_;

  auto cube = Physics::Box({p.size, p.size, p.size});

  for (int i = 0; i < (int)p.numParticles; i++)
  {
    auto offset = glm::vec3(Rng().RandFloat(-0.125f, 0.125f), Rng().RandFloat(-0.125f, 0.125f), Rng().RandFloat(-0.125f, 0.125f));
    offset *= glm::equal(p.normal, glm::vec3(0)); // Zero out the component of the normal.
    auto e                    = CreateRenderableEntityNoHashGrid(p.position + offset + p.normal * p.size / 2.0f, glm::identity<glm::quat>(), p.size);
    reg.emplace<Mesh>(e).name = "cube";
    reg.emplace<Name>(e).name = "Debris";
    reg.emplace<Lifetime>(e).remainingSeconds = p.lifetime;
    reg.emplace<Physics::RigidBodySettings>(e, Physics::RigidBodySettings{.shape = cube, .layer = Physics::Layers::DEBRIS});
    reg.emplace<Tint>(e, p.tint);
    const auto velocity                         = Math::RandVecInCone({Rng().RandFloat(), Rng().RandFloat()}, p.normal, p.spreadConeAngle) * p.speed;
    reg.emplace_or_replace<LinearVelocity>(e).v = velocity;
  }
}

PCG::Rng& World::Rng()
{
  return globals->game->rng;
}
