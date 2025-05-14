#ifndef COMMON_SHARED_H
#define COMMON_SHARED_H

#include "../voxels/Voxels.h.glsl"
#include "../Resources.h.glsl"

struct DDGIProbeGridInfo
{
  FVOG_IVEC2 probeRadianceResolution;
  //FVOG_IVEC2 probeDepthResolution;
  //FVOG_IVEC2 probeFilteredDepthResolution;
  FVOG_IVEC2 probeIrradianceResolution;
  FVOG_IVEC3 gridResolution;
  FVOG_FLOAT baseGridScale; // Scale of smallest cascade. Successive cascades have 2x the scale as the last.
};

#ifndef __cplusplus
FVOG_DECLARE_BUFFER_REFERENCE(DDGIArgs)
#else
struct DDGIArgs
#endif
{
  // Path tracer info
  Voxels voxels;
  FVOG_UINT32 internalColorSpace;
  FVOG_SHARED Texture2D noiseTexture;
  FVOG_UINT32 samples;
  FVOG_UINT32 bounces;

  // Probe info
  DDGIProbeGridInfo gridInfo;
  FVOG_SHARED Image2D packedProbeRadiance;
  FVOG_SHARED Image2D packedProbeIrradiance;
  FVOG_SHARED Texture2D packedProbeRadianceTex;
  FVOG_SHARED Texture2D packedProbeIrradianceTex;
  FVOG_SHARED Sampler linearSampler;
};

#ifndef __cplusplus
#ifndef DDGI_NO_PUSH_CONSTANTS
FVOG_DECLARE_ARGUMENTS(DDGIPushConstants)
{
  DDGIArgs args;
};
#endif
#endif

#endif
