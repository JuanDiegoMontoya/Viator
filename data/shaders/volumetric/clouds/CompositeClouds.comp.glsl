#include "RayMarchedClouds.shared.h"

FVOG_DECLARE_ARGUMENTS(RayMarchedCloudsCompositePushConstants)
{
  RayMarchedCloudsCompositeGpuParams pc;
};

#ifndef __cplusplus

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 resolution = imageSize(pc.outRadiance);
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, resolution)))
  {
    return;
  }

  const vec3 opaqueRadiance = texelFetch(pc.inOpaqueRadiance, gid, 0).rgb;
  const vec4 cloudRadianceTransmittance = texelFetch(pc.inCloudRadianceTransmittance, gid, 0);
  const vec3 outRadiance = opaqueRadiance * cloudRadianceTransmittance.a + cloudRadianceTransmittance.rgb;
  imageStore(pc.outRadiance, gid, vec4(outRadiance, 0));
}
#endif // !__cplusplus