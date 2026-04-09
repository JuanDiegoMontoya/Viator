#include "Particles.shared.h"
#include "../GlobalUniforms.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticlesUpdateGpuParams)
{
  ParticleList particles;
  IntList thisFrameLiveParticles;
  IntList nextFrameLiveParticles;
  IntList freeParticles;
  GlobalUniformsPtr globalUniforms;
};

FVOG_DECLARE_ARGUMENTS(ParticlesUpdateArgs)
{
  ParticlesUpdateGpuParams pc;
};

#ifndef __cplusplus

layout(local_size_x = PARTICLE_UPDATE_LOCAL_SIZE) in;
void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  if (gid >= pc.thisFrameLiveParticles.size)
  {
    return;
  }

  const int particleIndex = pc.thisFrameLiveParticles.values[gid].data;
  Particle particle = pc.particles.particles[particleIndex].data;

  const float dt = pc.globalUniforms.dt;

  particle.velocity += particle.acceleration * dt;
  particle.position += particle.velocity * dt;

  // TODO: Update other attributes.

  particle.lifeRemaining -= dt;

  if (particle.lifeRemaining <= 0)
  {
    const int index = atomicAdd(pc.freeParticles.size, 1);
    pc.freeParticles.values[index].data = particleIndex;
    pc.particles.particles[particleIndex].data.lifeRemaining = 0;
  }
  else
  {
    const int index = atomicAdd(pc.nextFrameLiveParticles.size, 1);
    pc.nextFrameLiveParticles.values[index].data = particleIndex;
    pc.particles.particles[particleIndex].data = particle;
  }
}

#endif // !__cplusplus