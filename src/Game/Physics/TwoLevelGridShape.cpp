#include "TwoLevelGridShape.h"
#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Globals.h"
#include "Game/Voxel/Grid.h"

#include "Physics.h"
#include "PhysicsUtils.h"

#include "entt/entity/registry.hpp"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Geometry/AABox.h"
#include "Jolt/Geometry/Sphere.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/geometric.hpp"
#include "glm/gtx/component_wise.hpp"
#include "tracy/Tracy.hpp"


// Amount to shrink block size by.
constexpr float VX_EPSILON = 0;

// Amount by which to expand the AABB of shapes tested against the grid. This is a hack to make the player not stick to surfaces.
constexpr float VX_AABB_EPSILON = 1e-1f;

Physics::TwoLevelGridShape::TwoLevelGridShape(const entt::registry& registry)
  : Shape(JPH::EShapeType::User1, JPH::EShapeSubType::User1), registry_(&registry)
{
}

const Voxel::Grid& Physics::TwoLevelGridShape::GetTwoLevelGrid() const
{
  return *registry_->ctx().get<const World&>().globals->grid;
}

void Physics::TwoLevelGridShape::CollideTwoLevelGrid2(const Shape* inShape1,
  const Shape* inShape2,
  JPH::Vec3Arg inScale1,
  JPH::Vec3Arg inScale2,
  JPH::Mat44Arg inCenterOfMassTransform1,
  JPH::Mat44Arg inCenterOfMassTransform2,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
  const JPH::CollideShapeSettings& inCollideShapeSettings,
  JPH::CollideShapeCollector& ioCollector,
  const JPH::ShapeFilter& inShapeFilter,
  std::function<bool(voxel_t)> isSolidCallback)
{
  ZoneScoped;
  ASSERT(inShape1->GetType() == JPH::EShapeType::User1);
  auto* s1 = static_cast<const TwoLevelGridShape*>(inShape1);
  const auto& s1Grid  = s1->GetTwoLevelGrid();
  
  const auto transform2_to_1 = inCenterOfMassTransform1.InversedRotationTranslation() * inCenterOfMassTransform2;
  const auto boundsOf2InSpaceOf1 = inShape2->GetLocalBounds().Scaled(inScale2).Transformed(transform2_to_1);

  // Test shape against every voxel AABB in its bounds
  // TODO: Investigate using AABox4.h to accelerate collision tests
  const auto s2min    = JPH::Vec3::sMax(boundsOf2InSpaceOf1.GetCenter() - boundsOf2InSpaceOf1.GetExtent(), JPH::Vec3::sReplicate(0));
  const auto s2max    = JPH::Vec3::sMin(boundsOf2InSpaceOf1.GetCenter() + boundsOf2InSpaceOf1.GetExtent(), ToJolt(s1Grid.Dimensions() - 1));
  const auto boxShape = JPH::BoxShape({0.5f - VX_EPSILON, 0.5f - VX_EPSILON, 0.5f - VX_EPSILON});

  for (int z = (int)std::floor(s2min.GetZ() - VX_AABB_EPSILON); z < (int)std::ceil(s2max.GetZ() + VX_AABB_EPSILON); z++)
  for (int y = (int)std::floor(s2min.GetY() - VX_AABB_EPSILON); y < (int)std::ceil(s2max.GetY() + VX_AABB_EPSILON); y++)
  for (int x = (int)std::floor(s2min.GetX() - VX_AABB_EPSILON); x < (int)std::ceil(s2max.GetX() + VX_AABB_EPSILON); x++)
  {
    const auto voxelPosWS = glm::vec3{x, y, z};
    const auto voxel = s1Grid.GetVoxelAt(voxelPosWS);

    if (isSolidCallback)
    {
      if (isSolidCallback(voxel) == false)
      {
        continue;
      } 
    }
    else if (!s1Grid.IsVoxelSolid(voxel))
    {
      continue;
    }

    if (const auto* subGrid = s1Grid.materials_[int(voxel)].subGrid)
    {
      auto scaleResult       = boxShape.ScaleShape(ToJolt(1.0f / glm::vec3(subGrid->dimensions)));
      const auto subBoxShape = scaleResult.Get();
      subBoxShape->SetEmbedded();

      // Calculate bounds of object in sub-grid space.
      const auto subGridDims = glm::vec3(subGrid->dimensions);
      const auto voxelPosSS = voxelPosWS * subGridDims;
      const auto s2minSS = glm::ivec3(glm::max(glm::floor(ToGlm(s2min) * subGridDims - voxelPosSS - 5 * VX_AABB_EPSILON), glm::vec3(0)));
      const auto s2maxSS = glm::ivec3(glm::min(glm::ceil(ToGlm(s2max) * subGridDims - voxelPosSS + 5 * VX_AABB_EPSILON), subGridDims));

      for (int zs = s2minSS.z; zs < s2maxSS.z; zs++)
      for (int ys = s2minSS.y; ys < s2maxSS.y; ys++)
      for (int xs = s2minSS.x; xs < s2maxSS.x; xs++)
      {
        if (subGrid->grid[Voxel::Grid::FlattenGenericCoord(subGrid->dimensions, {xs, ys, zs})] == Voxel::SubVoxel::Air)
        {
          continue;
        }

        const auto subVoxelOffset           = (glm::vec3{xs, ys, zs} + 0.5f) / glm::vec3(subGrid->dimensions);
        const auto boxCenterOfMassTransform = inCenterOfMassTransform1.PreTranslated(ToJolt(glm::vec3{x, y, z} + subVoxelOffset));
        JPH::CollisionDispatch::sCollideShapeVsShape(subBoxShape,
          inShape2,
          inScale1,
          inScale2,
          boxCenterOfMassTransform,
          inCenterOfMassTransform2,
          // If there's ever an ASSERT tripped in Jolt's hash map (e.g. for very big shapes), this hack is probably the culprit.
          // Each collision with a different voxel needs a unique shape ID.
          inSubShapeIDCreator1.PushID(Voxel::Grid::FlattenBottomLevelBrickCoord({x, y, z}), 16).PushID(Voxel::Grid::FlattenBottomLevelBrickCoord({xs, ys, zs}), 16),
          inSubShapeIDCreator2,
          inCollideShapeSettings,
          ioCollector,
          inShapeFilter);
      }
    }
    else
    {
      const auto boxCenterOfMassTransform = inCenterOfMassTransform1.PreTranslated({x + 0.5f, y + 0.5f, z + 0.5f});
      JPH::CollisionDispatch::sCollideShapeVsShape(&boxShape,
        inShape2,
        inScale1,
        inScale2,
        boxCenterOfMassTransform,
        inCenterOfMassTransform2,
        // If there's ever an ASSERT tripped in Jolt's hash map (e.g. for very big shapes), this hack is probably the culprit.
        // Each collision with a different voxel needs a unique shape ID.
        inSubShapeIDCreator1.PushID(Voxel::Grid::FlattenBottomLevelBrickCoord({x, y, z}), 16),
        inSubShapeIDCreator2,
        inCollideShapeSettings,
        ioCollector,
        inShapeFilter);
    }
  }
}

