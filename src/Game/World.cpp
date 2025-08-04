#include "World.h"

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

#include "FastNoise/FastNoise.h"
#include "tracy/Tracy.hpp"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Constraints/SwingTwistConstraint.h"

#include <execution>
#include <stack>

std::optional<glm::vec3> SampleWalkablePosition(const TwoLevelGrid& grid, PCG::Rng& rng, glm::vec3 origin, float minDistance, float maxDistance, bool isAirWalkable)
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

      Physics::GetBodyInterface().SetGravityFactor(rb.body, 1);
      Physics::GetBodyInterface().SetMotionQuality(rb.body, JPH::EMotionQuality::LinearCast);
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
        auto constraint = Physics::GetBodyInterface().CreateConstraint(settings, *prevBody2, body);
        // constraint->SetNumPositionStepsOverride(100);
        Physics::RegisterConstraint(constraint, *prevBody2, body);
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

void World::FixedUpdate(float dt)
{
  ZoneScoped;
  if (registry_.ctx().get<GameState>() == GameState::GAME || IsClient())
  {
    registry_.ctx().get<float>("time"_hs) += dt;
#ifndef GAME_HEADLESS
    registry_.ctx().get<std::vector<Debug::Line>>().clear();
#endif

    registry_.ClearModifiedComponents();

    ASSERT(registry_.view<const LocalPlayer>().size() <= 1);

    // Update previous transforms before updating it (this should be done after updating the game state from networking)
    for (auto&& [entity, transform, previousTransform] : registry_.view<const GlobalTransform, const PreviousGlobalTransform>().each())
    {
      if (transform.position != previousTransform.position || transform.rotation != previousTransform.rotation || transform.scale != previousTransform.scale)
      {
        auto& previousTransformMut    = registry_.get<PreviousGlobalTransform>(entity);
        previousTransformMut.position = transform.position;
        previousTransformMut.rotation = transform.rotation;
        previousTransformMut.scale    = transform.scale;
      }
    }

    for (auto entity : registry_.view<const LocalPlayer>())
    {
      UpdateLocalTransform(entity);
    }

    if (IsServer() && registry_.ctx().get<bool>("UpdateNPCSpawnDirector"_hs))
    {
      registry_.ctx().get<NpcSpawnDirector>().Update(dt);
    }

    if (IsServer())
    {
      auto& sunInfo = registry_.ctx().get<SunInfo>();
      if (!sunInfo.pauseDayNightCycle)
      {
        sunInfo.timeOfDay += dt / sunInfo.dayLength * 2;
        sunInfo.timeOfDay = glm::mod(sunInfo.timeOfDay, 2.0f);
      }
    }

    Physics::FixedUpdate(dt, *this);

    // Clamp movement input
    for (auto&& [entity, input] : registry_.view<InputState>().each())
    {
      input.strafe  = glm::clamp(input.strafe, -1.0f, 1.0f);
      input.forward = glm::clamp(input.forward, -1.0f, 1.0f);
      input.elevate = glm::clamp(input.elevate, -1.0f, 1.0f);
    }

    // Apply status effects
    if (IsServer())
    {
      for (auto&& [entity, health] : registry_.view<Health>().each())
      {
        if (Item::GetTotalEffectOnEntity(*this, entity, Item::EffectType::HealthRegeneration, 0) >= 1)
        {
          health.hp = glm::min(health.maxHp, health.hp + 4 * dt);
        }
      }

      for (auto&& [entity, transform, player] : registry_.view<const GlobalTransform, const Player>().each())
      {
        if (Item::GetTotalEffectOnEntity(*this, entity, Item::EffectType::Shine, 0) >= 1)
        {
          auto& light     = registry_.emplace_or_replace<GpuLight>(entity);
          light.color     = {1.0f, 0.4f, 0.2f};
          light.intensity = 500;
          light.type      = LIGHT_TYPE_POINT;
          light.range     = 200;
        }
        else
        {
          registry_.remove<GpuLight>(entity);
        }
      }
    }

    // Process linear transform paths
    // if (IsServer())
    {
      for (auto&& [entity, linearPath, transform] : registry_.view<LinearPath, LocalTransform>().each())
      {
        if (!IsServer() && !AncestorHasComponent<LocalAuthoritative>(entity))
        {
          continue;
        }

        ASSERT(!linearPath.frames.empty());
        if (linearPath.secondsElapsed <= 0)
        {
          linearPath.originalLocalTransform = transform;
        }

        linearPath.secondsElapsed += dt;

        // See if the path is finished, reset original transform
        {
          float sum = 0;
          for (const auto& frame : linearPath.frames)
          {
            sum += frame.offsetSeconds;
          }
          if (linearPath.secondsElapsed > sum)
          {
            transform = linearPath.originalLocalTransform;
            registry_.remove<LinearPath>(entity);
            continue;
          }
        }

        // Locate frames to interpolate between with simple linear search
        LinearPath::KeyFrame firstFrame;
        LinearPath::KeyFrame secondFrame = {};
        float sum                        = 0;
        for (const auto& frame : linearPath.frames)
        {
          firstFrame  = secondFrame;
          secondFrame = frame;
          sum += frame.offsetSeconds;
          if (sum >= linearPath.secondsElapsed)
          {
            break;
          }
        }

        // Do da interpolate
        const float alpha = Math::Ease((linearPath.secondsElapsed - firstFrame.offsetSeconds) / (sum - firstFrame.offsetSeconds), secondFrame.easing);
        const glm::vec3 newRelativePosition = glm::mix(firstFrame.position, secondFrame.position, alpha);
        const glm::quat newRelativeRotation = glm::slerp(firstFrame.rotation, secondFrame.rotation, alpha);
        const float newRelativeScale        = glm::mix(firstFrame.scale, secondFrame.scale, alpha);

        // Apply new relative transform stuff relatively to the original transform
        transform.position = linearPath.originalLocalTransform.position + newRelativePosition;
        transform.rotation = linearPath.originalLocalTransform.rotation * newRelativeRotation;
        transform.scale    = linearPath.originalLocalTransform.scale * newRelativeScale;

        UpdateLocalTransform(entity);
      }
    }

    // Avians
    if (IsServer())
    {
      for (auto&& [entity, input, transform, behavior] : registry_.view<InputState, LocalTransform, PredatoryBirdBehavior>().each())
      {
        behavior.accum += dt;

        const auto nearestPlayer = GetNearestPlayer(transform.position);

        if (nearestPlayer != entt::null)
        {
          const auto& pt = registry_.get<const GlobalTransform>(nearestPlayer);
          if (glm::distance(pt.position, transform.position) < 20)
          {
            behavior.target = nearestPlayer;

            if (behavior.state == PredatoryBirdBehavior::State::IDLE)
            {
              behavior.state = PredatoryBirdBehavior::State::CIRCLING;
              behavior.accum = 0;
            }
          }
          else if (behavior.state != PredatoryBirdBehavior::State::IDLE || behavior.accum < 0.2f) // Hack to prevent idle position from being jacked up when spawning
          {
            behavior.state        = PredatoryBirdBehavior::State::IDLE;
            behavior.idlePosition = transform.position + glm::vec3(0, 0, 0);
          }
        }
        else if (behavior.state != PredatoryBirdBehavior::State::IDLE)
        {
          behavior.state        = PredatoryBirdBehavior::State::IDLE;
          behavior.idlePosition = transform.position + glm::vec3(0, 0, 0);
        }

        // Birds always be moving forward.
        input.forward = 1;

        switch (behavior.state)
        {
        case PredatoryBirdBehavior::State::IDLE:
        {
          const auto target = behavior.idlePosition + glm::vec3(sin(behavior.accum * 1.2f) * 8, 2 + sin(behavior.accum * 4) * 2, cos(behavior.accum * 1.2f) * 8);
          transform.rotation = glm::quatLookAtRH(glm::normalize(target - transform.position), {0, 1, 0});
          input.forward      = 0.5f;
          break;
        }
        case PredatoryBirdBehavior::State::CIRCLING:
        {
          const auto& pt    = registry_.get<const GlobalTransform>(behavior.target);
          const auto target = pt.position + glm::vec3(sin(behavior.accum * 1.2f) * 8, 4 + sin(behavior.accum * 4) * 2, cos(behavior.accum * 1.2f) * 8);

          transform.rotation = glm::quatLookAtRH(glm::normalize(target - transform.position), {0, 1, 0});

          auto rayCast   = JPH::RRayCast(Physics::ToJolt(transform.position), Physics::ToJolt(target - transform.position));
          auto result    = JPH::RayCastResult();
          const bool hit = Physics::GetNarrowPhaseQuery().CastRay(rayCast,
            result,
            Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_WORLD),
            Physics::GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_WORLD));
          if (!hit)
          {
            behavior.lineOfSightDuration += dt;
            if (behavior.accum >= 5.0f && behavior.lineOfSightDuration >= 1)
            {
              behavior.accum = 0;
              behavior.state = PredatoryBirdBehavior::State::SWOOPING;
            }
          }
          else
          {
            behavior.lineOfSightDuration = 0;
          }
          break;
        }
        case PredatoryBirdBehavior::State::SWOOPING:
        {
          const auto& pt     = registry_.get<const GlobalTransform>(behavior.target);
          transform.rotation = glm::quatLookAtRH(glm::normalize(pt.position - transform.position), {0, 1, 0});

          if (glm::distance(pt.position, transform.position) < 1.5f || behavior.accum > 5.0f)
          {
            behavior.accum = 0;
            behavior.state = PredatoryBirdBehavior::State::CIRCLING;
          }

          break;
        }
        default:;
        }

        UpdateLocalTransform(entity);
      }
    }

    // Reset AI targets if target is invalid or dead.
    if (IsServer())
    {
      for (auto&& [entity, target] : registry_.view<AiTarget>().each())
      {
        if (!registry_.valid(target.currentTarget) || registry_.all_of<GhostPlayer>(target.currentTarget))
        {
          target.currentTarget = entt::null;
        }
      }
    }

    // Generate input for enemies
    if (IsServer())
    {
      ZoneScopedN("Pathfinding");
      // Won't work if entity is a child.
      for (auto&& [entity, input, aiTransform] : registry_.view<InputState, LocalTransform>(entt::exclude<Player>).each())
      {
        entt::entity pe           = entt::null;
        const GlobalTransform* pt = nullptr;
        float nearestDist2        = INFINITY;

        // Try to find the nearest player that satisfies target conditions.
        bool sawSomething = false;
        for (auto&& [pEntity, player, playerTransform] : registry_.view<const Player, const GlobalTransform>(entt::exclude<GhostPlayer>).each())
        {
          bool isCandidate = false;
          if (!registry_.any_of<AiVision, AiHearing>(entity))
          {
            isCandidate = true;
          }

          const auto dist2 = Math::Distance2(playerTransform.position, aiTransform.position);
          if (auto* aiv = registry_.try_get<AiVision>(entity))
          {
            // TODO: shoot visibility ray(s).
            if (dist2 < aiv->distance * aiv->distance &&
                glm::acos(glm::dot(glm::normalize(playerTransform.position - aiTransform.position), GetForward(aiTransform.rotation))) < aiv->coneAngleRad)
            {
              aiv->accumulator += dt;
              sawSomething = true;
              if (aiv->accumulator > aiv->invAcuity)
              {
                isCandidate = true;
              }
            }
          }

          if (auto* aih = registry_.try_get<AiHearing>(entity))
          {
            if (dist2 < aih->distance * aih->distance)
            {
              isCandidate = true;
            }
          }

          if (isCandidate && dist2 < nearestDist2)
          {
            nearestDist2 = dist2;
            pe           = pEntity;
            pt           = &playerTransform;
          }
        }

        if (auto* aiv = registry_.try_get<AiVision>(entity); aiv && !sawSomething)
        {
          aiv->accumulator = glm::max(aiv->accumulator - dt, 0.0f);
        }

        auto* aiTarget = registry_.try_get<AiTarget>(entity);

        const bool hasValidTarget = (aiTarget && registry_.valid(aiTarget->currentTarget)) || pe != entt::null;

        // Update aiTarget if it didn't have a valid target.
        if (aiTarget && !registry_.valid(aiTarget->currentTarget) && pe != entt::null)
        {
          aiTarget->currentTarget = pe;
        }

        // If there was already a target, maintain it.
        if (aiTarget && registry_.valid(aiTarget->currentTarget))
        {
          pe = aiTarget->currentTarget;
          pt = registry_.try_get<const GlobalTransform>(pe);
        }

        if (hasValidTarget && registry_.all_of<SimpleEnemyBehavior>(entity))
        {
          if (pt->position.y > aiTransform.position.y)
          {
            input.jump = true;
          }

          aiTransform.rotation = glm::quatLookAtRH(glm::normalize(pt->position - aiTransform.position), {0, 1, 0});

          input.forward = 1;
        }

        if (auto* w = registry_.try_get<const WormEnemyBehavior>(entity); w && hasValidTarget)
        {
          const auto desiredRotation = glm::quatLookAtRH(glm::normalize(pt->position - aiTransform.position), {0, 1, 0});
          const auto angle           = glm::acos(glm::dot(GetForward(desiredRotation), GetForward(aiTransform.rotation)));

          aiTransform.rotation = glm::slerp(aiTransform.rotation, desiredRotation, glm::min(1.0f, glm::radians(w->maxTurnSpeedDegPerSec * dt) / angle));

          input.forward = 1;
        }

        auto* cp            = registry_.try_get<Pathfinding::CachedPath>(entity);
        bool shouldFindPath = true;
        if (cp)
        {
          cp->updateAccum += dt;
          if (cp->updateAccum >= cp->timeBetweenUpdates)
          {
            cp->updateAccum = 0;
            cp->progress    = 0;
          }
          else
          {
            shouldFindPath = false;
          }
        }

        Pathfinding::Path path;
        const auto myFootPos = glm::ivec3(glm::floor(GetFootPosition(entity)));
        const auto myHeight  = (int)std::ceil(GetHeight(entity));

        // Get cached path.
        if (cp)
        {
          path = cp->path;
        }

        if (hasValidTarget && registry_.any_of<SimplePathfindingEnemyBehavior>(entity))
        {
          if (!(cp && !shouldFindPath))
          {
            // For ground characters, cast the player down. That way, if the player is in the air, the character will at least try to get under them instead of giving up.
            auto targetFootPos = glm::ivec3(pt->position);
            if (const auto* pc = registry_.try_get<const Physics::CharacterController>(pe))
            {
              const auto& playerCharacter = pc->character;
              const auto* playerShape     = playerCharacter->GetShape();
              auto shapeCast              = JPH::RShapeCast::sFromWorldTransform(playerShape, {1, 1, 1}, playerCharacter->GetWorldTransform(), {0, -10, 0});
              auto shapeCastCollector     = Physics::NearestHitCollector();
              Physics::GetNarrowPhaseQuery().CastShape(shapeCast,
                {},
                {},
                shapeCastCollector,
                Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_WORLD));
              if (shapeCastCollector.nearest)
              {
                targetFootPos = Physics::ToGlm(shapeCast.GetPointOnRay(shapeCastCollector.nearest->mFraction - 1e-2f));
              }
            }

            // path                 = Pathfinding::FindPath(*this, {.start = myFootPos, .goal = targetFootPos, .height = myHeight, .w = 1.5f});
            path = registry_.ctx().get<Pathfinding::PathCache>().FindOrGetCachedPath(*this,
              {
                .start  = glm::ivec3(myFootPos),
                .goal   = glm::ivec3(targetFootPos),
                .height = myHeight,
                .w      = 1.5f,
                .canFly = registry_.any_of<FlyingCharacterController>(entity),
              });

            if (cp)
            {
              cp->path = path;
            }
          }
        }

        // Only try to wander if there's no valid target.
        if (auto* wb = registry_.try_get<AiWanderBehavior>(entity); wb && !hasValidTarget)
        {
          wb->accumulator += dt;
          if (wb->accumulator > wb->timeBetweenMoves)
          {
            wb->accumulator  = 0;
            const auto& grid = registry_.ctx().get<TwoLevelGrid>();
            auto& rng        = registry_.ctx().get<PCG::Rng>();
            for (int i = 0; i < 5; i++)
            {
              if (auto pos = SampleWalkablePosition(grid, rng, aiTransform.position, wb->minWanderDistance, wb->maxWanderDistance, wb->targetCanBeFloating))
              {
                path = registry_.ctx().get<Pathfinding::PathCache>().FindOrGetCachedPath(*this,
                  {
                    .start            = glm::ivec3(myFootPos),
                    .goal             = glm::ivec3(*pos),
                    .height           = myHeight,
                    .w                = 1.5f,
                    .canFly           = registry_.any_of<FlyingCharacterController>(entity),
                    .maxNodesToSearch = 100,
                  });

                if (cp)
                {
                  cp->path = path;
                }
                break;
              }
            }
          }
        }

        if (!path.empty())
        {
#ifndef GAME_HEADLESS
          // Render path
          if (registry_.ctx().get<Debugging>().drawPathLines)
          {
            auto& lines = registry_.ctx().get<std::vector<Debug::Line>>();
            for (size_t i = 1; i < path.size(); i++)
            {
              lines.emplace_back(Debug::Line{
                .aPosition = path[i - 1],
                .aColor    = glm::vec4(0, 0, 1, 1),
                .bPosition = path[i + 0],
                .bColor    = glm::vec4(0, 0, 1, 1),
              });
            }
          }
#endif

          auto nextNode = path.front();
          if (cp && cp->progress < path.size())
          {
            nextNode = path[cp->progress];
            if (cp->progress < path.size() - 1 && glm::distance(nextNode, glm::vec3(myFootPos)) <= 1.25f)
            {
              cp->progress++;
            }
          }

          if (cp && glm::distance(glm::vec3(myFootPos), cp->path.back()) < 1.0f)
          {
            cp->path.clear();
            cp->progress = 0;
          }

          if (nextNode.y > myFootPos.y + 0.5f)
          {
            input.jump = true;
          }
          aiTransform.rotation = glm::quatLookAtRH(glm::normalize(nextNode - aiTransform.position), {0, 1, 0});
          input.forward        = 1;
        }

        UpdateLocalTransform(entity);
      }
    }

    // Apply input (could be generated by players or NPCs!)
    // if (IsServer())
    {
      for (auto&& [entity, input, transform] : registry_.view<const InputState, LocalTransform>(entt::exclude<GhostPlayer>).each())
      {
        if (!IsServer() && !registry_.all_of<LocalAuthoritative>(entity))
        {
          continue;
        }
        // Movement
        if (registry_.all_of<const NoclipCharacterController>(entity))
        {
          const auto right     = GetRight(transform.rotation);
          const auto forward   = GetForward(transform.rotation);
          auto tempCameraSpeed = 14.5f;
          tempCameraSpeed *= input.sprint ? 4.0f : 1.0f;
          tempCameraSpeed *= input.walk ? 0.25f : 1.0f;
          auto velocity = glm::vec3(0);
          velocity += input.forward * forward * tempCameraSpeed;
          velocity += input.strafe * right * tempCameraSpeed;
          velocity.y += input.elevate * tempCameraSpeed;
          transform.position += velocity * dt;
          UpdateLocalTransform(entity);
          registry_.get_or_emplace<LinearVelocity>(entity).v = velocity;
        }

        if (auto* fc = registry_.try_get<const FlyingCharacterController>(entity))
        {
          auto& velocity     = registry_.get<LinearVelocity>(entity).v;
          const auto right   = GetRight(transform.rotation);
          const auto forward = GetForward(transform.rotation);
          const auto dv      = fc->acceleration * dt;

          velocity += input.forward * forward * dv;
          velocity += input.strafe * right * dv;
          velocity += input.elevate * glm::vec3(0, 1, 0) * dv;

          if (glm::length(velocity) > fc->maxSpeed)
          {
            velocity = glm::normalize(velocity) * fc->maxSpeed;
          }

          transform.position += velocity * dt;

          UpdateLocalTransform(entity);
        }

        if (auto* attribs = registry_.try_get<WalkingMovementAttributes>(entity);
          attribs && registry_.any_of<Physics::CharacterController, Physics::CharacterControllerShrimple>(entity))
        {
          const auto rot   = glm::mat3_cast(transform.rotation);
          const auto right = rot[0];
          const auto gUp   = glm::vec3(0, 1, 0);
          // right and up will never be collinear if roll doesn't change
          const auto forward = glm::normalize(glm::cross(gUp, right));

          // Physics engine factors in deltaTime already
          float tempSpeed = attribs->acceleration * dt;
          tempSpeed *= input.walk ? attribs->walkModifier : 1.0f;

          auto deltaVelocity = glm::vec3(0);
          deltaVelocity.y = attribs->gravity * dt;
          attribs->timeSinceJumped += dt;

          if (auto* cc = registry_.try_get<const Physics::CharacterController>(entity))
          {
            auto& velocity          = registry_.get<LinearVelocity>(entity).v;
            const auto realVelocity = (transform.position - cc->previousPosition) / dt;
            velocity                = glm::mix(realVelocity, velocity, glm::lessThan(glm::abs(velocity), glm::abs(realVelocity)));

            const bool isOnGround = cc->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            deltaVelocity +=
              input.forward * forward * (isOnGround ? attribs->acceleration : attribs->airAcceleration) * (input.walk ? attribs->walkModifier : 1.0f) * dt;
            deltaVelocity +=
              input.strafe * right * (isOnGround ? attribs->acceleration : attribs->airAcceleration) * (input.walk ? attribs->walkModifier : 1.0f) * dt;

            auto* emitter = registry_.try_get<SoundEmitter>(entity);
            if (isOnGround && glm::length(glm::vec2(velocity.x, velocity.z)) >= attribs->runMaxSpeed * 0.5f)
            {
              if (!emitter || emitter->handle.expired())
              {
                registry_.emplace_or_replace<SoundEmitter>(entity,
                  SoundEmitter{GetAudio()->PlaySound({
                    .name        = "walk",
                    .volume      = 1,
                    .minDistance = 3,
                    .isLooping   = true,
                    .delay       = 0.1f,
                    .position    = registry_.all_of<LocalPlayer>(entity) ? std::nullopt : std::optional(transform.position),
                  })});
              }
            }
            else if (emitter && !emitter->handle.expired())
            {
              emitter->handle.lock()->SetIsLooping(false);
            }

            if (isOnGround && cc->previousGroundState != JPH::CharacterBase::EGroundState::OnGround && velocity.y < -1.0f)
            {
              GetAudio()->PlaySound({
                .name        = "walk",
                .minDistance = 3,
                .pitch       = 0.8f,
                .position    = registry_.all_of<LocalPlayer>(entity) ? std::nullopt : std::optional(transform.position),
              });
            }

            if (isOnGround)
            {
              if (input.jump)
              {
                GetAudio()->PlaySound({
                  .name        = "jump",
                  .volume      = 0.4f,
                  .minDistance = 3,
                  .pitch       = 0.8f,
                  .position    = registry_.all_of<LocalPlayer>(entity) ? std::nullopt : std::optional(transform.position),
                });
                velocity.y      = Item::GetTotalEffectOnEntity(*this, entity, Item::EffectType::JumpImpulseModifier, attribs->jumpInitialImpulse);
                deltaVelocity.y = 0;
              }
              attribs->timeSinceJumped = 0;
            }
            if (input.jump && attribs->timeSinceJumped < attribs->jumpControlTime)
            {
              velocity.y += attribs->jumpAcceleration * dt;
            }

            // Reduce acceleration from normal input if it would make the character go over its max speed.
            auto deltaXZ1           = glm::vec2(deltaVelocity.x, deltaVelocity.z);
            auto xzVel              = glm::vec2(velocity.x, velocity.z);
            const auto xzSpeed      = glm::length(xzVel);
            const auto baseMaxSpeed = attribs->runMaxSpeed * (input.walk ? attribs->walkModifier : 1.0f);
            const auto realMaxSpeed = Item::GetTotalEffectOnEntity(*this, entity, Item::EffectType::MovementSpeedModifier, baseMaxSpeed);
            if (glm::length(deltaXZ1 + xzVel) > realMaxSpeed)
            {
              const auto nextXZVel = glm::normalize(deltaXZ1 + xzVel) * glm::max(realMaxSpeed, xzSpeed);
              deltaXZ1             = nextXZVel - xzVel;
              deltaVelocity.x      = deltaXZ1[0];
              deltaVelocity.z      = deltaXZ1[1];
            }

            // Decelerate when no forward input is present.
            const auto velocityXZ = glm::vec2(velocity.x, velocity.z);
            const auto forwardXZ  = glm::normalize(glm::vec2(forward.x, forward.z));
            auto forwardVel       = forwardXZ * glm::dot(velocityXZ, forwardXZ);
            if (input.forward == 0 && glm::length(forwardVel) > 1e-3f)
            {
              auto deltaXZ = glm::vec2(deltaVelocity.x, deltaVelocity.z);
              auto offs    = -glm::normalize(forwardVel) * (isOnGround && !input.jump ? attribs->deceleration : attribs->airDeceleration) * dt;
              if (glm::dot(offs + forwardVel, forwardVel) < 0)
              { 
                offs = -forwardVel;
              }
              deltaXZ += offs;
              deltaVelocity.x = deltaXZ[0];
              deltaVelocity.z = deltaXZ[1];
            }

            // Decelerate when no strafe input is present.
            const auto rightXZ = glm::normalize(glm::vec2(right.x, right.z));
            auto rightVel      = rightXZ * glm::dot(velocityXZ, rightXZ);
            if (input.strafe == 0 && glm::length(rightVel) > 1e-3f)
            {
              auto deltaXZ = glm::vec2(deltaVelocity.x, deltaVelocity.z);
              auto offs    = -glm::normalize(rightVel) * (isOnGround && !input.jump ? attribs->deceleration : attribs->airDeceleration) * dt;
              if (glm::dot(offs + rightVel, rightVel) < 0)
              {
                offs = -rightVel;
              }
              deltaXZ += offs;
              deltaVelocity.x = deltaXZ[0];
              deltaVelocity.z = deltaXZ[1];
            }
            
            velocity += deltaVelocity;

            // Apply penalty force if going over max speed.
            auto xzVel2 = glm::vec2(velocity.x, velocity.z);
            const auto xzSpeed2 = glm::length(xzVel2);
            if (xzSpeed2 > realMaxSpeed)
            {
              xzVel2 += -glm::normalize(xzVel2) * (isOnGround && !input.jump ? attribs->deceleration : attribs->airDeceleration) * dt;
              // Set speed to max if this penalty would put us under it.
              if (glm::length(xzVel2) < realMaxSpeed)
              {
                xzVel2 = glm::normalize(xzVel2) * realMaxSpeed;
              }
              // Reverse direction if penalty would make us go backwards (this can happen when the max speed is very low relative to dt).
              if (glm::dot(glm::vec2(velocity.x, velocity.z), xzVel2) < 0)
              {
                xzVel2 *= -1;
              }
              velocity.x = xzVel2[0];
              velocity.z = xzVel2[1];
            }

            velocity.y = glm::max(velocity.y, attribs->terminalVelocity);
          }
          else
          {
            deltaVelocity += input.forward * forward * tempSpeed;
            deltaVelocity += input.strafe * right * tempSpeed;
          }

          if (auto* cs = registry_.try_get<const Physics::CharacterControllerShrimple>(entity))
          {
            auto velocity                 = registry_.get<LinearVelocity>(entity).v;
            auto& friction                = registry_.get_or_emplace<Friction>(entity).axes;
            constexpr auto groundFriction = glm::vec3(6.0f);
            friction                      = groundFriction;
            if (cs->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround)
            {
              deltaVelocity += input.jump ? gUp * 8.0f : glm::vec3(0);
              // Make it possible to launch entities up with an impulse
              if (cs->previousGroundState == JPH::CharacterBase::EGroundState::OnGround)
              {
                velocity.y = 0;
              }
            }
            else
            {
              constexpr float airControl = 0.4f;
              constexpr auto airFriction = glm::vec3(0.05f);
              friction                   = airFriction;
              deltaVelocity.x *= airControl;
              deltaVelocity.z *= airControl;
              // const auto prevY = cs->character->GetLinearVelocity().GetY();
              // deltaVelocity += glm::vec3{0, prevY - 15 * dt, 0};
              deltaVelocity += glm::vec3{0, -15 * dt, 0};
            }
            friction.y = 0;

            // Apply friction
            auto newVelocity = glm::vec3(velocity.x, 0, velocity.z);
            // newVelocity -= friction * newVelocity * dt;

            // Apply dv
            newVelocity.x += deltaVelocity.x;
            newVelocity.z += deltaVelocity.z;

            // Clamp xz speed
            const float speed = glm::length(newVelocity);
            if (speed > attribs->runMaxSpeed)
            {
              newVelocity = glm::normalize(newVelocity) * attribs->runMaxSpeed;
            }

            // Y is not affected by speed clamp or friction
            newVelocity.y = velocity.y + deltaVelocity.y;

            registry_.get<LinearVelocity>(entity).v = newVelocity;
          }
        }
      }
    }

    // Close open containers if too far away.
    if (IsServer())
    {
      for (auto&& [entity, player, transform] : registry_.view<Player, const GlobalTransform>().each())
      {
        if (registry_.valid(player.openContainerId))
        {
          player.showInteractPrompt = false;
          if (!registry_.all_of<Inventory>(player.openContainerId))
          {
            player.openContainerId = entt::null;
            continue;
          }

          if (auto* ct = registry_.try_get<const GlobalTransform>(player.openContainerId); ct && glm::distance(transform.position, ct->position) > 6)
          {
            player.openContainerId = entt::null;
            continue;
          }
        }
        else
        {
          player.openContainerId = entt::null;
        }
      }
    }

    // Player interaction
    // if (IsServer())
    {
      for (auto&& [entity, player, transform, input, inventory] :
        registry_.view<Player, const GlobalTransform, const InputState, Inventory>(entt::exclude<GhostPlayer>).each())
      {
        const auto forward         = GetForward(transform.rotation);
        constexpr float RAY_LENGTH = 4.0f;
        auto rayCast               = JPH::RRayCast(Physics::ToJolt(transform.position), Physics::ToJolt(forward * RAY_LENGTH));
        entt::entity hitEntity     = entt::null;

        auto result = JPH::RayCastResult();
        if (Physics::GetNarrowPhaseQuery().CastRay(rayCast,
              result,
              Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_PROJECTILE),
              Physics::GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_PROJECTILE),
              *Physics::GetIgnoreEntityAndChildrenFilter({registryOld_, entity})))
        {
          hitEntity = static_cast<entt::entity>(Physics::GetBodyInterface().GetUserData(result.mBodyID));
          if (registry_.valid(hitEntity))
          {
            // Ray actually hit a voxel... see if it hit a voxel with a corresponding block entity.
            if (registry_.all_of<VoxelsComponent>(hitEntity))
            {
              const auto hitPos      = transform.position + forward * (result.mFraction * RAY_LENGTH + 1e-3f);
              const auto voxelHitPos = glm::ivec3(hitPos);
              if (auto e2 = GetBlockEntity(voxelHitPos); e2 != entt::null)
              {
                hitEntity = e2;
              }
            }

            if (auto [e3, _] = GetComponentFromAncestorOrDescendant<Inventory>(hitEntity); e3 != entt::null)
            {
              hitEntity                 = e3;
              player.showInteractPrompt = true;
            }
            else
            {
              player.showInteractPrompt = false;
            }
          }
        }
        else
        {
          player.showInteractPrompt = false;
        }

        if (input.interact)
        {
          if (registry_.valid(hitEntity))
          {
            if (auto [ent, inv] = GetComponentFromAncestor<Inventory>(hitEntity); inv)
            {
              player.openContainerId = ent;
            }
          }
        }

        if (IsServer() && input.usePrimary)
        {
          if (inventory.ActiveSlot().id != entt::null)
          {
            Item::UsePrimary(*this, dt, inventory.activeSlotEntity, inventory.ActiveSlot());
            if (inventory.ActiveSlot().count <= 0)
            {
              inventory.OverwriteSlot(*this, inventory.activeSlotCoord, {}, entt::null);
            }
          }
        }
      }
    }

    // Update items in inventories (important to ensure cooldowns, etc. reset even when items are put away).
    if (IsServer())
    {
      for (auto&& [entity, player, inventory] : registry_.view<const Player, Inventory>(entt::exclude<GhostPlayer>).each())
      {
        for (size_t row = 0; row < inventory.height; row++)
        {
          for (size_t col = 0; col < inventory.width; col++)
          {
            auto& slot = inventory.slots[row][col];
            if (slot.id != entt::null)
            {
              entt::entity self = entt::null;
              if (inventory.activeSlotCoord == glm::ivec2{row, col})
              {
                self = inventory.activeSlotEntity;
              }
              Item::Update(*this, dt, self, slot);
            }
          }
        }
      }
    }

    if (IsServer())
    {
      for (auto&& [entity, cannotPickUp] : registry_.view<CannotBePickedUp>().each())
      {
        cannotPickUp.remainingSeconds -= dt;
        if (cannotPickUp.remainingSeconds <= 0)
        {
          registry_.remove<CannotBePickedUp>(entity);
        }
      }
    }

    // Dropped items get sucked towards the player as if by magnetic attraction.
    if (IsServer())
    {
      for (auto&& [entity, player, transform] : registry_.view<const Player, const GlobalTransform>(entt::exclude<GhostPlayer>).each())
      {
        // for (auto nearEntity : this->GetEntitiesInSphere(transform.position, 2))
        for (auto nearEntity : this->GetEntitiesInCapsule(transform.position - glm::vec3(0, 1.5f, 0), transform.position + glm::vec3(0, 1.5f, 0), 2))
        {
          if (registry_.all_of<DroppedItem>(nearEntity) && !registry_.any_of<CannotBePickedUp>(nearEntity))
          {
            auto itemPos      = registry_.get<const GlobalTransform>(nearEntity).position;
            auto dist         = glm::distance(transform.position, itemPos);
            auto itemToPlayer = glm::normalize(transform.position - itemPos);

            auto& velocity = registry_.get<LinearVelocity>(nearEntity).v;
            velocity += 300 * dt * itemToPlayer / (dist * dist);
            const auto speed         = glm::length(velocity);
            constexpr float maxSpeed = 10;
            if (speed > maxSpeed)
            {
              velocity = velocity / speed * maxSpeed;
            }
          }
        }
      }
    }

    if (IsClient())
    {
      // Recursively mark entities in hierarchies as owned.
      // auto ownedEntities = std::stack<entt::entity>();
      // for (auto entity : registry_.view<const LocalAuthoritative>())
      //{
      //  ownedEntities.push(entity);
      //}
      // while (!ownedEntities.empty())
      //{
      //  auto entity = ownedEntities.top();
      //  ownedEntities.pop();

      //  if (auto* h = registry_.try_get<const Hierarchy>(entity))
      //  {
      //    for (auto child : h->children)
      //    {
      //      registry_.emplace_or_replace<LocalAuthoritative>(child);
      //      ownedEntities.push(child);
      //    }
      //  }
      //}

      for (auto entity : registry_.view<LocalAuthoritative>())
      {
        if (auto* transform = registry_.try_get<const LocalTransform>(entity))
        {
          Networking::CallRPC("UpdateTransformRPC"_hs, *this, entity, *transform);
        }
      }
    }

    // Update status effects
    if (IsServer())
    {
      for (auto&& [entity, effects] : registry_.view<TemporaryEffects>().each())
      {
        std::erase_if(effects.effects,
          [dt](ItemState& effect)
          {
            if ((effect.useAccum -= dt) <= 0)
            {
              return true;
            }
            return false;
          });
      }
    }

    // Tick down ghost players
    if (IsServer())
    {
      for (auto&& [entity, ghost, player] : registry_.view<GhostPlayer, const Player>().each())
      {
        ghost.remainingSeconds -= dt;

        if (ghost.remainingSeconds <= 0)
        {
          RespawnPlayer(entity);
        }
      }
    }

    // Tick down invulnerability
    if (IsServer())
    {
      for (auto&& [entity, invulnerability] : registry_.view<Invulnerability>().each())
      {
        invulnerability.remainingSeconds -= dt;
        if (invulnerability.remainingSeconds <= 0)
        {
          registry_.remove<Invulnerability>(entity);
        }
      }
    }

    // Tick down per-entity immunity.
    if (IsServer())
    {
      for (auto&& [entity, cannotDamage] : registry_.view<CannotDamageEntities>().each())
      {
        for (auto it = cannotDamage.entities.begin(); it != cannotDamage.entities.end();)
        {
          auto& [e, time] = *it;
          time -= dt;
          if (time <= 0)
          {
            cannotDamage.entities.erase(it++);
          }
          else
          {
            ++it;
          }
        }
      }
    }

    // Reset despawn timer for entities
    if (IsServer())
    {
      for (auto&& [entity, transform, despawnInfo] : registry_.view<const GlobalTransform, const DespawnWhenFarFromPlayer>().each())
      {
        const auto maxDist2      = despawnInfo.maxDistance * despawnInfo.maxDistance;
        const auto nearestPlayer = GetNearestPlayer(transform.position);
        [[maybe_unused]] auto _  = registry_.get_or_emplace<Lifetime>(entity, despawnInfo.gracePeriod);
        if (nearestPlayer == entt::null || Math::Distance2(registry_.get<const GlobalTransform>(nearestPlayer).position, transform.position) <= maxDist2)
        {
          registry_.emplace_or_replace<Lifetime>(entity, despawnInfo.gracePeriod);
        }
      }
    }

    // Reset input
    for (auto&& [entity, input] : registry_.view<InputState>().each())
    {
      input = {};
    }

    // Tick down lifetimes
    if (IsServer())
    {
      for (auto&& [entity, lifetime] : registry_.view<Lifetime>().each())
      {
        lifetime.remainingSeconds -= dt;
        if (lifetime.remainingSeconds <= 0)
        {
          registry_.emplace<DeferredDelete>(entity);
        }
      }
    }

    // Delete expired sound emitters
    {
      for (auto&& [entity, emitter] : registry_.view<const SoundEmitter>().each())
      {
        if (emitter.handle.expired())
        {
          registry_.remove<SoundEmitter>(entity);
        }
      }
    }

    // Process entities with Health
    if (IsServer())
    {
      for (auto&& [entity, health, transform] : registry_.view<Health, const GlobalTransform>(entt::exclude<GhostPlayer>).each())
      {
        if (health.hp <= 0)
        {
          if (auto* loot = registry_.try_get<const Loot>(entity))
          {
            auto* table = registry_.ctx().get<LootRegistry>().Get(loot->name);
            ASSERT(table);

            for (auto drop : table->Collect(Rng()))
            {
              auto droppedEntity = Item::Materialize(*this, drop.item);
              Item::GiveCollider(*this, drop.item, droppedEntity);
              registry_.get<LocalTransform>(droppedEntity).position = transform.position;
              UpdateLocalTransform(droppedEntity);
              registry_.emplace<DroppedItem>(droppedEntity, DroppedItem{{.id = drop.item, .count = drop.count}});
              auto velocity = glm::vec3(0);
              if (auto* v = registry_.try_get<const LinearVelocity>(entity))
              {
                velocity = v->v;
              }
              const auto newEntityVelocity =
                velocity + Rng().RandFloat(1, 3) * Math::RandVecInCone({Rng().RandFloat(), Rng().RandFloat()}, glm::vec3(0, 1, 0), glm::half_pi<float>());
              registry_.emplace_or_replace<LinearVelocity>(droppedEntity, newEntityVelocity);
            }
          }

          if (registry_.all_of<Player>(entity))
          {
            KillPlayer(entity);
          }
          else
          {
            registry_.emplace<DeferredDelete>(entity); 
            SpawnHitParticles({
              .numParticles = 30,
              .position = transform.position,
              .normal = glm::vec3(0, 1, 0),
              .spreadConeAngle = glm::half_pi<float>(),
              .size = 0.065f,
              .tint = {.5f, .01f, .02f},
              .speed = 6,
              .lifetime = 3,
            });
          }
        }
      }
    }

    // Recursively mark entities in hierarchies for deletion.
    auto entitiesToDestroy = std::stack<entt::entity>();
    for (auto entity : registry_.view<DeferredDelete>())
    {
      entitiesToDestroy.push(entity);
    }
    while (!entitiesToDestroy.empty())
    {
      auto entity = entitiesToDestroy.top();
      entitiesToDestroy.pop();

      if (auto* h = registry_.try_get<Hierarchy>(entity))
      {
        while (!h->children.empty())
        {
          auto child = h->children.back();
          registry_.emplace_or_replace<DeferredDelete>(child); // Removes child from h->children.
          entitiesToDestroy.push(child);
        }
      }
    }

    // Here, we get a chance to perform game logic on any entity that is about to be deleted.

    // Non-player entities dump their inventory when they are destroyed.
    if (IsServer())
    {
      for (auto&& [entity, transform, inventory] : registry_.view<const GlobalTransform, Inventory, const DeferredDelete>(entt::exclude<Player>).each())
      {
        for (size_t row = 0; row < inventory.height; row++)
        {
          for (size_t col = 0; col < inventory.width; col++)
          {
            if (const auto droppedEntity = DropItemRPC(*this, entity, {row, col}); droppedEntity != entt::null)
            {
              registry_.get<LocalTransform>(droppedEntity).position = transform.position;
              UpdateLocalTransform(droppedEntity);
              auto velocity = glm::vec3(0);
              if (auto* v = registry_.try_get<const LinearVelocity>(entity))
              {
                velocity = v->v;
              }
              const auto newEntityVelocity =
                velocity + Rng().RandFloat(1, 3) * Math::RandVecInCone({Rng().RandFloat(), Rng().RandFloat()}, glm::vec3(0, 1, 0), glm::half_pi<float>());
              registry_.emplace_or_replace<LinearVelocity>(droppedEntity, newEntityVelocity);
            }
          }
        }
      }
    }

    // Actually destroy entities that were marked for deletion.
    for (auto entity : registry_.view<const DeferredDelete>())
    {
      spdlog::debug("Destroyed entity {}", entt::to_integral(entity));
      registry_.destroy(entity);
    }

    ticks_++;
  }
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
  registry_.ctx().get<HashGrid>().set(position, e);
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

  auto& items     = registry_.ctx().get<Item::Registry>();
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
  const auto& grid    = registry_.ctx().get<HashGrid>();

  auto entities = std::vector<entt::entity>();

  const auto lower = grid.QuantizeKey(center - radius);
  const auto upper = grid.QuantizeKey(center + radius);

  // Broadphase: iterate over all chunks touched by sphere.
  for (int z = lower.z; z <= upper.z; z++)
  {
    for (int y = lower.y; y <= upper.y; y++)
    {
      for (int x = lower.x; x <= upper.x; x++)
      {
        const auto [begin, end] = grid.equal_range_chunk({x, y, z});
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
  const auto& grid = registry_.ctx().get<HashGrid>();

  auto entities = std::vector<entt::entity>();

  const auto lower = grid.QuantizeKey(glm::min(start - radius, end - radius));
  const auto upper = grid.QuantizeKey(glm::max(start + radius, end + radius));

  for (int z = lower.z; z <= upper.z; z++)
  {
    for (int y = lower.y; y <= upper.y; y++)
    {
      for (int x = lower.x; x <= upper.x; x++)
      {
        const auto [beginIt, endIt] = grid.equal_range_chunk({x, y, z});
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
  auto& grid     = registry_.ctx().get<TwoLevelGrid>();
  auto prevVoxel = grid.GetVoxelAt(voxelPos);
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

  const auto& blockDef = registry_.ctx().get<BlockRegistry>().Get(prevVoxel);

  if (foundEntity == entt::null)
  {
    foundEntity = this->CreateRenderableEntity(worldPos);
    hp          = &registry_.emplace<BlockHealth>(foundEntity, blockDef.GetInitialHealth());
  }

  registry_.emplace_or_replace<Lifetime>(foundEntity).remainingSeconds = 5;

  if ((damageType & blockDef.GetDamageFlags()).flags == 0 || damageTier < blockDef.GetDamageTier())
  {
    return 0;
  }

  const auto initialHealth = hp->health;
  hp->health -= damage;
  if (hp->health <= 0)
  {
    blockDef.OnDestroyBlock(*this, voxelPos);

    const auto hasNoLoot95 = damageType & BlockDamageFlagBit::NO_LOOT_95_PERCENT;
    if ((!hasNoLoot95 || Rng().RandFloat() >= 0.95) && !(damageType & BlockDamageFlagBit::NO_LOOT))
    {
      const auto dropType = blockDef.GetLootDropType();
      if (auto* ip = std::get_if<ItemState>(&dropType))
      {
        auto itemSelf = Item::Materialize(*this, ip->id);

        registry_.get<LocalTransform>(itemSelf).position = worldPos;
        UpdateLocalTransform(itemSelf);
        Item::GiveCollider(*this, ip->id, itemSelf);
        registry_.emplace<DroppedItem>(itemSelf).item = *ip;

        const auto throwdir                                  = glm::vec3(Rng().RandFloat(-0.25f, 0.25f), 1, Rng().RandFloat(-0.25f, 0.25f));
        registry_.get_or_emplace<LinearVelocity>(itemSelf).v = throwdir * 2.0f;
      }
      else if (auto* lp = std::get_if<std::string>(&dropType))
      {
        auto* table = registry_.ctx().get<LootRegistry>().Get(*lp);
        ASSERT(table);
        for (auto drop : table->Collect(Rng()))
        {
          auto droppedEntity = Item::Materialize(*this, drop.item);
          Item::GiveCollider(*this, drop.item, droppedEntity);
          registry_.get<LocalTransform>(droppedEntity).position = worldPos;
          UpdateLocalTransform(droppedEntity);
          registry_.emplace<DroppedItem>(droppedEntity, DroppedItem{{.id = drop.item, .count = drop.count}});
          auto velocity = glm::vec3(0);
          const auto newEntityVelocity =
            velocity + Rng().RandFloat(1, 3) * Math::RandVecInCone({Rng().RandFloat(), Rng().RandFloat()}, glm::vec3(0, 1, 0), glm::half_pi<float>());
          registry_.emplace_or_replace<LinearVelocity>(droppedEntity, newEntityVelocity);
        }
      }
    }

    // Awaken bodies that are adjacent to destroyed voxel in case they were resting on it.
    // TODO: This doesn't seem to be robust. Setting mTimeBeforeSleep to 0 in PhysicsSettings seems to disable sleeping, which fixes this issue.
    Physics::GetBodyInterface().ActivateBodiesInAABox({Physics::ToJolt(worldPos), 2.0f}, {}, {});

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
    return GetRootEntityOfHierarchy(entity);
  }
  return entity;
}

bool World::IsClient() const
{
  const auto* networking = registry_.ctx().get<std::unique_ptr<Networking::Interface>*>();
  return networking->get() && dynamic_cast<Networking::Client*>(networking->get());
}

bool World::IsServer() const
{
  return !IsClient();
}

bool World::IsHosting() const
{
  const auto* networking = registry_.ctx().get<std::unique_ptr<Networking::Interface>*>();
  const auto* server     = dynamic_cast<const Networking::Server*>(networking->get());
  return networking->get() && server && server->GetNumberOfConnections();
}

Audio* World::GetAudio()
{
  return registry_.ctx().get<Head*>()->GetAudio();
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
    registry_.ctx().get<HashGrid>().set(gt.position, entity);
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