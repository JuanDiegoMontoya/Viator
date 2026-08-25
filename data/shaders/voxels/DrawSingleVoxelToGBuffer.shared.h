#ifndef DRAW_SINGLE_VOXEL_TO_GBUFFER_H
#define DRAW_SINGLE_VOXEL_TO_GBUFFER_H

#include "../Resources.h.glsl"
#include "../GlobalUniforms.h.glsl"
#include "Voxels.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_3(DrawSingleVoxelInstance, 4)
{
  FVOG_VEC3 voxelPosition;
  FVOG_MAT4 world_from_object;
  FVOG_MAT4 world_from_object_old;
  FVOG_MAT4 object_from_world;
  voxel_t voxel;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(DrawSingleVoxelToGBufferParams)
{
  GlobalUniformsPtr uniforms;
  FVOG_UINT32 voxelDataBufferIdx;     // Subgrids
  FVOG_UINT32 voxelMaterialBufferIdx; // Materials
  DrawSingleVoxelInstance instances;
};

FVOG_DECLARE_ARGUMENTS(DrawSingleVoxelToGBufferArgs)
{
  DrawSingleVoxelToGBufferParams pc;
};

#endif // DRAW_SINGLE_VOXEL_TO_GBUFFER_H