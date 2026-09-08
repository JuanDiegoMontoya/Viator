#ifndef FOLIAGE_SSS_SHARED_H
#define FOLIAGE_SSS_SHARED_H

#include "../Resources.h.glsl"

#define FOLIAGE_SSS_BSM_MAX_CASCADES 8

struct FoliageCBSMCascadeInfo
{
  FVOG_MAT4 clip_from_world;
  FVOG_MAT4 world_from_clip;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(FoliageCBSMInfoPtr)
{
  FoliageCBSMCascadeInfo cascades[FOLIAGE_SSS_BSM_MAX_CASCADES];
  FVOG_SHARED Texture2DArray meanExtinctionArray;
  FVOG_SHARED Texture2DArray volumePositionArray;
  FVOG_SHARED Image2DArray meanExtinctionImages;
  FVOG_SHARED Image2DArray volumePositionImages; // translucentStart, translucentEnd, opaqueDepth
  FVOG_UINT32 numCascades;
  FVOG_FLOAT frustumDepth;
};

#ifndef __cplusplus
#include "../Math.h.glsl"

// Returns RGB transmittance.
vec3 SampleFoliageCBSM(vec3 positionWS, FoliageCBSMInfoPtr shadowMap)
{
  for (int cascade = 0; cascade < shadowMap.numCascades; cascade++)
  {
    const vec4 myShadowClip = (shadowMap.cascades[cascade].clip_from_world * vec4(positionWS, 1.0));
    const vec3 myShadowNdc = myShadowClip.xyz / myShadowClip.w;
    const vec2 myShadowUv = myShadowNdc.xy * 0.5 + 0.5;
    if (all(greaterThanEqual(myShadowUv, vec2(0))) && all(lessThan(myShadowUv, vec2(1))))
    {
      const vec3 volumePosition = textureLod(shadowMap.volumePositionArray, gNearestClampSampler, vec3(myShadowUv, cascade), 0).xyz;
      const vec3 meanExtinction = textureLod(shadowMap.meanExtinctionArray, gNearestClampSampler, vec3(myShadowUv, cascade), 0).rgb;
      const float startDepth    = volumePosition.x;
      const float endDepth      = volumePosition.y;
      const float opaqueDepth   = volumePosition.z;

      if (opaqueDepth < myShadowNdc.z * shadowMap.frustumDepth - 0.2)
      {
        return vec3(0);
      }

      //const float distFraction = clamp(Remap(myShadowNdc.z * shadowMap.frustumDepth, startDepth, endDepth, 0, 1), 0, 1);
      const float maxDepth = endDepth - startDepth;
      const float depth = clamp(myShadowNdc.z * shadowMap.frustumDepth - startDepth, 0, maxDepth);
      return exp(-depth * meanExtinction);
    }
  }

  return vec3(0);
}

#endif // !__cplusplus

#endif // FOLIAGE_SSS_SHARED_H