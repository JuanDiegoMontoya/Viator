#pragma once
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

#include <string>
#include <optional>
#include <cstdint>

namespace Game2::Render
{
  enum class ParticleFlag : uint32_t
  {
    Solid                  = 1 << 0,
    UseSkyShadowMap        = 1 << 2,
    UseBeerShadowMap       = 1 << 3,
    DestroyOnCollision     = 1 << 5,
    ForceUpPosY            = 1 << 6,
    ForceUpPosZ            = 1 << 7,
    ForceRightPosX         = 1 << 8,
    CollideWithTranslucent = 1 << 9,
  };

  FVOG_DECLARE_FLAG_TYPE(ParticleFlags, ParticleFlag, uint32_t);

  struct Particle
  {
    ParticleFlags flags{};
    std::optional<std::string> baseColorTexture;
    glm::vec4 initialBaseColorFactor = {1, 1, 1, 1};
    glm::vec4 finalBaseColorFactor = {1, 1, 1, 1};
    glm::vec3 position{};
    glm::vec3 velocity{};
    glm::vec3 acceleration{};
    std::optional<std::string> particleArchetypeToSpawnOnHit;
    glm::vec2 initialScale = {1, 1};
    glm::vec2 finalScale = {1, 1};
    float life{};
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