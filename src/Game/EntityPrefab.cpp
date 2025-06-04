#include "EntityPrefab.h"

#include "Pathfinding.h"
#include "Game/World.h"
#include "Physics/Physics.h"
#include "shaders/Light.h.glsl" // "TEMP"

#include "Jolt/Physics/Constraints/DistanceConstraint.h"

entt::entity MeleeFrogDefinition::Spawn(World& world, glm::vec3 position, [[maybe_unused]] glm::quat rotation) const
{
  auto& registry = world.GetRegistry();
  auto sphere    = Physics::ShapeSettings{Physics::Sphere{0.4f}};

  auto e                         = world.CreateRenderableEntity(position, {1, 0, 0, 0}, 0.4f);
  registry.emplace<Mesh>(e).name = "frog";
  registry.emplace<Name>(e, "Frog");
  registry.emplace<Health>(e) = {100, 100};
  // registry_.emplace<SimpleEnemyBehavior>(e);
  registry.emplace<SimplePathfindingEnemyBehavior>(e);
  registry.emplace<Pathfinding::CachedPath>(e).timeBetweenUpdates = 1;
  registry.emplace<InputState>(e);
  registry.emplace<Loot>(e).name = "standard";
  registry.emplace<TeamFlags>(e, TeamFlagBits::ENEMY);
  registry.emplace<DespawnWhenFarFromPlayer>(e);
  registry.emplace<Enemy>(e);
  registry.emplace<AiVision>(e);
  registry.emplace<AiHearing>(e);
  registry.emplace<AiTarget>(e);
  registry.emplace<AiWanderBehavior>(e);
  registry.emplace<WalkingMovementAttributes>(e) = {.runBaseSpeed = 3.5f, .runMaxSpeed = 3.5f};

  auto& contactDamage  = registry.emplace<ContactDamage>(e);
  contactDamage.damage = 10;
  // Physics::AddCharacterController({registry_, e}, {sphere});
  registry.emplace<Physics::CharacterControllerShrimpleSettings>(e,
    Physics::CharacterControllerShrimpleSettings{
      .shape = sphere,
    });
  // registry_.emplace<FlyingCharacterController>(e) = {.maxSpeed = 6, .acceleration = 25};
  registry.emplace_or_replace<LinearVelocity>(e);
  registry.emplace<Physics::RigidBodySettings>(e,
    Physics::RigidBodySettings{
      .shape         = sphere,
      .gravityFactor = 0,
      .layer         = Physics::Layers::CHARACTER,
    });

  auto e2                         = world.CreateRenderableEntity({1.0f, 0.3f, -0.8f}, {1, 0, 0, 0}, 1.5f);
  registry.emplace<Name>(e2).name = "Child";
  registry.emplace<Mesh>(e2).name = "ar15";
  world.SetParent(e2, e);

  // Make hitbox/hurtbox collider.
  auto eHitbox                         = registry.create();
  registry.emplace<Name>(eHitbox).name = "Frog hitbox";
  registry.emplace<ForwardCollisionsToParent>(eHitbox);
  registry.emplace<RenderTransform>(eHitbox);
  registry.emplace<PreviousGlobalTransform>(eHitbox);
  auto& tpHitbox                             = registry.emplace<LocalTransform>(eHitbox);
  tpHitbox.position                          = {};
  tpHitbox.rotation                          = glm::identity<glm::quat>();
  tpHitbox.scale                             = 1;
  registry.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
  registry.emplace<Hierarchy>(eHitbox);
  registry.emplace<Physics::RigidBodySettings>(eHitbox,
    Physics::RigidBodySettings{
      .shape      = Physics::Sphere{0.75f},
      .isSensor   = true,
      .motionType = JPH::EMotionType::Kinematic,
      .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
    });
  world.SetParent(eHitbox, e);

  return e;
}

