#include "Game.h"
#ifndef GAME_HEADLESS
#include "Client/PlayerHead.h"
#include "Client/debug/Shapes.h"
#endif
#include "EntityPrefab.h"
#include "Physics/Physics.h"
#include "Physics/TwoLevelGridShape.h"
#include "TwoLevelGrid.h"
#include "Pathfinding.h"
#include "MathUtilities.h"
#include "HashGrid.h"
#include "Item.h"
#include "Prefab.h"
#include "Core/Reflection.h"
#include "Core/Serialization.h"
#include "Core/Logging.h"
#include "Core/Assert2.h"
#include "Networking/Interface.h"
#include "VoxLoader.h"
#include "World.h"
#include "Game/Audio.h"
#include "Game/Scripting.h"

#include "tracy/Tracy.hpp"
#include "entt/signal/dispatcher.hpp"

#include "FastNoise/FastNoise.h"

#include "spdlog/spdlog.h"

#include <chrono>
#include <stack>
#include <execution>
#include <algorithm>
#include <mutex>

#define GAME_CATCH_EXCEPTIONS 0

// We don't want this to happen when the component/entity is actually deleted, as we care about having a valid parent.
static void OnDeferredDeleteConstruct(entt::registry& registryRaw, entt::entity entity)
{
  ZoneScoped;
  auto& registry = registryRaw.ctx().get<World&>().GetRegistry();
  ASSERT(registry.valid(entity));
  SPDLOG_TRACE("[OnDeferredDeleteConstruct] Removing {}", entt::to_integral(entity));
  auto* h = registry.try_get<const Hierarchy>(entity);
  if (h && h->parent != entt::null)
  {
    ASSERT(registry.valid(h->parent));
    SPDLOG_TRACE("[OnDeferredDeleteConstruct] Removing {} as a child of {}", entt::to_integral(entity), entt::to_integral(h->parent));
    auto& ph = registry.get<Hierarchy>(h->parent);
    ph.RemoveChild(entity);
  }
}

// Helper to simplify logic for OnContact. Calls the input function twice with swapped arguments.
// Callee should return true if conditions were met so it isn't unnecessarily invoked twice.
template<typename P, typename F>
void TryTwice(const P& pair, F&& function)
{
  if (function(pair.entity1, pair.entity2))
  {
    return;
  }

  function(pair.entity2, pair.entity1);
}

