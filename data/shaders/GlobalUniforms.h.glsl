#ifndef GLOBAL_UNIFORMS_H
#define GLOBAL_UNIFORMS_H

#include "Resources.h.glsl"
#include "Debug/DebugCommon.h.glsl"
#include "voxels/RayTracedVoxelsShadowCommon.h.glsl"
#include "volumetric/clouds/BeerShadowMap.h.glsl"
#include "volumetric/clouds/WeatherGpuParams.shared.h"
#include "sky/SkyParams.shared.h"

FVOG_DECLARE_BUFFER_REFERENCE_2(GBuffer)
{
  FVOG_SHARED Texture2D gAlbedo;
  FVOG_SHARED Texture2D gDepth;
  FVOG_SHARED Texture2D gNormal;
  FVOG_SHARED Texture2D gRadiance;
  FVOG_SHARED Texture2D gIndirectIlluminance;
  FVOG_SHARED UImage2D gSpecial;
  FVOG_SHARED Image2D gTransmission;
  FVOG_SHARED Image2D gAlbedoTranslucent;
  FVOG_SHARED Image2D gNormalTranslucent;
  FVOG_SHARED Image2D gDepthTranslucent;
};

#define GLOBAL_UNIFORMS_FIELDS                \
  FVOG_MAT4 viewProj;                         \
  FVOG_MAT4 oldViewProj;                      \
  FVOG_MAT4 oldViewProjUnjittered;            \
  FVOG_MAT4 viewProjUnjittered;               \
  FVOG_MAT4 invViewProj;                      \
  FVOG_MAT4 proj;                             \
  FVOG_MAT4 invProj;                          \
  FVOG_MAT4 view;                             \
  FVOG_MAT4 oldView;                          \
  FVOG_MAT4 invView;                          \
  FVOG_VEC4 cameraPos;                        \
  FVOG_UINT32 meshletCount;                   \
  FVOG_UINT32 maxIndices;                     \
  FVOG_FLOAT bindlessSamplerLodBias;          \
  FVOG_UINT32 flags;                          \
  FVOG_FLOAT alphaHashScale;                  \
  FVOG_UINT32 frameNumber;                    \
  SkyData sky;                                \
  FVOG_SHARED Sampler linearSampler;          \
  GBuffer gBuffer;                            \
  DebugDrawData debugDraw;                    \
  FVOG_SHARED Texture2D blueNoise;            \
  CascadedShadowMapInfoPtr sunShadowMap;      \
  CascadedBeerShadowMapInfoPtr beerShadowMap; \
  WeatherGpuParams weatherParams;             \
  uint64_t voxelsPtr;                         \
  FVOG_FLOAT time;                            \
  FVOG_FLOAT dt

FVOG_DECLARE_BUFFER_REFERENCE_2(GlobalUniformsPtr)
{
  GLOBAL_UNIFORMS_FIELDS;
};

#ifdef __cplusplus
struct GlobalUniforms
#else
FVOG_DECLARE_STORAGE_BUFFERS_2(restrict PerFrameUniformsBuffer)
#endif
{
  GLOBAL_UNIFORMS_FIELDS;
}
#ifndef __cplusplus
perFrameUniformsBuffers[]
#endif
;

#endif // GLOBAL_UNIFORMS_H