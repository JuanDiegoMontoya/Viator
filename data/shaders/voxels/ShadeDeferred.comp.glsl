#include "ShadeDeferred.shared.h"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"
#include "../Config.shared.h"
#include "../sky/SkyUtil.h.glsl"

#define uniforms perFrameUniformsBuffers[uniformBufferIndex]









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

  const vec3 albedo_internal = color_convert_src_to_dst(texelFetch(gAlbedo, gid, 0).rgb, 
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);
  const vec3 normal = normalize(texelFetch(gNormal, gid, 0).xyz);
  const float depth = texelFetch(gDepth, gid, 0).x;
  const vec3 positionWorld = UnprojectUV_ZO(depth, uv, uniforms.invViewProj);
  const vec3 viewDirWS = normalize(positionWorld - uniforms.cameraPos.xyz);
  const uint special = imageLoad(gSpecial, gid).x;

  vec3 radiance_internal = color_convert_src_to_dst(texelFetch(gRadiance, gid, 0).rgb,
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);

  if (depth == FAR_DEPTH)
  {
    imageStore(sceneColor, gid, vec4(radiance_internal, 0.0));
    return;
  }

  // Hack for unlit objects to render properly.
  if (normal == vec3(0))
  {
    imageStore(sceneColor, gid, vec4(albedo_internal, 0.0));
    return;
  }

  vec3 irradiance_internal = vec3(0);
  if (giMethod == 1)
  {
    irradiance_internal = color_convert_src_to_dst(texelFetch(gIndirectIlluminance, gid, 0).rgb,
      COLOR_SPACE_sRGB_LINEAR,
      internalColorSpace);
  }
  else if (giMethod == 2)
  {
    irradiance_internal = albedo_internal * SampleIlluminanceField(positionWorld, normal, samplerr, ddgi);
  }

  // Shadow
  const float NoL = max(0, dot(normal, uniforms.sky.sunDir));
  const vec3 transmittanceToSun = getTransmittanceAlongRay(
    v_globalUniforms.sky,
    v_globalUniforms.transmittanceLut,
    v_globalUniforms.linearSampler,
    v_globalUniforms.sky.sunDir,
    positionWorld);

  const float bottom_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
      positionWorld * M_TO_KM_SCALE + vec3(0, uniforms.sky.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
      uniforms.sky.sunDir,
      vec3(0.0),
      uniforms.sky.atmosphere_bottom
  );
    
  bool view_ray_intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;

  const vec3 sun_light = uniforms.sky.sunColor * uniforms.sky.sunBrightness * transmittanceToSun / solid_angle_mapping_PDF(radians(0.5));
  const float sunVisibility = TraceSunRay(positionWorld + normal * 1e-3, uniforms.sky.sunDir);
	const vec3 sunlight_internal = float(!view_ray_intersects_ground) * sun_light * albedo_internal * NoL / M_PI * sunVisibility;
  const vec3 skylight_internal = albedo_internal * NoL / M_PI * sunVisibility * getAtmosphereAlongRay(uniforms.sky, uniforms.skyViewLut, uniforms.linearSampler, uniforms.sky.sunDir, positionWorld);
  vec3 finalRadiance = sunlight_internal + skylight_internal + radiance_internal + irradiance_internal;

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
        finalRadiance += Luminance(finalRadiance) * 50 * mix(colorA, colorB, noise);
      }

      // Directly visible ores
      if (special == 2)
      {
        finalRadiance *= 10;
      }
    }
  }

  imageStore(sceneColor, gid, vec4(finalRadiance, 1));
}
