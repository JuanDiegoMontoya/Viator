#include "RenderParticle.shared.h"
#include "../voxels/GBuffer.h.glsl"

layout(location = 0) in vec2 i_uv;
layout(location = 1) flat in int i_particleIndex;
layout(location = 2) in vec4 i_posCS_unjittered;
layout(location = 3) in vec4 i_posCS_old;

void main()
{
  const Particle particle = pc.particleList.particles[i_particleIndex].data;
  const vec4 baseColorFactor = unpackUnorm4x8(particle.currentBaseColorFactor);
  const vec4 albedo = texture(particle.baseColorTexture, gNearestClampSampler, i_uv);
  const float alpha = albedo.a * baseColorFactor.a;

  const ivec2 size = textureSize(pc.uniforms.blueNoise, 0);
  const ivec2 posSS = ivec2(gl_FragCoord.xy);
  const float noiseSample = texelFetch(pc.uniforms.blueNoise, posSS % size, 0).r;
  if (noiseSample >= alpha)
  //if (albedo.a < 0.5)
  {
    discard;
  }

  const vec2 motion = ((i_posCS_old.xy / i_posCS_old.w) - (i_posCS_unjittered.xy / i_posCS_unjittered.w)) * 0.5;;

  WriteGBuffer(baseColorFactor.rgb * albedo.rgb, vec3(0), vec3(0), motion, min(alpha, 0.9));
}