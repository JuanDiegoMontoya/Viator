#include "RenderParticle.shared.h"

// vertices in [0, 1]
vec2 CreateQuad(uint vertexID) // triangle list
{
  uint b = 1 << vertexID;
  return vec2((0x32 & b) != 0, (0x26 & b) != 0);
}

layout(location = 0) out vec2 o_uv;
layout(location = 1) flat out int o_particleIndex;
layout(location = 2) out vec4 o_posCS_unjittered;
layout(location = 3) out vec4 o_posCS_old;

void main()
{
  const int instanceIndex = gl_VertexIndex / 6; // Non-indexed draw
  const int particleIndex = pc.liveParticles.values[instanceIndex].data;
  const Particle particle = pc.particleList.particles[particleIndex].data;

  o_uv = CreateQuad(gl_VertexIndex % 6);
  o_particleIndex = particleIndex;

  const vec2 posOS = o_uv * 2 - 1;
  
  const mat4 view_from_world = pc.uniforms.view;
  const vec3 cameraRight = {view_from_world[0][0], view_from_world[1][0], view_from_world[2][0]};
  const vec3 cameraUp = {view_from_world[0][1], view_from_world[1][1], view_from_world[2][1]};
  
  const mat4 view_from_world_old = pc.uniforms.oldView;
  const vec3 cameraRightOld = {view_from_world_old[0][0], view_from_world_old[1][0], view_from_world_old[2][0]};
  const vec3 cameraUpOld = {view_from_world_old[0][1], view_from_world_old[1][1], view_from_world_old[2][1]};

  const vec3 posWS =
    particle.position +
    cameraRight * posOS.x * particle.currentScale.x +
    cameraUp * posOS.y * particle.currentScale.y;
    
  const vec3 posWS_old =
    particle.positionOld +
    cameraRightOld * posOS.x * particle.currentScale.x +
    cameraUpOld * posOS.y * particle.currentScale.y;

  o_posCS_unjittered = pc.uniforms.viewProjUnjittered * vec4(posWS, 1);
  o_posCS_old = pc.uniforms.oldViewProjUnjittered * vec4(posWS_old, 1);
  gl_Position = pc.uniforms.viewProj * vec4(posWS, 1);
}