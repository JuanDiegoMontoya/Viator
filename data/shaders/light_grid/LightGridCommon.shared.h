#ifndef LIGHT_GRID_COMMON_H
#define LIGHT_GRID_COMMON_H

#include "../Resources.h.glsl"
#include "../CommonTypes.shared.h"

#define LIGHT_GRID_MAX_CASCADES 8

struct LightGrid
{
  FVOG_SHARED UImage3D cellLightListOffset;
  FVOG_SHARED UImage3D cellLightListCount;
  FVOG_SHARED UImage3D cellLightListCountIntermediate;
  FVOG_FLOAT gridScale;
  FVOG_IVEC3 positionOffset;
  IntVector lightIndices;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(CascadedLightGrid)
{
  LightGrid grids[LIGHT_GRID_MAX_CASCADES];
  FVOG_UINT32 numCascades;
  FVOG_IVEC3 cascadeDimensions;
  FVOG_INT32 maxLightsPerCell;
};

#ifndef __cplusplus

// Returns ivec4(-1) if outside the grid.
ivec4 LG_GetCellPositionAndCascadeIndexAtPoint(CascadedLightGrid cascadedLightGrid, vec3 point)
{
  // If the cascades grow geometrically, then there's probably a simple formula to calculate the cascade and cell position.
  // Iteration allows for increased flexiblity in cascade sizes and dimensions at (likely) the expense of some perf.
  for (int cascade = 0; cascade < cascadedLightGrid.numCascades; cascade++)
  {
    LightGrid grid = cascadedLightGrid.grids[cascade];
    const ivec3 gridPos = ivec3(floor(point / grid.gridScale)) - grid.positionOffset;
    if (all(greaterThanEqual(gridPos, ivec3(0))) && all(lessThan(gridPos, cascadedLightGrid.cascadeDimensions)))
    {
      return ivec4(gridPos, cascade);
    }
  }
  return ivec4(-1);
}

uint LG_GetLightListOffsetForCell(CascadedLightGrid cascadedLightGrid, int cascade, ivec3 cellPosition)
{
  return imageLoad(cascadedLightGrid.grids[cascade].cellLightListOffset, cellPosition).x;
}

uint LG_GetLightCountForCell(CascadedLightGrid cascadedLightGrid, int cascade, ivec3 cellPosition)
{
  const int offset = int(LG_GetLightListOffsetForCell(cascadedLightGrid, cascade, cellPosition));
  const int count  = int(min(imageLoad(cascadedLightGrid.grids[cascade].cellLightListCount, cellPosition).x, cascadedLightGrid.maxLightsPerCell));
  // Clamp count to maximum length for the given offset. This will make it safe to use the count for 
  // iterating the light list in the unlikely event that the per-cascade index list is completely full.
  return uint(max(0, min(count, int(cascadedLightGrid.grids[cascade].lightIndices.capacity) - offset)));
}

#endif // !__cplusplus

#endif // LIGHT_GRID_COMMON_H