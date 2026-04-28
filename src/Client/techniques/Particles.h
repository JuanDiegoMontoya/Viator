#pragma once
#include "Client/Fvog/BasicTypes2.h"
#include "Client/Fvog/detail/VkFwd.h"

#include "glm/mat4x4.hpp"

#include <memory>
#include <span>
#include <string>

class VoxelRenderer;

namespace Game2::Render
{
  struct Particle;
  struct ParticleArchetypeSpawnInfo;
  struct ParticleArchetype;
}

namespace Techniques
{
  struct ParticlesCreateParams
  {
    VoxelRenderer* renderer{};
    std::initializer_list<Fvog::Format> gBufferFormats;
    Fvog::Format gDepthFormat{};
  };

  struct ParticlesSpawnParams
  {
    uint32_t frameNumber{};
    VkDeviceAddress skyShadowMap{};
    VkDeviceAddress skyBeerShadowMap{};
  };

  struct ParticlesUpdateParams
  {
    VkDeviceAddress globalUniforms{};
  };

  struct ParticlesRenderParams
  {
    VkDeviceAddress globalUniforms{};
  };

  class Particles
  {
  public:
    static std::unique_ptr<Particles> Create(const ParticlesCreateParams& params);

    virtual ~Particles() = default;

    virtual void RegisterArchetype(std::string name, const Game2::Render::ParticleArchetype& archetype) = 0;

    virtual void PushSingleParticles(std::span<const Game2::Render::Particle> singleParticles)                          = 0;
    virtual void PushParticleArchetypes(std::span<const Game2::Render::ParticleArchetypeSpawnInfo> archetypeSpawnInfos) = 0;

    virtual void Spawn(VkCommandBuffer cmd, const ParticlesSpawnParams& params)   = 0;
    virtual void Update(VkCommandBuffer cmd, const ParticlesUpdateParams& params) = 0;
    virtual void Render(VkCommandBuffer cmd, const ParticlesRenderParams& params) = 0;
  };
}