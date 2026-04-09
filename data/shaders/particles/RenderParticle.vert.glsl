#include "RenderParticle.shared.h"

// vertices in [0, 1]
vec2 CreateQuad(uint vertexID) // triangle list
{
  uint b = 1 << vertexID;
  return vec2((0x32 & b) != 0, (0x26 & b) != 0);
}

layout(location = 0) out vec2 o_uv;
layout(location = 1) flat out int o_particleIndex;

void main()
{
  const int instanceIndex = gl_VertexIndex / 6; // Non-indexed draw
  const int particleIndex = pc.liveParticles.values[instanceIndex].data;
  const Particle particle = pc.particleList.particles[particleIndex].data;

  o_uv = CreateQuad(gl_VertexIndex % 6);
  o_particleIndex = particleIndex;

  const vec2 posOS = o_uv * 2 - 1;
  
  const vec3 posWS =
    particle.position +
    pc.cameraRight * posOS.x * particle.currentScale.x +
    pc.cameraUp * posOS.y * particle.currentScale.y;

  gl_Position = pc.uniforms.viewProj * vec4(posWS, 1);
}