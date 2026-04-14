#include "Particles.shared.h"

FVOG_DECLARE_BUFFER_REFERENCE_3(ParticleArchetypeSpawnInfoPtr, 4)
{
  ParticleArchetypeSpawnInfo data;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticleArchetypeSpawnInfoList)
{
  FVOG_INT32 size;
  ParticleArchetypeSpawnInfoPtr spawnInfos;
};

#define PARTICLE_SPAWN_MODE_SINGLE    0
#define PARTICLE_SPAWN_MODE_ARCHETYPE 1
#define PARTICLE_SPAWN_MODE_INDIRECT  2

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticleSpawnGpuParams)
{
  FVOG_UINT32 spawnParticlesMode;
  ParticleList singleParticlesToSpawn;
  ParticleArchetypeSpawnInfoList archetypesToSpawn;
  ParticleVector indirectParticlesToSpawn;

  ParticleArchetypePtr archetypes;
  ParticleList particles;
  IntList liveParticles;
  IntList freeParticles;
  FVOG_UINT32 frameNumber;
};

FVOG_DECLARE_ARGUMENTS(ParticleSpawnArgs)
{
  ParticleSpawnGpuParams pc;
};

#ifndef __cplusplus

#include "../Hash.h.glsl"

layout(local_size_x = PARTICLE_SPAWN_LOCAL_SIZE) in;
void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  
  if (pc.spawnParticlesMode == PARTICLE_SPAWN_MODE_SINGLE)
  {
    if (gid >= pc.singleParticlesToSpawn.size)
    {
      return;
    }

    const Particle particle = pc.singleParticlesToSpawn.particles[gid].data;
    SpawnSingleParticle(particle, pc.liveParticles, pc.freeParticles, pc.particles);
  }
  else if (pc.spawnParticlesMode == PARTICLE_SPAWN_MODE_ARCHETYPE)
  {
    if (gid >= pc.archetypesToSpawn.size)
    {
      return;
    }

    const ParticleArchetypeSpawnInfo spawnInfo = pc.archetypesToSpawn.spawnInfos[gid].data;
    const ParticleArchetype archetype = pc.archetypes[spawnInfo.archetypeIndex].data;
    SpawnArchetype(
      gid,
      pc.frameNumber,
      archetype,
      pc.liveParticles,
      pc.freeParticles,
      pc.particles,
      spawnInfo
    );
  }
  else if (pc.spawnParticlesMode == PARTICLE_SPAWN_MODE_INDIRECT)
  {
    if (gid >= pc.indirectParticlesToSpawn.size)
    {
      return;
    }

    const Particle particle = pc.indirectParticlesToSpawn.particles[gid].data;
    SpawnSingleParticle(particle, pc.liveParticles, pc.freeParticles, pc.particles);
  }
}

#endif // !__cplusplus