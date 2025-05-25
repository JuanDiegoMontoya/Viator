#include "ProbeCommon.shared.h"

layout(local_size_x = 128, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridInfo[cascade].gridResolution.x * args.gridInfo[cascade].gridResolution.y * args.gridInfo[cascade].gridResolution.z;
  const int numTexels = args.gridInfo[cascade].probeIrradianceResolution.x * args.gridInfo[cascade].probeIrradianceResolution.y;
  const int probeIndex = gid / numTexels;

  if (probeIndex >= numProbes)
  {
    return;
  }

  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.gridInfo[cascade].probeIrradianceResolution);
  const vec3 rayDir = ProbeTexelCoordToDirection(texelCoord, args.gridInfo[cascade].probeIrradianceResolution);

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

    const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeRadiance).xy, args.gridInfo[cascade].probeRadianceResolution);
    const vec2 uvOffset = vec2(texelOffset) / imageSize(args.packedProbeRadiance).xy;
    const vec2 uv = ProbeDirectionToUv(sampleDir, probeIndex, imageSize(args.packedProbeRadiance).xy, args.gridInfo[cascade].probeRadianceResolution);

    tempAccum += textureLod(args.packedProbeRadianceTex, args.linearSampler, vec3(uvOffset + uv, cascade), 0).rgb * cosTheta / pdf / M_PI;
  }
  irradiance += tempAccum / SHRIMPLES;

  const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeIrradiance).xy, args.gridInfo[cascade].probeIrradianceResolution);
  const vec3 oldIrradiance = imageLoad(args.packedProbeIrradiance, ivec3(texelOffset + texelCoord, cascade)).rgb;
  const vec3 newIrradiance = mix(oldIrradiance, irradiance, 0.03);
  WriteToProbeWithBorder(args.packedProbeIrradiance, cascade, probeIndex, args.gridInfo[cascade].probeIrradianceResolution, texelCoord, vec4(newIrradiance, 0));
}
