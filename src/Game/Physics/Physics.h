#pragma once
#include "Shape.h"
#include "Core/ClassImplMacros.h"

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/Character/Character.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/ShapeCast.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/CollisionCollector.h"
#include "Jolt/Physics/Constraints/TwoBodyConstraint.h"

#include "entt/fwd.hpp"

#include "glm/gtc/quaternion.hpp"

#include <optional>
#include <vector>
#include <variant>

class World;
namespace Voxel
{
  struct Grid;
}

namespace Physics
{
  namespace Layers
  {
    constexpr JPH::ObjectLayer WORLD            = 0;
    constexpr JPH::ObjectLayer CHARACTER        = 1;
    constexpr JPH::ObjectLayer PROJECTILE       = 3;
    constexpr JPH::ObjectLayer DROPPED_ITEM     = 4;
    constexpr JPH::ObjectLayer DEBRIS           = 5;
    constexpr JPH::ObjectLayer HITBOX           = 6;
    // For damage-dealing colliders
    constexpr JPH::ObjectLayer HURTBOX          = 7;
    constexpr JPH::ObjectLayer HITBOX_AND_HURTBOX = 8;
    constexpr JPH::ObjectLayer NO_COLLIDE       = 9;
    constexpr JPH::ObjectLayer INTERACT         = 10;
    constexpr JPH::ObjectLayer NUM_LAYERS       = 11;

    // Cast-only layers
    constexpr JPH::ObjectLayer CAST_WORLD       = 12;
    constexpr JPH::ObjectLayer CAST_PROJECTILE  = 13;
    constexpr JPH::ObjectLayer CAST_CHARACTER   = 14;
    constexpr JPH::ObjectLayer CAST_INTERACT    = 15;
  }

  struct ShapeSettings
  {
    PolyShape shape;
    float density         = 1000; // kg/m^3
    glm::vec3 translation = {0, 0, 0};
    glm::quat rotation    = glm::identity<glm::quat>();
  };

  struct RigidBodySettings
  {
    ShapeSettings shape{};
    bool activate = true;
    bool isSensor = false;
    float gravityFactor = 1;
    JPH::EMotionType motionType = JPH::EMotionType::Dynamic;
    JPH::EMotionQuality motionQuality  = JPH::EMotionQuality::Discrete;
    JPH::ObjectLayer layer = Layers::DEBRIS;
    JPH::EAllowedDOFs degreesOfFreedom = JPH::EAllowedDOFs::All;
  };

  struct RigidBody
  {
    JPH::BodyID body;
  };

  struct CharacterControllerSettings
  {
    ShapeSettings shape;
  };

  struct CharacterController
  {
    JPH::CharacterVirtual* character;
    JPH::CharacterBase::EGroundState previousGroundState;
    glm::vec3 previousPosition;
  };

  struct CharacterControllerShrimpleSettings
  {
    ShapeSettings shape;
  };

  struct CharacterControllerShrimple
  {
    JPH::Character* character;
    JPH::CharacterBase::EGroundState previousGroundState;
  };

  // Automatically added when one of the Add* functions is called.
  struct Shape
  {
    JPH::RefConst<JPH::Shape> shape;
  };

  class Engine
  {
  public:
    static std::unique_ptr<Engine> Create(World& world);

    NO_COPY_NO_MOVE(Engine);
    Engine()          = default;
    virtual ~Engine() = default;

    virtual void RegisterConstraint(JPH::Ref<JPH::TwoBodyConstraint> constraint) = 0;
    virtual [[nodiscard]] std::vector<JPH::TwoBodyConstraint*> GetConstraintsForBody(JPH::BodyID body) = 0;
    virtual void RemoveConstraintsFromBody(JPH::BodyID body) = 0;
    virtual void DestroyConstraint(JPH::TwoBodyConstraint* constraint) = 0;

    virtual [[nodiscard]] const JPH::NarrowPhaseQuery& GetNarrowPhaseQuery() const = 0;
    virtual [[nodiscard]] JPH::BodyInterface& GetBodyInterface() = 0;
    virtual [[nodiscard]] JPH::PhysicsSystem& GetPhysicsSystem() = 0;

    virtual void FixedUpdate(float dt) = 0;

    virtual [[nodiscard]] entt::dispatcher& GetDispatcher() = 0;

    virtual void CreateObservers(entt::registry& registry) = 0;
  };

  struct ContactAddedPair
  {
    entt::entity entity1;
    entt::entity entity2;
    glm::vec3 position;
    glm::vec3 normal;
  };

  struct ContactPersistedPair
  {
    entt::entity entity1;
    entt::entity entity2;
    glm::vec3 position;
    glm::vec3 normal;
  };

  [[nodiscard]] std::unique_ptr<JPH::IgnoreMultipleBodiesFilter> GetIgnoreEntityAndChildrenFilter(entt::handle handle);

  void Initialize();
  void Terminate();

  struct NearestHitCollector : JPH::CastShapeCollector
  {
    void AddHit(const ResultType& inResult) override;

    std::optional<ResultType> nearest;
  };

  struct NearestRayCollector : JPH::CastRayCollector
  {
    void AddHit(const ResultType& inResult) override;

    std::optional<ResultType> nearest;
  };

  struct AllRayCollector : JPH::CastRayCollector
  {
    void AddHit(const ResultType& inResult) override;

    std::vector<ResultType> unorderedHits;
  };

#ifndef GAME_HEADLESS
  void DrawDebugUI(World& world);
#endif
}
