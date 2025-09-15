#ifndef SHADE_DEFERRED_H
#define SHADE_DEFERRED_H

#include "../Resources.h.glsl"
#include "../Color.h.glsl"
#include "Voxels.h.glsl"
#define DDGI_NO_PUSH_CONSTANTS
#include "../ddgi/ProbeCommon.shared.h"

FVOG_DECLARE_ARGUMENTS(ShadingPushConstants)
{
  Voxels voxels;
  FVOG_SHARED Image2D sceneColor;
  FVOG_UINT32 internalColorSpace;
  FVOG_UINT32 uniformBufferIndex;
  
#ifndef __cplusplus
  DDGIArgs ddgi;
#else
  VkDeviceAddress ddgi;
#endif
  FVOG_SHARED Sampler samplerr;
  FVOG_UINT32 giMethod; // 1 = per-pixel PT. 2 = DDGI
  FVOG_BOOL32 applySpelunkerEffect;
  FVOG_SHARED Texture2D ambientOcclusion;
};

#endif
