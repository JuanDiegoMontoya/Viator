#ifndef COMMON_SHARED_H
#define COMMON_SHARED_H

#include "../voxels/Voxels.h.glsl"
#include "../Resources.h.glsl"
#ifndef __cplusplus
#include "../Math.h.glsl"
#include "../DistanceFunctions.h.glsl"
#endif

#define DDGI_NUM_CASCADES 6

struct ProbeData
{
  float validity;
};

#ifndef __cplusplus
FVOG_DECLARE_BUFFER_REFERENCE(ProbeInfo)
{
  ProbeData data[];
};
#endif

struct DDGIProbeGridInfo
{
  FVOG_IVEC2 probeRadianceResolution;
  FVOG_IVEC2 probeIrradianceResolution;
  FVOG_IVEC2 probeDepthMomentsResolution;
  FVOG_IVEC3 gridResolution;
  FVOG_FLOAT baseGridScale; // Scale of smallest cascade. Successive cascades have 2x the scale as the last.
  FVOG_IVEC3 gridOffset; // Offset of the grid, in baseGridScale units, from the origin.
  FVOG_IVEC3 oldGridOffset; // Previous frame's gridOffset. Used to determine which probes to reset.
  FVOG_VEC3 gridOffsetFraction;
#ifdef __cplusplus
  VkDeviceAddress probes;
#else
  ProbeInfo probes;
#endif
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
  FVOG_BOOL32 showCascadeIndexAsColor;

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

// Stable index for a world-space position.
int ProbeIndexToStableIndex(int probeIndex, DDGIProbeGridInfo gridInfo)
{
  const vec3 probePos = ProbeIndexToCoord(probeIndex, gridInfo.gridResolution);
  const vec3 probePosKindaWS = probePos + gridInfo.gridOffset;
  const vec3 probePosKindaWSWrapped = mod(probePosKindaWS, gridInfo.gridResolution);
  return ProbeCoordToIndex(ivec3(probePosKindaWSWrapped), gridInfo.gridResolution);
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

vec3 SampleIlluminanceFieldRaw(vec3 positionWS, vec3 normalWS, Sampler linearSampler, DDGIArgs ddgi, int cascade, out float outWeight)
{
  if (bool(ddgi.showCascadeIndexAsColor))
  {
    outWeight = 1;
    return TurboColormap(float(cascade) / (DDGI_NUM_CASCADES - 1.0));
  }

  if (cascade >= DDGI_NUM_CASCADES)
  {
    return vec3(0);
  }

  float sumWeights = 0;
  float sumWeightsNoShadow = 0;
  vec3 irradiance_internal = vec3(0);
  vec3 irradiance_internalNoShadow = vec3(0);
  //uint rng = PCG_Hash(gid.x + PCG_Hash(gid.y));

  const vec3 posProbeSpacePreMod = ((positionWS - 0.5) / ddgi.gridInfo[cascade].baseGridScale) - ddgi.gridInfo[cascade].gridOffset;
  const vec3 posProbeSpace = mod(posProbeSpacePreMod, ddgi.gridInfo[cascade].gridResolution);
  const ivec3 minProbe = ivec3(floor(posProbeSpace));
  const ivec3 maxProbe = minProbe + 1;

  // Sample nearest 8 probes with weights.
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
    const int probeIndexA = ProbeCoordToIndex(ivec3(probePos), ddgi.gridInfo[cascade].gridResolution);
    const int probeIndex = ProbeIndexToStableIndex(probeIndexA, ddgi.gridInfo[cascade]);

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
    const ivec2 texelOffset2 = GetProbeTexelOffset(probeIndex, imageSize(ddgi.packedProbeRawDepth).xy, ddgi.gridInfo[cascade].probeRadianceResolution);
    const vec2 uvOffset2 = vec2(texelOffset2) / imageSize(ddgi.packedProbeRawDepth).xy;
    const vec2 uv2 = ProbeDirectionToUv(-dirToProbeBiased, probeIndex, imageSize(ddgi.packedProbeRawDepth).xy, ddgi.gridInfo[cascade].probeRadianceResolution);
    const float rawDepth = textureLod(ddgi.packedProbeRawDepthTex, linearSampler, vec3(uvOffset2 + uv2, cascade), 0).x;

    if (distToProbeWS > rawDepth)
    {
      shadowWeight = 0;
    }
#endif

    const float validityWeight = min(1.0, ddgi.gridInfo[cascade].probes.data[probeIndex].validity / 100);
    float weightNoTrilinear = backfaceWeight * shadowWeight * validityWeight;
    float weightNoTrilinearNoShadow = backfaceWeight * validityWeight;
    
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

  outWeight = sumWeights;
  irradiance_internal = mix(irradiance_internalNoShadow, irradiance_internal, smoothstep(0, 1e-5, sumWeights));

  // DDGI uses this artificial energy-reducing term to mitigate the perception of light leaks in dark rooms.
  // The chosen constant is arbitrary and must be in [0, 1].
  const float ENERGY_PRESERVATION = 0.8;
  return ENERGY_PRESERVATION * irradiance_internal;
}

int SelectMinimumCascade(vec3 positionWS, DDGIArgs ddgi, out vec3 posProbeSpacePreMod)
{
  // Select lowest possible cascade to sample.
  for (int i = 0; i < DDGI_NUM_CASCADES; i++)
  {
    posProbeSpacePreMod = ((positionWS - 0.5) / ddgi.gridInfo[i].baseGridScale) - ddgi.gridInfo[i].gridOffset;
    posProbeSpacePreMod -= ddgi.gridInfo[i].gridOffsetFraction;
    if (all(greaterThanEqual(posProbeSpacePreMod, vec3(0))) && all(lessThan(posProbeSpacePreMod, ddgi.gridInfo[i].gridResolution - 1)))
    {
      return i;
    }
  }
  // A suitable cascade was not found.
  return -1;
}

vec3 SampleIlluminanceField(vec3 positionWS, vec3 normalWS, Sampler linearSampler, DDGIArgs ddgi)
{
  vec3 posProbeSpacePreMod;
  const int cascade = SelectMinimumCascade(positionWS, ddgi, posProbeSpacePreMod);
  if (cascade == -1)
  {
    return vec3(0, 0, 0); // No available cascade
  }
  const vec3 posProbeSpace = mod(posProbeSpacePreMod, ddgi.gridInfo[cascade].gridResolution);

  const float MAGIC_IDK_WHY = 0.9999; // Chosen after visualizing with TurboColorMap. The exact value 1 does not work!
  const vec3 halfGridExtent = (ddgi.gridInfo[cascade].gridResolution - MAGIC_IDK_WHY) / 2;
  const vec3 centeredPos = posProbeSpace - halfGridExtent;
  const float distFromEdgeA = sd_Box(centeredPos, halfGridExtent);
  // Decrease the distance if negative. This gives some room for gridOffsetFraction to move the space around.
  const float distFromEdge = clamp(distFromEdgeA < 0 ? -distFromEdgeA - 1 : distFromEdgeA, 0, 1);

  float sumWeight;
  const vec3 lowerCascadeIlluminance = SampleIlluminanceFieldRaw(positionWS, normalWS, linearSampler, ddgi, cascade, sumWeight);
  if (distFromEdge > 1 || cascade + 1 >= DDGI_NUM_CASCADES)
  {
    return lowerCascadeIlluminance;
  }

  float sumWeight2;
  const vec3 upperCascadeIlluminance = SampleIlluminanceFieldRaw(positionWS, normalWS, linearSampler, ddgi, cascade + 1, sumWeight2);

  // These checks mitigate black spots that are caused by one of the two selected cascades being 
  // totally invalid (all eight probes in the cage are inside opaque blocks). However, they do 
  // not fix situations in which both cascades are invalid. These checks also add a visible discontinuity.
  if (sumWeight < 1e-4)
  {
    return upperCascadeIlluminance;
  }
  if (sumWeight2 < 1e-4)
  {
    return lowerCascadeIlluminance;
  }
  
  return mix(lowerCascadeIlluminance, upperCascadeIlluminance, 1 - distFromEdge);
}

#endif // !__cplusplus

#endif // COMMON_SHARED_H
