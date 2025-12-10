#pragma once
#include "Core/ClassImplMacros.h"
#include "Game/BlockFwd.h"

#include <array>

namespace Core::DSP
{
  template<size_t Dim, typename>
    requires(Dim >= 1 && Dim <= 3)
  class Image;
}

class World;
struct MapGenInfo;

enum class SurfaceBiome : uint8_t
{
  Desert,
  Corruption,
  Snow,
  Ocean,
  Rivers,
  // NOTE: the last biome in this list is the default!
  Forest,
  COUNT,
};

class SurfaceBiomeNoise
{
public:
  NO_COPY_NO_MOVE(SurfaceBiomeNoise);

  struct CreateInfo
  {
    const World* world          = nullptr;
    BlockId surfaceBlockType    = voxel_t::Null;
    BlockId subsurfaceBlockType = voxel_t::Null;
  };

  explicit SurfaceBiomeNoise(const CreateInfo& createInfo) : createInfo_(createInfo) {}
  virtual ~SurfaceBiomeNoise() = default;

  [[nodiscard]] virtual Core::DSP::Image<2, float> GenImageForChunk(glm::ivec2 posTL, const MapGenInfo& mapGenInfo) = 0;

  [[nodiscard]] virtual bool BroadPhase([[maybe_unused]] glm::ivec2 posTL)
  {
    return true;
  }

  [[nodiscard]] virtual float GetWeight(glm::ivec2 posWS) = 0;

  virtual void PlaceSurfaceFeatures(World& world, const MapGenInfo& mapGenInfo, glm::ivec3 posWS) = 0;

  [[nodiscard]] virtual int GetSurfaceThickness() const
  {
    return 1;
  }

  [[nodiscard]] virtual int GetSubsurfaceThickness() const
  {
    return 0;
  }

  [[nodiscard]] virtual BlockId GetSurfaceBlockType() const
  {
    return createInfo_.surfaceBlockType;
  }

  [[nodiscard]] virtual BlockId GetSubsurfaceBlockType() const
  {
    return createInfo_.subsurfaceBlockType;
  }

private:
  CreateInfo createInfo_;
};

std::array<std::unique_ptr<SurfaceBiomeNoise>, int(SurfaceBiome::COUNT)> GetSurfaceBiomeNoises(const World& world);