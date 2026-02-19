#version 450
#extension GL_GOOGLE_include_directive : require
#include "ShadeDeferred.shared.h"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"
#include "../Config.shared.h"
#include "../sky/SkyUtil.h.glsl"

#define uniforms perFrameUniformsBuffers[uniformBufferIndex]

#define WATER_SHADING_OPAQUE_TRANSMISSION       1 // Shades translucent surfaces as if they were opaque, but with transmission.
#define WATER_SHADING_REFLECTION                2 // Meshes don't appear in reflections.
#define WATER_SHADING_REFLECTION_AND_REFRACTION 3 // Reflection and refraction. Meshes don't appear in either.

#define WATER_SHADING 2





//	Simplex 3D Noise
//	by Ian McEwan, Ashima Arts
// TODO: clean up and move to header
vec4 permute(vec4 x)
{
  return mod(((x * 34.0) + 1.0) * x, 289.0);
}
vec4 taylorInvSqrt(vec4 r)
{
  return 1.79284291400159 - 0.85373472095314 * r;
}

float snoise(vec3 v)
{
  const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
  const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

  // First corner
  vec3 i  = floor(v + dot(v, C.yyy));
  vec3 x0 = v - i + dot(i, C.xxx);

  // Other corners
  vec3 g  = step(x0.yzx, x0.xyz);
  vec3 l  = 1.0 - g;
  vec3 i1 = min(g.xyz, l.zxy);
  vec3 i2 = max(g.xyz, l.zxy);

  //  x0 = x0 - 0. + 0.0 * C
  vec3 x1 = x0 - i1 + 1.0 * C.xxx;
  vec3 x2 = x0 - i2 + 2.0 * C.xxx;
  vec3 x3 = x0 - 1. + 3.0 * C.xxx;

  // Permutations
  i      = mod(i, 289.0);
  vec4 p = permute(permute(permute(i.z + vec4(0.0, i1.z, i2.z, 1.0)) + i.y + vec4(0.0, i1.y, i2.y, 1.0)) + i.x + vec4(0.0, i1.x, i2.x, 1.0));

  // Gradients
  // ( N*N points uniformly over a square, mapped onto an octahedron.)
  float n_ = 1.0 / 7.0; // N=7
  vec3 ns  = n_ * D.wyz - D.xzx;

  vec4 j = p - 49.0 * floor(p * ns.z * ns.z); //  mod(p,N*N)

  vec4 x_ = floor(j * ns.z);
  vec4 y_ = floor(j - 7.0 * x_); // mod(j,N)

  vec4 x = x_ * ns.x + ns.yyyy;
  vec4 y = y_ * ns.x + ns.yyyy;
  vec4 h = 1.0 - abs(x) - abs(y);

  vec4 b0 = vec4(x.xy, y.xy);
  vec4 b1 = vec4(x.zw, y.zw);

  vec4 s0 = floor(b0) * 2.0 + 1.0;
  vec4 s1 = floor(b1) * 2.0 + 1.0;
  vec4 sh = -step(h, vec4(0.0));

  vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
  vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

  vec3 p0 = vec3(a0.xy, h.x);
  vec3 p1 = vec3(a0.zw, h.y);
  vec3 p2 = vec3(a1.xy, h.z);
  vec3 p3 = vec3(a1.zw, h.w);

  // Normalise gradients
  vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
  p0 *= norm.x;
  p1 *= norm.y;
  p2 *= norm.z;
  p3 *= norm.w;

  // Mix final noise value
  vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
  m      = m * m;
  return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

float snoise_fbm(vec3 v, int n)
{
  float r = 0;
  for (int i = 0; i < n; i++)
  {
    r += snoise(v * (1 << i)) / (1 << i);
  }
  return r;
}




float FresnelSchlick(vec3 i, vec3 n, float eta)
{
  float F = ((1.0 - eta) * (1.0 - eta)) / ((1.0 + eta) * (1.0 + eta));
  return F + (1.0 - F) * pow((1.0 - dot(-i, n)), 5.0);
}


// isOpaque controls whether AO is applied and whether the illuminance buffer is sampled when using path traced GI.
vec3 CalcRadianceFromPoint(vec3 positionWS, vec3 normalWS, vec3 viewDirWS, vec3 albedoIS, vec2 uv, bool isOpaque, bool applyAo)
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
  }

  if (applyAo)
  {
    irradianceIS *= textureLod(ambientOcclusion, samplerr, uv, 0).x;
  }
  
  const vec3 transmittanceToSun = getTransmittanceAlongRay(
    v_globalUniforms.sky,
    v_globalUniforms.transmittanceLut,
    v_globalUniforms.linearSampler,
    v_globalUniforms.sky.sunDir,
    positionWS);

  const float bottom_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
      positionWS * M_TO_KM_SCALE + vec3(0, uniforms.sky.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
      uniforms.sky.sunDir,
      vec3(0.0),
      uniforms.sky.atmosphere_bottom
  );

  bool view_ray_intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;

  const vec3 sun_light = uniforms.sky.sunColor * uniforms.sky.sunBrightness * transmittanceToSun / solid_angle_mapping_PDF(radians(0.5));

  const float NoL = max(0, dot(normalWS, uniforms.sky.sunDir));
  vec3 sunVisibility;
  // Apply high quality shadows only to primary
  if (applyAo)
  {
    sunVisibility = TraceSunRay(positionWS + normalWS * 1e-3, uniforms.sky.sunDir);
  }
  else
  {
    sunVisibility = vec3(SampleCascadedShadowMap(positionWS, uniforms.sunShadowMap));
  }
  const vec3 sunlightIS = float(!view_ray_intersects_ground) * sun_light * albedoIS * NoL / M_PI * sunVisibility;
  const vec3 skylightIS = albedoIS * NoL / M_PI * sunVisibility * getAtmosphereAlongRay(uniforms.sky, uniforms.skyViewLut, uniforms.linearSampler, uniforms.sky.sunDir, positionWS);
  
  return sunlightIS + skylightIS + irradianceIS;
}

