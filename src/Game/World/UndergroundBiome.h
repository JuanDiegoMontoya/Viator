#pragma once
#include "Core/ClassImplMacros.h"
#include "Game/BlockFwd.h"

#include <functional>

namespace Core::DSP
{
  template<size_t Dim, typename>
    requires(Dim >= 1 && Dim <= 3)
  class Image;
}

class World;
struct MapGenInfo;

enum class UndergroundBiome
{
  DesertCaves,
  FunkyCaves,
  Corruption,
  // NOTE: the last biome in this enum is the default!
  SurfaceCaves,
  COUNT,
};

class UndergroundBiomeNoise
{
public:
  NO_COPY_NO_MOVE(UndergroundBiomeNoise);

  struct CreateInfo
  {
    const World* world         = nullptr;
    BlockId substrateBlockType = voxel_t::Null;
  };

  explicit UndergroundBiomeNoise(const CreateInfo& createInfo) : createInfo_(createInfo) {}
  virtual ~UndergroundBiomeNoise() = default;

  [[nodiscard]] virtual float GetWeight([[maybe_unused]] glm::ivec3 posWS) = 0;

  [[nodiscard]] virtual bool BroadPhase([[maybe_unused]] glm::ivec3 posTL)
  {
    return true;
  }

  [[nodiscard]] virtual BlockId GetSubstrateBlockType() const
  {
    return createInfo_.substrateBlockType;
  }

  [[nodiscard]] virtual Core::DSP::Image<3, float> GenImageForChunk(glm::ivec3 posTL,
    [[maybe_unused]] glm::ivec3 dimsTL,
    [[maybe_unused]] const MapGenInfo& mapGenInfo) = 0;

private:
  CreateInfo createInfo_;
};

std::array<std::unique_ptr<UndergroundBiomeNoise>, int(UndergroundBiome::COUNT)> GetUndergroundBiomeNoises(const World& world);

UndergroundBiome GetUndergroundBiomeAtPosition(const std::array<std::unique_ptr<UndergroundBiomeNoise>, int(UndergroundBiome::COUNT)>& biomes,
  glm::ivec3 positionWS,
  const std::function<void(UndergroundBiome, float)>& callback);