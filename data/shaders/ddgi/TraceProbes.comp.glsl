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

  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.gridInfo.probeRadianceResolution);

  const vec3 rayDir = ProbeTexelCoordToDirection(texelCoord, args.gridInfo.probeRadianceResolution);
  
  vec3 radiance = {0, 0, 0};
  float depth = 1234;

  uint randState = PCG_Hash(gid);

  const vec3 rayPos = ProbeIndexToCoord(probeIndex, args.gridInfo.gridResolution) * args.gridInfo.baseGridScale + 0.5;
  HitSurfaceParameters hit;
  if (vx_TraceRayMultiLevel(rayPos, rayDir, 8, hit))
  {
    // Pixel is black if hit is extremely close (i.e. probe is inside geometry).
    const float hitDist2 = distance2(rayPos, hit.positionWorld);
    if (hitDist2 > 1e-3)
    {
      depth = sqrt(hitDist2);
      const vec3 albedo = GetHitAlbedo(hit);
      radiance += GetHitEmission(hit);
      
      const int samples = 2;//args.samples;
      const int bounces = 2;//args.bounces;
      radiance += albedo * TraceIndirectLighting(texelCoord, hit.positionWorld, hit.flatNormalWorld, samples, bounces, args.noiseTexture);
      //const vec4 posClip = uniforms.viewProj * vec4(hit.positionWorld, 1.0);
      //gl_FragDepth = posClip.z / posClip.w;
      
      // Sun
      const vec3 sunDir = normalize(vec3(.7, 1, .3));
      const float NoL = max(0, dot(hit.flatNormalWorld, sunDir));
      vec3 sunlight_internal = albedo * NoL * TraceSunRay(hit.positionWorld + hit.flatNormalWorld * 1e-3, sunDir);

      radiance += sunlight_internal;

      // First-bounce lighting
      if (g_voxels.numLights > 0)
      {
        // Local light NEE
        const uint lightIndex = PCG_RandU32(randState) % g_voxels.numLights;
        const float lightPdf = 1.0 / g_voxels.numLights;
        GpuLight light = lightsBuffers[g_voxels.lightBufferIdx].lights[lightIndex];

        const float visibility = GetPunctualLightVisibility(hit.positionWorld + hit.flatNormalWorld * 1e-3, lightIndex);
        if (visibility > 0)
        {
          Surface surface;
          surface.albedo = albedo;
          surface.normal = hit.flatNormalWorld;
          surface.position = hit.positionWorld;
          radiance += visibility * EvaluatePunctualLightLambert(light, surface, COLOR_SPACE_sRGB_LINEAR) / lightPdf;
        }
      }
    }
  }
  else
  {
    radiance = rayDir * .5 + .5;
  }
  
  // Direct lighting
  if (g_voxels.numLights > 0)
  {
    // Local light NEE
    const uint lightIndex = PCG_RandU32(randState) % g_voxels.numLights;
    const float lightPdf = 1.0 / g_voxels.numLights;
    GpuLight light = lightsBuffers[g_voxels.lightBufferIdx].lights[lightIndex];

    const float visibility = GetPunctualLightVisibility(rayPos, lightIndex);
    if (visibility > 0)
    {
      Surface surface;
      surface.albedo = vec3(1); // Here, albedo is modulated when the actual surface applying this lighting is lit.
      surface.normal = rayDir;
      surface.position = rayPos;
      radiance += visibility * EvaluatePunctualLightLambert(light, surface, COLOR_SPACE_sRGB_LINEAR) / lightPdf;
    }
  }

  depth = min(depth, args.gridInfo.baseGridScale * M_SQRT_3);
  const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeRadiance), args.gridInfo.probeRadianceResolution);
  imageStore(args.packedProbeRadiance, texelOffset + texelCoord, vec4(radiance, 0));
  imageStore(args.packedProbeRawDepth, texelOffset + texelCoord, vec4(depth, 0, 0, 0));
  
  ///// For work texels on the edge of the probe, write to applicable border texels.
  // Sides
  if (texelCoord.x == 0)
  {
    const ivec2 borderCoord = {-1, args.gridInfo.probeRadianceResolution.y - 1 - texelCoord.y};
    imageStore(args.packedProbeRadiance, texelOffset + borderCoord, vec4(radiance, 0));
    imageStore(args.packedProbeRawDepth, texelOffset + borderCoord, vec4(depth, 0, 0, 0));
  }
  
  if (texelCoord.x == args.gridInfo.probeRadianceResolution.x - 1)
  {
    const ivec2 borderCoord = {args.gridInfo.probeRadianceResolution.x, args.gridInfo.probeRadianceResolution.y - 1 - texelCoord.y};
    imageStore(args.packedProbeRadiance, texelOffset + borderCoord, vec4(radiance, 0));
    imageStore(args.packedProbeRawDepth, texelOffset + borderCoord, vec4(depth, 0, 0, 0));
  }
  
  if (texelCoord.y == 0)
  {
    const ivec2 borderCoord = {args.gridInfo.probeRadianceResolution.x - 1 - texelCoord.x, -1};
    imageStore(args.packedProbeRadiance, texelOffset + borderCoord, vec4(radiance, 0));
    imageStore(args.packedProbeRawDepth, texelOffset + borderCoord, vec4(depth, 0, 0, 0));
  }
  
  if (texelCoord.y == args.gridInfo.probeRadianceResolution.y - 1)
  {
    const ivec2 borderCoord = {args.gridInfo.probeRadianceResolution.x - 1 - texelCoord.x, args.gridInfo.probeRadianceResolution.y};
    imageStore(args.packedProbeRadiance, texelOffset + borderCoord, vec4(radiance, 0));
    imageStore(args.packedProbeRawDepth, texelOffset + borderCoord, vec4(depth, 0, 0, 0));
  }

  // Corners
  if (texelCoord == ivec2(0, 0))
  {
    const ivec2 borderCoord = args.gridInfo.probeRadianceResolution;
    imageStore(args.packedProbeRadiance, texelOffset + borderCoord, vec4(radiance, 0));
    imageStore(args.packedProbeRawDepth, texelOffset + borderCoord, vec4(depth, 0, 0, 0));
  }
  
  if (texelCoord == args.gridInfo.probeRadianceResolution - 1)
  {
    const ivec2 borderCoord = {-1, -1};
    imageStore(args.packedProbeRadiance, texelOffset + borderCoord, vec4(radiance, 0));
    imageStore(args.packedProbeRawDepth, texelOffset + borderCoord, vec4(depth, 0, 0, 0));
  }
  
  if (texelCoord == ivec2(0, args.gridInfo.probeRadianceResolution.y - 1))
  {
    const ivec2 borderCoord = {args.gridInfo.probeRadianceResolution.x, -1};
    imageStore(args.packedProbeRadiance, texelOffset + borderCoord, vec4(radiance, 0));
    imageStore(args.packedProbeRawDepth, texelOffset + borderCoord, vec4(depth, 0, 0, 0));
  }

  if (texelCoord == ivec2(args.gridInfo.probeRadianceResolution.x - 1, 0))
  {
    const ivec2 borderCoord = {-1, args.gridInfo.probeRadianceResolution.y};
    imageStore(args.packedProbeRadiance, texelOffset + borderCoord, vec4(radiance, 0));
    imageStore(args.packedProbeRawDepth, texelOffset + borderCoord, vec4(depth, 0, 0, 0));
  }
}
