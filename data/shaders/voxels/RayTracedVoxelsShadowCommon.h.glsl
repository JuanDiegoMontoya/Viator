#ifndef RAY_TRACED_VOXELS_SHADOW_COMMON_H
#define RAY_TRACED_VOXELS_SHADOW_COMMON_H

#include "../Resources.h.glsl"

#define RTSM_MAX_CASCADES 8

struct CascadeInfo
{
  FVOG_MAT4 clip_from_world;
  FVOG_MAT4 world_from_clip;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(CascadedShadowMapInfoPtr)
{
  CascadeInfo cascades[RTSM_MAX_CASCADES];
  FVOG_SHARED Texture2DArray shadowMapArray;
  FVOG_UINT32 numCascades;
  FVOG_FLOAT frustumDepth;
};

#ifndef __cplusplus

bool SampleCascadedShadowMapDepth(out int cascade, out float depth, out vec2 uv, vec3 positionWS, CascadedShadowMapInfoPtr shadowMap)
{
  for (cascade = 0; cascade < shadowMap.numCascades; cascade++)
  {
    const vec4 myShadowClip = (shadowMap.cascades[cascade].clip_from_world * vec4(positionWS, 1.0));
    const vec3 myShadowNdc = myShadowClip.xyz / myShadowClip.w;
    const vec2 myShadowUv = myShadowNdc.xy * 0.5 + 0.5;
    if (all(greaterThanEqual(myShadowUv, vec2(0))) && all(lessThan(myShadowUv, vec2(1))))
    {
      const ivec2 shadowSize = textureSize(shadowMap.shadowMapArray, 0).xy;
      const ivec2 myShadowPixel = clamp(ivec2(myShadowUv * shadowSize), ivec2(0), shadowSize - 1);
      depth = texelFetch(shadowMap.shadowMapArray, ivec3(myShadowPixel, cascade), 0).x;
      uv = myShadowUv;
      return true;
    }
  }

  return false;
}

float SampleCascadedShadowMap(vec3 positionWS, CascadedShadowMapInfoPtr shadowMap)
{
  float sunVisibility = 0;

  for (int cascade = 0; cascade < shadowMap.numCascades; cascade++)
  {
    const vec4 myShadowClip = (shadowMap.cascades[cascade].clip_from_world * vec4(positionWS, 1.0));
    const vec3 myShadowNdc = myShadowClip.xyz / myShadowClip.w;
    const vec2 myShadowUv = myShadowNdc.xy * 0.5 + 0.5;
    if (all(greaterThanEqual(myShadowUv, vec2(0))) && all(lessThan(myShadowUv, vec2(1))))
    {
      const ivec2 shadowSize = textureSize(shadowMap.shadowMapArray, 0).xy;
      const ivec2 myShadowPixel = clamp(ivec2(myShadowUv * shadowSize), ivec2(0), shadowSize - 1);
      const float texShadowDepth = texelFetch(shadowMap.shadowMapArray, ivec3(myShadowPixel, cascade), 0).x;
      sunVisibility = float(myShadowNdc.z < texShadowDepth + 1e-4);
      break;
    }
  }

  return sunVisibility;
}
#endif

#endif // RAY_TRACED_VOXELS_SHADOW_COMMON_H