#pragma once
#include <variant>

#include "glm/vec3.hpp"

namespace Physics
{
  struct Sphere
  {
    bool operator==(const Sphere&) const = default;
    float radius{};
  };

  struct Capsule
  {
    bool operator==(const Capsule&) const = default;
    float radius{};
    float cylinderHalfHeight{};
  };

  struct Box
  {
    bool operator==(const Box&) const = default;
    glm::vec3 halfExtent{};
  };

  struct Plane
  {
    bool operator==(const Plane&) const = default;
    glm::vec3 normal{};
    float constant{};
  };

  struct UseTwoLevelGrid
  {
    bool operator==(const UseTwoLevelGrid&) const = default;
  };

  using PolyShape = std::variant<std::monostate, Sphere, Capsule, Box, Plane, UseTwoLevelGrid>;
}
