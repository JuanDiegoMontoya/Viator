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
  CascadedShadowMapInfoPtr skyShadowMap;
  CascadedBeerShadowMapInfoPtr skyBeerShadowMap;
};

FVOG_DECLARE_ARGUMENTS(ParticleSpawnArgs)
{
  ParticleSpawnGpuParams pc;
};

#ifndef __cplusplus

#include "../Hash.h.glsl"

void SpawnSingleParticleWithBehavior(inout uint seed, Particle particle)
{
  float spawnChance = 1;

  if (bool(particle.behaviorFlags & PARTICLE_BEHAVIOR_USE_SKY_SHADOW_MAP))
  {
    spawnChance *= SampleCascadedShadowMap(particle.position, pc.skyShadowMap);
  }

  if (bool(particle.behaviorFlags & PARTICLE_BEHAVIOR_USE_BEER_SHADOW_MAP))
  {
    spawnChance *= SampleCascadedBeerShadowMap(particle.position, pc.skyBeerShadowMap);
  }

  if (spawnChance < 1)
  {
    if (PCG_RandFloat(seed) > spawnChance)
    {
      return;
    }
  }

  SpawnSingleParticle(particle, pc.liveParticles, pc.freeParticles, pc.particles);
}

layout(local_size_x = PARTICLE_SPAWN_LOCAL_SIZE) in;
void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  uint seed = PCG_Hash(gid) ^ PCG_Hash(pc.frameNumber);
  
  if (pc.spawnParticlesMode == PARTICLE_SPAWN_MODE_SINGLE)
  {
    if (gid >= pc.singleParticlesToSpawn.size)
    {
      return;
    }

    const Particle particle = pc.singleParticlesToSpawn.particles[gid].data;
    SpawnSingleParticleWithBehavior(seed, particle);
  }
  else if (pc.spawnParticlesMode == PARTICLE_SPAWN_MODE_ARCHETYPE)
  {
    if (gid >= pc.archetypesToSpawn.size)
    {
      return;
    }

    const ParticleArchetypeSpawnInfo spawnInfo = pc.archetypesToSpawn.spawnInfos[gid].data;
    const ParticleArchetype archetype = pc.archetypes[spawnInfo.archetypeIndex].data;

    if (bool(archetype.prototype.behaviorFlags & PARTICLE_BEHAVIOR_USE_SKY_SHADOW_MAP) || bool(archetype.prototype.behaviorFlags & PARTICLE_BEHAVIOR_USE_BEER_SHADOW_MAP))
    {
      const Particle particle = Particle_CreateFromSpawnInfo(seed, archetype, spawnInfo);
      SpawnSingleParticleWithBehavior(seed, particle);
    }
    else
    {
      // More efficient batched spawning.
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
  }
  else if (pc.spawnParticlesMode == PARTICLE_SPAWN_MODE_INDIRECT)
  {
    if (gid >= pc.indirectParticlesToSpawn.size)
    {
      return;
    }

    const Particle particle = pc.indirectParticlesToSpawn.particles[gid].data;
    SpawnSingleParticleWithBehavior(seed, particle);
  }
}

#endif // !__cplusplus