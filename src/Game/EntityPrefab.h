#pragma once
#include "EntityPrefabFwd.h"
#include "Core/ClassImplMacros.h"
#include "World/SurfaceBiome.h"
#include "World/UndergroundBiome.h"

#include "entt/entity/fwd.hpp"

#include <span>
#include <string>
#include <vector>
#include <unordered_map>

class World;

// Workaround: this would normally be a sub-class of EntityPrefabDefinition, but GCC and Clang cannot compile it.
struct EntityPrefabDefinitionCreateInfo
{
  std::string name       = "entity";
  float minSpawnDistance = 30;
  float maxSpawnDistance = 90;
  bool canSpawnFloating  = false;
  bool isVisible         = true;
  std::unordered_map<SurfaceBiome, float> surfaceBiomeSpawnChance;
  std::unordered_map<UndergroundBiome, float> undergroundBiomeSpawnChance;
};

class EntityPrefabDefinition
{
public:
  explicit EntityPrefabDefinition(EntityPrefabDefinitionCreateInfo createInfo = {}) : info_(std::move(createInfo)) {}
  DEFAULT_MOVE(EntityPrefabDefinition);
  NO_COPY(EntityPrefabDefinition);

  virtual ~EntityPrefabDefinition() = default;

  virtual entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation = glm::identity<glm::quat>()) const = 0;

  [[nodiscard]] const EntityPrefabDefinitionCreateInfo& GetCreateInfo() const;

  [[nodiscard]] float GetSurfaceBiomeSpawnChance(SurfaceBiome biome) const;

  [[nodiscard]] float GetUndergroundBiomeSpawnChance(UndergroundBiome biome) const;

protected:
  EntityPrefabDefinitionCreateInfo info_;
};

class EntityPrefabRegistry
{
public:
  EntityPrefabRegistry() = default;

  ~EntityPrefabRegistry() = default;

  NO_COPY(EntityPrefabRegistry);
  DEFAULT_MOVE(EntityPrefabRegistry);

  [[nodiscard]] const EntityPrefabDefinition& Get(const std::string& name) const;

  [[nodiscard]] const EntityPrefabDefinition& Get(EntityPrefabId id) const;

  [[nodiscard]] EntityPrefabId GetId(const std::string& name) const;

  EntityPrefabId Add(const std::string& name, EntityPrefabDefinition* entityPrefabDefinition);

  std::span<const std::unique_ptr<EntityPrefabDefinition>> GetAllPrefabs() const;

private:
  std::unordered_map<std::string, EntityPrefabId> nameToId_;
  std::vector<std::unique_ptr<EntityPrefabDefinition>> idToDefinition_;
};


void RegisterDefaultEntityPrefabs(EntityPrefabRegistry& entityPrefabRegistry);