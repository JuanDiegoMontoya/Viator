#version 450
#extension GL_GOOGLE_include_directive : require
#include "ShadeDeferred.shared.h"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"
#include "../Config.shared.h"
#include "../sky/SkyUtil.h.glsl"
#include "FoliageSSS.shared.h"

#define uniforms perFrameUniformsBuffers[uniformBufferIndex]

#define WATER_SHADING_OPAQUE_TRANSMISSION       1 // Shades translucent surfaces as if they were opaque, but with transmission.
#define WATER_SHADING_REFLECTION                2 // Meshes don't appear in reflections.
#define WATER_SHADING_REFLECTION_AND_REFRACTION 3 // Reflection and refraction. Meshes don't appear in either.

#define WATER_SHADING 2


float FresnelSchlick(vec3 i, vec3 n, float eta)
{
  float F = ((1.0 - eta) * (1.0 - eta)) / ((1.0 + eta) * (1.0 + eta));
  return F + (1.0 - F) * pow((1.0 - dot(-i, n)), 5.0);
}


// isOpaque controls whether AO is applied and whether the illuminance buffer is sampled when using path traced GI.
vec3 CalcRadianceFromPoint(vec3 positionWS, vec3 normalWS, vec3 viewDirWS, vec3 albedoIS, vec2 uv, bool isOpaque, bool applyAo, bool isFoliage)
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  
  vec3 irradianceIS = vec3(0);
  if (giMethod == 1 && isOpaque)
  {
    irradianceIS = color_convert_src_to_dst(texelFetch(uniforms.gBuffer.gIndirectIlluminance, gid, 0).rgb,
      COLOR_SPACE_sRGB_LINEAR,
      internalColorSpace);
  }
  else if (giMethod == 2)
  {
    irradianceIS = albedoIS * SampleIlluminanceField(positionWS, normalWS, samplerr, ddgi);
    //return SampleIlluminanceField(positionWS, normalWS, samplerr, ddgi);
  }
  else if (giMethod == 3)
  {
    uint noiseOffsetState = 12345;
    const uint SAMPLES = 1;
    vec3 illum = vec3(0);
    for (uint i = 0; i < SAMPLES; i++)
    {
      const vec2 perSampleNoise = Hammersley(i, SAMPLES);
      ivec2 noiseOffset;
      noiseOffset.x = int(PCG_RandU32(noiseOffsetState));
      noiseOffset.y = int(PCG_RandU32(noiseOffsetState));
      const vec2 noiseTextureSample = texelFetch(uniforms.blueNoise, (gid + noiseOffset) % textureSize(uniforms.blueNoise, 0), 0).xy;
      const vec2 xi        = fract(perSampleNoise + noiseTextureSample);
      const vec3 curRayDir = normalize(map_to_unit_hemisphere_cosine_weighted(xi, normalWS));
      const float cos_theta = clamp(dot(normalWS, curRayDir), 0, 1);
      if (cos_theta <= 0) // Terminate path
      {
        break;
      }
      const float pdf = cosine_weighted_hemisphere_PDF(cos_theta);
      const vec3 brdf_over_pdf = albedoIS / M_PI / pdf; // Lambertian
      const vec3 throughput = throughput_t(cos_theta * brdf_over_pdf);

      HitSurfaceParameters hit;
      if (vx_TraceRayMultiLevel(positionWS + normalWS * 1e-3, curRayDir, 256, hit))
      {
        illum += throughput * GetHitEmission(hit);
        illum += throughput * SampleIlluminanceField(hit.positionWorld, hit.flatNormalWorld, samplerr, ddgi);
      }
      else
      {
        illum += throughput * hit.transmission * Sky_GetScatteringAlongRay(uniforms.sky, curRayDir, positionWS);
      }
    }
    irradianceIS = illum / float(SAMPLES);
  }

  float ao = 1;
  if (applyAo)
  {
    ao = textureLod(ambientOcclusion, samplerr, uv, 0).x;
    irradianceIS *= textureLod(ambientOcclusion, samplerr, uv, 0).x;
  }
  
  const vec3 transmittanceToSun = Sky_GetTransmittanceAlongRay(
    v_globalUniforms.sky,
    v_globalUniforms.sky.config.sunDir,
    positionWS);

  const float bottom_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
      positionWS * M_TO_KM_SCALE + vec3(0, uniforms.sky.config.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
      uniforms.sky.config.sunDir,
      vec3(0.0),
      uniforms.sky.config.atmosphere_bottom
  );

  bool view_ray_intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;

  const vec3 sun_light = uniforms.sky.config.sunColor * uniforms.sky.config.sunBrightness * transmittanceToSun / solid_angle_mapping_PDF(radians(0.5));

  const float NoL = max(0, dot(normalWS, uniforms.sky.config.sunDir));
  vec3 sunVisibility;
  // Apply high quality shadows only to primary
  if (applyAo)
  {
    sunVisibility = TraceSunRay(positionWS + normalWS * 1e-3, uniforms.sky.config.sunDir);
  }
  else
  {
    sunVisibility = vec3(SampleCascadedShadowMap(positionWS, uniforms.sunShadowMap));
  }
  
  const vec3 sunVisibilityBsm = vec3(SampleCascadedBeerShadowMap(positionWS, uniforms.beerShadowMap));

  vec3 sss = vec3(0);
  if (uniforms.foliageSSSPtr != 0)
  {
    FoliageCBSMInfoPtr sssPtr = FoliageCBSMInfoPtr(uniforms.foliageSSSPtr);
    const vec3 sssTransmittance = SampleFoliageCBSM(positionWS + uniforms.sky.config.sunDir * 0.5, sssPtr);

    // Half lambert, AKA wrapped diffuse.
    const float NoL2 = square(-dot(normalWS, uniforms.sky.config.sunDir) * 0.5 + 0.5);
    const vec3 sunlightIS2 = sunVisibilityBsm * float(!view_ray_intersects_ground) * sun_light * albedoIS * NoL2 / M_PI;
    const vec3 skylightIS2 = sunVisibilityBsm * albedoIS * NoL2 / M_PI * Sky_GetScatteringAlongRay(uniforms.sky, uniforms.sky.config.sunDir, positionWS);
    if (isFoliage)
    {
      sss = (sunlightIS2 + skylightIS2) * sssTransmittance * 1;
    }
  }

  sunVisibility *= sunVisibilityBsm;
  const vec3 sunlightIS = float(!view_ray_intersects_ground) * sun_light * albedoIS * NoL / M_PI * sunVisibility;
  const vec3 skylightIS = albedoIS * NoL / M_PI * sunVisibility * Sky_GetScatteringAlongRay(uniforms.sky, uniforms.sky.config.sunDir, positionWS);
  
  // Local lighting
  const ivec4 cellPosAndCascade = LG_GetCellPositionAndCascadeIndexAtPoint(uniforms.cascadedLightGrid, positionWS);
  vec3 localLightIS = vec3(0);

  if (cellPosAndCascade.w >= 0)
  {
    const ivec3 cellPos    = cellPosAndCascade.xyz;
    const int cascade      = cellPosAndCascade.w;
    const uint lightOffset = LG_GetLightListOffsetForCell(uniforms.cascadedLightGrid, cascade, cellPos);
    const uint lightCount  = LG_GetLightCountForCell(uniforms.cascadedLightGrid, cascade, cellPos);

    for (uint i = 0; i < lightCount; i++)
    {
      const uint lightIndex = uniforms.cascadedLightGrid.grids[cascade].lightIndices.values[lightOffset + i].data;
      vec3 transmission;
      const float visibility = GetPunctualLightVisibility2(positionWS + normalWS * 1e-3, lightIndex, transmission);
      
      const GpuLight light = uniforms.lights[lightIndex].data;

      Surface surface;
      surface.albedo = albedoIS;
      surface.normal = normalWS;
      surface.position = positionWS;

      localLightIS += visibility * transmission * EvaluatePunctualLightLambert(light, surface, COLOR_SPACE_sRGB_LINEAR);
    }
    //return 100 * (lightCount + 1) * TurboColormap(float(cellPosAndCascade.w) / uniforms.cascadedLightGrid.numCascades);
  }
  
  return sss + (sunlightIS + skylightIS) + irradianceIS + localLightIS;
}

