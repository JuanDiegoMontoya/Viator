#pragma once
#include "entt/entity/fwd.hpp"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

#include <string>
#include <vector>

// TODO: not a component
struct DeltaTime
{
  float game;     // Affected by game effects that scale the passage of time.
  float real;     // Real time, unaffected by gameplay, inexorably marching on.
  float fraction; // The fraction of a game tick that has progressed since the last. Used for interpolation.
};

struct Name
{
  // TODO: Should be variant<const char*, std::string> and have helper to extract C string.
  // We don't want to force an allocation for a name that's probably going to be a literal.
  std::string name;
};

struct LocalTransform
{
  glm::vec3 position;
  glm::quat rotation;
  float scale;
};

struct GlobalTransform
{
  glm::vec3 position;
  glm::quat rotation;
  float scale;
};

struct Hierarchy
{
  void AddChild(entt::entity child);
  void RemoveChild(entt::entity child);

  entt::entity parent = entt::null;
  std::vector<entt::entity> children;

  bool useLocalPositionAsGlobal = false;
  bool useLocalRotationAsGlobal = false;
};

struct DeferredDelete {};

struct LocalAuthoritative {};
