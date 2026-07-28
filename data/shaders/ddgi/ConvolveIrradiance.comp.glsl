#include "ProbeCommon.shared.h"

layout(local_size_x = DDGI_WORKGROUP_SIZE, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridResolution.x * args.gridResolution.y * args.gridResolution.z;
  const int numTexels = args.probeIrradianceResolution.x * args.probeIrradianceResolution.y;
  const int probeIndex = gid / numTexels;
  const int stableProbeIndex = ProbeIndexToStableIndex(probeIndex, cascade, args);

  if (probeIndex >= numProbes)
  {
    return;
  }

  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.probeIrradianceResolution);
  const vec3 rayDir = ProbeTexelCoordToDirection(texelCoord, args.probeIrradianceResolution);

  if (probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].validity == 0)
  {
    WriteToProbeWithBorder(args.packedProbeIrradiance, cascade, stableProbeIndex, args.probeIrradianceResolution, texelCoord, vec4(0));
    return;
  }

  vec3 irradiance = vec3(0);

  uint rng = PCG_Hash(gid);

  // Sample probe
  vec3 tempAccum = vec3(0);
  const int SHRIMPLES = 64;
  for (int i = 0; i < SHRIMPLES; i++)
  {
    // TODO: Use hammersley sequence for more even distribution.
    const vec2 xi = vec2(PCG_RandFloat(rng), PCG_RandFloat(rng));
    const vec3 sampleDir = normalize(map_to_unit_hemisphere_cosine_weighted(xi, rayDir));
    const float cosTheta = clamp(dot(sampleDir, rayDir), 0, 1);

    if (cosTheta <= 0)
    {
      continue;
    }
    const float pdf = cosine_weighted_hemisphere_PDF(cosTheta);
    const float brdf = 1.0 / M_PI; // Lambert

    const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.probeRadianceResolution);
    const vec2 uvOffset = vec2(texelOffset) / imageSize(args.packedProbeRadiance).xy;
    const vec2 uv = ProbeDirectionToUv(sampleDir, stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.probeRadianceResolution);

    tempAccum += textureLod(args.packedProbeRadianceTex, args.linearSampler, vec3(uvOffset + uv, cascade), 0).rgb * cosTheta / pdf * brdf;
  }
  irradiance += tempAccum / SHRIMPLES;

  const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, imageSize(args.packedProbeIrradiance).xy, args.probeIrradianceResolution);

  const vec3 oldIrradiance = imageLoad(args.packedProbeIrradiance, ivec3(texelOffset + texelCoord, cascade)).rgb;
  const float age = float(probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].age);
  const float alpha = max(args.convolveTemporalAlpha, 1.0 / age);
  const vec3 newIrradiance = mix(oldIrradiance, irradiance, alpha);
  WriteToProbeWithBorder(args.packedProbeIrradiance, cascade, stableProbeIndex, args.probeIrradianceResolution, texelCoord, vec4(any(isnan(newIrradiance)) ? vec3(0) : newIrradiance, 0));
}