vec3 CalcRadianceFromPointSpecular(vec3 positionWS, vec3 normalWS, vec3 viewDirWS, vec3 albedoIS, bool isOpaque)
{
  const vec3 rayPos = positionWS + normalWS * 1e-3;
  const vec3 rayDir = reflect(viewDirWS, normalWS);

  HitSurfaceParameters hit;
  //if (vx_TraceRaySimple(rayPos, rayDir, 64, hit))
  if (vx_TraceRayMultiLevel(rayPos, rayDir, 128, hit))
  {
    const bool isFoliage = bool(vx_GetVoxelFlags(hit.voxel) & VOXEL_IS_SSS_FOLIAGE);
    return hit.transmission * (GetHitEmission(hit) + CalcRadianceFromPoint(hit.positionWorld, hit.flatNormalWorld, rayDir, GetHitAlbedo(hit), vec2(0), true, false, isFoliage));
  }

  return hit.transmission * Sky_GetScatteringAlongRay(uniforms.sky, rayDir, rayPos);
}

// eta = initial ior / next ior
vec3 CalcRadianceFromPointRefract(vec3 positionWS, vec3 normalWS, vec3 viewDirWS, vec3 albedoIS, float eta, bool isOpaque)
{
  const vec3 rayPos = positionWS + normalWS * 1e-3;
  const vec3 rayDir = refract(viewDirWS, normalWS, eta);
  if (length(rayDir) < 0.5)
  {
    return CalcRadianceFromPointSpecular(positionWS, normalWS, viewDirWS, albedoIS, isOpaque);
  }

  HitSurfaceParameters hit;
  //if (vx_TraceRaySimple(rayPos, rayDir, 64, hit))
  if (vx_TraceRayMultiLevel(rayPos, rayDir, 64, hit))
  {
    const bool isFoliage = bool(vx_GetVoxelFlags(hit.voxel) & VOXEL_IS_SSS_FOLIAGE);
    return hit.transmission * (GetHitEmission(hit) + CalcRadianceFromPoint(hit.positionWorld, hit.flatNormalWorld, rayDir, GetHitAlbedo(hit), vec2(0), true, false, isFoliage));
  }

  return hit.transmission * Sky_GetScatteringAlongRay(uniforms.sky, rayDir, rayPos);
}


layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, imageSize(sceneColor))))
  {
    return;
  }

  vx_Init(voxels);

  const vec2 uv = (vec2(gid) + 0.5) / imageSize(sceneColor);

  const vec4 albedoRaw = texelFetch(uniforms.gBuffer.gAlbedo, gid, 0).rgba;
  const vec3 albedo_internal = color_convert_src_to_dst(albedoRaw.rgb, 
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);
  const vec3 normal = normalize(texelFetch(uniforms.gBuffer.gNormal, gid, 0).xyz);
  const float depth = texelFetch(uniforms.gBuffer.gDepth, gid, 0).x;
  const vec3 positionWorld = UnprojectUV_ZO(depth == FAR_DEPTH ? 0.75 : depth, uv, uniforms.invViewProj);
  const vec3 viewDirWS = normalize(positionWorld - uniforms.cameraPos.xyz);
  const uint special = imageLoad(uniforms.gBuffer.gSpecial, gid).x;
  
  const float depthTranslucent = imageLoad(uniforms.gBuffer.gDepthTranslucent, gid).x;
  const vec3 albedoTranslucent_internal = color_convert_src_to_dst(imageLoad(uniforms.gBuffer.gAlbedoTranslucent, gid).rgb, 
    COLOR_SPACE_sRGB_LINEAR, 
    internalColorSpace);
  const vec3 positionTranslucentWS = uniforms.cameraPos.xyz + viewDirWS * depthTranslucent;

  vec3 normalTranslucent = imageLoad(uniforms.gBuffer.gNormalTranslucent, gid).xyz;
  if (dot(-viewDirWS, vec3(0, 1, 0)) > 0)
  {
    normalTranslucent = vec3(0, 1, 0);
  }
  if (depthTranslucent < 1e30)
  {
    normalTranslucent = normalize(normalTranslucent + .05 * Simplex_Fbm(vec3(0, 1, 0) * uniforms.frameNumber * .01 + 2 * positionTranslucentWS, 3));
  }

  vec3 transmission = vec3(1);
  const float opaqueToCameraDist = depth == FAR_DEPTH ? 1e99 : distance(positionWorld, uniforms.cameraPos.xyz);
  if (depthTranslucent < opaqueToCameraDist || depth == FAR_DEPTH)
  {
    transmission = imageLoad(uniforms.gBuffer.gTransmission, gid).rgb;
    transmission *= vec3(0.4, 0.75, 0.9);
  }

  vec3 radiance_internal = color_convert_src_to_dst(texelFetch(uniforms.gBuffer.gRadiance, gid, 0).rgb,
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);

  const float ior_water = 1.3;
  const float ior_air = 1.0;
  
  vec3 finalRadianceOpaque = vec3(0);
  if (depth != FAR_DEPTH)
  {
    const bool isFoliage = albedoRaw.a == 1;
    finalRadianceOpaque = transmission * (radiance_internal + CalcRadianceFromPoint(positionWorld, normal, viewDirWS, albedo_internal, uv, true, true, isFoliage));
  }
  vec3 finalRadianceTranslucent = vec3(0);
  if (depthTranslucent < 1e9)
  {
#if WATER_SHADING == WATER_SHADING_REFLECTION || WATER_SHADING == WATER_SHADING_REFLECTION_AND_REFRACTION
    finalRadianceTranslucent = CalcRadianceFromPointSpecular(positionTranslucentWS, normalTranslucent, viewDirWS, albedoTranslucent_internal, false);
#endif

#if WATER_SHADING == WATER_SHADING_OPAQUE_TRANSMISSION
    finalRadianceTranslucent = CalcRadianceFromPoint(positionTranslucentWS, normalTranslucent, viewDirWS, albedoTranslucent_internal, uv, false, false);
#elif WATER_SHADING == WATER_SHADING_REFLECTION_AND_REFRACTION // Replace opaque color with refracted.
    if (opaqueToCameraDist > depthTranslucent && depthTranslucent > 1e-3)
    {
      finalRadianceOpaque = CalcRadianceFromPointRefract(positionTranslucentWS, normalTranslucent, viewDirWS, albedoTranslucent_internal, ior_air / ior_water, false);
    }
#endif
  }

  // Sky.
  if (depth == FAR_DEPTH)
  {
    finalRadianceOpaque = radiance_internal * transmission;
  }
  else if (normal == vec3(0)) // Sprites, billboards, etc.
  {
    const vec3 avgLuminance = SampleAverageLuminance(positionWorld, uniforms.linearSampler, ddgi);
    const float artisticLightScale = 2; // Makes sprites "pop" a little more from their surroundings.
    finalRadianceOpaque = transmission * (radiance_internal + artisticLightScale * albedo_internal * avgLuminance);
  }

  // Spelunker potion effect
  if (bool(applySpelunkerEffect) && depth != FAR_DEPTH)
  {
    if (special != 0)
    {
      // Ores behind walls
      if (special == 1)
      {
        float noise = Simplex_Noise(vec3(0.2 * uv, sin(Simplex_Noise(vec3(0.5 * uv * 5, 0.5 * float(uniforms.frameNumber) / 120)))));
        vec3 colorA = vec3(.8, .3, .1);
        vec3 colorB = vec3(.9, .5, .2);
        //fragColor.xyz = vec3(mix(colorA, colorB, noise));
        finalRadianceOpaque += Luminance(finalRadianceOpaque) * 50 * mix(colorA, colorB, noise);
      }

      // Directly visible ores
      if (special == 2)
      {
        finalRadianceOpaque *= 10;
      }
    }
  }

  vec3 reflectedColor = finalRadianceTranslucent;
  const float reflectance = FresnelSchlick(viewDirWS, normalTranslucent, ior_air / ior_water);
  vec3 realFinalRadiance = mix(finalRadianceOpaque, reflectedColor, reflectance);
  if (depthTranslucent >= 1e9 || depthTranslucent < 0.125 || depthTranslucent > opaqueToCameraDist)
  {
    realFinalRadiance = finalRadianceOpaque;
  }

  if (depth != FAR_DEPTH)
  {
    // TODO: TEMP. Scattering will be double counted if opaque stuff appears behind clouds.
    // The incorrectness might be subtle enough to ignore, however.
    vec3 transmittance, scattering;
    Sky_GetAerialPerspective(uniforms.sky, positionWorld, transmittance, scattering);
    realFinalRadiance = realFinalRadiance * transmittance + scattering;
  }

  imageStore(sceneColor, gid, vec4(realFinalRadiance, 1));
}
