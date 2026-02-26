#version 450 core

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_radiance;
layout(location = 3) out vec2 o_motion;
layout(location = 4) out float o_reactiveMask;

// FSR 2 reactive mask. Unused when FSR 2 is disabled
//layout(location = 1) out float o_reactiveMask;

void main()
{
  o_albedo = v_color;
  o_normal = vec4(0, 0, 0, 1);
  o_radiance = vec4(0, 0, 0, 1);
  o_motion = vec2(0);
  
  // Values above 0.9 are not recommended for use, as they are "unlikely [...] to ever produce good results"
  o_reactiveMask = min(o_albedo.a, 0.9);
}