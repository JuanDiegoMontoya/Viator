#pragma once
#include "EntityPrefabFwd.h"
#include "Core/ClassImplMacros.h"

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
  float spawnChance      = 0;
  float minSpawnDistance = 30;
  float maxSpawnDistance = 90;
  bool canSpawnFloating  = false;
  bool isVisible         = true;
};

class EntityPrefabDefinition
{
public:
  explicit EntityPrefabDefinition(const EntityPrefabDefinitionCreateInfo& createInfo = {}) : info_(createInfo) {}
  DEFAULT_MOVE(EntityPrefabDefinition);
  NO_COPY(EntityPrefabDefinition);

  virtual ~EntityPrefabDefinition() = default;

  virtual entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation = glm::identity<glm::quat>()) const = 0;

  [[nodiscard]] const EntityPrefabDefinitionCreateInfo& GetCreateInfo() const
  {
    return info_;
  }

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

  [[nodiscard]] const EntityPrefabDefinition& Get(const std::string& name) const
  {
    return *idToDefinition_.at(nameToId_.at(name));
  }
  [[nodiscard]] const EntityPrefabDefinition& Get(EntityPrefabId id) const
  {
    return *idToDefinition_.at(id);
  }
  [[nodiscard]] EntityPrefabId GetId(const std::string& name) const;

  EntityPrefabId Add(const std::string& name, EntityPrefabDefinition* entityPrefabDefinition)
  {
    const auto myId = static_cast<uint32_t>(idToDefinition_.size());
    nameToId_.emplace(name, myId);
    idToDefinition_.emplace_back(entityPrefabDefinition);
    return myId;
  }

  std::span<const std::unique_ptr<EntityPrefabDefinition>> GetAllPrefabs() const
  {
    return std::span(idToDefinition_);
  }

private:
  std::unordered_map<std::string, EntityPrefabId> nameToId_;
  std::vector<std::unique_ptr<EntityPrefabDefinition>> idToDefinition_;
};
















class MeleeFrogDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat) const override;
};

class FlyingFrogDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat) const override;
};

class WormBossDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat) const override;
};

class TorchDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation) const override;
};

class ChestDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation) const override;
};

class ShrimpleMeshPrefabDefinition : public EntityPrefabDefinition
{
public:
  explicit ShrimpleMeshPrefabDefinition(std::string_view model, glm::vec3 tint = {1, 1, 1}, const EntityPrefabDefinitionCreateInfo& createInfo = {})
    : EntityPrefabDefinition(createInfo), modelName_(model), tint_(tint)
  {
  }

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation) const override;

private:
  std::string modelName_;
  glm::vec3 tint_;
};