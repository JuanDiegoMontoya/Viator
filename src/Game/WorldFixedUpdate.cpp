#include "World.h"
#include "Game.h"
#include "Globals.h"
#include "Audio.h"
#include "Item.h"
#include "Game/Physics/Physics.h"
#include "Client/debug/Shapes.h"
#include "Systems/CharacterAI.h"
#include "Systems/CharacterController.h"
#include "Game/Voxel/Grid.h"
#include "Physics/PhysicsUtils.h"
#include "Networking/RPC.h"

#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Constraints/DistanceConstraint.h"
#include "tracy/Tracy.hpp"
#include "spdlog/spdlog.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <stack>

void World::FixedUpdate(float dt)
{
  ZoneScoped;
  if (globals->game->gameState == GameState::GAME || IsClient())
  {
    globals->game->time += dt;
#ifndef GAME_HEADLESS
    globals->debugLines.clear();
#endif

    registry_.ClearModifiedComponents();

    ASSERT(registry_.view<const LocalPlayer>().size() <= 1);

    // Update previous transforms before updating it (this should be done after updating the game state from networking)
    {
      ZoneScopedN("Update PreviousGlobalTransform");
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
    }

    for (auto entity : registry_.view<const LocalPlayer>())
    {
      UpdateLocalTransform(entity);
    }

    if (IsServer() && globals->game->updateNpcSpawnDirector)
    {
      globals->game->npcSpawnDirector.Update(*this, dt);
    }

    if (IsServer())
    {
      auto& sunInfo = globals->game->sunInfo;
      if (!sunInfo.pauseDayNightCycle)
      {
        sunInfo.timeOfDay += dt / sunInfo.dayLength * 2;
        sunInfo.timeOfDay = glm::mod(sunInfo.timeOfDay, 2.0f);
      }
    }

    GetPhysicsEngine().FixedUpdate(dt);

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
      ZoneScopedN("Apply status effects");
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
      ZoneScopedN("Process linear paths");
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

    Systems::UpdateInputForBirds(*this, dt);

    // Reset AI targets if target is invalid or dead.
    if (IsServer())
    {
      ZoneScopedN("Reset AI targets");
      for (auto&& [entity, target] : registry_.view<AiTarget>().each())
      {
        if (!registry_.valid(target.currentTarget) || registry_.all_of<GhostPlayer>(target.currentTarget))
        {
          target.currentTarget = entt::null;
        }
      }
    }

    Systems::UpdateInputForPathfindingCharacters(*this, dt);

    Systems::UpdateCharacterControllers(*this, dt);

    // Close open containers if too far away.
    if (IsServer())
    {
      ZoneScopedN("Close distant open containers");
      for (auto&& [entity, player, transform] : registry_.view<Player, const GlobalTransform>().each())
      {
        auto& reg   = registry_;
        auto& world = *this;
        auto& grid  = *globals->grid;
        auto hit    = Voxel::Grid::HitSurfaceParameters();
        auto child  = world.GetChildNamed(entity, "baller");
        if (grid.TraceRaySimple(transform.position, GetForward(transform.rotation), 5, hit))
        {
          if (child == entt::null)
          {
            child = world.CreateRenderableEntityNoHashGrid({}, glm::identity<glm::quat>(), 0.125f);
            reg.emplace<Name>(child, "baller");
            reg.emplace<Mesh>(child, "icosphere_3");
            world.SetParent(child, entity);
            reg.get<Hierarchy>(child).useLocalPositionAsGlobal = true;
          }
          reg.get<LocalTransform>(child).position = hit.positionWorld;
          world.UpdateLocalTransform(child);
        }
        else
        {
          if (child != entt::null)
          {
            reg.emplace<DeferredDelete>(child);
          }
        }

        if (registry_.valid(player.openContainerId))
        {
          player.showInteractPrompt = false;
          if (!registry_.any_of<Inventory, SimpleScriptable>(player.openContainerId))
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

    {
      ZoneScopedN("Destroy entities that depend on constraints");
      for (auto&& [entity, rigidBody] : registry_.view<const Physics::RigidBody, const DestroyWhenConstraintsBroken>().each())
      {
        if (GetPhysicsEngine().GetConstraintsForBody(rigidBody.body).empty())
        {
          registry_.emplace<DeferredDelete>(entity);
        }
      }
    }

    // Player interaction
    // if (IsServer())
    {
      ZoneScopedN("Player interaction");
      for (auto&& [entity, player, transform, input, inventory] :
        registry_.view<Player, const GlobalTransform, const InputState, Inventory>(entt::exclude<GhostPlayer>).each())
      {
        const auto forward         = GetForward(transform.rotation);
        constexpr float RAY_LENGTH = 4.0f;
        const auto rayCast         = JPH::RRayCast(Physics::ToJolt(transform.position), Physics::ToJolt(forward * RAY_LENGTH));
        entt::entity hitEntity     = entt::null;

        bool showInteractPrompt = false;

        auto result = JPH::RayCastResult();
        if (GetPhysicsEngine().GetNarrowPhaseQuery().CastRay(rayCast,
              result,
              GetPhysicsEngine().GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_PROJECTILE),
              GetPhysicsEngine().GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_PROJECTILE),
              *Physics::GetIgnoreEntityAndChildrenFilter({registryOld_, entity})))
        {
          const auto hitPos      = transform.position + forward * (result.mFraction * RAY_LENGTH + 1e-3f);
          const auto voxelHitPos = glm::ivec3(hitPos);
          const auto hitVoxel    = globals->grid->GetVoxelAt(voxelHitPos);

          hitEntity = static_cast<entt::entity>(GetPhysicsEngine().GetBodyInterface().GetUserData(result.mBodyID));
          if (registry_.valid(hitEntity))
          {
            // Ray actually hit a voxel... see if it hit a voxel with a corresponding block entity.
            if (registry_.all_of<VoxelsComponent>(hitEntity))
            {
              if (auto e2 = GetBlockEntity(voxelHitPos); e2 != entt::null)
              {
                hitEntity = e2;
              }
            }

            // Handle voxels with inventories.
            if (auto [e3, _] = GetComponentFromAncestorOrDescendant<Inventory>(hitEntity); e3 != entt::null)
            {
              hitEntity          = e3;
              showInteractPrompt = true;
            }
            else if (auto [e32, simpleScriptable] = GetComponentFromAncestorOrDescendant<SimpleScriptable>(hitEntity); e32 != entt::null)
            {
              if (simpleScriptable->interactable)
              {
                hitEntity          = e32;
                showInteractPrompt = true;
              }
            }

            // Handle other interactable voxels like doors.
            if (globals->blockRegistry->GetRegistry().any_of<Block::Component::TransformWhenUsed>(entt::entity(hitVoxel)))
            {
              showInteractPrompt = true;

              if (input.interact)
              {
                Block::OnUseBlock(*this, voxelHitPos, hitVoxel);
              }
            }
          }
        }

        if (!showInteractPrompt)
        {
          // Cast a ray that looks for other kinds of interactables, like rope attachment points, but only if an interact prompt is not already active.
          auto result2 = JPH::RayCastResult();
          if (GetPhysicsEngine().GetNarrowPhaseQuery().CastRay(rayCast,
                result2,
                GetPhysicsEngine().GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::INTERACT),
                GetPhysicsEngine().GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_INTERACT),
                *Physics::GetIgnoreEntityAndChildrenFilter({registryOld_, entity})))
          {
            const auto hitPos = transform.position + forward * (result2.mFraction * RAY_LENGTH + 1e-3f);
            hitEntity = static_cast<entt::entity>(GetPhysicsEngine().GetBodyInterface().GetUserData(result2.mBodyID));
            if (registry_.valid(hitEntity))
            {
              if (registry_.all_of<RopeAttachmentPoint>(hitEntity))
              {
                showInteractPrompt = true;
              }
            }
          }
        }

        player.showInteractPrompt = showInteractPrompt;

        if (input.interact)
        {
          if (registry_.valid(hitEntity))
          {
            if (auto [ent, inv] = GetComponentFromAncestor<Inventory>(hitEntity); inv)
            {
              player.openContainerId = ent;
            }
            else if (auto [ent2, script] = GetComponentFromAncestor<SimpleScriptable>(hitEntity); script)
            {
              player.openContainerId = ent2;
            }

            if (const auto* p = registry_.try_get<const RopeAttachmentPoint>(hitEntity))
            {
              if (!player.attachedToRope)
              {
                player.attachedToRope = true;

                const auto fakePhysics = GetChildNamed(entity, "Player fake physics");
                const auto& h          = registry_.get<const Hierarchy>(hitEntity);

                // Constrain player to the point on the rope that they grabbed.
                {
                  auto settings          = JPH::Ref(new JPH::DistanceConstraintSettings());
                  settings->mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
                  settings->mMinDistance = 0.0f;
                  settings->mMaxDistance = 0.0f;
                  settings->mConstraintPriority = 100;
                  settings->mLimitsSpringSettings = JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, 8.0f, 1);
                  //settings->mPoint1             = JPH::Vec3(0, 0, -0.5f);

                  auto& lt = registry_.get<LocalTransform>(hitEntity);
                  lt.position = transform.position;
                  UpdateLocalTransform(hitEntity);

                  auto constraint = GetPhysicsEngine().GetBodyInterface().CreateConstraint(settings,
                    registry_.get<const Physics::RigidBody>(fakePhysics).body,
                    registry_.get<const Physics::RigidBody>(h.parent).body);
                  GetPhysicsEngine().RegisterConstraint(constraint);
                }

                // Constrain player to the base of the rope with p->distanceFromBase.
                {
                  auto settings                 = JPH::Ref(new JPH::DistanceConstraintSettings());
                  settings->mSpace              = JPH::EConstraintSpace::LocalToBodyCOM;
                  settings->mMinDistance        = p->distanceFromBase;
                  settings->mMaxDistance        = p->distanceFromBase;
                  settings->mConstraintPriority   = 101;
                  //settings->mLimitsSpringSettings = JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, 1.0f, 1);

                  auto constraint = GetPhysicsEngine().GetBodyInterface().CreateConstraint(settings,
                    registry_.get<const Physics::RigidBody>(fakePhysics).body,
                    registry_.get<const Physics::RigidBody>(GetChildNamed(GetRootEntityOfHierarchy(hitEntity), "Rope Physics")).body);
                  GetPhysicsEngine().RegisterConstraint(constraint);
                }
              }
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

    // Shorten distance constraints attached to entities with ShortenConstraintsOverTime.
    {
      ZoneScopedN("Process ShortenConstraintsOverTime");
      for (auto&& [entity, shortenConstraint, rigidBody] : registry_.view<ShortenConstraintsOverTime, const Physics::RigidBody>().each())
      {
        const auto constraints = GetPhysicsEngine().GetConstraintsForBody(rigidBody.body);
        for (auto* constraint : constraints)
        {
          if (constraint->GetSubType() == JPH::EConstraintSubType::Distance)
          {
            auto* dConstraint = static_cast<JPH::DistanceConstraint*>(constraint);
            if (abs(dConstraint->GetTotalLambdaPosition()) < shortenConstraint.maxAbsLambdaPosition)
            {
              shortenConstraint.velocity = glm::min(shortenConstraint.maxVelocity, shortenConstraint.velocity + shortenConstraint.acceleration * dt);
              const auto minDist = dConstraint->GetMinDistance();
              const auto maxDist = dConstraint->GetMaxDistance();
              dConstraint->SetDistance(minDist, glm::max(minDist, maxDist - shortenConstraint.velocity * dt));
            }

            if (shortenConstraint.springFrequency < shortenConstraint.maxSpringFrequency)
            {
              shortenConstraint.springFrequency =
                glm::min(shortenConstraint.maxSpringFrequency, shortenConstraint.springFrequency + shortenConstraint.springFrequencyVelocity * dt);
              dConstraint->SetLimitsSpringSettings(
                JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, shortenConstraint.springFrequency, dConstraint->GetLimitsSpringSettings().mDamping));
            }

            break;
          }
        }
      }
    }

    if (IsServer())
    {
      ZoneScopedN("Process DespawnWhenFarFromEntity");
      for (auto&& [entity, far, myTransform] : registry_.view<const DespawnWhenFarFromEntity, const GlobalTransform>().each())
      {
        if (!registry_.valid(far.entity))
        {
          registry_.emplace_or_replace<DeferredDelete>(entity);
          continue;
        }
        
        const auto* targetTransform = registry_.try_get<const GlobalTransform>(far.entity);
        ASSERT(targetTransform);

        if (Math::Distance2(myTransform.position, targetTransform->position) > far.maxDistance * far.maxDistance)
        {
          registry_.emplace_or_replace<DeferredDelete>(entity);
        }
      }
    }

    // Handle block tick queue.
    if (IsServer())
    {
      ProcessBlockTickQueue();
    }

    if (IsServer())
    {
      ProcessRandomBlockUpdates();
    }

    // Update items in inventories (important to ensure cooldowns, etc. reset even when items are put away).
    if (IsServer())
    {
      ZoneScopedN("Update items in inventories");
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
      ZoneScopedN("Update CannotBePickedUp");
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
      ZoneScopedN("Dropped item magnetism");
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
      ZoneScopedN("Update status effects");
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
      ZoneScopedN("Update ghost players");
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
      ZoneScopedN("Update Invulnerability");
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
      ZoneScopedN("Update CannotDamageEntities");
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
      ZoneScopedN("Update DespawnWhenFarFromPlayer");
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
      ZoneScopedN("Update Lifetime");
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
      ZoneScopedN("Delete expired sound emitters");
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
      ZoneScopedN("Update entities with Health");
      for (auto&& [entity, health, transform] : registry_.view<Health, const GlobalTransform>(entt::exclude<GhostPlayer>).each())
      {
        if (health.hp <= 0)
        {
          if (auto* loot = registry_.try_get<const Loot>(entity))
          {
            auto* table = globals->game->lootRegistry.Get(loot->name);
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
      ZoneScopedN("Dump inventories from dying entities");
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
    {
      ZoneScopedN("Update DeferredDelete");
      for (auto entity : registry_.view<const DeferredDelete>())
      {
        spdlog::debug("Destroyed entity {}", entt::to_integral(entity));
        registry_.destroy(entity);
      }
    }

    ticks_++;
  }
}

void World::ProcessBlockTickQueue()
{
  ZoneScoped;
  auto queue = std::move(*globals->waterQueue);
  auto set   = std::move(*globals->waterSet);

  int processed = 0;
  while (!queue.empty())
  {
    const auto pos = queue.front();
    queue.pop();
    if (!set.contains(pos))
    {
      Block::OnUpdateBlock(*this, pos);
      set.emplace(pos);
      processed++;
    }
  }

  if (processed > 0)
  {
    spdlog::info("Processed {} scheduled block updates", processed);
  }
}

void World::ProcessRandomBlockUpdates()
{
  ZoneScoped;

  auto& rng = globals->game->rng;
  const auto& grid = globals->grid;
  for (int i = 0; i < 100; i++)
  {
    const auto voxelPosition = glm::ivec3(rng.RandU32(0, grid->Dimensions().x), rng.RandU32(0, grid->Dimensions().y), rng.RandU32(0, grid->Dimensions().z));
    Block::OnRandomUpdateBlock(*this, voxelPosition);
  }
}