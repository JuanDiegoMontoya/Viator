#include "Particles.shared.h"

FVOG_DECLARE_BUFFER_REFERENCE_2(GarbageCollectParticlesGpuParams)
{
  IntList liveParticles;
  IntList freeParticles;
  ParticleList particles;
};

#ifndef __cplusplus

FVOG_DECLARE_ARGUMENTS(GarbageCollectParticlesArgs)
{
  GarbageCollectParticlesGpuParams pc;
};

layout(local_size_x = PARTICLE_UPDATE_LOCAL_SIZE) in;
void main()
{
  const int gid = int(gl_GlobalInvocationID.x);

  if (gid >= pc.liveParticles.size)
  {
    return;
  }

  const int particleIndex = pc.liveParticles.values[gid].data;
  const Particle particle = pc.particles.particles[particleIndex].data;
  
  if (particle.lifeRemaining <= 0)
  {
    const int index = atomicAdd(pc.freeParticles.size, 1);
    pc.freeParticles.values[index].data = particleIndex;
  }
}

#endif // !__cplusplus