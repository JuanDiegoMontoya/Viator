#include "ProbeCommon.shared.h"

layout(local_size_x = 128, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridInfo[cascade].gridResolution.x * args.gridInfo[cascade].gridResolution.y * args.gridInfo[cascade].gridResolution.z;
  const int numTexels = args.gridInfo[cascade].probeIrradianceResolution.x * args.gridInfo[cascade].probeIrradianceResolution.y;
  const int probeIndex = gid / numTexels;
  const int stableProbeIndex = ProbeIndexToStableIndex(probeIndex, args.gridInfo[cascade]);

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
    const float brdf = 1.0 / M_PI; // Lambert

    const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.gridInfo[cascade].probeRadianceResolution);
    const vec2 uvOffset = vec2(texelOffset) / imageSize(args.packedProbeRadiance).xy;
    const vec2 uv = ProbeDirectionToUv(sampleDir, stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.gridInfo[cascade].probeRadianceResolution);

    tempAccum += textureLod(args.packedProbeRadianceTex, args.linearSampler, vec3(uvOffset + uv, cascade), 0).rgb * cosTheta / pdf * brdf;
  }
  irradiance += tempAccum / SHRIMPLES;

  const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, imageSize(args.packedProbeIrradiance).xy, args.gridInfo[cascade].probeIrradianceResolution);
  const vec3 oldIrradiance = imageLoad(args.packedProbeIrradiance, ivec3(texelOffset + texelCoord, cascade)).rgb;
  const float validity = probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].validity;
  const float alpha = max(0.05, 1.0 / validity);
  const vec3 newIrradiance = mix(oldIrradiance, irradiance, alpha);
  WriteToProbeWithBorder(args.packedProbeIrradiance, cascade, stableProbeIndex, args.gridInfo[cascade].probeIrradianceResolution, texelCoord, vec4(newIrradiance, 0));

  // Compute the average luminance of this probe (used for fog).
  const int AVG_SAMPLES = 32;
  vec3 averageLuminance = vec3(0);
  for (int i = 0; i < AVG_SAMPLES; i++)
  {
    const vec2 xi = Hammersley(i, AVG_SAMPLES);
    const vec3 direction = map_to_unit_sphere(xi);

    const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.gridInfo[cascade].probeRadianceResolution);
    const vec2 uvOffset = vec2(texelOffset) / imageSize(args.packedProbeRadiance).xy;
    const vec2 uv = ProbeDirectionToUv(direction, stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.gridInfo[cascade].probeRadianceResolution);

    averageLuminance += textureLod(args.packedProbeRadianceTex, args.linearSampler, vec3(uvOffset + uv, cascade), 0).rgb;
  }
  const float pdf = uniform_sphere_PDF();
  averageLuminance = averageLuminance / AVG_SAMPLES;
  const vec3 oldAverageLuminance = probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].averageLuminance;
  const vec3 newAverageLuminance = mix(oldAverageLuminance, averageLuminance, alpha);
  probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].averageLuminance = newAverageLuminance;
}
