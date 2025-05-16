#include "ProbeCommon.shared.h"

layout(local_size_x = 128, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);

  const int numProbes = args.gridInfo.gridResolution.x * args.gridInfo.gridResolution.y * args.gridInfo.gridResolution.z;
  const int numTexels = args.gridInfo.probeIrradianceResolution.x * args.gridInfo.probeIrradianceResolution.y;
  const int probeIndex = gid / numTexels;

  if (probeIndex >= numProbes)
  {
    return;
  }

  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.gridInfo.probeIrradianceResolution);
  const vec3 rayDir = ProbeTexelCoordToDirection(texelCoord, args.gridInfo.probeIrradianceResolution);

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

    const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeRadiance), args.gridInfo.probeRadianceResolution);
    const vec2 uvOffset = vec2(texelOffset) / imageSize(args.packedProbeRadiance);
    const vec2 uv = ProbeDirectionToUv(sampleDir, probeIndex, imageSize(args.packedProbeRadiance), args.gridInfo.probeRadianceResolution);

    tempAccum += textureLod(args.packedProbeRadianceTex, args.linearSampler, uvOffset + uv, 0).rgb * cosTheta / pdf;
  }
  irradiance += tempAccum / SHRIMPLES;

  WriteToProbeWithBorder(args.packedProbeIrradiance, probeIndex, args.gridInfo.probeIrradianceResolution, texelCoord, vec4(irradiance, 0));
}
