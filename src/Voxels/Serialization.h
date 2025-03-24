#pragma once
#include "entt/fwd.hpp"

#include <filesystem>
#include <vector>
#include <span>
#include <unordered_map>

class World;

namespace Core::Serialization
{
  void Initialize();

  void SaveRegistryToFile(const World& world, const std::filesystem::path& path);
  void LoadRegistryFromFile(World& world, const std::filesystem::path& path);

  std::vector<char> SerializeEntity(const World& world, entt::entity entity);
  void DeserializeEntity(World& world, std::span<const char> entityBytes, std::unordered_map<entt::entity, entt::entity>& remoteToLocal);
}