vec3 CalcRadianceFromPointSpecular(vec3 positionWS, vec3 normalWS, vec3 viewDirWS, vec3 albedoIS, bool isOpaque)
{
  const vec3 rayPos = positionWS + normalWS * 1e-3;
  const vec3 rayDir = reflect(viewDirWS, normalWS);

  HitSurfaceParameters hit;
  //if (vx_TraceRaySimple(rayPos, rayDir, 64, hit))
  if (vx_TraceRayMultiLevel(rayPos, rayDir, 128, hit))
  {
    return hit.transmission * (GetHitEmission(hit) + CalcRadianceFromPoint(hit.positionWorld, hit.flatNormalWorld, rayDir, GetHitAlbedo(hit), vec2(0), true, false));
  }

  return hit.transmission * getAtmosphereAlongRay(uniforms.sky, uniforms.skyViewLut, uniforms.linearSampler, rayDir, rayPos);
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
    return hit.transmission * (GetHitEmission(hit) + CalcRadianceFromPoint(hit.positionWorld, hit.flatNormalWorld, rayDir, GetHitAlbedo(hit), vec2(0), true, false));
  }

  return hit.transmission * getAtmosphereAlongRay(uniforms.sky, uniforms.skyViewLut, uniforms.linearSampler, rayDir, rayPos);
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

  const vec3 albedo_internal = color_convert_src_to_dst(texelFetch(uniforms.gBuffer.gAlbedo, gid, 0).rgb, 
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
  normalTranslucent = normalize(normalTranslucent + .05 * snoise_fbm(vec3(0, 1, 0) * uniforms.frameNumber * .01 + 2 * positionTranslucentWS, 3));

  vec3 transmission = vec3(1);
  const float opaqueToCameraDist = depth == FAR_DEPTH ? 1e99 : distance(positionWorld, uniforms.cameraPos.xyz);
  if (depthTranslucent < opaqueToCameraDist || depth == FAR_DEPTH)
  {
    transmission = imageLoad(uniforms.gBuffer.gTransmission, gid).rgb;
    transmission *= vec3(0.2, 0.4, 0.6);
  }

  vec3 radiance_internal = color_convert_src_to_dst(texelFetch(uniforms.gBuffer.gRadiance, gid, 0).rgb,
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);

  const float ior_water = 1.3;
  const float ior_air = 1.0;
  
  vec3 finalRadianceOpaque = vec3(0);
  if (depth != FAR_DEPTH)
  {
    finalRadianceOpaque = transmission * (radiance_internal + CalcRadianceFromPoint(positionWorld, normal, viewDirWS, albedo_internal, uv, true, true));
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
    imageStore(sceneColor, gid, vec4(artisticLightScale * albedo_internal * avgLuminance, 0.0));
    return;
  }

  // Spelunker potion effect
  if (bool(applySpelunkerEffect))
  {
    if (special != 0)
    {
      // Ores behind walls
      if (special == 1)
      {
        float noise = snoise(vec3(0.2 * uv, sin(snoise(vec3(0.5 * uv * 5, 0.5 * float(uniforms.frameNumber) / 120)))));
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

  imageStore(sceneColor, gid, vec4(realFinalRadiance, 1));
}
