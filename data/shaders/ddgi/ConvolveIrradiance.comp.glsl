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

  // 1D probe index -> 2D texel offset (corner of probe in the atlas).
  const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeIrradiance), args.gridInfo.probeIrradianceResolution);
  imageStore(args.packedProbeIrradiance, texelOffset + texelCoord, vec4(irradiance, 0));

  ///// For work texels on the edge of the probe, write to applicable border texels.
  // Sides
  if (texelCoord.x == 0)
  {
    const ivec2 borderCoord = {-1, args.gridInfo.probeIrradianceResolution.y - 1 - texelCoord.y};
    imageStore(args.packedProbeIrradiance, texelOffset + borderCoord, vec4(irradiance, 0));
  }
  
  if (texelCoord.x == args.gridInfo.probeIrradianceResolution.x - 1)
  {
    const ivec2 borderCoord = {args.gridInfo.probeIrradianceResolution.x, args.gridInfo.probeIrradianceResolution.y - 1 - texelCoord.y};
    imageStore(args.packedProbeIrradiance, texelOffset + borderCoord, vec4(irradiance, 0));
  }
  
  if (texelCoord.y == 0)
  {
    const ivec2 borderCoord = {args.gridInfo.probeIrradianceResolution.x - 1 - texelCoord.x, -1};
    imageStore(args.packedProbeIrradiance, texelOffset + borderCoord, vec4(irradiance, 0));
  }
  
  if (texelCoord.y == args.gridInfo.probeIrradianceResolution.y - 1)
  {
    const ivec2 borderCoord = {args.gridInfo.probeIrradianceResolution.x - 1 - texelCoord.x, args.gridInfo.probeIrradianceResolution.y};
    imageStore(args.packedProbeIrradiance, texelOffset + borderCoord, vec4(irradiance, 0));
  }

  // Corners
  if (texelCoord == ivec2(0, 0))
  {
    const ivec2 borderCoord = args.gridInfo.probeIrradianceResolution;
    imageStore(args.packedProbeIrradiance, texelOffset + borderCoord, vec4(irradiance, 0));
  }
  
  if (texelCoord == args.gridInfo.probeIrradianceResolution - 1)
  {
    const ivec2 borderCoord = {-1, -1};
    imageStore(args.packedProbeIrradiance, texelOffset + borderCoord, vec4(irradiance, 0));
  }
  
  if (texelCoord == ivec2(0, args.gridInfo.probeIrradianceResolution.y - 1))
  {
    const ivec2 borderCoord = {args.gridInfo.probeIrradianceResolution.x, -1};
    imageStore(args.packedProbeIrradiance, texelOffset + borderCoord, vec4(irradiance, 0));
  }

  if (texelCoord == ivec2(args.gridInfo.probeIrradianceResolution.x - 1, 0))
  {
    const ivec2 borderCoord = {-1, args.gridInfo.probeIrradianceResolution.y};
    imageStore(args.packedProbeIrradiance, texelOffset + borderCoord, vec4(irradiance, 0));
  }
}