// *ppair is modified to contain the actual entities that collided
static void OnContactAdded(World& world, Physics::ContactAddedPair* ppair)
{
  ZoneScoped;
  ASSERT(ppair);
  auto& pair = *ppair;

  if (world.GetRegistry().all_of<ForwardCollisionsToParent>(pair.entity1))
  {
    auto& h = world.GetRegistry().get<const Hierarchy>(pair.entity1);
    if (h.parent != entt::null)
    {
      pair.entity1 = h.parent;
      OnContactAdded(world, ppair);
      return;
    }
  }

  if (world.GetRegistry().all_of<ForwardCollisionsToParent>(pair.entity2))
  {
    auto& h = world.GetRegistry().get<const Hierarchy>(pair.entity2);
    if (h.parent != entt::null)
    {
      pair.entity2 = h.parent;
      OnContactAdded(world, ppair);
      return;
    }
  }

  // Projectile hit sound
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().all_of<Projectile>(entity1))
      {
        if (auto len = glm::length(world.GetRegistry().get<LinearVelocity>(entity1).v); len > 2.0f)
        {
          world.GetAudio()->PlaySound({
            .name     = "land",
            .rolloff  = 0.5f,
            .position = ppair->position,
          });

          // Particles are already spawned when projectiles hit creatures.
          if (!world.GetRegistry().all_of<Health>(entity2))
          {
            const auto numParticles = uint32_t(glm::clamp(glm::ceil(glm::mix(1.0f, 4.0f, len / 80.0f)), 0.0f, 4.0f));
            world.SpawnHitParticles({
              .numParticles    = numParticles,
              .position        = ppair->position,
              .normal          = ppair->normal,
              .spreadConeAngle = glm::half_pi<float>(),
            });
          }
        }
        return true;
      }
      return false;
    });

  // Projectiles hurt creatures
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().any_of<Health>(entity1) && world.GetRegistry().all_of<Projectile, ContactDamage>(entity2))
      {
        if (world.AreEntitiesEnemies(entity1, entity2))
        {
          if (auto* h = world.GetRegistry().try_get<Health>(entity1); h && h->hp > 0)
          {
            const auto& projectile = world.GetRegistry().get<const Projectile>(entity2);
            const auto& damage     = world.GetRegistry().get<const ContactDamage>(entity2);
            auto& projVelocity     = world.GetRegistry().get<const LinearVelocity>(entity2);

            //const auto currentSpeed2  = glm::dot(projectile.velocity, projectile.velocity);
            //const auto energyFraction = (currentSpeed2) / (projectile.initialSpeed * projectile.initialSpeed);
            const auto energyFraction = glm::length(projVelocity.v) / projectile.initialSpeed;

            const auto effectiveKnockback = damage.knockback * energyFraction;
            if (world.DamageEntity(entity1, damage.damage * energyFraction) > 0)
            {
              world.SpawnHitParticles({
                .numParticles    = 5,
                .position        = ppair->position,
                .normal          = ppair->normal,
                .spreadConeAngle = glm::pi<float>(),
                .tint            = {.5f, .01f, .02f},
                .speed           = 5,
              });
              world.GetAudio()->PlaySound({.name = "land", .position = world.GetRegistry().get<const GlobalTransform>(entity2).position});

              auto pushDir = projVelocity.v;
              pushDir.y    = 0;
              if (glm::length(pushDir) > 1e-3f)
              {
                pushDir = glm::normalize(pushDir);
              }
              pushDir *= effectiveKnockback * 3;
              pushDir.y = effectiveKnockback;
              // world.SetLinearVelocity(entity1, pushDir);
              auto& velocity = world.GetRegistry().get<LinearVelocity>(entity1);
              pushDir.y /= exp2(glm::max(0.0f, velocity.v.y * 1.0f)); // Reduce velocity gain (prevent stuff from flying super high- subject to change).
              if (auto* m = world.GetRegistry().try_get<const KnockbackMultiplier>(entity1))
              {
                pushDir *= m->factor;
              }
              velocity.v += pushDir;
              if (auto* cc = world.GetRegistry().try_get<Physics::CharacterControllerShrimple>(entity1))
              {
                cc->previousGroundState = JPH::CharacterBase::EGroundState::InAir;
              }
            }
            world.GetRegistry().emplace_or_replace<DeferredDelete>(entity2);
            world.GetRegistry().remove<Projectile>(entity2);
          }
          return true;
        }
      }
      return false;
    });

  // Players pick up dropped items
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().all_of<Player, Inventory>(entity1) && world.GetRegistry().all_of<DroppedItem>(entity2) &&
          !world.GetRegistry().any_of<CannotBePickedUp>(entity2))
      {
        auto& i = world.GetRegistry().get<Inventory>(entity1);
        auto& d = world.GetRegistry().get<DroppedItem>(entity2);

        if (d.item.id != entt::null)
        {
          world.GetAudio()->PlaySound({.name = "coin", .highlander = true});
          i.TryStackItem(world, d.item);
          if (d.item.count > 0)
          {
            if (auto slotCoords = i.GetFirstEmptySlot())
            {
              i.OverwriteSlot(world, *slotCoords, d.item, entity1);
              d.item = {.count = 0};
            }
          }

          if (d.item.count == 0)
          {
            world.GetRegistry().remove<DroppedItem>(entity2);
            world.GetRegistry().get_or_emplace<DeferredDelete>(entity2);
          }
        }
        return true;
      }
      return false;
    });
}

static void OnContactPersisted(World& world, Physics::ContactPersistedPair* ppair)
{
  ZoneScoped;
  ASSERT(ppair);
  auto& pair = *ppair;

  if (world.GetRegistry().all_of<ForwardCollisionsToParent>(pair.entity1))
  {
    auto& h = world.GetRegistry().get<const Hierarchy>(pair.entity1);
    if (h.parent != entt::null)
    {
      pair.entity1 = h.parent;
      OnContactPersisted(world, ppair);
      return;
    }
  }

  if (world.GetRegistry().all_of<ForwardCollisionsToParent>(pair.entity2))
  {
    auto& h = world.GetRegistry().get<const Hierarchy>(pair.entity2);
    if (h.parent != entt::null)
    {
      pair.entity2 = h.parent;
      OnContactPersisted(world, ppair);
      return;
    }
  }

  // Players take damage from enemy team
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().all_of<Player, Health>(entity1) && world.GetRegistry().all_of<ContactDamage>(entity2))
      {
        if (world.AreEntitiesEnemies(entity1, entity2))
        {
          const auto& contactDamage = world.GetRegistry().get<const ContactDamage>(entity2);

          if (world.DamageEntity(entity1, contactDamage.damage) > 0)
          {
            world.GetRegistry().emplace<Invulnerability>(entity1).remainingSeconds = 0.5f;
            world.GetAudio()->PlaySound({.name = "hurt"});
          }
        }
        return true;
      }
      return false;
    });

  // Other sources of contact damage hurt creatures
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().any_of<Health>(entity1) && world.GetRegistry().all_of<ContactDamage>(entity2) &&
          !world.GetRegistry().any_of<Projectile>(entity2) && !world.GetRegistry().any_of<Player>(entity1))
      {
        if (world.AreEntitiesEnemies(entity1, entity2) && world.CanEntityDamageEntity(entity2, entity1))
        {
          auto& pos1         = world.GetRegistry().get<const GlobalTransform>(entity1).position;
          auto& pos2         = world.GetRegistry().get<const GlobalTransform>(entity2).position;
          const auto& damage = world.GetRegistry().get<const ContactDamage>(entity2);
          if (world.DamageEntity(entity1, damage.damage) > 0)
          {
            world.SpawnHitParticles({
              .numParticles = 5,
              .position = ppair->position,
              .normal = ppair->normal,
              .spreadConeAngle = glm::half_pi<float>(),
              .tint = {0.5f, 0.01f, 0.02f},
            });
            world.GetAudio()->PlaySound({.name = "land", .position = world.GetRegistry().get<const GlobalTransform>(entity2).position});
            auto pushDir = pos1 - pos2;
            pushDir.y    = 0;
            if (glm::length(pushDir) > 1e-3f)
            {
              pushDir = glm::normalize(pushDir);
            }
            pushDir *= damage.knockback * 3;
            pushDir.y      = damage.knockback;
            auto& velocity = world.GetRegistry().get<LinearVelocity>(entity1);
            pushDir.y /= exp2(glm::max(0.0f, velocity.v.y * 1.0f)); // Reduce velocity gain (prevent stuff from flying super high- subject to change).
            if (auto* m = world.GetRegistry().try_get<const KnockbackMultiplier>(entity1))
            {
              pushDir *= m->factor;
            }
            velocity.v += pushDir;
            if (auto* cc = world.GetRegistry().try_get<Physics::CharacterControllerShrimple>(entity1))
            {
              cc->previousGroundState = JPH::CharacterBase::EGroundState::InAir;
            }
            world.GetRegistry().get_or_emplace<CannotDamageEntities>(entity2).entities[entity1] = 0.2f;
          }
        }
        return true;
      }
      return false;
    });
}

