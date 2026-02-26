#version 450 core
#include "Resources.h.glsl"
#include "voxels/GBuffer.h.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in Texture2D v_texture;
layout(location = 2) in vec3 v_tint;

FVOG_DECLARE_ARGUMENTS(BillboardPushConstants)
{
  FVOG_UINT32 billboardsIndex;
  FVOG_UINT32 globalUniformsIndex;
  FVOG_VEC3 cameraRight;
  FVOG_VEC3 cameraUp;
  Sampler texSampler;
}pc;

void main()
{
  o_albedo = texture(v_texture, pc.texSampler, v_uv);
  o_albedo.rgb *= v_tint;
  o_normal = vec4(0, 0, 0, 1);
  o_radiance = vec4(0, 0, 0, 1);
  o_motion = vec2(0);

  if (o_albedo.a < 0.01)
  {
    discard;
  }
  // Values above 0.9 are not recommended for use, as they are "unlikely [...] to ever produce good results"
  o_reactiveMask = min(o_albedo.a, 0.9);
}