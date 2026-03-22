#ifndef RAY_MARCHED_CLOUDS_H
#define RAY_MARCHED_CLOUDS_H

#include "../../Resources.h.glsl"
#define DDGI_NO_PUSH_CONSTANTS
#include "../../ddgi/ProbeCommon.shared.h"

FVOG_DECLARE_BUFFER_REFERENCE_2(RayMarchedCloudsRenderGpuParams)
{
  FVOG_SHARED Texture2D gDepth;
  FVOG_SHARED Image2D outMotionVectors;
  FVOG_SHARED Image2D outRadianceTransmittance;
  FVOG_SHARED Sampler nearestSampler;

  FVOG_MAT4 world_from_clip;
  FVOG_MAT4 clip_from_world;
  FVOG_MAT4 clip_from_world_old;
  FVOG_MAT4 clip_from_world_unjittered;
  FVOG_MAT4 clip_from_world_old_unjittered;
  FVOG_UINT32 numRayMarchSteps;
  FVOG_VEC2 jitterUV;

  FVOG_VEC3 sunDirection;
  FVOG_VEC3 sunIntensity;

  FVOG_UINT32 globalUniformsIndex;
  
#ifndef __cplusplus
  DDGIArgs ddgi;
#else
  VkDeviceAddress ddgi;
#endif

  FVOG_FLOAT zNear;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(UpscaleCloudsGpuParams)
{
  FVOG_SHARED Texture2D inLowResCloudRadianceTransmittance;
  FVOG_SHARED Texture2D inLowResCloudMotionVectors;
  FVOG_SHARED Texture2D inOldCloudRadianceTransmittance;
  FVOG_SHARED Texture2D inHighResDepth;
  FVOG_SHARED Texture2D inHighResDepthPrev;
  FVOG_SHARED Image2D outCloudRadianceTransmittance;
  FVOG_SHARED Sampler linearSampler;
  FVOG_VEC2 jitterUV;
  FVOG_FLOAT zNear;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(RayMarchedCloudsCompositeGpuParams)
{
  FVOG_SHARED Texture2D inOpaqueRadiance;
  FVOG_SHARED Texture2D inCloudRadianceTransmittance;
  FVOG_SHARED Image2D outRadiance;
};

#endif // RAY_MARCHED_CLOUDS_H