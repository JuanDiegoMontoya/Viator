#ifndef SHADE_DEFERRED_H
#define SHADE_DEFERRED_H

#include "../Resources.h.glsl"
#include "../Color.h.glsl"
#include "Voxels.h.glsl"

#ifdef __cplusplus
using namespace shared;
#endif
FVOG_DECLARE_ARGUMENTS(ShadingPushConstants)
{
  Voxels voxels;
  Texture2D gAlbedo;
  Texture2D gDepth;
  Texture2D gNormal;
  Texture2D gRadiance;
  Texture2D gIndirectIlluminance;
  Image2D sceneColor;
  FVOG_UINT32 internalColorSpace;
  FVOG_UINT32 uniformBufferIndex;
};

#endif