void OnNoclipCharacterControllerConstruct(entt::registry& registryRaw, entt::entity entity)
{
  auto& registry = registryRaw.ctx().get<World&>().GetRegistry();
  registry.remove<FlyingCharacterController>(entity);
  registry.remove<Physics::CharacterController>(entity);
  registry.remove<Physics::CharacterControllerShrimple>(entity);
}

void OnFlyingCharacterControllerConstruct(entt::registry& registryRaw, entt::entity entity)
{
  auto& registry = registryRaw.ctx().get<World&>().GetRegistry();
  registry.remove<NoclipCharacterController>(entity);
  registry.remove<Physics::CharacterController>(entity);
  registry.remove<Physics::CharacterControllerShrimple>(entity);
}

void OnCharacterControllerConstruct(entt::registry& registryRaw, entt::entity entity)
{
  auto& registry = registryRaw.ctx().get<World&>().GetRegistry();
  registry.remove<NoclipCharacterController>(entity);
  registry.remove<FlyingCharacterController>(entity);
  registry.remove<Physics::CharacterControllerShrimple>(entity);
  registry.remove<Friction>(entity); // TODO: temporary until CC has inertia
}

void OnCharacterControllerShrimpleConstruct(entt::registry& registryRaw, entt::entity entity)
{
  auto& registry = registryRaw.ctx().get<World&>().GetRegistry();
  registry.remove<NoclipCharacterController>(entity);
  registry.remove<FlyingCharacterController>(entity);
  registry.remove<Physics::CharacterController>(entity);
}

void OnGlobalTransformRemove(entt::registry& registry, entt::entity entity)
{
  registry.ctx().get<HashGrid>().erase(entity);
}

void OnLinearPathRemove(entt::registry& registryRaw, entt::entity entity)
{
  auto& world = registryRaw.ctx().get<World&>();
  auto& registry = world.GetRegistry();
  if (!registry.all_of<DeferredDelete>(entity))
  {
    auto& path = registry.get<const LinearPath>(entity);
    registry.emplace_or_replace<LocalTransform>(entity, path.originalLocalTransform);
    world.UpdateLocalTransform(entity);
  }
}

static Head* gHead_HORRIBLE_HACK{};
static std::unique_ptr<Networking::Interface>* gNetworking_HORRIBLE_HACK{};
static Scripting* scripting{};
Game::Game(uint32_t)
{
  Core::Logging::Initialize();
  spdlog::info("Initializing game");
  world_ = std::make_unique<World>();
  Physics::Initialize(*world_);
  Physics::GetDispatcher().sink<Physics::ContactAddedPair*>().connect<&OnContactAdded>(*world_);
  Physics::GetDispatcher().sink<Physics::ContactPersistedPair*>().connect<&OnContactPersisted>(*world_);

#ifdef GAME_HEADLESS
  head_                                            = std::make_unique<NullHead>();
  world_->GetRegistry().ctx().emplace<GameState>() = GameState::GAME;
  world_->InitializeGameState();
#else
  head_ = std::make_unique<PlayerHead>(PlayerHead::CreateInfo{
    .name        = "Gabagool",
    .maximize    = false,
    .decorate    = true,
    .presentMode = VK_PRESENT_MODE_FIFO_KHR,
    .world       = world_.get(),
  });
  gHead_HORRIBLE_HACK = head_.get();
#endif
  gNetworking_HORRIBLE_HACK = &networking_;

  scripting = new Scripting();
  Core::Reflection::Initialize(*scripting);
  CreateContextVariablesAndObservers(*world_);

  Core::Serialization::Initialize();
}

