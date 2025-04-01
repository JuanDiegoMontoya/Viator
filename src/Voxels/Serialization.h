#pragma once
#include "Assert2.h"

#include "entt/fwd.hpp"
#include "entt/meta/resolve.hpp"
#include "entt/meta/meta.hpp"

#include <filesystem>
#include <vector>
#include <span>
#include <unordered_map>
#include <iosfwd>

class World;
struct TwoLevelGrid;
struct InputState;

namespace Core::Serialization
{
  void Initialize();

  void SaveRegistryToFile(const World& world, const std::filesystem::path& path);
  void LoadRegistryFromFile(World& world, const std::filesystem::path& path);

  void SerializeEntity(std::stringstream& stream, const World& world, entt::entity entity);
  void DeserializeEntity(std::stringstream& stream, World& world, std::unordered_map<entt::entity, entt::entity>& remoteToLocal);

  // Serialize and deserialize anything to binary.
  void SerializeObjectStream(std::stringstream& stream, entt::meta_any object);
  entt::meta_any DeserializeObjectStream(std::stringstream& stream, const entt::meta_type& type);

  template<typename T>
  T DeserializeObjectStream(std::stringstream& stream)
  {
    auto meta = DeserializeObjectStream(stream, entt::resolve<T>());
    auto object = meta.try_cast<T>();
    ASSERT(object);
    return std::move(*object); // Required for non-copyable types.
  }

  std::vector<char> SerializeObject(entt::meta_any object);
  entt::meta_any DeserializeObject(std::span<const char> objectBytes);
}
