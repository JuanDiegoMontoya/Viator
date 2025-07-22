#include "ProbeCommon.shared.h"
#include "../sky/SkyUtil.h.glsl"

#define uniforms perFrameUniformsBuffers[args.globalUniformsIndex]

layout(local_size_x = 128, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridInfo[cascade].gridResolution.x * args.gridInfo[cascade].gridResolution.y * args.gridInfo[cascade].gridResolution.z;
  const int numTexels = args.gridInfo[cascade].probeRadianceResolution.x * args.gridInfo[cascade].probeRadianceResolution.y;
  const int probeIndex = gid / numTexels;
  const int texelIndex = gid % numTexels;

  if (probeIndex >= numProbes)
  {
    return;
  }

  vx_Init(args.voxels);

  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.gridInfo[cascade].probeRadianceResolution);

  vec3 rayDir = ProbeTexelCoordToDirection(texelCoord, args.gridInfo[cascade].probeRadianceResolution);
  rayDir = normalize(rayDir + vec3(1e-4, 0, 0)); // HACK: perfectly 45-degree ray directions are not liked by DDA.
  
  vec3 radiance = {0, 0, 0};
  float depth = 1234;

  const uint frameNumber = uniforms.frameNumber;
  uint randState = PCG_Hash(gid + frameNumber);
  
  const vec3 rayPos = (ProbeIndexToCoord(probeIndex, args.gridInfo[cascade].gridResolution) + args.gridInfo[cascade].gridOffset) * args.gridInfo[cascade].baseGridScale + 0.5;
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
      //radiance += albedo * TraceIndirectLighting(texelCoord + int(PCG_Hash(frameNumber)), hit.positionWorld, hit.flatNormalWorld, samples, bounces, args.noiseTexture);
      radiance += albedo * SampleIlluminanceField(hit.positionWorld, hit.flatNormalWorld, args.linearSampler, args);

      // Sun
      const float NoL = max(0, dot(hit.flatNormalWorld, uniforms.sky.sunDir));
      const vec3 transmittanceToSun = getTransmittanceAlongRay(
        v_globalUniforms.sky,
        v_globalUniforms.transmittanceLut,
        v_globalUniforms.linearSampler,
        v_globalUniforms.sky.sunDir,
        hit.positionWorld);

      const float bottom_atmosphere_intersection_distance =
        ray_sphere_intersect_nearest(hit.positionWorld * M_TO_KM_SCALE + vec3(0, uniforms.sky.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
          uniforms.sky.sunDir,
          vec3(0.0),
          uniforms.sky.atmosphere_bottom);

      bool view_ray_intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;
      const float sunVisibility = TraceSunRay(hit.positionWorld + hit.flatNormalWorld * 1e-3, uniforms.sky.sunDir);
      vec3 skylight_internal = albedo * NoL / M_PI * sunVisibility * getAtmosphereAlongRay(uniforms.sky, uniforms.skyViewLut, uniforms.linearSampler, uniforms.sky.sunDir, hit.positionWorld);
      vec3 sun_light = uniforms.sky.sunColor * uniforms.sky.sunBrightness * transmittanceToSun / solid_angle_mapping_PDF(radians(0.5)) / M_PI;
      vec3 sunlight_internal = albedo * NoL * sun_light * sunVisibility;

      radiance += float(!view_ray_intersects_ground) * (sunlight_internal + skylight_internal);

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
    else
    {
      depth = 0;
    }
  }
  else
  {
    radiance = getAtmosphereAlongRay(uniforms.sky, uniforms.skyViewLut, args.linearSampler, rayDir, rayPos);
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

  depth = min(depth, args.gridInfo[cascade].baseGridScale * M_SQRT_3);

  const int stableProbeIndex = ProbeIndexToStableIndex(probeIndex, args.gridInfo[cascade]);
  WriteToProbeWithBorder(args.packedProbeRadiance, cascade, stableProbeIndex, args.gridInfo[cascade].probeRadianceResolution, texelCoord, vec4(radiance, 0));
  WriteToProbeWithBorder(args.packedProbeRawDepth, cascade, stableProbeIndex, args.gridInfo[cascade].probeRadianceResolution, texelCoord, vec4(depth, 0, 0, 0));
}
