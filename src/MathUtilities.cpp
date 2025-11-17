#include "MathUtilities.h"
#include "Core/Assert2.h"

#include "glm/packing.hpp"
#include "glm/gtc/constants.hpp"

#include <cstdint>
#include <utility>
#include <cmath>

#define PI (3.14159265f)

namespace
{
  glm::vec2 SignNotZero(glm::vec2 v)
  {
    return glm::vec2((v.x >= 0.0f) ? +1.0f : -1.0f, (v.y >= 0.0f) ? +1.0f : -1.0f);
  }
} // namespace

glm::mat4 Math::InfReverseZPerspectiveRH(float fovY_radians, float aspectWbyH, float zNear)
{
  float f = 1.0f / tan(fovY_radians / 2.0f);
  // clang-format off
  return {
    f / aspectWbyH, 0.0f, 0.0f, 0.0f,
    0.0f, -f, 0.0f, 0.0f, // Negate [1][1] to work with Vulkan
    0.0f, 0.0f, 0.0f, -1.0f,
    0.0f, 0.0f, zNear, 0.0f
  };
  // clang-format on
}

glm::mat4 Math::InfReverseZPerspectiveLH(float fovY_radians, float aspectWbyH, float zNear)
{
  auto mat = InfReverseZPerspectiveRH(fovY_radians, aspectWbyH, zNear);
  mat[2][3] *= -1;
  return mat;
}

void Math::MakeFrustumPlanes(const glm::mat4& viewProj, glm::vec4 (&planes)[6])
{
  for (auto i = 0; i < 4; ++i)
  {
    planes[0][i] = viewProj[i][3] + viewProj[i][0];
  }
  for (auto i = 0; i < 4; ++i)
  {
    planes[1][i] = viewProj[i][3] - viewProj[i][0];
  }
  for (auto i = 0; i < 4; ++i)
  {
    planes[2][i] = viewProj[i][3] + viewProj[i][1];
  }
  for (auto i = 0; i < 4; ++i)
  {
    planes[3][i] = viewProj[i][3] - viewProj[i][1];
  }
  for (auto i = 0; i < 4; ++i)
  {
    planes[4][i] = viewProj[i][3] + viewProj[i][2];
  }
  for (auto i = 0; i < 4; ++i)
  {
    planes[5][i] = viewProj[i][3] - viewProj[i][2];
  }

  for (auto& plane : planes)
  {
    plane /= glm::length(glm::vec3(plane));
    plane.w = -plane.w;
  }
}

glm::vec3 Math::UnprojectUV_ZO(float depth, glm::vec2 uv, const glm::mat4& invXProj)
{
  glm::vec4 ndc   = glm::vec4(uv * 2.0f - 1.0f, depth, 1.0f);
  glm::vec4 world = invXProj * ndc;
  return glm::vec3(world) / world.w;
}

glm::vec2 Math::Vec3ToOct(glm::vec3 v)
{
  glm::vec2 p = glm::vec2{v.x, v.y} * (1.0f / (abs(v.x) + abs(v.y) + abs(v.z)));
  return (v.z <= 0.0f) ? ((1.0f - glm::abs(glm::vec2{p.y, p.x})) * SignNotZero(p)) : p;
}

glm::vec3 Math::OctToVec3(glm::vec2 e)
{
  using glm::vec2;
  using glm::vec3;
  vec3 v           = vec3(vec2(e.x, e.y), 1.0f - abs(e.x) - abs(e.y));
  vec2 signNotZero = vec2((v.x >= 0.0f) ? +1.0f : -1.0f, (v.y >= 0.0f) ? +1.0f : -1.0f);
  if (v.z < 0.0f)
  {
    vec2(v.x, v.y) = (1.0f - abs(vec2(v.y, v.x))) * signNotZero;
  }
  return normalize(v);
}

glm::vec3 Math::OctToVec3(uint32_t snorm)
{
  return OctToVec3(glm::unpackSnorm2x16(snorm));
}

