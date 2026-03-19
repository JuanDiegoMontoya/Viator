#include "RayMarchedClouds.shared.h"

FVOG_DECLARE_ARGUMENTS(RayMarchedCloudsUpscalePushConstants)
{
  UpscaleCloudsGpuParams pc;
};

#ifndef __cplusplus

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 outResolution = imageSize(pc.outCloudRadianceTransmittance);
  const vec2 uv = (gid + 0.5) / outResolution;

  if (any(greaterThanEqual(gid, outResolution)))
  {
    return;
  }

  const vec2 uvForLowRes = uv + pc.jitterUV;
  const vec2 motionUV = textureLod(pc.inLowResCloudMotionVectors, pc.linearSampler, uv, 0).xy;
  const vec2 uvForPrev = uv + motionUV;

  const vec4 currentSample = textureLod(pc.inLowResCloudRadianceTransmittance, pc.linearSampler, uvForLowRes, 0);
  const vec4 historySample = textureLod(pc.inOldCloudRadianceTransmittance, pc.linearSampler, uvForPrev, 0);

  // Reject history when off-screen.
  const float historyWeight = 0.98 * float(all(greaterThanEqual(uvForPrev, vec2(0))) && all(lessThan(uvForPrev, vec2(1))));
  const vec4 blendedSample = mix(currentSample, historySample, historyWeight);

  imageStore(pc.outCloudRadianceTransmittance, gid, blendedSample);
}

#endif // !__cplusplus