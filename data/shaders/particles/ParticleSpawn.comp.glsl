#include "Particles.shared.h"

struct ParticleArchetypeSpawnInfo
{
  FVOG_UINT32 archetypeIndex;
  FVOG_INT32 count;
  FVOG_VEC3 positionWS;
  FVOG_VEC3 velocity;
};

FVOG_DECLARE_BUFFER_REFERENCE_3(ParticleArchetypeSpawnInfoPtr, 4)
{
  ParticleArchetypeSpawnInfo data;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticleArchetypeSpawnInfoList)
{
  FVOG_INT32 size;
  ParticleArchetypeSpawnInfoPtr spawnInfos;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticleSpawnGpuParams)
{
  FVOG_BOOL32 spawnSingleParticlesMode;
  ParticleList singleParticlesToSpawn;
  ParticleArchetypeSpawnInfoList archetypesToSpawn;

  ParticleArchetypeList archetypeList;
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

void SpawnArchetypes()
{
  const int gid = int(gl_GlobalInvocationID.x);

  if (gid >= pc.archetypesToSpawn.size)
  {
    return;
  }

  const int spawnInfoIndex = pc.freeParticles.values[gid].data;
  const ParticleArchetypeSpawnInfo spawnInfo = pc.archetypesToSpawn.spawnInfos[spawnInfoIndex].data;
  const ParticleArchetype archetype = pc.archetypeList.archetypes[spawnInfo.archetypeIndex].data;

  const int baseAlloc = atomicAdd(pc.liveParticles.size, spawnInfo.count);
  const int allocated = min(spawnInfo.count, pc.particles.size - baseAlloc);
  const bool overflowed = allocated < spawnInfo.count;

  if (subgroupElect() && subgroupOr(overflowed))
  {
    atomicExchange(pc.liveParticles.size, pc.particles.size);
  }

  const int baseFree = atomicAdd(pc.freeParticles.size, -allocated) - 1;

  uint seed = PCG_Hash(gid) ^ PCG_Hash(pc.frameNumber);
  for (int i = 0; i < allocated; i++)
  {
    Particle particle = archetype.prototype;

    const vec3 positionOffset     = PCG_RandVec3(seed, archetype.positionOffsetMin, archetype.positionOffsetMax);
    const vec3 velocityOffset     = PCG_RandVec3(seed, archetype.velocityMin, archetype.velocityMax);
    const vec3 accelerationOffset = PCG_RandVec3(seed, archetype.accelerationMin, archetype.accelerationMax);

    particle.position += spawnInfo.positionWS + positionOffset;
    particle.velocity += spawnInfo.velocity + velocityOffset;
    particle.acceleration += accelerationOffset;

    const int myParticleIndex = pc.freeParticles.values[baseFree - i].data;
    pc.liveParticles.values[baseAlloc + i].data = myParticleIndex;
    pc.particles.particles[myParticleIndex].data = particle;
  }
}

void SpawnSingles()
{
  const int gid = int(gl_GlobalInvocationID.x);

  if (gid >= pc.singleParticlesToSpawn.size)
  {
    return;
  }

  const int alloc = atomicAdd(pc.liveParticles.size, 1);
  const bool overflowed = alloc >= pc.particles.size;

  if (subgroupElect() && subgroupOr(overflowed))
  {
    atomicExchange(pc.liveParticles.size, pc.particles.size);
  }

  if (overflowed)
  {
    return;
  }
  
  const Particle particle = pc.singleParticlesToSpawn.particles[gid].data;

  const int free = int(atomicAdd(pc.freeParticles.size, -1)) - 1;
  const int myParticleIndex = pc.freeParticles.values[free].data;
  pc.liveParticles.values[alloc].data = myParticleIndex;
  pc.particles.particles[myParticleIndex].data = particle;
}

layout(local_size_x = 64) in;
void main()
{
  if (bool(pc.spawnSingleParticlesMode))
  {
    SpawnSingles();
  }
  else
  {
    SpawnArchetypes();
  }
}

#endif // !__cplusplus