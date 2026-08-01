#include "ProbeCommon.shared.h"
#include "../sky/SkyUtil.h.glsl"
#include "../Color.h.glsl"

#define uniforms perFrameUniformsBuffers[args.globalUniformsIndex]

layout(local_size_x = DDGI_WORKGROUP_SIZE, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);

  const int numTexels       = args.probeRadianceResolution.x * args.probeRadianceResolution.y;
  const int probeIndexIndex = gid / numTexels;

  if (probeIndexIndex >= args.probesToUpdate.size)
  {
    return;
  }

  const uint encoded = args.probesToUpdate.values[probeIndexIndex].data;
  int cascade;
  int probeIndex;
  DecodeCascadeAndProbeIndex(encoded, cascade, probeIndex);

  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.probeRadianceResolution);
  
  const int stableProbeIndex = ProbeIndexToStableIndex(probeIndex, cascade, args);
  
  const ProbeData probeData = probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex];
  const float validity = probeData.validity;
  const float age = probeData.age;
  const float alpha = max(args.minTemporalAlpha, 1.0 / float(age));

  const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.probeRadianceResolution);

  // Calculate variance of luminance (for raw radiance) in small region.
  float m1 = 0;
  float m2 = 0;
  float N  = 0;
  for (int y = -1; y <= 1; y++)
  for (int x = -1; x <= 1; x++)
  {
    const float luminance = imageLoad(args.packedProbeFastRadianceLuminance, ivec3(texelOffset + texelCoord + ivec2(x, y), cascade)).x;
    m1 += luminance;
    m2 += square(luminance);
    N++;
  }
  const float mu     = m1 / N;
  const float sigma  = sqrt(abs(m2 / N - mu * mu));
  const float gamma  = args.varianceClipGamma; // Higher gamma = larger bounding box (stable result, but more ghosting/less responsiveness).
  const float minLum = mu - gamma * sigma;
  const float maxLum = mu + gamma * sigma;

  const vec3 oldRadiance_sRGB  = imageLoad(args.packedProbeRadiance, ivec3(texelOffset + texelCoord, cascade)).rgb;
  const float luminanceOld     = Luminance(oldRadiance_sRGB);
  const float clampedLuminance = clamp(luminanceOld, minLum, maxLum);
  vec3 oldRadiance = oldRadiance_sRGB;
  if (luminanceOld > 1e-4)
  {
    oldRadiance = oldRadiance / luminanceOld * clampedLuminance;
  }

  const vec3 radianceRaw = imageLoad(args.packedProbeRadianceRaw, ivec3(texelOffset + texelCoord, cascade)).rgb;
  const vec3 newRadiance = mix(oldRadiance, radianceRaw, alpha);
  WriteToProbeWithBorder(args.packedProbeRadiance, cascade, stableProbeIndex, args.probeRadianceResolution, texelCoord, vec4(newRadiance, 0));
}