void CreateContextVariablesAndObservers(World& world)
{
  ZoneScoped;
  auto& registry = world.GetRegistryRaw();
#ifndef GAME_HEADLESS
  registry.ctx().emplace<GameState>() = GameState::MENU;
  registry.ctx().emplace<std::vector<Debug::Line>>();
#endif

  registry.ctx().emplace<World&>(world); // Observers only see a registry, so this is needed for flexibility and correctness.
  registry.ctx().emplace<PCG::Rng>();
  registry.ctx().emplace<Debugging>();
  registry.ctx().emplace<TimeScale>();
  registry.ctx().emplace<TickRate>().hz = 30;
  registry.ctx().emplace_as<float>("time"_hs) = 0; // TODO: TEMP
  registry.ctx().emplace<Pathfinding::PathCache>(); // Note: should be invalidated when voxel grid changes
  registry.ctx().emplace<HashGrid>(16);
  registry.ctx().emplace<Head*>() = gHead_HORRIBLE_HACK; // Hack
  registry.ctx().emplace<std::unique_ptr<Networking::Interface>*>() = gNetworking_HORRIBLE_HACK; // Hack
  registry.ctx().emplace<NpcSpawnDirector>(world);
  registry.ctx().emplace_as<bool>("UpdateNPCSpawnDirector"_hs, true);
  registry.ctx().emplace<SunInfo>();
  registry.ctx().emplace<Scripting*>(scripting);

  registry.on_construct<DeferredDelete>().connect<&OnDeferredDeleteConstruct>();
  registry.on_construct<NoclipCharacterController>().connect<&OnNoclipCharacterControllerConstruct>();
  registry.on_construct<FlyingCharacterController>().connect<&OnFlyingCharacterControllerConstruct>();
  registry.on_construct<Physics::CharacterController>().connect<&OnCharacterControllerConstruct>();
  registry.on_construct<Physics::CharacterControllerShrimple>().connect<&OnCharacterControllerShrimpleConstruct>();
  registry.on_destroy<GlobalTransform>().connect<&OnGlobalTransformRemove>();
  registry.on_destroy<LinearPath>().connect<&OnLinearPathRemove>();

  Physics::CreateObservers(registry);

  world.InitializeGameDefinitions();
}

void SetVoxelAtRPC(World& world, glm::ivec3 voxelPosition, voxel_t voxel)
{
  auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
  grid.SetVoxelAt(voxelPosition, voxel);
}

Game::~Game()
{
  Physics::Terminate();
}

void Game::Run()
{
  ZoneScoped;
  isRunning_ = true;

  auto previousTimestamp  = std::chrono::steady_clock::now();
  double fixedUpdateAccum = 0;

  while (isRunning_)
  {
    ZoneScopedN("Main Loop");
#if GAME_CATCH_EXCEPTIONS
    try
#endif
    {
      const auto timeScale      = world_->GetRegistry().ctx().get<TimeScale>().scale;
      const auto tickHz         = world_->GetRegistry().ctx().get<TickRate>().hz;
      const double tickDuration = 1.0 / tickHz;

      const auto currentTimestamp = std::chrono::steady_clock::now();
      const auto realDeltaTime    = std::chrono::duration_cast<std::chrono::microseconds>(currentTimestamp - previousTimestamp).count() / 1'000'000.0;
      previousTimestamp           = currentTimestamp;

      auto dt = DeltaTime{
        .game     = static_cast<float>(realDeltaTime * timeScale),
        .real     = static_cast<float>(realDeltaTime),
        .fraction = float(fixedUpdateAccum / tickDuration),
      };

      if (head_)
      {
        head_->VariableUpdatePre(dt, *world_);
      }

      //if (world_->IsServer())
      if (world_->GetRegistry().ctx().get<GameState>() == GameState::GAME)
      {
        for (auto&& [entity, player, inputLook, transform, gtransform] : world_->GetRegistry().view<const Player, const InputLookState, LocalTransform, GlobalTransform>().each())
        {
          if (world_->IsServer() || world_->GetRegistry().all_of<LocalAuthoritative>(entity))
          {
            transform.rotation  = glm::angleAxis(inputLook.yaw, glm::vec3{0, 1, 0}) * glm::angleAxis(inputLook.pitch, glm::vec3{1, 0, 0});
            gtransform.rotation = transform.rotation;
          }
        }
      }

      if (world_->GetRegistry().ctx().get<GameState>() == GameState::GAME)
      {
        constexpr int MAX_TICKS = 10;
        int accumTicks          = 0;
        fixedUpdateAccum += realDeltaTime * timeScale;
        while (fixedUpdateAccum > tickDuration && accumTicks++ < MAX_TICKS)
        {
          fixedUpdateAccum -= tickDuration;
          if (networking_)
          {
            SPDLOG_TRACE("networking_->ProcessMessages()");
            networking_->ProcessMessages(*world_);

            for (auto entity : world_->GetRegistry().view<const NetworkNeedUpdateLocalTransform>())
            {
              world_->UpdateLocalTransform(entity);
            }
            world_->GetRegistryRaw().clear<NetworkNeedUpdateLocalTransform>();

            SPDLOG_TRACE("networking_->SendMessages()");
            networking_->SendMessages(*world_);
          }
          SPDLOG_TRACE("world->FixedUpdate({})", tickDuration);
          world_->FixedUpdate(static_cast<float>(tickDuration));
        }

        dt.fraction = std::clamp(float(fixedUpdateAccum / tickDuration), 0.0f, 1.0f);
      }
      else if (networking_) // Process network messages if we aren't yet in the game (i.e. still connecting).
      {
        networking_->ProcessMessages(*world_);
      }

      if (head_)
      {
        head_->VariableUpdatePost(dt, *world_);
      }

      if (world_->GetRegistry().ctx().contains<CloseApplication>())
      {
        isRunning_ = false;
      }
    }
#if GAME_CATCH_EXCEPTIONS
    catch(std::exception& e)
    {
      fprintf(stderr, "Exception caught: %s\n", e.what());
      throw;
    }
#endif
  }
}

