#ifndef GLOBAL_UNIFORMS_H
#define GLOBAL_UNIFORMS_H

#include "Resources.h.glsl"

#define CULL_MESHLET_FRUSTUM    (1 << 0)
#define CULL_MESHLET_HIZ        (1 << 1)
#define CULL_PRIMITIVE_BACKFACE (1 << 2)
#define CULL_PRIMITIVE_FRUSTUM  (1 << 3)
#define CULL_PRIMITIVE_SMALL    (1 << 4)
#define CULL_PRIMITIVE_VSM      (1 << 5)
#define USE_HASHED_TRANSPARENCY (1 << 6)

struct SkyParameters
{
  FVOG_VEC3 sunDir;
  FVOG_VEC3 sunColor;
};

#ifndef __cplusplus

// Sample the sky in a particular direction.
vec3 SampleSky(SkyParameters sky, vec3 direction)
{
  return direction * 0.5 + 0.5;
}

#endif

#ifdef __cplusplus
struct GlobalUniforms
#else
FVOG_DECLARE_STORAGE_BUFFERS_2(restrict readonly PerFrameUniformsBuffer)
#endif
{
  FVOG_MAT4 viewProj;
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
}
#ifndef __cplusplus
perFrameUniformsBuffers[]
#endif
;

#endif // GLOBAL_UNIFORMS_H