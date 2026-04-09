#ifndef GBUFFER_H_GLSL
#define GBUFFER_H_GLSL

layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_radiance;
layout(location = 3) out vec2 o_motion;

// FSR 2 reactive mask. Unused when FSR 2 is disabled
layout(location = 4) out float o_reactiveMask;

void WriteGBuffer(vec3 albedo, vec3 normal, vec3 radiance, vec2 motion, float reactiveMask)
{
  o_albedo       = vec4(albedo, 1);
  o_normal       = vec4(normal, 1);
  o_radiance     = vec4(radiance, 1);
  o_motion       = motion;
  o_reactiveMask = reactiveMask;
}

#endif // GBUFFER_H_GLSL