entt::entity FlyingFrogDefinition::Spawn(World& world, glm::vec3 position, [[maybe_unused]] glm::quat quat) const
{
  auto& registry = world.GetRegistry();

  auto e                         = world.CreateRenderableEntity(position, {1, 0, 0, 0}, 0.4f);
  registry.emplace<Mesh>(e).name = "frog";
  registry.emplace<Name>(e, "Flying Frog");
  registry.emplace<Tint>(e, glm::vec3{0.25, 0.05f, 1.0f});
  registry.emplace<Health>(e) = {70, 70};
  registry.emplace<PredatoryBirdBehavior>(e);
  // registry_.emplace<Pathfinding::CachedPath>(e).timeBetweenUpdates = 1;
  registry.emplace<InputState>(e);
  registry.emplace<Loot>(e).name = "standard";
  registry.emplace<TeamFlags>(e, TeamFlagBits::ENEMY);
  registry.emplace<DespawnWhenFarFromPlayer>(e);
  registry.emplace<Enemy>(e);

  auto& contactDamage  = registry.emplace<ContactDamage>(e);
  contactDamage.damage = 15;

  registry.emplace<FlyingCharacterController>(e) = {.maxSpeed = 3.5, .acceleration = 15};
  registry.emplace_or_replace<LinearVelocity>(e);
  registry.emplace<Physics::RigidBodySettings>(e,
    Physics::RigidBodySettings{
      .shape         = Physics::Sphere{0.4f},
      .gravityFactor = 0,
      .layer         = Physics::Layers::CHARACTER,
    });

  // Make hitbox/hurtbox collider.
  auto eHitbox                         = registry.create();
  registry.emplace<Name>(eHitbox).name = "Frog hitbox";
  registry.emplace<ForwardCollisionsToParent>(eHitbox);
  registry.emplace<RenderTransform>(eHitbox);
  registry.emplace<PreviousGlobalTransform>(eHitbox);
  auto& tpHitbox                             = registry.emplace<LocalTransform>(eHitbox);
  tpHitbox.position                          = {};
  tpHitbox.rotation                          = glm::identity<glm::quat>();
  tpHitbox.scale                             = 1;
  registry.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
  registry.emplace<Hierarchy>(eHitbox);
  registry.emplace<Physics::RigidBodySettings>(eHitbox,
    Physics::RigidBodySettings{
      .shape      = Physics::Sphere{0.95f},
      .isSensor   = true,
      .motionType = JPH::EMotionType::Kinematic,
      .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
    });
  world.SetParent(eHitbox, e);

  return e;
}

