#include "../Resources.h.glsl"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"
#include "../Config.shared.h"

#include "Voxels.h.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_radiance;

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
  Voxels voxels;
  FVOG_UINT32 uniformBufferIndex;
}pc;

#define uniforms perFrameUniformsBuffers[pc.uniformBufferIndex]

// More precise way to generate a ray direction.
// https://sibaku.github.io/computer-graphics/2017/01/10/Camera-Ray-Generation.html
vec3 CreateRay(vec2 uv, mat4 view_from_clip, mat4 world_from_view)
{
  const vec2 posNDC = uv * 2.0 - 1.0;
  const vec4 posClip = vec4(posNDC, 0.0, 1.0);
  vec4 dirView = view_from_clip * posClip;
  dirView.w = 0.0;
  const vec3 dirWorld = (world_from_view * dirView).xyz;
  return normalize(dirWorld);
}

void main()
{
  vx_Init(pc.voxels);
  const vec3 rayDir = CreateRay(uv, uniforms.invProj, uniforms.invView);
  const vec3 rayPos = uniforms.cameraPos.xyz;

  vec3 albedo = {0, 0, 0};
  vec3 normal = {0, 0, 0};
  vec3 radiance = {0, 0, 0};

  HitSurfaceParameters hit;
  //if (vx_TraceRaySimple(rayPos, rayDir, 512, hit))
  //if (vx_TraceRayMultiLevel(rayPos, rayDir, 32, hit))
  if (vx_TraceRayUnified(rayPos, rayDir, 132, hit))
  {
    albedo = GetHitAlbedo(hit);
    normal = hit.flatNormalWorld;
    radiance += GetHitEmission(hit);

    const vec4 posClip = uniforms.viewProj * vec4(hit.positionWorld, 1.0);
    gl_FragDepth = posClip.z / posClip.w;
  }
  else
  {
    albedo = rayDir * .5 + .5;
    radiance = albedo;
    gl_FragDepth = FAR_DEPTH;
  }

  o_albedo = vec4(albedo, 1);
  o_normal = vec4(normal, 1);
  vec3 bonus = vec3(0);
  // bonus.r += gSubGridVoxelsTraversed / 5;
  // bonus.g += gVoxelsTraversed / 30;
  // bonus.b += gBottomLevelBricksTraversed / 4;
  o_radiance = vec4(radiance + bonus, 1);
}