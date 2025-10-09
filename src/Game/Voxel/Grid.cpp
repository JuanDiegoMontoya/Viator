#include "Grid.h"
#include "Core/Assert2.h"

#include "tracy/Tracy.hpp"
#include "spdlog/spdlog.h"

#include "glm/vector_relational.hpp"
#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/vec4.hpp"

#include <type_traits>
#include <algorithm>

namespace Voxel
{
  // Assert that we can memset these types and produce them from a bag of bytes.
  static_assert(std::is_trivially_constructible_v<Grid::TopLevelBrick>);
  static_assert(std::is_trivially_constructible_v<Grid::TopLevelBrickPtr>);
  static_assert(std::is_trivially_constructible_v<Grid::BottomLevelBrick>);
  static_assert(std::is_trivially_constructible_v<Grid::BottomLevelBrickPtr>);

  // Ray position in [0, subGrid.dimensions)
  bool Grid::TraceRaySubGrid(glm::vec3 rayPosLocal,
    glm::vec3 rayDirection,
    const SubGrid& subGrid,
    InitialDDAState init,
    glm::bvec3 cases,
    float& t,
    float tMax,
    Grid::HitSurfaceParameters& hit,
    TraceTranslucencyMode translucencyMode) const
  {
    using namespace glm;
    constexpr auto EPSILON = 1e-3f;
    rayPosLocal            = clamp(rayPosLocal, vec3(EPSILON), vec3(vec3(subGrid.dimensions) - EPSILON));
    vec3 mapPos            = (floor(rayPosLocal));
    vec3 sideDist          = (vec3(init.S) - vec3(init.stepDir) * fract(rayPosLocal)) * init.deltaDist;

    float tLocalPrev = 0.0;
    vec3 p           = mapPos + 0.5f - vec3(init.stepDir) * 0.5f; // Point on axis plane
    vec3 normal      = i8vec3(vec3(cases) * -vec3(init.stepDir));

    while (all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, vec3(subGrid.dimensions))))
    {
      const uint subVoxelIndex = FlattenGenericCoord(subGrid.dimensions, ivec3(mapPos));
      const SubVoxel subVoxel  = subGrid.grid[subVoxelIndex];

      const float density = subGrid.materials[int(subVoxel) - 1].density;
      // const vec3 albedo   = color_sRGB_EOTF(SUBGRIDS[subGridIndex].materials[int(subVoxel) - 1].colorSrgb.rgb);

      if (subVoxel != SubVoxel::Air &&
          ((density < 0 && translucencyMode == TraceTranslucencyMode::ALL) ||
            (density >= 0 && translucencyMode == TraceTranslucencyMode::FIRST_TRANSLUCENT_ONLY) || translucencyMode == TraceTranslucencyMode::ALL_OPAQUE))
      {
        hit.flatNormalWorld       = normal;
        hit.subVoxelMaterialIndex = uint(subVoxel) - 1;
        return true;
      }

      bvec4 conds = lessThan(vec4(sideDist.x, sideDist.x, sideDist.y, sideDist.y), vec4(sideDist.y, sideDist.z, sideDist.z, sideDist.x));

      cases.x = conds.x && conds.y;
      cases.y = (!cases.x) && conds.z && conds.w;
      cases.z = (!cases.x) && (!cases.y);

      sideDist += (max((2.0f * vec3(cases) - 1.0f) * init.deltaDist, 0.0f));

      mapPos += (i8vec3(cases) * init.stepDir);

      p                                 = mapPos + 0.5f - vec3(init.stepDir) * 0.5f; // Point on axis plane
      normal                            = i8vec3(vec3(cases) * -vec3(init.stepDir));
      const float tLocal                = (dot(normal, p - rayPosLocal)) / dot(normal, rayDirection);
      const float tDelta                = min((tLocal - tLocalPrev) / subGrid.dimensions.x, tMax - t);
      [[maybe_unused]] const float tOld = t;
      t += tDelta;

      if (subVoxel != SubVoxel::Air && density >= 0)
      {
        // hit.transmission *= exp(-density * 20 * tDelta * (1 - albedo));
        // if (!hit.hitTranslucent)
        //{
        //   hit.firstTranslucentHitT = tOld;
        //   hit.hitTranslucent       = true;
        // }
      }

      if (t >= tMax)
      {
        return false;
      }

      tLocalPrev = tLocal;
    }

    return false;
  }

  bool Grid::TraceRaySimple(glm::vec3 rayPosition, glm::vec3 rayDirection, float tMax, HitSurfaceParameters& hit, std::function<bool(voxel_t)>&& predicate) const
  {
    using namespace glm;
    // https://www.shadertoy.com/view/X3BXDd
    vec3 mapPos = glm::floor(rayPosition); // integer cell coordinate of initial cell

    const vec3 deltaDist = 1.0f / abs(rayDirection); // ray length required to step from one cell border to the next in x, y and z directions

    const vec3 S       = vec3(step(0.0f, rayDirection)); // S is rayDir non-negative? 0 or 1
    const vec3 stepDir = 2.0f * S - 1.0f;                // Step sign

    // if 1./abs(rayDir[i]) is inf, then rayDir[i] is 0., but then S = step(0., rayDir[i]) is 1
    // so S cannot be 0. while deltaDist is inf, and stepDir * fract(pos) can never be 1.
    // Therefore we should not have to worry about getting NaN here :)

    // initial distance to cell sides, then relative difference between traveled sides
    vec3 sideDist = (S - stepDir * fract(rayPosition)) * deltaDist; // alternative: //sideDist = (S-stepDir * (pos - map)) * deltaDist;

    bvec3 cases = bvec3(sideDist);

    vec3 p           = mapPos + 0.5f - stepDir * 0.5f; // Point on axis plane
    vec3 normal      = vec3(ivec3(vec3(cases))) * -vec3(stepDir);
    float t          = 0;
    float tLocalPrev = 0;

    for (int i = 0; i < tMax * 3; i++)
    {
      if (all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, vec3(dimensions_))))
      {
        const voxel_t voxel    = GetVoxelAt(ivec3(mapPos));
        const vec3 hitWorldPos = rayPosition + rayDirection * t;
        const vec3 uvw         = hitWorldPos - mapPos; // Don't use fract here

        hit.voxel         = voxel;
        hit.voxelPosition = ivec3(mapPos);

        const auto& material = materials_[int(voxel)];
        // TODO: This is a hack that ignores the predicate. The predicate must account for subgrids.
        if (material.subGrid)
        {
          InitialDDAState init;
          init.deltaDist   = deltaDist;
          init.S           = bvec3(S);
          init.stepDir     = i8vec3(stepDir);
          const float oldT = t;
          if (TraceRaySubGrid(uvw * vec3(material.subGrid->dimensions), rayDirection, *material.subGrid, init, bvec3(cases), t, tMax, hit, TraceTranslucencyMode::ALL))
          {
            hit.positionWorld = rayPosition + rayDirection * t;
            return true;
          }
          else
          {
            t = oldT;
          }
        }
        else if (predicate(voxel))
        {
          hit.positionWorld   = hitWorldPos;
          hit.texCoords       = {}; // vx_GetTexCoords(normal, uvw);
          hit.flatNormalWorld = normal;
          return true;
        }
      }

      // Decide which way to go!
      bvec4 conds = lessThan(vec4(sideDist.x, sideDist.x, sideDist.y, sideDist.y), vec4(sideDist.y, sideDist.z, sideDist.z, sideDist.x));

      // This mimics the if, elseif and else clauses
      // * is 'and', 1.-x is negation
      cases.x = conds.x && conds.y;             // if       x dir
      cases.y = !cases.x && conds.z && conds.w; // else if  y dir
      cases.z = !cases.x && !cases.y;           // else     z dir

      // usually would have been:     sideDist += cases * deltaDist;
      // but this gives NaN when  cases[i] * deltaDist[i]  becomes  0. * inf
      // This gives NaN result in a component that should not have been affected,
      // so we instead give negative results for inf by mapping 'cases' to +/- 1
      // and then clamp negative values to zero afterwards, giving the correct result! :)
      sideDist += max((2.0f * vec3(cases) - 1.0f) * deltaDist, 0.0f);

      mapPos += vec3(cases) * stepDir;

      p                  = mapPos + 0.5f - stepDir * 0.5f; // Point on axis plane
      normal             = i8vec3(vec3(cases) * -vec3(stepDir));
      float tLocal       = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
      const float tDelta = min(tLocal - tLocalPrev, tMax - t);
      t += tDelta;

      if (t >= tMax)
      {
        return false;
      }

      tLocalPrev = tLocal;
    }

    return false;
  }

  bool Grid::TraceRaySimple(glm::vec3 rayPosition, glm::vec3 rayDirection, float tMax, HitSurfaceParameters& hit, bool skipNonSolid) const
  {
    return TraceRaySimple(rayPosition,
      rayDirection,
      tMax,
      hit,
      [&](voxel_t voxel) { return voxel != voxel_t::Air && (!skipNonSolid || materials_[(uint32_t)voxel].isSolid); });
  }

  Grid::Grid() : buffer(SketchyBuffer::Create(16, false)) {}

  Grid::Grid(glm::ivec3 topLevelBrickDims, size_t bufferSize, bool createGpuBuffer)
    : buffer(SketchyBuffer::Create(bufferSize, createGpuBuffer, "World")),
      topLevelBricksDims_(topLevelBrickDims),
      dimensions_(topLevelBricksDims_.x * TL_BRICK_VOXELS_PER_SIDE, topLevelBricksDims_.y * TL_BRICK_VOXELS_PER_SIDE, topLevelBricksDims_.z * TL_BRICK_VOXELS_PER_SIDE)
  {
    ZoneScoped;
    numTopLevelBricks_ = topLevelBricksDims_.x * topLevelBricksDims_.y * topLevelBricksDims_.z;
    DEBUG_ASSERT(topLevelBricksDims_.x > 0 && topLevelBricksDims_.y > 0 && topLevelBricksDims_.z > 0);

    topLevelBrickPtrs = buffer->Allocate(sizeof(TopLevelBrickPtr) * numTopLevelBricks_, sizeof(TopLevelBrickPtr));
    for (size_t i = 0; i < numTopLevelBricks_; i++)
    {
      auto& topLevelBrickPtr = buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + i];
      std::construct_at(&topLevelBrickPtr);
      topLevelBrickPtr = {.voxelsDoBeAllSame = true, .voxelIfAllSame = voxel_t::Air};
#ifndef GAME_HEADLESS
      buffer->MarkDirtyPages(&topLevelBrickPtr);
#endif
    }
    topLevelBrickPtrsBaseIndex = uint32_t(topLevelBrickPtrs.offset / sizeof(TopLevelBrickPtr));

    mutex_ = std::make_unique<std::mutex>();
  }

  Grid::GridHierarchyCoords Grid::GetCoordsOfVoxelAt(glm::ivec3 voxelCoord) const
  {
    // ZoneScoped;
    const auto topLevelCoord    = voxelCoord / TL_BRICK_VOXELS_PER_SIDE;
    const auto bottomLevelCoord = (voxelCoord / BL_BRICK_SIDE_LENGTH) % TL_BRICK_SIDE_LENGTH;
    const auto localVoxelCoord  = voxelCoord % BL_BRICK_SIDE_LENGTH;

    // DEBUG_ASSERT(glm::all(glm::lessThan(topLevelCoord, topLevelBricksDims_)));
    // DEBUG_ASSERT(glm::all(glm::lessThan(bottomLevelCoord, glm::ivec3(TL_BRICK_SIDE_LENGTH))));
    // DEBUG_ASSERT(glm::all(glm::lessThan(localVoxelCoord, glm::ivec3(BL_BRICK_SIDE_LENGTH))));

    return {topLevelCoord, bottomLevelCoord, localVoxelCoord};
  }

  voxel_t Grid::GetVoxelAt(glm::ivec3 voxelCoord) const
  {
    // ZoneScoped;
    if (!IsPositionInGrid(voxelCoord)) [[unlikely]]
    {
      return voxel_t::Air;
    }

    return GetVoxelAtUnchecked(voxelCoord);
  }

  voxel_t Grid::GetVoxelAtUnchecked(glm::ivec3 voxelCoord) const
  {
    auto [topLevelCoord, bottomLevelCoord, localVoxelCoord] = GetCoordsOfVoxelAt(voxelCoord);

    const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelCoord);
    DEBUG_ASSERT(topLevelIndex < numTopLevelBricks_);
    const auto& topLevelBrickPtr = buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

    if (topLevelBrickPtr.voxelsDoBeAllSame) [[unlikely]]
    {
      return topLevelBrickPtr.voxelIfAllSame;
    }

    const auto bottomLevelIndex = FlattenBottomLevelBrickCoord(bottomLevelCoord);
    DEBUG_ASSERT(bottomLevelIndex < CELLS_PER_TL_BRICK);
    const auto& bottomLevelBrickPtr = buffer->GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[bottomLevelIndex];

    if (bottomLevelBrickPtr.voxelsDoBeAllSame) [[unlikely]]
    {
      return bottomLevelBrickPtr.voxelIfAllSame;
    }

    const auto localVoxelIndex = FlattenVoxelCoord(localVoxelCoord);
    DEBUG_ASSERT(localVoxelIndex < CELLS_PER_BL_BRICK);
    return buffer->GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels[localVoxelIndex];
  }

  void Grid::SetVoxelAt(glm::ivec3 voxelCoord, voxel_t voxel)
  {
    // ZoneScoped;
    if (!IsPositionInGrid(voxelCoord))
    {
      return;
    }

    SetVoxelAtUnchecked(voxelCoord, voxel);
  }

  void Grid::SetVoxelAtUnchecked(glm::ivec3 voxelCoord, voxel_t voxel)
  {
    auto [topLevelCoord, bottomLevelCoord, localVoxelCoord] = GetCoordsOfVoxelAt(voxelCoord);

    const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelCoord);
    DEBUG_ASSERT(topLevelIndex < numTopLevelBricks_);
    auto& topLevelBrickPtr = buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

    if (topLevelBrickPtr.voxelsDoBeAllSame) [[unlikely]]
    {
      // Make a top-level brick
      topLevelBrickPtr = TopLevelBrickPtr{.voxelsDoBeAllSame = false, .topLevelBrick = AllocateTopLevelBrick(topLevelBrickPtr.voxelIfAllSame)};
#ifndef GAME_HEADLESS
      buffer->MarkDirtyPages(&topLevelBrickPtr);
#endif
    }

    const auto bottomLevelIndex = FlattenBottomLevelBrickCoord(bottomLevelCoord);
    DEBUG_ASSERT(bottomLevelIndex < CELLS_PER_TL_BRICK);
    DEBUG_ASSERT(topLevelBrickPtr.topLevelBrick < buffer->SizeBytes() / sizeof(TopLevelBrick));
    auto& bottomLevelBrickPtr = buffer->GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[bottomLevelIndex];

    if (bottomLevelBrickPtr.voxelsDoBeAllSame) [[unlikely]]
    {
      // Make a bottom-level brick
      bottomLevelBrickPtr = BottomLevelBrickPtr{.voxelsDoBeAllSame = false, .bottomLevelBrick = AllocateBottomLevelBrick(bottomLevelBrickPtr.voxelIfAllSame)};
#ifndef GAME_HEADLESS
      buffer->MarkDirtyPages(&bottomLevelBrickPtr);
#endif
    }

    const auto localVoxelIndex = FlattenVoxelCoord(localVoxelCoord);
    DEBUG_ASSERT(localVoxelIndex < CELLS_PER_BL_BRICK);
    DEBUG_ASSERT(bottomLevelBrickPtr.bottomLevelBrick < buffer->SizeBytes() / sizeof(BottomLevelBrick));
    auto& blBrick  = buffer->GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick];
    [[maybe_unused]] auto& dstVoxel = blBrick.voxels[localVoxelIndex] = voxel;
    blBrick.occupancy.Set(localVoxelIndex, materials_[(uint32_t)voxel].isVisible);