void Physics::TwoLevelGridShape::CollideTwoLevelGrid(const Shape* inShape1,
  const Shape* inShape2,
  JPH::Vec3Arg inScale1,
  JPH::Vec3Arg inScale2,
  JPH::Mat44Arg inCenterOfMassTransform1,
  JPH::Mat44Arg inCenterOfMassTransform2,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
  const JPH::CollideShapeSettings& inCollideShapeSettings,
  JPH::CollideShapeCollector& ioCollector,
  const JPH::ShapeFilter& inShapeFilter)
{
  CollideTwoLevelGrid2(inShape1,
    inShape2,
    inScale1,
    inScale2,
    inCenterOfMassTransform1,
    inCenterOfMassTransform2,
    inSubShapeIDCreator1,
    inSubShapeIDCreator2,
    inCollideShapeSettings,
    ioCollector,
    inShapeFilter,
    nullptr);
}

// inShapeCast == any other shape
// inShape == TwoLevelGridShape
// We want the swept shape to be NOT TwoLevelGridShape (its swept shape would be horrific)
void Physics::TwoLevelGridShape::CastTwoLevelGrid(const JPH::ShapeCast& inShapeCast,
  const JPH::ShapeCastSettings& inShapeCastSettings,
  const JPH::Shape* inShape,
  JPH::Vec3Arg inScale,
  const JPH::ShapeFilter& inShapeFilter,
  [[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform2,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
  JPH::CastShapeCollector& ioCollector)
{
  ZoneScoped;
  ASSERT(inShape->GetType() == JPH::EShapeType::User1);
  auto* s2           = static_cast<const TwoLevelGridShape*>(inShape);
  const auto& s2Grid = s2->GetTwoLevelGrid();

  const auto castBoundsWorldSpaceStart = inShapeCast.mShapeWorldBounds;
  const auto extent                    = inShapeCast.mShapeWorldBounds.GetExtent();
  const auto castBoundsWorldSpaceEnd =
    JPH::AABox::sFromTwoPoints(
      inShapeCast.mShapeWorldBounds.GetCenter() - extent + inShapeCast.mDirection,
      inShapeCast.mShapeWorldBounds.GetCenter() + extent + inShapeCast.mDirection);

  const auto min = JPH::Vec3::sMin(castBoundsWorldSpaceStart.GetCenter() - extent, castBoundsWorldSpaceEnd.GetCenter() - extent);
  const auto max = JPH::Vec3::sMax(castBoundsWorldSpaceStart.GetCenter() + extent, castBoundsWorldSpaceEnd.GetCenter() + extent);

  const auto castBoundsWorldSpace = JPH::AABox::sFromTwoPoints(min, max);

  auto shapeCastSettings2 = inShapeCastSettings;
  //shapeCastSettings2.mUseShrunkenShapeAndConvexRadius = true;
  //shapeCastSettings2.mReturnDeepestPoint = true;
  
  // Test cast shape against every voxel AABB in its bounds
  const auto castMin  = castBoundsWorldSpace.GetCenter() - castBoundsWorldSpace.GetExtent();
  const auto castMax  = castBoundsWorldSpace.GetCenter() + castBoundsWorldSpace.GetExtent();
  const auto boxShape = JPH::BoxShape({0.5f - VX_EPSILON, 0.5f - VX_EPSILON, 0.5f - VX_EPSILON});
  boxShape.SetEmbedded();
  for (int z = (int)std::floor(castMin.GetZ() - VX_AABB_EPSILON); z < (int)std::ceil(castMax.GetZ() + VX_AABB_EPSILON); z++)
  for (int y = (int)std::floor(castMin.GetY() - VX_AABB_EPSILON); y < (int)std::ceil(castMax.GetY() + VX_AABB_EPSILON); y++)
  for (int x = (int)std::floor(castMin.GetX() - VX_AABB_EPSILON); x < (int)std::ceil(castMax.GetX() + VX_AABB_EPSILON); x++)
  {
    const auto voxelPosWS = glm::vec3{x, y, z};
    const auto voxel = s2Grid.GetVoxelAt(voxelPosWS);
    // Skip voxel if non-solid
    if (!s2Grid.IsVoxelSolid(voxel))
    {
      continue;
    }
    if (const auto* subGrid = s2Grid.materials_[int(voxel)].subGrid)
    {
      auto scaleResult       = boxShape.ScaleShape(ToJolt(1.0f / glm::vec3(subGrid->dimensions)));
      const auto subBoxShape = scaleResult.Get();
      subBoxShape->SetEmbedded();

      // Calculate bounds of cast in sub-grid space.
      const auto subGridDims = glm::vec3(subGrid->dimensions);
      const auto voxelPosSS = voxelPosWS * subGridDims;
      const auto s2minSS = glm::ivec3(glm::max(glm::floor(ToGlm(castMin) * subGridDims - voxelPosSS - 5 * VX_AABB_EPSILON), glm::vec3(0)));
      const auto s2maxSS = glm::ivec3(glm::min(glm::ceil(ToGlm(castMax) * subGridDims - voxelPosSS + 5 * VX_AABB_EPSILON), subGridDims));

      for (int zs = s2minSS.z; zs < s2maxSS.z; zs++)
      for (int ys = s2minSS.y; ys < s2maxSS.y; ys++)
      for (int xs = s2minSS.x; xs < s2maxSS.x; xs++)
      {
        if (subGrid->grid[Voxel::Grid::FlattenGenericCoord(subGrid->dimensions, {xs, ys, zs})] == Voxel::SubVoxel::Air)
        {
          continue;
        }

        const auto subVoxelOffset = (glm::vec3{xs, ys, zs} + 0.5f) / glm::vec3(subGrid->dimensions);

        auto negVec     = JPH::Vec3{-x - subVoxelOffset.x, -y - subVoxelOffset.y, -z - subVoxelOffset.z};
        auto posVec     = JPH::Vec3{x + subVoxelOffset.x, y + subVoxelOffset.y, z + subVoxelOffset.z};
        auto shapeCast2 = inShapeCast.PostTranslated(negVec);

        const auto boxCenterOfMassTransform = inCenterOfMassTransform2.PreTranslated(posVec);
        JPH::CollisionDispatch::sCastShapeVsShapeLocalSpace(shapeCast2,
          shapeCastSettings2,
          subBoxShape,
          inScale,
          inShapeFilter,
          boxCenterOfMassTransform,
          inSubShapeIDCreator1,
          // If there's ever an assert tripped in Jolt's hash map (e.g. for very big shapes), this hack is probably the culprit.
          // Each collision with a different voxel needs a unique shape ID.
          inSubShapeIDCreator2.PushID(Voxel::Grid::FlattenBottomLevelBrickCoord({x, y, z}), 16).PushID(Voxel::Grid::FlattenBottomLevelBrickCoord({xs, ys, zs}), 16),
          ioCollector);
      }
    }
    else
    {
      auto negVec     = JPH::Vec3{-x - 0.5f, -y - 0.5f, -z - 0.5f};
      auto posVec     = JPH::Vec3{x + 0.5f, y + 0.5f, z + 0.5f};
      auto shapeCast2 = inShapeCast.PostTranslated(negVec);

      const auto boxCenterOfMassTransform = inCenterOfMassTransform2.PreTranslated(posVec);
      JPH::CollisionDispatch::sCastShapeVsShapeLocalSpace(shapeCast2,
        shapeCastSettings2,
        &boxShape,
        inScale,
        inShapeFilter,
        boxCenterOfMassTransform,
        inSubShapeIDCreator1,
        // If there's ever an assert tripped in Jolt's hash map (e.g. for very big shapes), this hack is probably the culprit.
        // Each collision with a different voxel needs a unique shape ID.
        inSubShapeIDCreator2.PushID(Voxel::Grid::FlattenBottomLevelBrickCoord({x, y, z}), 16),
        ioCollector);
    }
  }
}

void Physics::TwoLevelGridShape::sRegister()
{
  for (auto shapeSubType : JPH::sConvexSubShapeTypes)
  {
    JPH::CollisionDispatch::sRegisterCollideShape(JPH::EShapeSubType::User1, shapeSubType, CollideTwoLevelGrid);
    JPH::CollisionDispatch::sRegisterCastShape(JPH::EShapeSubType::User1, shapeSubType, JPH::CollisionDispatch::sReversedCastShape);
    JPH::CollisionDispatch::sRegisterCollideShape(shapeSubType, JPH::EShapeSubType::User1, JPH::CollisionDispatch::sReversedCollideShape);
    JPH::CollisionDispatch::sRegisterCastShape(shapeSubType, JPH::EShapeSubType::User1, CastTwoLevelGrid);
  }
}

void Physics::TwoLevelGridShape::CastRay(const JPH::RayCast& inRay,
  [[maybe_unused]] const JPH::RayCastSettings& inRayCastSettings,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator,
  JPH::CastRayCollector& ioCollector,
  [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter) const
{
  auto hit = Voxel::Grid::HitSurfaceParameters();
  const auto direction = Physics::ToGlm(inRay.mDirection.Normalized());
  auto tMax = inRay.mDirection.Length();
  const auto origin = Physics::ToGlm(inRay.mOrigin);
  if (GetTwoLevelGrid().TraceRaySimple(origin, direction, tMax, hit))
  {
    auto id     = inSubShapeIDCreator.PushID(Voxel::Grid::FlattenBottomLevelBrickCoord(glm::ivec3(hit.voxelPosition)), 16).GetID();
    auto result = JPH::CastRayCollector::ResultType();
    result.mSubShapeID2 = id;
    result.mBodyID      = inShapeFilter.mBodyID2;
    result.mFraction    = glm::distance(origin, hit.positionWorld) / tMax;
    if (result.mFraction <= 1)
    {
      ioCollector.AddHit(result);
    }
  }
}

bool Physics::TwoLevelGridShape::CastRay([[maybe_unused]] const JPH::RayCast& inRay,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
  [[maybe_unused]] JPH::RayCastResult& ioHit) const
{
  const auto origin    = Physics::ToGlm(inRay.mOrigin);
  const auto direction = glm::normalize(Physics::ToGlm(inRay.mDirection));
  const auto tMax      = inRay.mDirection.Length();
  auto hit = Voxel::Grid::HitSurfaceParameters();
  if (GetTwoLevelGrid().TraceRaySimple(origin, direction, tMax, hit))
  {
    auto id            = inSubShapeIDCreator.PushID(Voxel::Grid::FlattenBottomLevelBrickCoord(glm::ivec3(hit.voxelPosition)), 16).GetID();
    ioHit.mSubShapeID2 = id;
    ioHit.mBodyID      = {}; // TODO?
    ioHit.mFraction    = glm::distance(origin, hit.positionWorld) / tMax;
    if (ioHit.mFraction <= 1)
    {
      return true;
    }
  }
  return false;
}

void Physics::TwoLevelGridShape::CollidePoint([[maybe_unused]] JPH::Vec3Arg inPoint,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
  [[maybe_unused]] JPH::CollidePointCollector& ioCollector,
  [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter) const
{
  ASSERT(false);
}

void Physics::TwoLevelGridShape::CollideSoftBodyVertices([[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform,
  [[maybe_unused]] JPH::Vec3Arg inScale,
  [[maybe_unused]] const JPH::CollideSoftBodyVertexIterator& inVertices,
  [[maybe_unused]] JPH::uint inNumVertices,
  [[maybe_unused]] int inCollidingShapeIndex) const
{
  ASSERT(false);
}

float Physics::TwoLevelGridShape::GetInnerRadius() const
{
  return float(glm::compMin(GetTwoLevelGrid().Dimensions())) / 2.0f;
}

JPH::AABox Physics::TwoLevelGridShape::GetLocalBounds() const
{
  return JPH::AABox(JPH::Vec3Arg(0, 0, 0),
    JPH::Vec3Arg((float)GetTwoLevelGrid().Dimensions().x, (float)GetTwoLevelGrid().Dimensions().y, (float)GetTwoLevelGrid().Dimensions().z));
}

JPH::MassProperties Physics::TwoLevelGridShape::GetMassProperties() const
{
  ASSERT(false);
  return {};
}

const JPH::PhysicsMaterial* Physics::TwoLevelGridShape::GetMaterial([[maybe_unused]] const JPH::SubShapeID& inSubShapeID) const
{
  return nullptr;
}

JPH::Shape::Stats Physics::TwoLevelGridShape::GetStats() const
{
  return Stats(sizeof(*this), 0);
}

JPH::uint Physics::TwoLevelGridShape::GetSubShapeIDBitsRecursive() const
{
  ASSERT(0);
  return 0;
}

void Physics::TwoLevelGridShape::GetSubmergedVolume([[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform,
  [[maybe_unused]] JPH::Vec3Arg inScale,
  [[maybe_unused]] const JPH::Plane& inSurface,
  [[maybe_unused]] float& outTotalVolume,
  [[maybe_unused]] float& outSubmergedVolume,
  [[maybe_unused]] JPH::Vec3& outCenterOfBuoyancy
#ifdef JPH_DEBUG_RENDERER
  , [[maybe_unused]] JPH::RVec3Arg inBaseOffset
#endif
) const
{
  ASSERT(false);
}

namespace
{
  bool IsGridPositionSolid(const Voxel::Grid& grid, glm::vec3 position)
  {
    const auto voxelPos = glm::ivec3(position);
    if (!grid.IsPositionInGrid(voxelPos))
    {
      return false;
    }
    
    const auto voxel     = grid.GetVoxelAt(voxelPos);
    const auto& material = grid.materials_[int(voxel)];

    if (!material.isSolid)
    {
      return false;
    }

    if (const auto* subGrid = material.subGrid)
    {
      const auto subGridPos   = glm::ivec3((position - glm::vec3(voxelPos)) * glm::vec3(subGrid->dimensions));
      const auto subVoxel = subGrid->grid[Voxel::Grid::FlattenGenericCoord(subGrid->dimensions, subGridPos)];
      return subVoxel != Voxel::SubVoxel::Air;
    }

    return true;
  }
}

JPH::Vec3 Physics::TwoLevelGridShape::GetSurfaceNormal([[maybe_unused]] const JPH::SubShapeID& inSubShapeID, [[maybe_unused]] JPH::Vec3Arg inLocalSurfacePosition) const
{
  // Check both voxels on the edge (from the component that is nearest to an integer), then use the position of the solid one.
  const auto inLocalPos     = inLocalSurfacePosition;
  const auto absDiffFromInt = JPH::Vec3(
    abs(inLocalPos.GetX() - round(inLocalPos.GetX())),
    abs(inLocalPos.GetY() - round(inLocalPos.GetY())),
    abs(inLocalPos.GetZ() - round(inLocalPos.GetZ())));
  const auto nearestIntCompIdx = absDiffFromInt.GetLowestComponentIndex();

  auto pos0 = inLocalPos;
  pos0.SetComponent(nearestIntCompIdx, round(inLocalPos[nearestIntCompIdx]));

  auto pos1 = inLocalPos;
  pos1.SetComponent(nearestIntCompIdx, round(inLocalPos[nearestIntCompIdx] - 1.0f));

  const auto v0pos = glm::ivec3(glm::floor(ToGlm(pos0)));
  const auto v1pos = glm::ivec3(glm::floor(ToGlm(pos1)));

  [[maybe_unused]] auto v0 = GetTwoLevelGrid().GetVoxelAt(v0pos);
  [[maybe_unused]] auto v1 = GetTwoLevelGrid().GetVoxelAt(v1pos);

  // Choose position of solid voxel, which is the one we're colliding with. If both voxels are solid (which shouldn't happen), pick an arbitrary position.
  auto solidVoxelPos                = v1pos;
  [[maybe_unused]] auto airVoxelPos = v0pos;
  auto solidVoxel                   = v1;

  if (GetTwoLevelGrid().IsVoxelSolid(v0))
  {
    solidVoxelPos = v0pos;
    airVoxelPos   = v1pos;
    solidVoxel    = v0;
  }

  const auto& material = GetTwoLevelGrid().materials_[int(solidVoxel)];
  if (material.subGrid)
  {
    const auto& subGrid = *material.subGrid;
    const auto inSubPos = ToJolt((ToGlm(inLocalSurfacePosition) - glm::vec3(solidVoxelPos)) * glm::vec3(subGrid.dimensions));
    const auto absSubDiffFromInt =
      JPH::Vec3(abs(inSubPos.GetX() - round(inSubPos.GetX())), abs(inSubPos.GetY() - round(inSubPos.GetY())), abs(inSubPos.GetZ() - round(inSubPos.GetZ())));
    const auto nearestSubIntCompIdx = absSubDiffFromInt.GetLowestComponentIndex();

    auto subPos0 = inSubPos;
    subPos0.SetComponent(nearestSubIntCompIdx, round(inSubPos[nearestSubIntCompIdx]));

    auto subPos1 = inSubPos;
    subPos1.SetComponent(nearestSubIntCompIdx, round(inSubPos[nearestSubIntCompIdx] - 1.0f));

    const auto v0subPos = glm::ivec3(glm::floor(ToGlm(subPos0)));
    const auto v1SubPos = glm::ivec3(glm::floor(ToGlm(subPos1)));

    auto solidSubVoxelPos                = v1SubPos;
    [[maybe_unused]] auto airSubVoxelPos = v0subPos;

    const bool isOob = any(lessThan(v0subPos, glm::ivec3(0))) || any(greaterThanEqual(v0subPos, glm::ivec3(subGrid.dimensions)));
    if (!isOob)
    {
      const auto subVoxel = subGrid.grid[Voxel::Grid::FlattenGenericCoord(subGrid.dimensions, v0subPos)];
      if (subVoxel != Voxel::SubVoxel::Air)
      {
        solidSubVoxelPos = v0subPos;
        airSubVoxelPos   = v1SubPos;
      }
    }

    return ToJolt(airSubVoxelPos - solidSubVoxelPos);
  }

  return ToJolt(airVoxelPos - solidVoxelPos);
}

int Physics::TwoLevelGridShape::GetTrianglesNext([[maybe_unused]] GetTrianglesContext& ioContext,
  [[maybe_unused]] int inMaxTrianglesRequested,
  [[maybe_unused]] JPH::Float3* outTriangleVertices,
  [[maybe_unused]] const JPH::PhysicsMaterial** outMaterials) const
{
  ASSERT(false);
  return 0;
}

void Physics::TwoLevelGridShape::GetTrianglesStart([[maybe_unused]] GetTrianglesContext& ioContext,
  [[maybe_unused]] const JPH::AABox& inBox,
  [[maybe_unused]] JPH::Vec3Arg inPositionCOM,
  [[maybe_unused]] JPH::QuatArg inRotation,
  [[maybe_unused]] JPH::Vec3Arg inScale) const
{
  ASSERT(false);
}

float Physics::TwoLevelGridShape::GetVolume() const
{
  ASSERT(false);
  return 0;
}

#ifdef JPH_DEBUG_RENDERER
void Physics::TwoLevelGridShape::Draw([[maybe_unused]] JPH::DebugRenderer* inRenderer,
  [[maybe_unused]] JPH::RMat44Arg inCenterOfMassTransform,
  [[maybe_unused]] JPH::Vec3Arg inScale,
  [[maybe_unused]] JPH::ColorArg inColor,
  [[maybe_unused]] bool inUseMaterialColors,
  [[maybe_unused]] bool inDrawWireframe) const
{
  //ASSERT(false);
}
#endif