bool SwapInventorySlotsRPC(World& world, entt::entity parent1, glm::ivec2 parent1Slot, entt::entity parent2, glm::ivec2 parent2Slot)
{
  auto* inventory1 = world.GetRegistry().try_get<Inventory>(parent1);
  auto* inventory2 = world.GetRegistry().try_get<Inventory>(parent2);
  if (!inventory1 || !inventory2)
  {
    spdlog::warn("Failed to swap inventory slots.");
    return false;
  }

  auto item1 = inventory1->slots[parent1Slot.x][parent1Slot.y];
  auto item2 = inventory2->slots[parent2Slot.x][parent2Slot.y];

  inventory1->OverwriteSlot(world, parent1Slot, item2, parent1);
  inventory2->OverwriteSlot(world, parent2Slot, item1, parent2);

  return true;
}

void TeleportPlayerRPC(World& world, entt::entity player, LocalTransform transform)
{
  world.GetRegistry().get_or_emplace<LocalTransform>(player) = transform;
  world.UpdateLocalTransform(player);
}

bool SwapInventorySlotAndArmorSlotRPC(World& world, entt::entity parent1, glm::ivec2 parent1Slot, entt::entity parent2, ArmorAndAccessories::Slot parent2Slot)
{
  auto* inventory1 = world.GetRegistry().try_get<Inventory>(parent1);
  auto* armor2 = world.GetRegistry().try_get<ArmorAndAccessories>(parent2);
  if (!inventory1 || !armor2)
  {
    spdlog::warn("Failed to swap inventory slot and armor slot.");
    return false;
  }

  auto item1 = inventory1->slots[parent1Slot.x][parent1Slot.y];
  auto item2 = armor2->slots[parent2Slot];

  auto item1Target = Item::Component::AllowedSlots::Accessory;

  if (parent2Slot == ArmorAndAccessories::SLOT_HEAD)
  {
    item1Target = Item::Component::AllowedSlots::Head;
  }

  if (parent2Slot == ArmorAndAccessories::SLOT_BODY)
  {
    item1Target = Item::Component::AllowedSlots::Body;
  }

  if (parent2Slot == ArmorAndAccessories::SLOT_LEGS)
  {
    item1Target = Item::Component::AllowedSlots::Legs;
  }

  // Check that target for the item being moved into restricted slots is compatible.
  if (item1.id != entt::null && !Item::ItemIsCompatibleWithSlot(world, item1.id, item1Target))
  {
    return false;
  }

  inventory1->OverwriteSlot(world, parent1Slot, item2, parent1);
  armor2->OverwriteSlot(world, parent2Slot, item1);

  return true;
}

bool SwapArmorSlotsRPC(World& world, entt::entity parent1, ArmorAndAccessories::Slot parent1Slot, entt::entity parent2, ArmorAndAccessories::Slot parent2Slot)
{
  auto* armor1 = world.GetRegistry().try_get<ArmorAndAccessories>(parent1);
  auto* armor2 = world.GetRegistry().try_get<ArmorAndAccessories>(parent2);
  if (!armor1 || !armor2)
  {
    spdlog::warn("Failed to swap armor slots.");
    return false;
  }

  // Only accessory slots can be swapped.
  if (parent1Slot < ArmorAndAccessories::SLOT_ACCESSORY0 || parent2Slot < ArmorAndAccessories::SLOT_ACCESSORY0)
  {
    return false;
  }

  
  auto item1 = armor1->slots[parent1Slot];
  auto item2 = armor2->slots[parent2Slot];

  armor1->OverwriteSlot(world, parent1Slot, item2);
  armor2->OverwriteSlot(world, parent2Slot, item1);

  return true;
}

glm::vec3 GetForward(glm::quat rotation)
{
  return -glm::mat3_cast(rotation)[2];
}

glm::vec3 GetUp(glm::quat rotation)
{
  return glm::mat3_cast(rotation)[1];
}

glm::vec3 GetRight(glm::quat rotation)
{
  return glm::mat3_cast(rotation)[0];
}