#ifndef GAME_HEADLESS
    buffer->MarkDirtyPages(&dstVoxel);
    buffer->MarkDirtyPages(&blBrick.occupancy.bitmask[localVoxelIndex / 32u]);
#endif
    dirtyTopLevelBricks.insert(&topLevelBrickPtr);
    dirtyBottomLevelBricks.insert(&bottomLevelBrickPtr);
  }

  void Grid::CoalesceBricksSLOW()
  {
    ZoneScoped;

    // Check all voxels of each bottom-level brick
    for (size_t i = 0; i < numTopLevelBricks_; i++)
    {
      auto& topLevelBrickPtr = buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + i];
      if (topLevelBrickPtr.voxelsDoBeAllSame)
      {
        continue;
      }

      for (auto& bottomLevelBrickPtr : buffer->GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks)
      {
        CoalesceBottomLevelBrick(bottomLevelBrickPtr);
      }
    }

    // Check just the top-level grids after collapsing bottom-levels
    for (size_t i = 0; i < numTopLevelBricks_; i++)
    {
      auto& topLevelBrickPtr = buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + i];
      if (topLevelBrickPtr.voxelsDoBeAllSame)
      {
        continue;
      }

      CoalesceTopLevelBrick(topLevelBrickPtr);
    }

    dirtyTopLevelBricks.clear();
    dirtyBottomLevelBricks.clear();
  }

  void Grid::CoalesceDirtyBricks()
  {
    ZoneScoped;
    for (auto* bottomLevelBrickPtr : dirtyBottomLevelBricks)
    {
      CoalesceBottomLevelBrick(*bottomLevelBrickPtr);
    }

    for (auto* topLevelBrickPtr : dirtyTopLevelBricks)
    {
      CoalesceTopLevelBrick(*topLevelBrickPtr);
    }

    dirtyTopLevelBricks.clear();
    dirtyBottomLevelBricks.clear();
  }

  void Grid::CoalesceTopLevelBrick(TopLevelBrickPtr& topLevelBrickPtr)
  {
    ZoneScoped;

    // Brick is already coalesced
    if (topLevelBrickPtr.voxelsDoBeAllSame)
    {
      return;
    }

    const voxel_t firstVoxel = buffer->GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[0].voxelIfAllSame;
    for (auto& bottomLevelBrickPtr : buffer->GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks)
    {
      if (!bottomLevelBrickPtr.voxelsDoBeAllSame || bottomLevelBrickPtr.voxelIfAllSame != firstVoxel)
      {
        return;
      }
    }

    FreeTopLevelBrick(topLevelBrickPtr.topLevelBrick);
    topLevelBrickPtr.voxelsDoBeAllSame = true;
    topLevelBrickPtr.voxelIfAllSame    = firstVoxel;
#ifndef GAME_HEADLESS
    buffer->MarkDirtyPages(&topLevelBrickPtr);
#endif
  }

  void Grid::CoalesceTopLevelBrickAndChildren(TopLevelBrickPtr& topLevelBrickPtr)
  {
    ZoneScoped;

    if (topLevelBrickPtr.voxelsDoBeAllSame)
    {
      return;
    }

    for (auto& bottomLevelBrickPtr : buffer->GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks)
    {
      CoalesceBottomLevelBrick(bottomLevelBrickPtr);
    }
  }

  void Grid::CoalesceBottomLevelBrick(BottomLevelBrickPtr& bottomLevelBrickPtr)
  {
    ZoneScoped;

    // Brick is already coalesced
    if (bottomLevelBrickPtr.voxelsDoBeAllSame)
    {
      return;
    }

    const voxel_t firstVoxel = buffer->GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels[0];
    for (const auto& voxel : buffer->GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels)
    {
      if (firstVoxel != voxel)
      {
        return;
      }
    }

    FreeBottomLevelBrick(bottomLevelBrickPtr.bottomLevelBrick);
    bottomLevelBrickPtr.voxelsDoBeAllSame = true;
    bottomLevelBrickPtr.voxelIfAllSame    = firstVoxel;
#ifndef GAME_HEADLESS
    auto lk = std::unique_lock(*mutex_);
    buffer->MarkDirtyPages(&bottomLevelBrickPtr);
#endif
  }

  int Grid::FlattenTopLevelBrickCoord(glm::ivec3 coord) const
  {
    return (coord.z * topLevelBricksDims_.x * topLevelBricksDims_.y) + (coord.y * topLevelBricksDims_.x) + coord.x;
  }

  int Grid::FlattenBottomLevelBrickCoord(glm::ivec3 coord)
  {
    return (coord.z * TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH) + (coord.y * TL_BRICK_SIDE_LENGTH) + coord.x;
  }

  int Grid::FlattenVoxelCoord(glm::ivec3 coord)
  {
    return (coord.z * BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH) + (coord.y * BL_BRICK_SIDE_LENGTH) + coord.x;
  }

  const Grid::TopLevelBrickPtr& Grid::GetTopLevelBrickPtr(uint32_t index) const
  {
    return buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + index];
  }

  Grid::TopLevelBrickPtr& Grid::GetTopLevelBrickPtr(uint32_t index)
  {
    return buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + index];
  }

  const Grid::TopLevelBrick& Grid::GetTopLevelBrick(uint32_t ptr) const
  {
    return buffer->GetBase<TopLevelBrick>()[ptr];
  }

  Grid::TopLevelBrick& Grid::GetTopLevelBrick(uint32_t ptr)
  {
    return buffer->GetBase<TopLevelBrick>()[ptr];
  }

  const Grid::BottomLevelBrickPtr& Grid::GetBottomLevelBrickPtr(uint32_t ptr) const
  {
    return buffer->GetBase<BottomLevelBrickPtr>()[ptr];
  }

  Grid::BottomLevelBrickPtr& Grid::GetBottomLevelBrickPtr(uint32_t ptr)
  {
    return buffer->GetBase<BottomLevelBrickPtr>()[ptr];
  }

  const Grid::BottomLevelBrick& Grid::GetBottomLevelBrick(uint32_t ptr) const
  {
    return buffer->GetBase<BottomLevelBrick>()[ptr];
  }

  Grid::BottomLevelBrick& Grid::GetBottomLevelBrick(uint32_t ptr)
  {
    return buffer->GetBase<BottomLevelBrick>()[ptr];
  }

  uint32_t Grid::AllocateTopLevelBrick(voxel_t initialVoxel)
  {
    ZoneScoped;

    uint32_t index;

    {
      auto lk = std::unique_lock(*mutex_);
      // The alignment of the allocation should be the size of the object being allocated so it can be indexed from the base ptr
      auto allocation = buffer->Allocate(sizeof(TopLevelBrick), sizeof(TopLevelBrick));
      index           = uint32_t(allocation.offset / sizeof(TopLevelBrick));
      topLevelBrickIndexToAlloc.emplace(index, allocation);
    }

    // Initialize
    auto& top = buffer->GetBase<TopLevelBrick>()[index];
    std::construct_at(&top);
    std::ranges::fill(top.bricks, BottomLevelBrickPtr{.voxelsDoBeAllSame = true, .voxelIfAllSame = initialVoxel});
#ifndef GAME_HEADLESS
    auto lk = std::unique_lock(*mutex_);
    buffer->MarkDirtyPages(&top);
#endif
    return index;
  }

  uint32_t Grid::AllocateBottomLevelBrick(voxel_t initialVoxel)
  {
    ZoneScoped;

    uint32_t index;

    {
      auto lk         = std::unique_lock(*mutex_);
      auto allocation = buffer->Allocate(sizeof(BottomLevelBrick), sizeof(BottomLevelBrick));
      index           = uint32_t(allocation.offset / sizeof(BottomLevelBrick));
      bottomLevelBrickIndexToAlloc.emplace(index, allocation);
    }

    // Initialize
    auto& bottom = buffer->GetBase<BottomLevelBrick>()[index];
    std::construct_at(&bottom);
    std::ranges::fill(bottom.voxels, initialVoxel);
    std::ranges::fill(bottom.occupancy.bitmask, materials_[(uint32_t)initialVoxel].isVisible ? ~0u : 0u);
#ifndef GAME_HEADLESS
    auto lk = std::unique_lock(*mutex_);
    buffer->MarkDirtyPages(&bottom);
#endif
    return index;
  }

  void Grid::FreeTopLevelBrick(uint32_t index)
  {
    ZoneScoped;
    auto lk = std::unique_lock(*mutex_);
    auto it = topLevelBrickIndexToAlloc.find(index);
    DEBUG_ASSERT(it != topLevelBrickIndexToAlloc.end());
    buffer->Free(it->second);
    topLevelBrickIndexToAlloc.erase(it);
  }

  void Grid::FreeBottomLevelBrick(uint32_t index)
  {
    ZoneScoped;
    auto lk = std::unique_lock(*mutex_);
    auto it = bottomLevelBrickIndexToAlloc.find(index);
    DEBUG_ASSERT(it != bottomLevelBrickIndexToAlloc.end());
    buffer->Free(it->second);
    bottomLevelBrickIndexToAlloc.erase(it);
  }

  bool Grid::IsPositionInGrid(glm::ivec3 worldPos) const
  {
    return glm::all(glm::greaterThanEqual(worldPos, glm::ivec3(0))) && glm::all(glm::lessThan(worldPos, dimensions_));
  }

  void Grid::SetMaterialArray(std::vector<Material> materials)
  {
    ZoneScoped;
    materials_ = std::move(materials);

    // SketchyBuffer::Alloc is not RAII.
    for (auto& alloc : subGridAllocations)
    {
      buffer->Free(alloc);
    }
    subGridAllocations.clear();

    int i = 0;
    for (auto& material : materials_)
    {
      i++;

      if (auto* grid = material.subGrid)
      {
        // Raw voxel data.
        if (!(grid->dimensions.x == grid->dimensions.y && grid->dimensions.x == grid->dimensions.z))
        {
          spdlog::warn("Material at index {} has non-cube shape ({}, {}, {}) and will not render correctly.",
            i,
            grid->dimensions.x,
            grid->dimensions.y,
            grid->dimensions.z);
        }
        const auto gridSize = grid->dimensions.x * grid->dimensions.y * grid->dimensions.z * sizeof(SubVoxel);
        auto gridAlloc      = buffer->Allocate(gridSize, 4);
        auto* mem           = buffer->GetBase<SubVoxel>() + gridAlloc.offset / sizeof(SubVoxel);
        std::memcpy(mem, grid->grid.get(), gridSize);
#ifndef GAME_HEADLESS
        buffer->MarkRange(gridAlloc.offset, gridSize);
#endif

        // GpuSubGrid
        ASSERT(gridAlloc.offset / sizeof(SubVoxel) <= UINT32_MAX);
        auto subGridInfo = GpuSubGrid{
          .dimensions = glm::ivec3(grid->dimensions),
          .gridBase   = uint32_t(gridAlloc.offset / sizeof(SubVoxel)),
          //.materials  = grid->materials,
        };
        std::ranges::copy(grid->materials, subGridInfo.materials);
        auto gridInfoAlloc = buffer->Allocate(sizeof(subGridInfo), sizeof(subGridInfo));
        auto* mem2         = buffer->GetBase<GpuSubGrid>() + gridInfoAlloc.offset / sizeof(subGridInfo);
        std::memcpy(mem2, &subGridInfo, sizeof(subGridInfo));
#ifndef GAME_HEADLESS
        buffer->MarkDirtyPages(mem2);
#endif
        material.subGrid->myIndexINTERNAL = uint32_t(gridInfoAlloc.offset / sizeof(subGridInfo));

        subGridAllocations.emplace_back(gridAlloc);
        subGridAllocations.emplace_back(gridInfoAlloc);
      }
    }
  }

  void Grid::SetVoxelAtUncheckedNoDirty(glm::ivec3 voxelCoord, voxel_t voxel)
  {
    auto [topLevelCoord, bottomLevelCoord, localVoxelCoord] = GetCoordsOfVoxelAt(voxelCoord);

    const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelCoord);
    DEBUG_ASSERT(topLevelIndex < numTopLevelBricks_);
    auto& topLevelBrickPtr = buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

    if (topLevelBrickPtr.voxelsDoBeAllSame) [[unlikely]]
    {
      // Make a top-level brick
      topLevelBrickPtr = TopLevelBrickPtr{.voxelsDoBeAllSame = false, .topLevelBrick = AllocateTopLevelBrickNoDirty(topLevelBrickPtr.voxelIfAllSame)};
    }

    const auto bottomLevelIndex = FlattenBottomLevelBrickCoord(bottomLevelCoord);
    DEBUG_ASSERT(bottomLevelIndex < CELLS_PER_TL_BRICK);
    DEBUG_ASSERT(topLevelBrickPtr.topLevelBrick < buffer->SizeBytes() / sizeof(TopLevelBrick));
    auto& bottomLevelBrickPtr = buffer->GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[bottomLevelIndex];

    if (bottomLevelBrickPtr.voxelsDoBeAllSame) [[unlikely]]
    {
      // Make a bottom-level brick
      bottomLevelBrickPtr = BottomLevelBrickPtr{.voxelsDoBeAllSame = false, .bottomLevelBrick = AllocateBottomLevelBrickNoDirty(bottomLevelBrickPtr.voxelIfAllSame)};
    }

    const auto localVoxelIndex = FlattenVoxelCoord(localVoxelCoord);
    DEBUG_ASSERT(localVoxelIndex < CELLS_PER_BL_BRICK);
    DEBUG_ASSERT(bottomLevelBrickPtr.bottomLevelBrick < buffer->SizeBytes() / sizeof(BottomLevelBrick));
    auto& blBrick                   = buffer->GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick];
    blBrick.voxels[localVoxelIndex] = voxel;
    blBrick.occupancy.Set(localVoxelIndex, materials_[(uint32_t)voxel].isVisible);
  }

  void Grid::SetVoxelAtNoDirty(glm::ivec3 voxelCoord, voxel_t voxel)
  {
    // ZoneScoped;
    DEBUG_ASSERT(IsPositionInGrid(voxelCoord));
    SetVoxelAtUncheckedNoDirty(voxelCoord, voxel);
  }

  uint32_t Grid::AllocateTopLevelBrickNoDirty(voxel_t initialVoxel)
  {
    ZoneScoped;

    uint32_t index;

    {
      auto lk = std::unique_lock(*mutex_);
      // The alignment of the allocation should be the size of the object being allocated so it can be indexed from the base ptr
      auto allocation = buffer->Allocate(sizeof(TopLevelBrick), sizeof(TopLevelBrick));
      index           = uint32_t(allocation.offset / sizeof(TopLevelBrick));
      topLevelBrickIndexToAlloc.emplace(index, allocation);
    }

    // Initialize
    auto& top = buffer->GetBase<TopLevelBrick>()[index];
    std::construct_at(&top);
    std::ranges::fill(top.bricks, BottomLevelBrickPtr{.voxelsDoBeAllSame = true, .voxelIfAllSame = initialVoxel});
    return index;
  }

  uint32_t Grid::AllocateBottomLevelBrickNoDirty(voxel_t initialVoxel)
  {
    ZoneScoped;

    uint32_t index;

    {
      auto lk         = std::unique_lock(*mutex_);
      auto allocation = buffer->Allocate(sizeof(BottomLevelBrick), sizeof(BottomLevelBrick));
      index           = uint32_t(allocation.offset / sizeof(BottomLevelBrick));
      bottomLevelBrickIndexToAlloc.emplace(index, allocation);
    }

    // Initialize
    auto& bottom = buffer->GetBase<BottomLevelBrick>()[index];
    std::construct_at(&bottom);
    std::ranges::fill(bottom.voxels, initialVoxel);
    std::ranges::fill(bottom.occupancy.bitmask, materials_[(uint32_t)initialVoxel].isVisible ? ~0u : 0u);
    return index;
  }

  void Grid::MarkTopLevelBrickAndChildrenDirty(glm::ivec3 topLevelBrickPos)
  {
    ZoneScoped;
    const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelBrickPos);
    DEBUG_ASSERT(topLevelIndex < numTopLevelBricks_);
    auto& topLevelBrickPtr = buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

    auto lk = std::unique_lock(*mutex_);
