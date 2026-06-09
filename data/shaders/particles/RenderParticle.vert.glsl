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

  vec3 right = cameraRight;
  vec3 up = cameraUp;
  vec3 rightOld = cameraRightOld;
  vec3 upOld = cameraUpOld;

  if (bool(particle.behaviorFlags & PARTICLE_BEHAVIOR_FORCE_UP_POS_Y))
  {
    up    = vec3(0, 1, 0);
    upOld = vec3(0, 1, 0);
  }

  if (bool(particle.behaviorFlags & PARTICLE_BEHAVIOR_FORCE_UP_NORM_VELOCITY))
  {
    const float len = length(particle.velocity);
    if (len > 1e-3)
    {
      up    = -particle.velocity / len;
      upOld = up;
    }
    else
    {
      up    = vec3(0, 1, 0);
      upOld = vec3(0, 1, 0);
    }
  }
  
  if (bool(particle.behaviorFlags & PARTICLE_BEHAVIOR_FORCE_RIGHT_POS_X))
  {
    up    = vec3(0, 0, 1);
    upOld = vec3(0, 0, 1);
  }

  if (bool(particle.behaviorFlags & PARTICLE_BEHAVIOR_FORCE_RIGHT_POS_X))
  {
    right    = vec3(1, 0, 0);
    rightOld = vec3(1, 0, 0);
  }

  const vec2 scale = unpackHalf2x16(particle.currentScale);

  const vec3 posWS =
    particle.position +
    right * posOS.x * scale.x +
    up * posOS.y * scale.y;

  const vec3 posWS_old =
    particle.positionOld +
    rightOld * posOS.x * scale.x +
    upOld * posOS.y * scale.y;

  o_posCS_unjittered = pc.uniforms.viewProjUnjittered * vec4(posWS, 1);
  o_posCS_old = pc.uniforms.oldViewProjUnjittered * vec4(posWS_old, 1);
  gl_Position = pc.uniforms.viewProj * vec4(posWS, 1);
}