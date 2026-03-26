#include "BeerShadowMap.h.glsl"
//#include "RayMarchedClouds.shared.h"
#include "../../GlobalUniforms.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(RenderBeerShadowMapGpuParams)
{
  CascadedBeerShadowMapInfoPtr cbsm;
  GlobalUniformsPtr globalUniforms;
  FVOG_UINT32 numRayMarchSteps;
  FVOG_FLOAT time;
  FVOG_FLOAT jitterScale;
};

FVOG_DECLARE_ARGUMENTS(RenderBeerShadowMapPushConstants)
{
  RenderBeerShadowMapGpuParams pc;
};

#ifndef __cplusplus

#include "CloudDensity.h.glsl"
#include "../../Hash.h.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 outResolution = textureSize(pc.cbsm.shadowMapArray, 0).xy;
  const uint cascadeIndex = gl_GlobalInvocationID.z;

  if (any(greaterThanEqual(gid, outResolution)))
  {
    return;
  }

  const vec2 uv = (gid + 0.5) / outResolution;

  const vec3 rayPos = UnprojectUV_ZO(0.0, uv, pc.cbsm.cascades[cascadeIndex].world_from_clip);
  const vec3 rayEnd = UnprojectUV_ZO(1.0, uv, pc.cbsm.cascades[cascadeIndex].world_from_clip);
  const vec3 rayDir = normalize(rayEnd - rayPos);
  const float maxT  = pc.cbsm.frustumDepth;

  float startT         = 0;
  float endT           = 0;
  float meanExtinction = 0;
  uint numStepsInCloud = 0;
  float prevT          = 0;

  uint seed               = pc.globalUniforms.frameNumber;
  const ivec2 frameOffset = ivec2(PCG_RandU32(seed), PCG_RandU32(seed));
  const ivec2 noiseSize   = textureSize(pc.globalUniforms.blueNoise, 0);
  const ivec2 noiseCoord  = (gid + frameOffset) % noiseSize;
  const float jitter      = 0.5 + pc.jitterScale * (texelFetch(pc.globalUniforms.blueNoise, noiseCoord, 0).x - 0.5);

  for (uint i = 0; i < pc.numRayMarchSteps; i++)
  {
    const float t = (i + jitter) * maxT / pc.numRayMarchSteps;
    const vec3 curPos = rayPos + rayDir * t;
    // Without this factor, the darkening is too subtle, maybe due to a bug. Either way, this "fixes" it.
    const float HACK_MAGIC_FACTOR = 25;
    const float density = HACK_MAGIC_FACTOR * CloudDensityAtPoint(curPos, pc.time);

    // What we consider to be "in" a cloud.
    const float extinctionThreshold = 1e-4;
    if (density >= extinctionThreshold)
    {
      numStepsInCloud++;
      if (startT == 0)
      {
        startT = prevT;
      }
      endT = t;
      meanExtinction = mix(meanExtinction, density, 1.0 / numStepsInCloud);
    }

    prevT = t;
  }

  vec3 beerParams = vec3(0);
  if (startT != 0 && startT != endT)
  {
    beerParams = vec3(startT, endT, meanExtinction);
  }

  imageStore(pc.cbsm.shadowMapImages, ivec3(gl_GlobalInvocationID.xyz), vec4(beerParams, 0));
}

#endif // !__cplusplus