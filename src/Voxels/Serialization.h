#pragma once
#include "entt/fwd.hpp"

#include <filesystem>
#include <vector>
#include <span>
#include <unordered_map>

class World;
struct TwoLevelGrid;
struct InputState;

namespace Core::Serialization
{
  void Initialize();

  void SaveRegistryToFile(const World& world, const std::filesystem::path& path);
  void LoadRegistryFromFile(World& world, const std::filesystem::path& path);

  std::vector<char> SerializeEntity(const World& world, entt::entity entity);
  void DeserializeEntity(World& world, std::span<const char> entityBytes, std::unordered_map<entt::entity, entt::entity>& remoteToLocal);

  // Contains entities that may be part of the same hierarchy.
  // This is like, totally temporary.
  struct SerializedEntityBundle
  {
    std::vector<entt::entity> entities;
    std::vector<std::vector<char>> serializedEntities;
  };

  std::vector<char> SerializeEntityBundle(const SerializedEntityBundle& bundle);
  SerializedEntityBundle DeserializeEntityBundle(std::span<const char> bundleBytes);

  std::vector<char> SerializeTwoLevelGrid(const TwoLevelGrid& grid);
  std::unique_ptr<TwoLevelGrid> DeserializeTwoLevelGrid(std::span<const char> gridBytes);

  struct Packet
  {
    entt::id_type type{};
    std::vector<char> bytes;
  };

  std::vector<char> SerializePacket(const Packet& packet);
  Packet DeserializePacket(std::span<const char> packetBytes);

  std::vector<char> SerializeInputState(const InputState& inputState);
  InputState DeserializeInputState(std::span<const char> inputStateBytes);

  std::vector<char> SerializeEntityId(entt::entity entity);
  entt::entity DeserializeEntityId(std::span<const char> entityIdBytes);
}
