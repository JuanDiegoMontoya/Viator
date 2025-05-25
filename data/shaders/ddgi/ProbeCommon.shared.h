#ifndef COMMON_SHARED_H
#define COMMON_SHARED_H

#include "../voxels/Voxels.h.glsl"
#include "../Resources.h.glsl"

#define DDGI_NUM_CASCADES 6

struct DDGIProbeGridInfo
{
  FVOG_IVEC2 probeRadianceResolution;
  FVOG_IVEC2 probeIrradianceResolution;
  FVOG_IVEC2 probeDepthMomentsResolution;
  FVOG_IVEC3 gridResolution;
  FVOG_FLOAT baseGridScale; // Scale of smallest cascade. Successive cascades have 2x the scale as the last.
  FVOG_IVEC3 gridOffset; // Offset of the grid, in baseGridScale units, from the origin.
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
  FVOG_UINT32 globalUniformsIndex;

  // Probe info
  DDGIProbeGridInfo gridInfo[DDGI_NUM_CASCADES];
  FVOG_SHARED Image2DArray packedProbeRadiance;
  FVOG_SHARED Image2DArray packedProbeIrradiance;
  FVOG_SHARED Image2DArray packedProbeRawDepth;
  FVOG_SHARED Image2DArray packedProbeDepthMoments;
  FVOG_SHARED Texture2DArray packedProbeRadianceTex;
  FVOG_SHARED Texture2DArray packedProbeIrradianceTex;
  FVOG_SHARED Texture2DArray packedProbeRawDepthTex;
  FVOG_SHARED Texture2DArray packedProbeDepthMomentsTex;
  FVOG_SHARED Sampler linearSampler;
};

#ifndef __cplusplus
#ifndef DDGI_NO_PUSH_CONSTANTS
FVOG_DECLARE_ARGUMENTS(DDGIPushConstants)
{
  DDGIArgs args;
};
#endif // !DDGI_NO_PUSH_CONSTANTS

///// Helper functions for shaders.

ivec3 ProbeIndexToCoord(int probeIndex, ivec3 gridResolution)
{
  return ivec3(
    probeIndex % gridResolution.x,
    (probeIndex / gridResolution.x) % gridResolution.y,
    probeIndex / (gridResolution.x * gridResolution.y)
  );
}

// Number of texels in a probe, including border texels.
// Accounts for 1-texel border.
int TotalTexelsPerProbe(ivec2 probeResolution)
{
  return (2 + probeResolution.x) * (2 + probeResolution.y);
}

ivec2 GetWorkTexelCoord(int workIndex, ivec2 probeResolution)
{
  const int numTexels = probeResolution.x * probeResolution.y;
  const int texelIndex = workIndex % numTexels;
  return ivec2(
    texelIndex % probeResolution.x,
    texelIndex / probeResolution.x
  );
}

// Gets the lower corner of a probe's __work area__ in image space.
ivec2 GetProbeTexelOffset(int probeIndex, ivec2 packedProbeImageSize, ivec2 probeResolution)
{
  const ivec2 probeGridSize2d = packedProbeImageSize / (2 + probeResolution);
  return (2 + probeResolution) * ivec2(
    probeIndex % probeGridSize2d.x,
    probeIndex / probeGridSize2d.x
  ) + 1;
}

vec2 ProbeDirectionToUv(vec3 direction, int probeIndex, ivec2 packedProbeImageSize, ivec2 probeResolution)
{
  const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, packedProbeImageSize, probeResolution);
  const vec2 uvOffset = vec2(texelOffset) / packedProbeImageSize;
  return (Vec3ToOct(normalize(direction)) * .5 + .5) / (packedProbeImageSize / probeResolution);
}

vec3 ProbeTexelCoordToDirection(ivec2 texelCoord, ivec2 probeResolution)
{
  const vec2 uv = (texelCoord + 0.5) / probeResolution;
  return OctToVec3(uv * 2 - 1);
}

int ProbeCoordToIndex(ivec3 probeCoord, ivec3 gridResolution)
{
  return (probeCoord.z * gridResolution.x * gridResolution.y) + 
    (probeCoord.y * gridResolution.x) + 
    probeCoord.x;
}

void WriteToProbeWithBorder(Image2DArray packedProbeImage, int cascade, int probeIndex, ivec2 probeResolution, ivec2 texelCoord, vec4 value)
{
  const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(packedProbeImage).xy, probeResolution);
  imageStore(packedProbeImage, ivec3(texelOffset + texelCoord, cascade), value);
  
  ///// For work texels on the edge of the probe, write to applicable border texels.
  // Sides
  if (texelCoord.x == 0)
  {
    const ivec2 borderCoord = {-1, probeResolution.y - 1 - texelCoord.y};
    imageStore(packedProbeImage, ivec3(texelOffset + borderCoord, cascade), value);
  }
  
  if (texelCoord.x == probeResolution.x - 1)
  {
    const ivec2 borderCoord = {probeResolution.x, probeResolution.y - 1 - texelCoord.y};
    imageStore(packedProbeImage, ivec3(texelOffset + borderCoord, cascade), value);
  }
  
  if (texelCoord.y == 0)
  {
    const ivec2 borderCoord = {probeResolution.x - 1 - texelCoord.x, -1};
    imageStore(packedProbeImage, ivec3(texelOffset + borderCoord, cascade), value);
  }
  
  if (texelCoord.y == probeResolution.y - 1)
  {
    const ivec2 borderCoord = {probeResolution.x - 1 - texelCoord.x, probeResolution.y};
    imageStore(packedProbeImage, ivec3(texelOffset + borderCoord, cascade), value);
  }

  // Corners
  if (texelCoord == ivec2(0, 0))
  {
    const ivec2 borderCoord = probeResolution;
    imageStore(packedProbeImage, ivec3(texelOffset + borderCoord, cascade), value);
  }
  
  if (texelCoord == probeResolution - 1)
  {
    const ivec2 borderCoord = {-1, -1};
    imageStore(packedProbeImage, ivec3(texelOffset + borderCoord, cascade), value);
  }
  
  if (texelCoord == ivec2(0, probeResolution.y - 1))
  {
    const ivec2 borderCoord = {probeResolution.x, -1};
    imageStore(packedProbeImage, ivec3(texelOffset + borderCoord, cascade), value);
  }

  if (texelCoord == ivec2(probeResolution.x - 1, 0))
  {
    const ivec2 borderCoord = {-1, probeResolution.y};
    imageStore(packedProbeImage, ivec3(texelOffset + borderCoord, cascade), value);
  }
}

#endif // !__cplusplus

#endif // COMMON_SHARED_H
