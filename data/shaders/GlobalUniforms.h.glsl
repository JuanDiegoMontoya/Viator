#ifndef GLOBAL_UNIFORMS_H
#define GLOBAL_UNIFORMS_H

#include "Resources.h.glsl"
#include "Debug/DebugCommon.h.glsl"
#include "voxels/RayTracedVoxelsShadowCommon.h.glsl"

#define CULL_MESHLET_FRUSTUM    (1 << 0)
#define CULL_MESHLET_HIZ        (1 << 1)
#define CULL_PRIMITIVE_BACKFACE (1 << 2)
#define CULL_PRIMITIVE_FRUSTUM  (1 << 3)
#define CULL_PRIMITIVE_SMALL    (1 << 4)
#define CULL_PRIMITIVE_VSM      (1 << 5)
#define USE_HASHED_TRANSPARENCY (1 << 6)

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

#define PROFILE_LAYER_COUNT 2
// An atmosphere layer density which can be calculated as:
//   density = exp_term * exp(exp_scale * h) + linear_term * h + constant_term,
struct DensityProfileLayer
{
  FVOG_FLOAT const_term;
  FVOG_FLOAT exp_scale;
  FVOG_FLOAT exp_term;
  FVOG_FLOAT layer_width;
  FVOG_FLOAT lin_term;
};

struct SkyParameters
{
  FVOG_VEC3 sunDir;
  FVOG_VEC3 sunColor;
  FVOG_FLOAT sunBrightness;

  FVOG_FLOAT atmosphere_bottom;
  FVOG_FLOAT atmosphere_top;
  
  FVOG_VEC3 mie_scattering;
  FVOG_VEC3 mie_extinction;
  FVOG_FLOAT mie_scale_height;
  FVOG_FLOAT mie_phase_function_g;
  DensityProfileLayer mie_density[PROFILE_LAYER_COUNT];
  
  FVOG_VEC3 rayleigh_scattering;
  FVOG_FLOAT rayleigh_scale_height;
  DensityProfileLayer rayleigh_density[PROFILE_LAYER_COUNT];
  
  FVOG_VEC3 absorption_extinction;
  DensityProfileLayer absorption_density[PROFILE_LAYER_COUNT];
};

#ifdef __cplusplus
inline SkyParameters InitSkyParameters()
{
  SkyParameters params;
  params.absorption_density[0] = { 0.0f, 0.2f, 0.00182376393f, 35.0f, 0.0f };
  params.absorption_density[1] = { 0.0f, -0.06666666666f, 20.6245170027f, 65.0f, 0.0f };
  params.absorption_extinction = { 0.00229072f,  0.00154036f,  0.0f};
  params.atmosphere_bottom = 6360.0f;
  params.atmosphere_top = 6460.0f;

  params.mie_density[0] = { 0.0f, -0.8333333134651184f, 1.0f, 100.0f, 0.0f};
  params.mie_density[1] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; 
  params.mie_extinction = { 0.00443999981507659f, 0.00443999981507659f, 0.00443999981507659f};
  params.mie_phase_function_g = 0.800000011920929f;
  params.mie_scale_height = 1.2000000476837158f;
  params.mie_scattering = { 0.003996000159531832f, 0.003996000159531832f, 0.003996000159531832f },

  params.rayleigh_density[0] = { 0.0f, -0.125f, 1.0f, 100.0f, 0.0f };
  params.rayleigh_density[1] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  params.rayleigh_scale_height = 8.696f;
  params.rayleigh_scattering = { 0.006604931f, 0.012344918f, 0.029412623f };
  
  return params;
}
struct GlobalUniforms
#else
FVOG_DECLARE_STORAGE_BUFFERS_2(restrict PerFrameUniformsBuffer)
#endif
{
  FVOG_MAT4 viewProj;
  FVOG_MAT4 oldViewProj;
  FVOG_MAT4 oldViewProjUnjittered;
  FVOG_MAT4 viewProjUnjittered;
  FVOG_MAT4 invViewProj;
  FVOG_MAT4 proj;
  FVOG_MAT4 invProj;
  FVOG_MAT4 view;
  FVOG_MAT4 invView;
  FVOG_VEC4 cameraPos;
  FVOG_UINT32 meshletCount;
  FVOG_UINT32 maxIndices;
  FVOG_FLOAT bindlessSamplerLodBias;
  FVOG_UINT32 flags;
  FVOG_FLOAT alphaHashScale;
  FVOG_UINT32 frameNumber;
  SkyParameters sky;
  FVOG_SHARED Texture2D skyViewLut;
  FVOG_SHARED Texture2D transmittanceLut;
  FVOG_SHARED Sampler linearSampler;
  GBuffer gBuffer;
  DebugDrawData debugDraw;
  FVOG_SHARED Texture2D blueNoise;
  CascadedShadowMapInfoPtr sunShadowMap;
  FVOG_FLOAT time; // Seconds
  FVOG_FLOAT dt;
}
#ifndef __cplusplus
perFrameUniformsBuffers[]
#endif
;

#endif // GLOBAL_UNIFORMS_H