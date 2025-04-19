#ifndef PER_PIXEL_PATH_TRACER_H
#define PER_PIXEL_PATH_TRACER_H

#include "../Resources.h.glsl"
#include "../Color.h.glsl"
#include "Voxels.h.glsl"

#ifdef __cplusplus
using namespace shared;
#endif
FVOG_DECLARE_ARGUMENTS(PerPixelPathtracerArguments)
{
  Voxels voxels;
  Texture2D gDepth;
  Texture2D gNormal;
  Image2D gIndirectIrradiance;
  FVOG_UINT32 internalColorSpace;
  FVOG_UINT32 uniformBufferIndex;
  Texture2D noiseTexture;
};

#endif // PER_PIXEL_PATH_TRACER_H
