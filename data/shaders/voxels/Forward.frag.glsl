#include "Forward.h.glsl"
#include "GBuffer.h.glsl"

layout(location = 0) in vec3 i_color;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec4 i_posCS;
layout(location = 3) in vec4 i_posCS_old;

void main()
{
  const vec3 normal = normalize(i_normal);
  
  const ivec2 gid = ivec2(gl_FragCoord.xy);

  o_albedo = vec4(i_color, 1);
  o_normal = vec4(normal, 1);
  o_radiance = vec4(0, 0, 0, 1);
  o_motion = ((i_posCS_old.xy / i_posCS_old.w) - (i_posCS.xy / i_posCS.w)) * 0.5;
  o_reactiveMask = 0;

  //GpuMaterial material = MaterialBuffers[pc.materialBufferIndex].materials[pc.materialId];
  //o_color.rgb = material.baseColorFactor.rgb;

  // if (bool(material.flags & MATERIAL_HAS_BASE_COLOR))
  // {
  //   o_color.rgb *= texture(Fvog_sampler2D(material.baseColorTextureIndex, pc.samplerIndex), i_uv).rgb;
  // }

  //o_color.rgb = vec3(i_uv, 0);
}