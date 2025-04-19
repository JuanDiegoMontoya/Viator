#include "ShadeDeferred.shared.h"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"
#include "../Config.shared.h"

#define uniforms perFrameUniformsBuffers[uniformBufferIndex]

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
  const vec3 normal = texelFetch(gNormal, gid, 0).xyz;
  const float depth = texelFetch(gDepth, gid, 0).x;
  const vec3 positionWorld = UnprojectUV_ZO(depth, uv, uniforms.invViewProj);

  // Hack for unlit objects to render properly.
  if (normal == vec3(0))
  {
    imageStore(sceneColor, gid, vec4(albedo_internal, 0.0));
    return;
  }

  const vec3 radiance_internal = color_convert_src_to_dst(texelFetch(gRadiance, gid, 0).rgb,
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);

  const vec3 irradiance_internal = color_convert_src_to_dst(texelFetch(gIndirectIlluminance, gid, 0).rgb,
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);

  // Shadow
  const vec3 sunDir = normalize(vec3(.7, 1, .3));
  const float NoL = max(0, dot(normal, sunDir));
	vec3 sunlight_internal = albedo_internal * NoL * TraceSunRay(positionWorld + normal * 1e-3, sunDir);

  imageStore(sceneColor, gid, vec4(sunlight_internal + radiance_internal + irradiance_internal, 1));
}
