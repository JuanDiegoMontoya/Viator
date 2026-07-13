#include "LightGridCommon.shared.h"
#include "../Light.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(ScatterLightsToCellsGpuParams)
{
  FVOG_UINT32 lightCount;
  GpuLightPtr lights;

  CascadedLightGrid cascadedLightGrid;
  FVOG_VEC3 cameraPosition;
  FVOG_BOOL32 isSecondPass; // In the second pass, perform writes to per-cell lists.
};

FVOG_DECLARE_ARGUMENTS(ScatterLightsToCellsArgs)
{
  ScatterLightsToCellsGpuParams pc;
};

#ifndef __cplusplus

#include "../Math.h.glsl"

layout(local_size_x = 128) in;
void main()
{
  const int cascade    = int(gl_GlobalInvocationID.z);
  const int lightIndex = int(gl_GlobalInvocationID.x);

  if (cascade >= pc.cascadedLightGrid.numCascades || lightIndex >= pc.lightCount)
  {
    return;
  }

  LightGrid grid = pc.cascadedLightGrid.grids[cascade];
  const GpuLight light = pc.lights[lightIndex].data;

  const float factor = 1.733; // sqrt(0.5^2) / 0.5. How much we need to scale a sphere that is inscribed within a box to fully encompass that box.
  const vec3 minPos = light.position - light.range * factor;
  const vec3 maxPos = light.position + light.range * factor;

  const ivec3 minPosI = clamp(ivec3(floor(minPos / grid.gridScale)) - grid.positionOffset, ivec3(0), pc.cascadedLightGrid.cascadeDimensions);
  const ivec3 maxPosI = clamp(ivec3(ceil(maxPos / grid.gridScale)) - grid.positionOffset, ivec3(0), pc.cascadedLightGrid.cascadeDimensions);

  for (int z = minPosI.z; z < maxPosI.z; z++)
  for (int y = minPosI.y; y < maxPosI.y; y++)
  for (int x = minPosI.x; x < maxPosI.x; x++)
  {
    const ivec3 cellPos = ivec3(x, y, z);
    const vec3 cellMinPosWS = (vec3(x, y, z) + grid.positionOffset) * grid.gridScale;
    const vec3 cellMaxPosWS = cellMinPosWS + grid.gridScale;
    if (SphereAABBIntersect(light.position, light.range, cellMinPosWS, cellMaxPosWS))
    {
      if (!bool(pc.isSecondPass)) // First pass, only record count.
      {
        imageAtomicAdd(grid.cellLightListCount, cellPos, 1);
      }
      else // Second pass: write light index to list.
      {
        const uint relIndex = imageAtomicAdd(grid.cellLightListCountIntermediate, cellPos, 1);
        if (relIndex > pc.cascadedLightGrid.maxLightsPerCell)
        {
          continue;
        }
        
        const uint globalIndex = imageLoad(grid.cellLightListOffset, cellPos).x + relIndex;
        grid.lightIndices.values[globalIndex].data = lightIndex;
      }
    }
  }
}

#endif // !__cplusplus