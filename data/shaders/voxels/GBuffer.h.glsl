#ifndef GBUFFER_H_GLSL
#define GBUFFER_H_GLSL

layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_radiance;

// FSR 2 reactive mask. Unused when FSR 2 is disabled
//layout(location = 1) out float o_reactiveMask;

#endif // GBUFFER_H_GLSL
