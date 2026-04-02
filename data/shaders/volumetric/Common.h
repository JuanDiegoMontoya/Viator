#ifndef COMMON_H
#define COMMON_H

#include "../Resources.h.glsl"
#define DDGI_NO_PUSH_CONSTANTS
#include "../ddgi/ProbeCommon.shared.h"
#include "../voxels/Voxels.h.glsl"
#include "../GlobalUniforms.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(Vol_FogEmitter)
{
  FVOG_VEC3 position;
  FVOG_FLOAT radiusInner;
  FVOG_FLOAT radiusOuter;
  FVOG_FLOAT density;
  FVOG_VEC3 color;
  FVOG_UVEC3 padding;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(FogList)
{
  FVOG_INT32 count;
  FVOG_UINT32 padding;
  Vol_FogEmitter emitters;
};

struct VolumetricUniforms
{
  FVOG_VEC3 viewPos;
  FVOG_FLOAT time;
  FVOG_MAT4 invViewProjScene;
  FVOG_MAT4 viewProjVolume;
  FVOG_MAT4 invViewProjVolume;
  FVOG_MAT4 sunViewProj;
  FVOG_FLOAT volumeNearPlane;
  FVOG_FLOAT volumeFarPlane;
  FVOG_UINT32 useScatteringTexture;
  FVOG_FLOAT anisotropyG;
  FVOG_FLOAT noiseOffsetScale;
  FVOG_UINT32 frog;
  FVOG_FLOAT groundFogDensity;

  FVOG_SHARED Texture2D inSceneLuminance;
  FVOG_SHARED Texture2D gDepth;
  FVOG_SHARED Texture3D inScatteringAndTransmittanceVolume;
  FVOG_SHARED Texture3D fogDensityVolume;
  FVOG_SHARED Texture2D blueNoise;
  FVOG_SHARED Image3D inScatteringAndTransmittanceVolumeRW;
  FVOG_SHARED Image3D fogDensityVolumeRW;
  FVOG_SHARED Image2D outSceneLuminance;
  FVOG_SHARED Sampler linearSampler;
  FVOG_SHARED Texture1D mieScattering;
  FVOG_SHARED Texture2D globalSurfaceHeight;
  FVOG_SHARED Texture2D globalSurfaceFog;
  FVOG_SHARED Texture3D globalFog;
#ifndef __cplusplus
  DDGIArgs ddgi;
#else
  VkDeviceAddress ddgi;
#endif
  Voxels voxels;
  FVOG_UINT32 globalUniformsIndex;
  FVOG_INT32 sunSelfShadowSteps;
  FVOG_FLOAT sunSelfShadowDist;
  FogList fogList;
};

#ifndef __cplusplus

FVOG_DECLARE_STORAGE_BUFFERS_2(VolumetricUniformsBuffers)
{
  VolumetricUniforms uniforms;
}buffers[];

#ifndef VOLUMETRIC_NO_PUSH_CONSTANTS
FVOG_DECLARE_ARGUMENTS(PushConstants)
{
  FVOG_UINT32 uniformBufferIdx;
}pc;
#endif // !VOLUMETRIC_NO_PUSH_CONSTANTS

#define uniforms buffers[pc.uniformBufferIdx].uniforms
#define globalUniforms perFrameUniformsBuffers[uniforms.globalUniformsIndex]

// unproject with zero-origin convention [0, 1]
vec3 UnprojectUVZO(float depth, vec2 uv, mat4 invXProj)
{
  vec4 clipSpacePosition = vec4(uv * 2.0 - 1.0, depth, 1.0); // [0, 1] -> [-1, 1]

  vec4 worldSpacePosition = invXProj * clipSpacePosition;
  worldSpacePosition /= worldSpacePosition.w;

  return worldSpacePosition.xyz;
}

// unproject with GL convention [-1, 1]
vec3 UnprojectUVGL(float depth, vec2 uv, mat4 invXProj)
{
  depth = depth * 2.0 - 1.0; // [0, 1] -> [-1, 1]
  return UnprojectUVZO(depth, uv, invXProj);
}

#define M_PI 3.1415926

// Henyey-Greenstein phase function for anisotropic in-scattering
float phaseHG(float g, float cosTheta)
{
  return (1.0 - g * g) / (4.0 * M_PI * pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5));
}

// Schlick's efficient approximation of HG
float phaseSchlick(float k, float cosTheta)
{
  float denom = 1.0 - k * cosTheta;
  return (1.0 - k * k) / (4.0 * M_PI * denom * denom);
}

// Conversion of HG's G parameter to Schlick's K parameter
float gToK(float g)
{
  return clamp(1.55 * g - 0.55 * g * g * g, -0.999, 0.999);
}

// Beer-Lambert law
float beer(float d)
{
  return exp(-d);
}

// Powder scattering effect for large volumes (darkens edges, used with beer)
float powder(float d)
{
  return 1.0 - exp(-d * 2.0);
}

#endif // !__cplusplus
#endif