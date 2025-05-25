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

vec3 SampleIlluminanceField(vec3 positionWS, vec3 normalWS, Sampler linearSampler, DDGIArgs ddgi)
{
  // Select lowest possible cascade to sample.
  int cascade = -1;
  for (int i = 0; i < DDGI_NUM_CASCADES; i++)
  {
    const vec3 posProbeSpacePreMod = ((positionWS - 0.5) / ddgi.gridInfo[i].baseGridScale) - ddgi.gridInfo[i].gridOffset;
    if (all(greaterThanEqual(posProbeSpacePreMod, vec3(0))) && all(lessThan(posProbeSpacePreMod, ddgi.gridInfo[i].gridResolution - 1)))
    {
      cascade = i;
      break;
    }
  }

  // Sample nearest 8 probes and apply trilinear weights.
  if (cascade != -1)
  {
    float sumWeights = 0;
    float sumWeightsNoShadow = 0;
    vec3 irradiance_internal = vec3(0);
    vec3 irradiance_internalNoShadow = vec3(0);
    //uint rng = PCG_Hash(gid.x + PCG_Hash(gid.y));

    const vec3 posProbeSpacePreMod = ((positionWS - 0.5) / ddgi.gridInfo[cascade].baseGridScale) - ddgi.gridInfo[cascade].gridOffset;
    const vec3 posProbeSpace = mod(posProbeSpacePreMod, ddgi.gridInfo[cascade].gridResolution);
    const ivec3 minProbe = ivec3(floor(posProbeSpace));
    const ivec3 maxProbe = minProbe + 1;

    for (int z = minProbe.z; z <= maxProbe.z; z++)
    for (int y = minProbe.y; y <= maxProbe.y; y++)
    for (int x = minProbe.x; x <= maxProbe.x; x++)
    {
      //const ivec3 p = ivec3(round(probeCoord));
      const vec3 probePos = vec3(x, y, z);
      const vec3 probePosWS = (probePos + ddgi.gridInfo[cascade].gridOffset) * ddgi.gridInfo[cascade].baseGridScale + 0.5;
      const float trilinearWeight = TrilinearWeight(probePos, posProbeSpace);

      // Give less weight to probes that lie below the plane of the shaded point.
      const vec3 dirToProbe = normalize(probePos - posProbeSpace);
      //const float backfaceWeight = max(1e-4, dot(dirToProbe, normal));
      const float backfaceWeight = square(max(1e-4, dot(dirToProbe, normalWS) * 0.5 + 0.5)) + 0.2; // Wrap shading term

      // Sample probe illuminance and depth moments.
      const int probeIndex = ProbeCoordToIndex(ivec3(probePos), ddgi.gridInfo[cascade].gridResolution);

      const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(ddgi.packedProbeIrradiance).xy, ddgi.gridInfo[cascade].probeIrradianceResolution);
      const vec2 uvOffset = vec2(texelOffset) / imageSize(ddgi.packedProbeIrradiance).xy;
      const vec2 uv = ProbeDirectionToUv(normalWS, probeIndex, imageSize(ddgi.packedProbeIrradiance).xy, ddgi.gridInfo[cascade].probeIrradianceResolution);
      const vec3 illuminance = textureLod(ddgi.packedProbeIrradianceTex, linearSampler, vec3(uvOffset + uv, cascade), 0).rgb;
      
      float shadowWeight = 1;
      const float normalBias = 0.45 * ddgi.gridInfo[cascade].baseGridScale;
      //const vec3 probeToPointBiasedWS = positionWS - probePosWS + (normal + 3.0 * viewDirWS) * normalBias;
      const vec3 probeToPointBiasedWS = (positionWS + normalWS * normalBias) - probePosWS;
      const vec3 dirToProbeBiased = normalize(-probeToPointBiasedWS);
      const float distToProbeWS = length(probeToPointBiasedWS);
      //const float distToProbeWS = length(probePos - posProbeSpace) * ddgi.gridInfo.baseGridScale;

#if 1 // Chebyshev test.
      const ivec2 texelOffset2 = GetProbeTexelOffset(probeIndex, imageSize(ddgi.packedProbeDepthMoments).xy, ddgi.gridInfo[cascade].probeDepthMomentsResolution);
      const vec2 uvOffset2 = vec2(texelOffset2) / imageSize(ddgi.packedProbeDepthMoments).xy;
      const vec2 uv2 = ProbeDirectionToUv(-dirToProbeBiased, probeIndex, imageSize(ddgi.packedProbeDepthMoments).xy, ddgi.gridInfo[cascade].probeDepthMomentsResolution);
      const vec2 depthMoments = textureLod(ddgi.packedProbeDepthMomentsTex, linearSampler, vec3(uvOffset2 + uv2, cascade), 0).xy;
      const float mean = depthMoments.x;
      const float mean2 = depthMoments.y;

      if (distToProbeWS > mean)
      {
        const float variance = abs(mean * mean - mean2);
        shadowWeight = variance / (variance + square(max(distToProbeWS - mean, 0.0)));
      }
#else // Regular shadow test.
      const ivec2 texelOffset2 = GetProbeTexelOffset(probeIndex, imageSize(ddgi.packedProbeRawDepth), ddgi.gridInfo.probeRadianceResolution);
      const vec2 uvOffset2 = vec2(texelOffset2) / imageSize(ddgi.packedProbeRawDepth);
      const vec2 uv2 = ProbeDirectionToUv(-dirToProbeBiased, probeIndex, imageSize(ddgi.packedProbeRawDepth), ddgi.gridInfo.probeRadianceResolution);
      const float rawDepth = textureLod(ddgi.packedProbeRawDepthTex, samplerr, uvOffset2 + uv2, 0).x;

      if (distToProbeWS > rawDepth)
      {
        shadowWeight = 0;
      }
#endif

      float weightNoTrilinear = backfaceWeight * shadowWeight;
      float weightNoTrilinearNoShadow = backfaceWeight;
      
      weightNoTrilinear = max(1e-5, weightNoTrilinear);
      weightNoTrilinearNoShadow = max(1e-5, weightNoTrilinearNoShadow);

      // Since tiny leaks are very visible, we crush tiny weights here while keeping the curve continuous.
      const float threshold = 0.2;
      if (weightNoTrilinear < threshold)
      {
        weightNoTrilinear *= square(weightNoTrilinear) / square(threshold);
      }
      if (weightNoTrilinearNoShadow < threshold)
      {
        weightNoTrilinearNoShadow *= square(weightNoTrilinearNoShadow) / square(threshold);
      }

      const float weight = trilinearWeight * weightNoTrilinear;
      const float weightNoShadow = trilinearWeight * weightNoTrilinearNoShadow;
#if PERCEPTUAL_BLEND
      irradiance_internal += weight * sqrt(illuminance);
      irradiance_internalNoShadow += weightNoShadow * sqrt(illuminance);
#else
      irradiance_internal += weight * illuminance;
      irradiance_internalNoShadow += weightNoShadow * illuminance;
#endif
      sumWeights += weight;
      sumWeightsNoShadow += weightNoShadow;
    }

    irradiance_internal /= sumWeights;
    irradiance_internalNoShadow /= sumWeightsNoShadow;

#if PERCEPTUAL_BLEND
    irradiance_internal *= irradiance_internal;
    irradiance_internalNoShadow *= irradiance_internalNoShadow;
#endif

    // TODO: Smooth blend (mix + smoothstep) when sumWeights is small.
    if (sumWeights < 1e-4)
    {
      irradiance_internal = irradiance_internalNoShadow;
    }
    return irradiance_internal;
  }

  return vec3(1, 0, 0); // No available cascade
}

#endif // !__cplusplus

#endif // COMMON_SHARED_H
