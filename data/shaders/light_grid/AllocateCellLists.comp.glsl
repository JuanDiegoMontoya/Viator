#include "LightGridCommon.shared.h"

FVOG_DECLARE_BUFFER_REFERENCE_2(AllocateCellListsGpuParams)
{
  CascadedLightGrid cascadedLightGrid;
  FVOG_UINT32 cascade;
};

FVOG_DECLARE_ARGUMENTS(AllocateCellListsArgs)
{
  AllocateCellListsGpuParams pc;
};

#ifndef __cplusplus

layout(local_size_x = 128) in;

void main()
{
  const ivec3 cellPos = ivec3(gl_GlobalInvocationID.xyz);
  if (any(greaterThanEqual(cellPos, pc.cascadedLightGrid.cascadeDimensions)))
  {
    return;
  }

  LightGrid grid = pc.cascadedLightGrid.grids[pc.cascade];

  const uint myCount  = min(imageLoad(grid.cellLightListCount, cellPos).x, pc.cascadedLightGrid.maxLightsPerCell);
  const uint myOffset = atomicAdd(grid.lightIndices.size, int(myCount));
  imageStore(grid.cellLightListOffset, cellPos, uvec4(myOffset));
  
  // Handle overflow of the cascade's light indices.
  if (myOffset + myCount >= grid.lightIndices.capacity)
  {
    const uint newCount = uint(max(0, int(grid.lightIndices.capacity) - int(myOffset)));
    imageStore(grid.cellLightListCount, cellPos, uvec4(newCount));
  }
}

#endif // !_cplusplus