std::shared_ptr<TwoLevelGrid::SubGrid> VoxToSubGrid(const Vox::Chunk& root)
{
  auto processed = Vox::ProcessModel(root);
  ASSERT(processed.voxelChunk);
  ASSERT(processed.sizeChunk);
  ASSERT(processed.paletteChunk);
  ASSERT(!processed.iMapChunk, "TODO: IMAP chunk");
  const auto dims = glm::ivec3(processed.sizeChunk->sizeX, processed.sizeChunk->sizeZ, processed.sizeChunk->sizeY); // Z-up
  auto subVoxels  = std::make_unique<TwoLevelGrid::SubVoxel[]>(dims.x * dims.y * dims.z);

  for (uint32_t i = 0; i < processed.voxelChunk->numVoxels; i++)
  {
    const auto voxel    = processed.voxelChunk->voxels[i];
    const auto position = glm::ivec3(dims.x - 1 - voxel.x, voxel.z, voxel.y); // Z-up RH
    ASSERT(glm::all(glm::greaterThanEqual(position, glm::ivec3(0))) && glm::all(glm::lessThan(position, dims)));
    subVoxels[TwoLevelGrid::FlattenGenericCoord(dims, position)] = TwoLevelGrid::SubVoxel(voxel.colorIndex);
  }

  auto subGrid = std::make_shared<TwoLevelGrid::SubGrid>(TwoLevelGrid::SubGrid{
    .dimensions = dims,
    .grid       = std::move(subVoxels),
  });

  for (int i = 0; i < 255; i++)
  {
    const auto color                = processed.paletteChunk->colors[i];
    subGrid->materials[i].colorSrgb = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, 1};
    if (i < processed.materials.size())
    {
      if (const auto emissionInfo = Vox::ParseEmissionInfoFromDict(processed.materials[i]->attributes))
      {
        // TODO: Fix color space.
        subGrid->materials[i].emissionSrgb = subGrid->materials[i].colorSrgb * emissionInfo->emission * exp2(emissionInfo->power);
      }
    }
  }

  return subGrid;
}


void Hierarchy::AddChild(entt::entity child)
{
  ASSERT(std::count(children.begin(), children.end(), child) == 0);
  children.emplace_back(child);
}

void Hierarchy::RemoveChild(entt::entity child)
{
  ASSERT(std::count(children.begin(), children.end(), child) == 1);
  std::erase(children, child);
}

void SetActiveSlotRPC(World& world, entt::entity parent, glm::ivec2 rowCol)
{
  auto* inv = world.GetRegistry().try_get<Inventory>(parent);

  if (!inv)
  {
    spdlog::warn("Failed to set active slot: missing inventory");
    return;
  }

  if (!inv->canHaveActiveItem)
  {
    return;
  }

  if (rowCol != inv->activeSlotCoord)
  {
    if (inv->ActiveSlot().id != entt::null)
    {
      Item::Dematerialize(world, inv->ActiveSlot().id, inv->activeSlotEntity);
      inv->activeSlotEntity = entt::null;
    }
    inv->activeSlotCoord = rowCol;
    if (inv->ActiveSlot().id != entt::null)
    {
      inv->activeSlotEntity = Item::Materialize(world, inv->ActiveSlot().id);
      if (inv->activeSlotEntity != entt::null)
      {
        world.SetParent(inv->activeSlotEntity, parent);
      }
    }
  }
}

void ScrollHotbarRPC(World& world, entt::entity parent, int32_t offset)
{
  auto* inv = world.GetRegistry().try_get<Inventory>(parent);
  if (!inv)
  {
    spdlog::warn("Could not scroll in hotbar: missing inventory");
    return;
  }

  const auto newCol = (int)glm::mod((float)inv->activeSlotCoord.y + offset, (float)inv->width);
  SetActiveSlotRPC(world, parent, glm::ivec2{0, newCol});
}

entt::entity DropItemRPC(World& world, entt::entity parent, glm::ivec2 slot)
{
  auto* inv = world.GetRegistry().try_get<Inventory>(parent);
  if (!inv)
  {
    spdlog::warn("Could not drop item: missing inventory");
    return entt::null;
  }

  if (slot.x < 0 || slot.y < 0 || slot.x >= inv->height || slot.y >= inv->width)
  {
    // TODO: Warn client who sent bad input.
    spdlog::warn("Could not drop item: bad slot");
    return entt::null;
  }

  auto& item = inv->slots[slot[0]][slot[1]];
  if (item.id == entt::null)
  {
    return entt::null;
  }

  if (inv->canHaveActiveItem && inv->activeSlotCoord == slot)
  {
    Item::Dematerialize(world, item.id, inv->activeSlotEntity);
    inv->activeSlotEntity = entt::null;
  }

  auto entity = Item::Materialize(world, item.id);
  Item::GiveCollider(world, item.id, entity);
  world.GetRegistry().emplace<DroppedItem>(entity).item = std::exchange(item, {});
  world.GetRegistry().emplace<CannotBePickedUp>(entity).remainingSeconds = 1.0f;
  return entity;
}

