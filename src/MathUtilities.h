#pragma once
#include "glm/mat3x3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

namespace Math
{
  glm::mat4 InfReverseZPerspectiveRH(float fovY_radians, float aspectWbyH, float zNear);
  glm::mat4 InfReverseZPerspectiveLH(float fovY_radians, float aspectWbyH, float zNear);

  constexpr uint32_t PreviousPower2(uint32_t x)
  {
    uint32_t v = 1;
    while ((v << 1) < x)
    {
      v <<= 1;
    }
    return v;
  }

  void MakeFrustumPlanes(const glm::mat4& viewProj, glm::vec4 (&planes)[6]);

  // Zero-origin unprojection. E.g., pass sampled depth, screen UV, and invViewProj to get a world-space pos
  glm::vec3 UnprojectUV_ZO(float depth, glm::vec2 uv, const glm::mat4& invXProj);

  glm::vec2 Vec3ToOct(glm::vec3 v);
  glm::vec3 OctToVec3(glm::vec2 e);
  glm::vec3 OctToVec3(uint32_t snorm);

  struct SuffixAndDivisor
  {
    const char* suffix;
    double divisor;
  };

  SuffixAndDivisor BytesToSuffixAndDivisor(uint64_t bytes);

  glm::vec3 RandVecInCone(glm::vec2 xi, glm::vec3 N, float angle);

  float Distance2(glm::vec3 a, glm::vec3 b);
  float Distance2(glm::vec2 a, glm::vec2 b);

  // Vector projection of a onto b
  glm::vec3 Project(glm::vec3 a, glm::vec3 b);

  float PointLineSegmentDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b);

  glm::vec3 SphericalToCartesian(float elevation, float azimuth, float radius = 1.0f);

  enum class Easing : uint32_t
  {
    LINEAR,
    EASE_IN_SINE,
    EASE_OUT_SINE,
    EASE_IN_OUT_BACK,
    EASE_IN_CUBIC,
    EASE_OUT_CUBIC,
  };

  float Ease(float t, Easing easing);

  float EaseInSine(float t);
  float EaseOutSine(float t);
  float EaseInOutBack(float t);
  float EaseInCubic(float t);
  float EaseOutCubic(float t);

  float Remap(float x, float start1, float end1, float start2, float end2);

  namespace SDF
  {
    float Box(glm::vec3 p, glm::vec3 b);
    float Box(glm::vec2 p, glm::vec2 b);
  }

  namespace Intersect
  {
    bool BoxVsBox(glm::vec3 b1min, glm::vec3 b1max, glm::vec3 b2min, glm::vec3 b2max);
  }

  float GaussianNorm(float x, float mean, float stddev);

  glm::vec3 KelvinToSrgb(float kelvin);
} // namespace Math
