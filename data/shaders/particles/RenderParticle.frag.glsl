#include "RenderParticle.shared.h"
#include "../voxels/GBuffer.h.glsl"

layout(location = 0) in vec2 i_uv;
layout(location = 1) flat in int i_particleIndex;

void main()
{
  const Particle particle = pc.particleList.particles[i_particleIndex].data;
  // TODO: stochastic discard on alpha
  WriteGBuffer(particle.baseColorFactor.rgb, vec3(0), vec3(0), vec2(0), 0.0);
}