entt::entity DropItemFromArmorRPC(World& world, entt::entity parent, ArmorAndAccessories::Slot slot)
{
  auto* armor = world.GetRegistry().try_get<ArmorAndAccessories>(parent);
  if (!armor)
  {
    spdlog::warn("Could not drop item: missing ArmorAndAccessories");
    return entt::null;
  }

  if (slot < 0 || slot >= ArmorAndAccessories::SLOT_COUNT)
  {
    spdlog::warn("Could not drop item: bad armor slot");
    return entt::null;
  }

  auto& item = armor->slots[slot];
  if (item.id == entt::null)
  {
    return entt::null;
  }
  
  auto entity = Item::Materialize(world, item.id);
  Item::GiveCollider(world, item.id, entity);
  world.GetRegistry().emplace<DroppedItem>(entity).item = std::exchange(item, {});
  world.GetRegistry().emplace<CannotBePickedUp>(entity).remainingSeconds = 1.0f;
  return entity;
}

entt::entity ThrowItemRPC(World& world, entt::entity parent, entt::entity thrower, glm::ivec2 slot)
{
  auto* userTransform = world.GetRegistry().try_get<const GlobalTransform>(thrower);
  if (!userTransform)
  {
    spdlog::warn("Failed to throw item: thrower does not have global transform");
    return entt::null;
  }

  auto dropped = DropItemRPC(world, parent, slot);
  if (dropped != entt::null)
  {
    const auto throwdir = GetForward(userTransform->rotation);
    const auto pos = userTransform->position + throwdir * 1.0f;
    world.GetRegistry().get<LocalTransform>(dropped).position = pos;
    world.GetRegistry().get<LinearVelocity>(dropped).v = throwdir * 3.0f;
    world.UpdateLocalTransform(dropped);
  }

  return dropped;
}

entt::entity ThrowItemFromArmorRPC(World& world, entt::entity parent, entt::entity thrower, ArmorAndAccessories::Slot slot)
{
  auto* userTransform = world.GetRegistry().try_get<const GlobalTransform>(thrower);
  if (!userTransform)
  {
    spdlog::warn("Failed to throw item: thrower does not have global transform");
    return entt::null;
  }

  auto dropped = DropItemFromArmorRPC(world, parent, slot);
  if (dropped != entt::null)
  {
    const auto throwdir                                       = GetForward(userTransform->rotation);
    const auto pos                                            = userTransform->position + throwdir * 1.0f;
    world.GetRegistry().get<LocalTransform>(dropped).position = pos;
    world.GetRegistry().get<LinearVelocity>(dropped).v        = throwdir * 3.0f;
    world.UpdateLocalTransform(dropped);
  }

  return dropped;
}

void Inventory::OverwriteSlot(World& world, glm::ivec2 rowCol, ItemState itemState, entt::entity parent)
{
  const bool dstIsActive = rowCol == activeSlotCoord;
  if (canHaveActiveItem && dstIsActive && ActiveSlot().id != entt::null)
  {
    Item::Dematerialize(world, ActiveSlot().id, activeSlotEntity);
    activeSlotEntity = entt::null;
  }
  slots[rowCol[0]][rowCol[1]] = itemState;
  if (canHaveActiveItem && dstIsActive && itemState.id != entt::null)
  {
    activeSlotEntity = Item::Materialize(world, ActiveSlot().id);
    world.SetParent(activeSlotEntity, parent);
  }
}

void Inventory::TryStackItem(World& world, ItemState& item)
{
  for (auto& row : slots)
  {
    for (auto& slot : row)
    {
      if (item.count <= 0)
      {
        return;
      }

      if (slot.id == item.id)
      {
        // Moves stack from item to slot, up to max stack size.
        const auto avail = glm::min(item.count, Item::GetMaxStackSize(world, item.id) - slot.count);
        slot.count += avail;
        item.count -= avail;
      }
    }
  }
}

std::optional<glm::ivec2> Inventory::GetFirstEmptySlot() const
{
  for (size_t row = 0; row < height; row++)
  {
    for (size_t col = 0; col < width; col++)
    {
      if (slots[row][col].id == entt::null)
      {
        return glm::ivec2{row, col};
      }
    }
  }

  return std::nullopt;
}

int Inventory::CountItem(ItemId item) const
{
  int count = 0;
  for (const auto& row : slots)
  {
    for (const auto& slot : row)
    {
      if (slot.id == item)
      {
        count += (int)slot.count;
      }
    }
  }
  return count;
}

bool Inventory::CanCraftRecipe(const Crafting::Recipe& recipe) const
{
  for (const auto& ingredient : recipe.ingredients)
  {
    if (CountItem(ingredient.item) < ingredient.count)
    {
      return false;
    }
  }

  return true;
}

