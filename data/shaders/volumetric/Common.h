#ifndef COMMON_H
#define COMMON_H

#include "../Resources.h.glsl"
#define DDGI_NO_PUSH_CONSTANTS
#include "../ddgi/ProbeCommon.shared.h"
#include "../voxels/Voxels.h.glsl"
#include "../GlobalUniforms.h.glsl"

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
#ifndef __cplusplus
  DDGIArgs ddgi;
#else
  VkDeviceAddress ddgi;
#endif
  Voxels voxels;
  FVOG_UINT32 globalUniformsIndex;
  FVOG_INT32 sunSelfShadowSteps;
  FVOG_FLOAT sunSelfShadowDist;
};

#ifndef __cplusplus

FVOG_DECLARE_STORAGE_BUFFERS_2(VolumetricUniformsBuffers)
{
  VolumetricUniforms uniforms;
}buffers[];

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
  FVOG_UINT32 uniformBufferIdx;
}pc;

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

float LinearizeDepthZO(float nonlinearZ, float zn, float zf)
{
  return zn / (zf + nonlinearZ * (zn - zf));
}

// the inverse of LinearizeDepthZO
float InvertDepthZO(float linearZ, float zn, float zf)
{
  return (zn - zf * linearZ) / (linearZ * (zn - zf));
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