#ifndef GAME_HEADLESS
    buffer->MarkDirtyPages(&topLevelBrickPtr);
#endif
    dirtyTopLevelBricks.emplace(&topLevelBrickPtr);

    if (topLevelBrickPtr.voxelsDoBeAllSame)
    {
      return;
    }

    auto& topLevelBrick = buffer->GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick];
#ifndef GAME_HEADLESS
    buffer->MarkDirtyPages(&topLevelBrick);
#endif
    for (auto& bottomLevelBrickPtr : topLevelBrick.bricks)
    {
#ifndef GAME_HEADLESS
      buffer->MarkDirtyPages(&bottomLevelBrickPtr);
#endif
      dirtyBottomLevelBricks.emplace(&bottomLevelBrickPtr);

      if (bottomLevelBrickPtr.voxelsDoBeAllSame)
      {
        continue;
      }

      [[maybe_unused]] const auto& bottomLevelBrick = buffer->GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick];
#ifndef GAME_HEADLESS
      buffer->MarkDirtyPages(&bottomLevelBrick);
#endif
    }
  }

  void Grid::MarkAllBricksDirty()
  {
    ZoneScoped;
    for (int z = 0; z < topLevelBricksDims_.z; z++)
    {
      for (int y = 0; y < topLevelBricksDims_.y; y++)
      {
        for (int x = 0; x < topLevelBricksDims_.x; x++)
        {
          MarkTopLevelBrickAndChildrenDirty({x, y, z});
        }
      }
    }
  }

  Grid::TopLevelBrickPtr& Grid::GetTopLevelBrickPointerFromTopLevelPosition(glm::ivec3 topLevelCoord)
  {
    const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelCoord);
    DEBUG_ASSERT(topLevelIndex < numTopLevelBricks_);
    return buffer->GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];
  }
} // namespace Voxel