void TryCraftRecipeRPC(World& world, entt::entity parent, Crafting::Recipe recipe)
{
  auto* inv = world.GetRegistry().try_get<Inventory>(parent);
  if (!inv)
  {
    spdlog::warn("Could not craft recipe: missing inventory");
    return;
  }

  if (!inv->CanCraftRecipe(recipe))
  {
    spdlog::warn("Could not craft recipe: not enough resources");
    return;
  }

  // For every ingredient, look at entire inventory and eat the required items. It's assumed that the required items are available.
  for (auto& ingredient : recipe.ingredients)
  {
    for (size_t rowIdx = 0; rowIdx < inv->slots.size(); rowIdx++)
    {
      auto& row = inv->slots[rowIdx];
      for (size_t colIdx = 0; colIdx < row.size(); colIdx++)
      {
        auto& slot = row[colIdx];
        if (slot.id == ingredient.item)
        {
          const auto consumed = glm::min(ingredient.count, (int)slot.count);
          ingredient.count -= consumed;
          slot.count -= consumed;
          if (slot.count <= 0)
          {
            inv->OverwriteSlot(world, {rowIdx, colIdx}, {});
          }
        }
      }
    }
  }

  // For each output, try to stack it with an existing slot. Otherwise, put it in a free spot. If there is no free spot, drop it.
  for (auto& output : recipe.output)
  {
    auto item = ItemState{output.item, output.count};
    inv->TryStackItem(world, item);
    if (item.count > 0)
    {
      if (auto slot = inv->GetFirstEmptySlot())
      {
        inv->OverwriteSlot(world, *slot, item, parent);
      }
      else
      {
        const auto& t = world.GetRegistry().get<const GlobalTransform>(parent);
        world.CreateDroppedItem(item, t.position, t.rotation, t.scale);
      }
    }
  }
}

std::vector<ItemIdAndCount> RandomLootDrop::Sample(PCG::Rng& rng) const
{
  auto items = std::vector<ItemIdAndCount>();
  for (int i = 0; i < count; i++)
  {
    if (rng.RandFloat() < chanceForOne)
    {
      items.emplace_back(item, 1);
    }
  }
  return items;
}

std::vector<ItemIdAndCount> PoolLootDrop::Sample(PCG::Rng& rng) const
{
  ASSERT(!pool.empty());
  if (rng.RandFloat() <= chance)
  {
    const auto sampled = (int)rng.RandFloat(0.5f, GetTotalWeight() + 0.5f);

    int sum = 0;
    for (const auto& element : pool)
    {
      sum += element.weight;
      if (sampled >= sum)
      {
        return element.items;
      }
    }
  }
  
  return {};
}

int PoolLootDrop::GetTotalWeight() const
{
  int sum = 0;
  for (const auto& element : pool)
  {
    sum += element.weight;
  }
  return sum;
}

std::vector<ItemIdAndCount> LootDrops::Collect(PCG::Rng& rng) const
{
  auto items = std::vector<ItemIdAndCount>();

  for (const auto& drop : drops)
  {
    auto sampled = std::vector<ItemIdAndCount>();

    if (auto* r = std::get_if<RandomLootDrop>(&drop))
    {
      sampled = r->Sample(rng);
    }
    if (auto* p = std::get_if<PoolLootDrop>(&drop))
    {
      sampled = p->Sample(rng);
    }

    items.insert(items.end(), sampled.begin(), sampled.end());
  }

  return items;
}

void LootRegistry::Add(std::string name, std::unique_ptr<LootDrops>&& lootDrops)
{
  nameToLoot_.emplace(std::move(name), std::move(lootDrops));
}

const LootDrops* LootRegistry::Get(const std::string& name)
{
  if (auto it = nameToLoot_.find(name); it != nameToLoot_.end())
  {
    return it->second.get();
  }
  return nullptr;
}

void NpcSpawnDirector::Update(float dt)
{
  accumulator += dt;

  auto& registry = world_->GetRegistry();
  auto& rng      = registry.ctx().get<PCG::Rng>();
  auto& grid     = registry.ctx().get<TwoLevelGrid>();

  while (accumulator >= timeBetweenSpawns)
  {
    accumulator -= timeBetweenSpawns;

    constexpr size_t MAX_ENEMIES = 20;

    for (auto&& [entity, player, transform] : registry.view<const Player, const GlobalTransform>().each())
    {
      for (const auto& pDefinition : registry.ctx().get<EntityPrefabRegistry>().GetAllPrefabs())
      {
        if (registry.view<Enemy>().size() >= MAX_ENEMIES)
        {
          continue;
        }

        const auto& info = pDefinition->GetCreateInfo();

        if (rng.RandFloat() > info.spawnChance)
        {
          continue;
        }

        // Multiple attempts to spawn the entity in case spawn positions are invalid.
        for (int attempt = 0; attempt < 10; attempt++)
        {
          if (auto realPos = SampleWalkablePosition(grid, rng, transform.position, info.minSpawnDistance, info.maxSpawnDistance, info.canSpawnFloating))
          {
            auto newEntity = pDefinition->Spawn(*world_, *realPos);
            spdlog::debug("[NpcSpawnDirector] Spawned {}: {}", pDefinition->GetCreateInfo().name, entt::to_integral(newEntity));
            break;
          }
        }
      }
    }
  }
}

void ArmorAndAccessories::OverwriteSlot([[maybe_unused]] World& world, Slot slot, ItemState itemState)
{
  slots[slot] = itemState;
}
