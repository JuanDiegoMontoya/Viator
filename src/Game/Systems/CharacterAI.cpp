#include "CharacterAI.h"

#include "Client/debug/Shapes.h"
#include "Game/Game.h"
#include "Game/Pathfinding.h"
#include "Game/World.h"
#include "Game/Voxel/Grid.h"

#include "Game/Physics/Physics.h"
#include "Game/Physics/PhysicsUtils.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "tracy/Tracy.hpp"

void Systems::UpdateInputForBirds(World& world, float dt)
{
  // Avians
  if (world.IsServer())
  {
    ZoneScoped;
    auto& registry_ = world.GetRegistry();
    for (auto&& [entity, input, transform, behavior] : registry_.view<InputState, LocalTransform, PredatoryBirdBehavior>().each())
    {
      behavior.accum += dt;

      const auto nearestPlayer = world.GetNearestPlayer(transform.position);

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
        const auto target  = behavior.idlePosition + glm::vec3(sin(behavior.accum * 1.2f) * 8, 2 + sin(behavior.accum * 4) * 2, cos(behavior.accum * 1.2f) * 8);
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
        const bool hit = world.GetPhysicsEngine().GetNarrowPhaseQuery().CastRay(rayCast,
          result,
          world.GetPhysicsEngine().GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_WORLD),
          world.GetPhysicsEngine().GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_WORLD));
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

      world.UpdateLocalTransform(entity);
    }
  }
}

void Systems::UpdateInputForPathfindingCharacters(World& world, float dt)
{
  // Generate input for enemies
  if (world.IsServer())
  {
    ZoneScopedN("Pathfinding");
    auto& registry_ = world.GetRegistry();
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
      const auto myFootPos = glm::ivec3(glm::floor(world.GetFootPosition(entity)));
      const auto myHeight  = (int)std::ceil(world.GetHeight(entity));

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
            world.GetPhysicsEngine().GetNarrowPhaseQuery().CastShape(shapeCast,
              {},
              {},
              shapeCastCollector,
              world.GetPhysicsEngine().GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_WORLD));
            if (shapeCastCollector.nearest)
            {
              targetFootPos = Physics::ToGlm(shapeCast.GetPointOnRay(shapeCastCollector.nearest->mFraction - 1e-2f));
            }
          }

          // path                 = Pathfinding::FindPath(*this, {.start = myFootPos, .goal = targetFootPos, .height = myHeight, .w = 1.5f});
          path = registry_.ctx().get<Pathfinding::PathCache>().FindOrGetCachedPath(world,
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
          const auto& grid = registry_.ctx().get<Voxel::Grid>();
          auto& rng        = registry_.ctx().get<PCG::Rng>();
          for (int i = 0; i < 5; i++)
          {
            if (auto pos = SampleWalkablePosition(grid, rng, aiTransform.position, wb->minWanderDistance, wb->maxWanderDistance, wb->targetCanBeFloating))
            {
              path = registry_.ctx().get<Pathfinding::PathCache>().FindOrGetCachedPath(world,
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

      world.UpdateLocalTransform(entity);
    }
  }
}
