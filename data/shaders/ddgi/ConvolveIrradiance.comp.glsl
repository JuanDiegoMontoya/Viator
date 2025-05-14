#include "ProbeCommon.shared.h"

layout(local_size_x = 128, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);

  const int numProbes = args.gridInfo.gridResolution.x * args.gridInfo.gridResolution.y * args.gridInfo.gridResolution.z;
  const int numTexels = args.gridInfo.probeRadianceResolution.x * args.gridInfo.probeRadianceResolution.y;
  const int probeIndex = gid / numTexels;
  const int texelIndex = gid % numTexels;

  if (probeIndex >= numProbes)
  {
    return;
  }

  vx_Init(args.voxels);

  const ivec3 probeCoord = ivec3(
    probeIndex % args.gridInfo.gridResolution.x,
    (probeIndex / args.gridInfo.gridResolution.x) % args.gridInfo.gridResolution.y,
    probeIndex / (args.gridInfo.gridResolution.x * args.gridInfo.gridResolution.y)
  );

  const ivec2 texelCoord = ivec2(
    texelIndex % args.gridInfo.probeRadianceResolution.x,
    texelIndex / args.gridInfo.probeRadianceResolution.x
  );

  // 1D probe index -> 2D texel offset (corner of probe in the atlas).
  // TODO: Account for 1-texel border.
  const ivec2 probeGridSize2d = imageSize(args.packedProbeRadiance) / args.gridInfo.probeRadianceResolution;
  const ivec2 texelOffset = args.gridInfo.probeRadianceResolution * ivec2(
    probeIndex % probeGridSize2d.x,
    probeIndex / probeGridSize2d.x
  );

  const vec2 uv = (texelCoord + 0.5) / args.gridInfo.probeRadianceResolution;

  const vec3 rayDir = OctToVec3(uv * 2 - 1);
  
  vec3 irradiance = vec3(0);
      
  uint rng = PCG_Hash(gid);

  // Sample probe
  vec3 tempAccum = vec3(0);
  const int SHRIMPLES = 8;
  for (int i = 0; i < SHRIMPLES; i++)
  {
    // TEMP: sample hemisphere of radiance probe
    const vec2 xi = vec2(PCG_RandFloat(rng), PCG_RandFloat(rng));
    const vec3 sampleDir = normalize(map_to_unit_hemisphere_cosine_weighted(xi, rayDir));
    const float cosTheta = clamp(dot(sampleDir, rayDir), 0, 1);

    if (cosTheta <= 0)
    {
      continue;
    }
    const float pdf = cosine_weighted_hemisphere_PDF(cosTheta);

    const vec2 uvOffset = vec2(texelOffset) / imageSize(args.packedProbeRadiance);
    const vec2 uv = (Vec3ToOct(normalize(sampleDir)) * .5 + .5) / (imageSize(args.packedProbeRadiance) / args.gridInfo.probeRadianceResolution);
    const ivec2 texel = ivec2(uv * args.gridInfo.probeRadianceResolution);

    tempAccum += textureLod(args.packedProbeRadianceTex, args.linearSampler, uvOffset + uv, 0).rgb * cosTheta / pdf;
  }
  irradiance += tempAccum / SHRIMPLES;

  imageStore(args.packedProbeIrradiance, texelOffset + texelCoord, vec4(irradiance, 0));
}
