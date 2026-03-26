#include "BeerShadowMap.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(BlurBeerShadowMapGpuParams)
{
  FVOG_SHARED Texture2DArray inBeerShadowMap;
  FVOG_SHARED Texture2DArray inBeerShadowMapHistory;
  FVOG_SHARED Image2DArray outBeerShadowMap;
  FVOG_BOOL32 doHorizontalPass;
  FVOG_FLOAT historyWeight;
};

FVOG_DECLARE_ARGUMENTS(BlurBeerShadowMapArgs)
{
  BlurBeerShadowMapGpuParams pc;
};

#ifndef __cplusplus

#define KERNEL_5x5
#include "../../denoising/spatial/Kernels.h.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
  const ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);
  const ivec3 outResolution = imageSize(pc.outBeerShadowMap);

  if (any(greaterThanEqual(gid, outResolution)))
  {
    return;
  }

  const vec2 invOutResolution = 1.0 / outResolution.xy;
  const vec2 uv = (gid.xy + 0.5) * invOutResolution;

  vec3 newBeerParams = vec3(0);

  for (int i = 0; i < kWidth; i++)
  {
    const vec2 direction     = vec2(bool(pc.doHorizontalPass), !bool(pc.doHorizontalPass));
    const vec2 offset        = vec2(float(i) - kRadius) * direction * invOutResolution;
    const vec2 sampleUv      = uv + offset;
    const float kernelWeight = kernel1D[i];
    newBeerParams += kernelWeight * textureLod(pc.inBeerShadowMap, gNearestMirrorSampler, vec3(sampleUv, gid.z), 0).xyz;
  }

  // Hillaire 2020 suggests not blurring X component (start depth), but this brings about strange artifacts.
  //newBeerParams.x = texelFetch(pc.inBeerShadowMap, gid, 0).x;

  if (pc.historyWeight > 0)
  {
    const vec3 oldBeerParams = texelFetch(pc.inBeerShadowMapHistory, gid, 0).xyz;
    imageStore(pc.outBeerShadowMap, gid, vec4(mix(newBeerParams, oldBeerParams, pc.historyWeight), 0));
  }
  else
  {
    imageStore(pc.outBeerShadowMap, gid, vec4(newBeerParams, 0));
  }
}

#endif // !__cplusplus