Math::SuffixAndDivisor Math::BytesToSuffixAndDivisor(uint64_t bytes)
{
  const auto* suffix = "B";
  double divisor     = 1.0;
  if (bytes > 1000)
  {
    suffix  = "KB";
    divisor = 1000;
  }
  if (bytes > 1'000'000)
  {
    suffix  = "MB";
    divisor = 1'000'000;
  }
  if (bytes > 1'000'000'000)
  {
    suffix  = "GB";
    divisor = 1'000'000'000;
  }
  return {suffix, divisor};
}

glm::vec3 Math::RandVecInCone(glm::vec2 xi, glm::vec3 N, float angle)
{
  float phi = 2.0f * glm::pi<float>() * xi.x;

  float theta    = sqrt(xi.y) * angle;
  float cosTheta = cos(theta);
  float sinTheta = sin(theta);

  glm::vec3 H;
  H.x = cos(phi) * sinTheta;
  H.y = sin(phi) * sinTheta;
  H.z = cosTheta;

  glm::vec3 up        = abs(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
  glm::vec3 tangent   = normalize(cross(up, N));
  glm::vec3 bitangent = cross(N, tangent);
  glm::mat3 tbn       = glm::mat3(tangent, bitangent, N);

  glm::vec3 sampleVec = tbn * H;
  return normalize(sampleVec);
}

float Math::Distance2(glm::vec3 a, glm::vec3 b)
{
  return glm::dot(a - b, a - b);
}

float Math::Distance2(glm::vec2 a, glm::vec2 b)
{
  return glm::dot(a - b, a - b);
}

glm::vec3 Math::Project(glm::vec3 a, glm::vec3 b)
{
  return b * glm::dot(a, b) / glm::dot(b, b);
}

float Math::PointLineSegmentDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b)
{
  DEBUG_ASSERT(glm::any(glm::greaterThan(glm::abs(b - a), glm::vec3(1e-4f))));
  glm::vec3 pa = p - a, ba = b - a;
  // Vector projection but fraction is clamped.
  float h = glm::clamp(dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
  return glm::distance(pa, ba * h);
}

glm::vec3 Math::SphericalToCartesian(float elevation, float azimuth, float radius)
{
  return {radius * std::sin(elevation) * std::cos(azimuth), radius * std::cos(elevation), radius * std::sin(elevation) * std::sin(azimuth)};
}

float Math::Ease(float t, Easing easing)
{
  switch (easing)
  {
  case Easing::LINEAR: return t;
  case Easing::EASE_IN_SINE: return EaseInSine(t);
  case Easing::EASE_OUT_SINE: return EaseOutSine(t);
  case Easing::EASE_IN_OUT_BACK: return EaseInOutBack(t);
  case Easing::EASE_IN_CUBIC: return EaseInCubic(t);
  case Easing::EASE_OUT_CUBIC: return EaseOutCubic(t);
  default: assert(false); return t;
  }
}

float Math::EaseInSine(float t)
{
  return 1 - std::cos(t * PI / 2);
}

float Math::EaseOutSine(float t)
{
  return std::sin(t * PI / 2);
}

float Math::EaseInOutBack(float t)
{
  constexpr auto c1 = 1.70158f;
  constexpr auto c2 = c1 * 1.525f;

  return t < 0.5 ? (std::pow(2 * t, 2.0f) * ((c2 + 1) * 2 * t - c2)) / 2 : (std::pow(2 * t - 2, 2.0f) * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2;
}

float Math::EaseInCubic(float t)
{
  return t * t * t;
}

float Math::EaseOutCubic(float t)
{
  const float t1 = 1.0f - t;
  return 1.0f - t1 * t1 * t1;
}

float Math::Remap(float x, float start1, float end1, float start2, float end2)
{
  DEBUG_ASSERT(start1 != end1);
  DEBUG_ASSERT(start2 != end2);
  const auto norm = (x - start1) / (end1 - start1);
  return norm * (end2 - start2) + start2;
}

bool Math::Intersect::BoxVsBox(glm::vec3 b1min, glm::vec3 b1max, glm::vec3 b2min, glm::vec3 b2max)
{
  DEBUG_ASSERT(glm::all(glm::greaterThan(b1max, b1min)));
  DEBUG_ASSERT(glm::all(glm::greaterThan(b2max, b2min)));
  return !(glm::any(glm::lessThan(b2max, b1min)) || glm::any(glm::greaterThan(b2min, b1max)));
}

float Math::SDF::Box(glm::vec3 p, glm::vec3 b)
{
  glm::vec3 q = glm::abs(p) - b;
  return glm::length(glm::max(q, 0.0f)) + glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f);
}

float Math::SDF::Box(glm::vec2 p, glm::vec2 b)
{
  return Box(glm::vec3(p, 0.0f), glm::vec3(b, 0.0f));
}

float Math::GaussianNorm(float x, float mean, float stddev)
{
  const float factor = 1.0f / (stddev * glm::root_two_pi<float>());
  const float num    = (x - mean) * (x - mean);
  const float den    = stddev * stddev;
  return factor * std::exp(-0.5f * (num / den));
}
