#include "Common.shared.h"

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
  
  vec3 radiance = {0, 0, 0};

  uint randState = PCG_Hash(gid);

  const vec3 rayPos = probeCoord * args.gridInfo.baseGridScale + 0.5;
  HitSurfaceParameters hit;
  if (vx_TraceRayMultiLevel(rayPos, rayDir, 8, hit))
  {
    // Pixel is black if hit is extremely close (i.e. probe is inside geometry).
    if (distance2(rayPos, hit.positionWorld) > 1e-3)
    {
      const vec3 albedo = GetHitAlbedo(hit);
      radiance += GetHitEmission(hit);
      
      const int samples = 8;//args.samples;
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
    //gl_FragDepth = FAR_DEPTH;
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

  imageStore(args.packedProbeRadiance, texelOffset + texelCoord, vec4(radiance, 0));
}
