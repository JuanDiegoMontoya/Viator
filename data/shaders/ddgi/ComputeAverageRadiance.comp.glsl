#include "ProbeCommon.shared.h"

layout(local_size_x = DDGI_WORKGROUP_SIZE, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);

  const int probeIndexIndex = gid;

  if (probeIndexIndex >= args.probesToUpdate.size)
  {
    return;
  }

  const uint encoded = args.probesToUpdate.values[probeIndexIndex].data;
  int cascade;
  int probeIndex;
  DecodeCascadeAndProbeIndex(encoded, cascade, probeIndex);

  const int stableProbeIndex = ProbeIndexToStableIndex(probeIndex, cascade, args);

  if (probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].validity == 0)
  {
    probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].averageLuminance = vec3(0);
    return;
  }

  uint rng = PCG_Hash(gid);

  // Compute the average luminance of this probe (used for fog).
  const int AVG_SAMPLES = 64;
  vec3 averageLuminance = vec3(0);
  for (int i = 0; i < AVG_SAMPLES; i++)
  {
    const vec2 xi = Hammersley(i, AVG_SAMPLES);
    const vec3 direction = map_to_unit_sphere(xi);

    const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.probeRadianceResolution);
    const vec2 uvOffset = vec2(texelOffset) / imageSize(args.packedProbeRadiance).xy;
    const vec2 uv = ProbeDirectionToUv(direction, stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.probeRadianceResolution);

    averageLuminance += textureLod(args.packedProbeRadianceTex, args.linearSampler, vec3(uvOffset + uv, cascade), 0).rgb;
  }

  const float pdf = uniform_sphere_PDF();
  averageLuminance = averageLuminance / AVG_SAMPLES;
  
  const float age = float(probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].age);

  const float fastAlpha = max(0.2, 1.0 / age);
  const vec3 oldFastAvgLum = probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].fastAverageLuminance;
  const vec3 oldFastAvgLum2 = probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].fastAverageLuminance2;
  const vec3 newFastAvgLum = mix(oldFastAvgLum, averageLuminance, fastAlpha);
  const vec3 newFastAvgLum2 = mix(oldFastAvgLum2, averageLuminance * averageLuminance, fastAlpha);
  probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].fastAverageLuminance = newFastAvgLum;
  probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].fastAverageLuminance2 = newFastAvgLum2;
  
  const vec3 mu     = newFastAvgLum;
  const vec3 sigma  = sqrt(abs(newFastAvgLum2 - mu * mu));
  const float gamma  = 1;
  const vec3 minLum = mu - gamma * sigma;
  const vec3 maxLum = mu + gamma * sigma;

  vec3 oldAverageLuminance = probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].averageLuminance;
  oldAverageLuminance = clamp(oldAverageLuminance, minLum, maxLum);

  const float alpha = max(0.05, 1.0 / age);
  const vec3 newAverageLuminance = mix(oldAverageLuminance, averageLuminance, alpha);
  probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].averageLuminance = any(isnan(newAverageLuminance)) ? vec3(0) : newAverageLuminance;
}
