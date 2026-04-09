#pragma once
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

#include <string>
#include <optional>
#include <cstdint>

namespace Game2::Render
{
  struct Particle
  {
    std::string baseColorTexture;
    glm::vec4 baseColorFactor;
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 acceleration;
    bool isSolid; // Collides with voxels.
    bool spawnParticleOnHit;
    std::string particleArchetypeToSpawnOnHit;
    glm::vec2 initialScale;
    glm::vec2 currentScale;
    glm::vec2 finalScale;
    float initialLife;
    float lifeRemaining;
  };

  struct ParticleArchetype
  {
    Particle prototype;
    glm::vec3 positionOffsetMin;
    glm::vec3 positionOffsetMax;
    glm::vec3 velocityMin;
    glm::vec3 velocityMax;
    glm::vec3 accelerationMin;
    glm::vec3 accelerationMax;
  };

  struct ParticleArchetypeSpawnInfo
  {
    std::string archetypeName;
    int32_t count;
    glm::vec3 positionWS;
    glm::vec3 velocity;
  };
} // namespace Game::Render