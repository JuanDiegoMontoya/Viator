#ifndef BEER_SHADOW_MAP_H
#define BEER_SHADOW_MAP_H

#include "../../Resources.h.glsl"
#include "../../voxels/RayTracedVoxelsShadowCommon.h.glsl"

#define BSM_MAX_CASCADES 8

struct BeerCascadeInfo
{
  FVOG_MAT4 clip_from_world;
  FVOG_MAT4 world_from_clip;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(CascadedBeerShadowMapInfoPtr)
{
  BeerCascadeInfo cascades[BSM_MAX_CASCADES];
  FVOG_SHARED Texture2DArray shadowMapArray;
  FVOG_SHARED Image2DArray shadowMapImages;
  FVOG_UINT32 numCascades;
  FVOG_FLOAT frustumDepth;
};

#ifndef __cplusplus
#include "../../Math.h.glsl"
vec3 SampleCascadedBeerShadowMapRaw(vec3 positionWS, CascadedBeerShadowMapInfoPtr shadowMap)
{
  for (int cascade = 0; cascade < shadowMap.numCascades; cascade++)
  {
    const vec4 myShadowClip = (shadowMap.cascades[cascade].clip_from_world * vec4(positionWS, 1.0));
    const vec3 myShadowNdc = myShadowClip.xyz / myShadowClip.w;
    const vec2 myShadowUv = myShadowNdc.xy * 0.5 + 0.5;
    if (all(greaterThanEqual(myShadowUv, vec2(0))) && all(lessThan(myShadowUv, vec2(1))))
    {
      const ivec2 shadowSize     = textureSize(shadowMap.shadowMapArray, 0).xy;
      const ivec2 myShadowPixel  = clamp(ivec2(myShadowUv * shadowSize), ivec2(0), shadowSize - 1);
      const vec3 beerParams      = texelFetch(shadowMap.shadowMapArray, ivec3(myShadowPixel, cascade), 0).xyz;
      const float startDepth     = beerParams.x;
      const float endDepth       = beerParams.y;
      const float meanExtinction = beerParams.z;

      return vec3(startDepth, endDepth, meanExtinction);
    }
  }

  return vec3(0);
}

// Returns transmittance.
float SampleCascadedBeerShadowMap(vec3 positionWS, CascadedBeerShadowMapInfoPtr shadowMap)
{
  for (int cascade = 0; cascade < shadowMap.numCascades; cascade++)
  {
    const vec4 myShadowClip = (shadowMap.cascades[cascade].clip_from_world * vec4(positionWS, 1.0));
    const vec3 myShadowNdc = myShadowClip.xyz / myShadowClip.w;
    const vec2 myShadowUv = myShadowNdc.xy * 0.5 + 0.5;
    if (all(greaterThanEqual(myShadowUv, vec2(0))) && all(lessThan(myShadowUv, vec2(1))))
    {
      const ivec2 shadowSize     = textureSize(shadowMap.shadowMapArray, 0).xy;
      const ivec2 myShadowPixel  = clamp(ivec2(myShadowUv * shadowSize), ivec2(0), shadowSize - 1);
      const vec3 beerParams      = textureLod(shadowMap.shadowMapArray, gLinearClampSampler, vec3(myShadowUv, cascade), 0).xyz;
      const float startDepth     = beerParams.x;
      const float endDepth       = beerParams.y;
      const float meanExtinction = beerParams.z;

      const float distFraction = clamp(Remap(myShadowNdc.z * shadowMap.frustumDepth, startDepth, endDepth, 0, 1), 0, 1);
      return exp(-distFraction * meanExtinction);
    }
  }

  return 0;
}
#endif // !__cplusplus

#endif // BEER_SHADOW_MAP_H