entt::entity WormBossDefinition::Spawn(World& world, glm::vec3 position, [[maybe_unused]] glm::quat quat) const
{
  auto& registry             = world.GetRegistry();
  entt::entity head          = entt::null;
  auto prevBody2             = std::optional<JPH::BodyID>();
  entt::entity prevEntity    = entt::null;
  constexpr float WORM_SCALE = 1.0f;
  for (int i = 0; i < 30; i++)
  {
    auto a = world.CreateRenderableEntity(position + glm::vec3{0, 0, 2 * WORM_SCALE * i}, glm::identity<glm::quat>(), i == 0 ? WORM_SCALE : 1.0f);
    registry.emplace<Name>(a).name = i == 0 ? "Worm head" : "Worm body";
    registry.emplace<Mesh>(a).name = "frog";
    registry.emplace<Tint>(a, glm::vec3{1, 0, 0});
    auto body = JPH::BodyID();

    registry.emplace<Physics::RigidBodySettings>(a,
      Physics::RigidBodySettings{
        .shape      = Physics::ShapeSettings{Physics::Sphere{WORM_SCALE}, 100000.0f / (1000 * i + 1.0f)},
        .activate   = true,
        .isSensor   = true,
        .motionType = i == 0 ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
        .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
      });

    const auto& rb = registry.get<Physics::RigidBody>(a);
    body           = rb.body;

    Physics::GetBodyInterface().SetGravityFactor(rb.body, 0.0f);

    registry.emplace<Friction>(a, glm::vec3(i == 0 ? 5.0f : 0.2f));

    if (i == 0)
    {
      head = a;

      registry.emplace<FlyingCharacterController>(a)               = {.maxSpeed = 38, .acceleration = 55.0f};
      registry.emplace<WormEnemyBehavior>(a).maxTurnSpeedDegPerSec = 65;
      registry.emplace<InputState>(a);
      registry.emplace<ContactDamage>(a) = {.damage = 20, .knockback = 5};
      registry.emplace<TeamFlags>(a, TeamFlagBits::ENEMY);
      registry.emplace<Health>(a)                     = {500, 500};
      registry.emplace<Loot>(a).name                  = "worm";
      registry.emplace<KnockbackMultiplier>(a).factor = 0.5f;
      registry.emplace<DespawnWhenFarFromPlayer>(a);
      registry.emplace<Enemy>(a);
    }

    if (prevBody2)
    {
      registry.emplace<ForwardCollisionsToParent>(a);
      auto prevPos = registry.get<GlobalTransform>(a).position;
      // auto settings = JPH::Ref(new JPH::SwingTwistConstraintSettings);
      //// settings->mPosition1           = settings->mPosition2  = Physics::ToJolt(position) + JPH::Vec3(-0.5f, 0, 0);
      // settings->mPosition1 = settings->mPosition2 = Physics::ToJolt((prevPos + position) / 2.0f);
      // settings->mTwistAxis1 = settings->mTwistAxis2 = JPH::Vec3::sAxisX();
      // settings->mPlaneAxis1 = settings->mPlaneAxis2 = JPH::Vec3::sAxisY();
      // settings->mNormalHalfConeAngle                = JPH::DegreesToRadians(30);
      // settings->mPlaneHalfConeAngle                 = JPH::DegreesToRadians(30);
      // settings->mTwistMinAngle                      = JPH::DegreesToRadians(-20);
      // settings->mTwistMaxAngle                      = JPH::DegreesToRadians(20);

      auto settings          = JPH::Ref(new JPH::DistanceConstraintSettings());
      settings->mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
      settings->mMinDistance = WORM_SCALE * 2;
      settings->mMaxDistance = WORM_SCALE * 2;

      // auto settings   = JPH::Ref(new JPH::FixedConstraintSettings());
      // settings->mAutoDetectPoint = true;

      auto constraint = Physics::GetBodyInterface().CreateConstraint(settings, *prevBody2, body);
      // constraint->SetNumPositionStepsOverride(10);
      Physics::RegisterConstraint(constraint, *prevBody2, body);

      auto& h                    = registry.get<Hierarchy>(a);
      h.useLocalPositionAsGlobal = true;
      h.useLocalRotationAsGlobal = true;
      world.SetParent(a, prevEntity);

      // auto hitboxShape                     = JPH::Ref(new JPH::SphereShape(WORM_SCALE));
      // auto eHitbox                         = registry.create();
      // registry.emplace<Name>(eHitbox).name = "Worm hitbox";
      // registry.emplace<ForwardCollisionsToParent>(eHitbox);
      // registry.emplace<PreviousGlobalTransform>(eHitbox);
      // auto& tpHitbox                             = registry.emplace<LocalTransform>(eHitbox);
      // tpHitbox.position                          = {};
      // tpHitbox.rotation                          = glm::identity<glm::quat>();
      // tpHitbox.scale                             = 1;
      // registry.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
      // registry.emplace<Hierarchy>(eHitbox);
      // Physics::AddRigidBody({registry, eHitbox},
      //   {
      //     .shape      = hitboxShape,
      //     .isSensor   = true,
      //     .motionType = JPH::EMotionType::Kinematic,
      //     .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
      //   });
      // SetParent({registry, eHitbox}, a);
    }
    prevBody2  = body;
    prevEntity = a;
  }
  return head;
}

entt::entity TorchDefinition::Spawn(World& world, glm::vec3 position, glm::quat rotation) const
{
  auto& registry    = world.GetRegistry();
  const auto entity = world.CreateRenderableEntity(position, rotation);
  registry.emplace<Mesh>(entity, "torch");
  registry.emplace<Name>(entity, "Torch");
  auto& light     = registry.emplace<GpuLight>(entity);
  light.type      = LIGHT_TYPE_POINT;
  light.color     = {1, 0.58f, 0.3f};
  light.intensity = 15;
  light.range     = 200;
  return entity;
}

entt::entity ChestDefinition::Spawn(World& world, glm::vec3 position, glm::quat rotation) const
{
  auto& registry    = world.GetRegistry();
  const auto entity = world.CreateRenderableEntity(position, rotation);
  registry.emplace<Name>(entity, "Chest");
  registry.emplace<Inventory>(entity).canHaveActiveItem = false;
  return entity;
}

entt::entity ShrimpleMeshPrefabDefinition::Spawn(World& world, glm::vec3 position, glm::quat rotation) const
{
  auto& registry    = world.GetRegistry();
  const auto entity = world.CreateRenderableEntity(position, rotation);
  registry.emplace<Mesh>(entity, modelName_);
  registry.emplace<Name>(entity, modelName_);
  registry.emplace<Tint>(entity, tint_);
  return entity;
}
