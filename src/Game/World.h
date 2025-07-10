#pragma once
#include "RegistryProxy.h"
#include "ClassImplMacros.h"
#include "Game.h"
#include "ItemFwd.h"
#include "entt/entity/entity.hpp"
#include "entt/entity/registry.hpp"
#include <optional>

namespace PCG
{
  struct Rng;
}

class Audio;

class World
{
public:
  NO_COPY_NO_MOVE(World);
  explicit World() : registry_(registryOld_) {}
  void FixedUpdate(float dt);

  RegistryProxy& GetRegistry()
  {
    return registry_;
  }

  const RegistryProxy& GetRegistry() const
  {
    return registry_;
  }

  entt::registry& GetRegistryRaw()
  {
    return registryOld_;
  }

  const entt::registry& GetRegistryRaw() const
  {
    return registryOld_;
  }

  PCG::Rng& Rng()
  {
    return registry_.ctx().get<PCG::Rng>();
  }

  void InitializeGameState();

  void InitializeGameDefinitions();

  struct MapGenInfo
  {
    int seed        = 1234;
    int worldHeight = 600;
    int seaLevel    = 400;
    int surfaceThickness = 80;
    bool spawnYggdrasil  = false;
  };

  void CreateGrid(glm::ivec3 numChunks);
  void CreateInitialEntities();
  void GenerateMap(const MapGenInfo& mapGenInfo);

  // Adds LocalTransform, GlobalTransform, InterpolatedTransform, RenderTransform, and Hierarchy components.
  entt::entity CreateRenderableEntityNoHashGrid(glm::vec3 position, glm::quat rotation = glm::quat(1, 0, 0, 0), float scale = 1);
  entt::entity CreateRenderableEntity(glm::vec3 position, glm::quat rotation = glm::quat(1, 0, 0, 0), float scale = 1);
  entt::entity CreateDroppedItem(ItemState item, glm::vec3 position, glm::quat rotation = {1, 0, 0, 0}, float scale = 1);

  [[nodiscard]] entt::entity TryGetLocalPlayer();
  [[nodiscard]] const GlobalTransform* TryGetLocalPlayerTransform();

  void SetLocalScale(entt::entity entity, float scale);
  [[nodiscard]] entt::entity GetChildNamed(entt::entity entity, std::string_view name) const;

  // Look for ancestor with LinearVelocity and return its value. Otherwise, return 0.
  [[nodiscard]] glm::vec3 GetInheritedLinearVelocity(entt::entity entity);

  // Travels up hierarchy, searching for TeamFlags component.
  [[nodiscard]] const TeamFlags* GetTeamFlags(entt::entity entity) const;

  template<typename T>
  [[nodiscard]] std::pair<entt::entity, T*> GetComponentFromAncestor(entt::entity entity)
  {
    assert(registry_.valid(entity));
    if (auto* component = registry_.try_get<T>(entity))
    {
      return {entity, component};
    }
    if (auto* h = registry_.try_get<Hierarchy>(entity); h && h->parent != entt::null)
    {
      return GetComponentFromAncestor<T>(h->parent);
    }
    return {entt::null, nullptr};
  }

  template<typename T>
  [[nodiscard]] bool AncestorHasComponent(entt::entity entity)
  {
    assert(registry_.valid(entity));
    if (registry_.all_of<T>(entity))
    {
      return true;
    }
    if (auto* h = registry_.try_get<Hierarchy>(entity); h && h->parent != entt::null)
    {
      return AncestorHasComponent<T>(h->parent);
    }
    return false;
  }

  template<typename T>
  [[nodiscard]] std::pair<entt::entity, T*> GetComponentFromDescendant(entt::entity entity)
  {
    assert(registry_.valid(entity));
    if (auto* component = registry_.try_get<T>(entity))
    {
      return {entity, component};
    }
    if (auto* h = registry_.try_get<Hierarchy>(entity))
    {
      for (auto child : h->children)
      {
        if (auto pair = GetComponentFromDescendant<T>(child); pair.first != entt::null)
        {
          return pair;
        }
      }
    }
    return {entt::null, nullptr};
  }

  template<typename T>
  [[nodiscard]] std::pair<entt::entity, T*> GetComponentFromAncestorOrDescendant(entt::entity entity)
  {
    if (auto pair = GetComponentFromAncestor<T>(entity); pair.first != entt::null)
    {
      return pair;
    }
    return GetComponentFromDescendant<T>(entity);
  }

  entt::entity CreatePlayer();
  Physics::CharacterController& GivePlayerCharacterController(entt::entity playerEntity);
  Physics::CharacterControllerShrimple& GivePlayerCharacterControllerShrimple(entt::entity playerEntity);
  FlyingCharacterController& GivePlayerFlyingCharacterController(entt::entity playerEntity);

  void GivePlayerColliders(entt::entity playerEntity);

  // Remove character controller and collision, and give ghost component
  void KillPlayer(entt::entity playerEntity);

  // Restore character controller and collision, and remove ghost component
  void RespawnPlayer(entt::entity playerEntity);

  // Apply damage and resistances to an entity. Does not apply knockback.
  // Returns the amount of damage actually applied.
  float DamageEntity(entt::entity entity, float damage);

  [[nodiscard]] bool CanEntityDamageEntity(entt::entity entitySource, entt::entity entityTarget) const;

  [[nodiscard]] bool AreEntitiesEnemies(entt::entity entity1, entt::entity entity2) const;

  [[nodiscard]] std::vector<entt::entity> GetEntitiesInSphere(glm::vec3 center, float radius) const;
  [[nodiscard]] std::vector<entt::entity> GetEntitiesInCapsule(glm::vec3 start, glm::vec3 end, float radius);

  [[nodiscard]] entt::entity GetNearestPlayer(glm::vec3 position);

  // Returns the amount of damage successfully inflicted.
  float DamageBlock(glm::ivec3 voxelPos, float damage, int damageTier, BlockDamageFlags damageType);

  const BlockDefinition& GetBlockDefinitionFromItem(ItemId item);
  ItemId GetItemIdFromBlock(BlockId block);

  [[nodiscard]] entt::entity GetBlockEntity(glm::ivec3 voxelPosition);

  // Given an entity which is part of a hierarchy, return the highest-level entity in the tree.
  // The returned entity could be the same as the one passed in.
  [[nodiscard]] entt::entity GetRootEntityOfHierarchy(entt::entity entity) const;

  [[nodiscard]] glm::vec3 GetFootPosition(entt::entity entity);
  [[nodiscard]] float GetHeight(entt::entity entity);

  // Call to propagate local transform updates to global transform and children.
  void UpdateLocalTransform(entt::entity entity, int depth = 0);
  void SetParent(entt::entity child, entt::entity parent);

  [[nodiscard]] uint64_t GetTicks() const
  {
    return ticks_;
  }

  // True if this process has a networked client.
  // Clients only simulate a limited set of the game.
  [[nodiscard]] bool IsClient() const;

  // Equivalent to !IsClient. True if this process is running the master simulation.
  [[nodiscard]] bool IsServer() const;

  // True if this process has a networked server. Implies that this process
  // is running the master simulation AND there are one or more remote peers.
  [[nodiscard]] bool IsHosting() const;

  [[nodiscard]] Audio* GetAudio();

  void CreateRenderingMaterials();

private:
  uint64_t ticks_ = 0;
  entt::registry registryOld_;
  RegistryProxy registry_;
};

std::optional<glm::vec3> SampleWalkablePosition(const TwoLevelGrid& grid, PCG::Rng& rng, glm::vec3 origin, float minDistance, float maxDistance, bool isAirWalkable);
