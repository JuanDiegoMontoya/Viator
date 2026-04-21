#ifndef PARTICLES_H_GLSL
#define PARTICLES_H_GLSL

#include "../Resources.h.glsl"
#include "../voxels/RayTracedVoxelsShadowCommon.h.glsl"
#include "../volumetric/clouds/BeerShadowMap.h.glsl"

#define PARTICLE_BEHAVIOR_SOLID                        (1 << 0)
#define PARTICLE_BEHAVIOR_SPAWN_ARCHETYPE_ON_COLLISION (1 << 1)
#define PARTICLE_BEHAVIOR_USE_SKY_SHADOW_MAP           (1 << 2)
#define PARTICLE_BEHAVIOR_USE_BEER_SHADOW_MAP          (1 << 3)
#define PARTICLE_BEHAVIOR_USE_BASE_COLOR_TEXTURE       (1 << 4)
#define PARTICLE_BEHAVIOR_DESTROY_ON_COLLISION         (1 << 5)
#define PARTICLE_BEHAVIOR_FORCE_UP_POS_Y               (1 << 6)
#define PARTICLE_BEHAVIOR_FORCE_UP_POS_Z               (1 << 7)
#define PARTICLE_BEHAVIOR_FORCE_RIGHT_POS_X            (1 << 8)
#define PARTICLE_BEHAVIOR_COLLIDE_WITH_TRANSLUCENT     (1 << 9)

struct Particle
{
  FVOG_UINT32 behaviorFlags;
  FVOG_SHARED Texture2D baseColorTexture;
  FVOG_UINT32 initialBaseColorFactor;
  FVOG_UINT32 currentBaseColorFactor;
  FVOG_UINT32 finalBaseColorFactor;
  FVOG_VEC3 position;
  FVOG_VEC3 positionOld;
  FVOG_VEC3 velocity;
  FVOG_VEC3 acceleration;
  FVOG_UINT32 particleArchetypeToSpawnOnHit;
  FVOG_VEC2 initialScale;
  FVOG_VEC2 currentScale;
  FVOG_VEC2 finalScale;
  FVOG_FLOAT initialLife;
  FVOG_FLOAT lifeRemaining;
};

struct ParticleArchetype
{
  Particle prototype;
  FVOG_VEC3 positionOffsetMin;
  FVOG_VEC3 positionOffsetMax;
  FVOG_VEC3 velocityMin;
  FVOG_VEC3 velocityMax;
  FVOG_VEC3 accelerationMin;
  FVOG_VEC3 accelerationMax;
};

FVOG_DECLARE_BUFFER_REFERENCE_3(ParticlePtr, 4)
{
  Particle data;
};

FVOG_DECLARE_BUFFER_REFERENCE_3(ParticleArchetypePtr, 4)
{
  ParticleArchetype data;
};

FVOG_DECLARE_BUFFER_REFERENCE_3(IntPtr, 4)
{
  FVOG_INT32 data;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticleList)
{
  FVOG_INT32 size;
  ParticlePtr particles;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticleVector)
{
  FVOG_INT32 size;
  FVOG_INT32 capacity;
  ParticlePtr particles;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(IntList)
{
  FVOG_INT32 size;
  IntPtr values;
};

struct ParticleArchetypeSpawnInfo
{
  FVOG_UINT32 archetypeIndex;
  FVOG_INT32 count;
  FVOG_VEC3 positionWS;
  FVOG_VEC3 velocity;
};

#ifndef __cplusplus
#define PARTICLE_SPAWN_LOCAL_SIZE 64
#define PARTICLE_UPDATE_LOCAL_SIZE 128
#include "../Hash.h.glsl"
#include "../BasicTypes.h.glsl"

bool SpawnSingleParticle(
  Particle particle,
  IntList liveParticles,
  IntList freeParticles,
  ParticleList particles)
{
  const int alloc = atomicAdd(liveParticles.size, 1);
  const bool overflowed = alloc >= particles.size;

  if (overflowed && subgroupElect())
  {
    atomicExchange(liveParticles.size, particles.size);
  }

  if (overflowed)
  {
    return false;
  }

  const int free = int(atomicAdd(freeParticles.size, -1)) - 1;
  const int myParticleIndex = freeParticles.values[free].data;
  liveParticles.values[alloc].data = myParticleIndex;
  particles.particles[myParticleIndex].data = particle;
  return true;
}

Particle Particle_CreateFromArchetype(inout uint seed, ParticleArchetype archetype)
{
  Particle particle = archetype.prototype;

  particle.position += PCG_RandVec3(seed, archetype.positionOffsetMin, archetype.positionOffsetMax);
  particle.velocity += PCG_RandVec3(seed, archetype.velocityMin, archetype.velocityMax);
  particle.acceleration += PCG_RandVec3(seed, archetype.accelerationMin, archetype.accelerationMax);

  return particle;
}

Particle Particle_CreateFromSpawnInfo(inout uint seed, ParticleArchetype archetype, ParticleArchetypeSpawnInfo spawnInfo)
{
  Particle particle = Particle_CreateFromArchetype(seed, archetype);

  particle.position += spawnInfo.positionWS;
  particle.velocity += spawnInfo.velocity;

  return particle;
}

void SpawnArchetype(
  int gid,
  uint frameNumber,
  ParticleArchetype archetype,
  IntList liveParticles,
  IntList freeParticles,
  ParticleList particles,
  ParticleArchetypeSpawnInfo spawnInfo)
{
  const int baseAlloc = atomicAdd(liveParticles.size, spawnInfo.count);
  const int allocated = max(0, min(spawnInfo.count, particles.size - baseAlloc));
  const bool overflowed = allocated < spawnInfo.count;

  if (overflowed && subgroupElect())
  {
    atomicExchange(liveParticles.size, particles.size);
  }

  if (allocated <= 0)
  {
    return;
  }

  const int baseFree = int(atomicAdd(freeParticles.size, -allocated)) - 1;

  uint seed = PCG_Hash(gid) ^ PCG_Hash(frameNumber);
  for (int i = 0; i < allocated; i++)
  {
    Particle particle = Particle_CreateFromSpawnInfo(seed, archetype, spawnInfo);

    const int myParticleIndex = freeParticles.values[baseFree - i].data;

    liveParticles.values[baseAlloc + i].data = myParticleIndex;
    particles.particles[myParticleIndex].data = particle;
  }
}

bool SpawnParticleIndirect(Particle particle, ParticleVector indirectParticles, DispatchIndirectCommandPtr dispatch)
{
  const int alloc = atomicAdd(indirectParticles.size, 1);
  const bool overflowed = alloc >= indirectParticles.capacity;

  if (overflowed && subgroupElect())
  {
    atomicExchange(indirectParticles.size, indirectParticles.capacity);
  }

  if (overflowed)
  {
    return false;
  }

  const uint localGroups = (alloc + 1 + PARTICLE_SPAWN_LOCAL_SIZE - 1) / PARTICLE_SPAWN_LOCAL_SIZE;
  const uint groups = subgroupMax(localGroups);
  if (subgroupElect())
  {
    atomicMax(dispatch.data.x, groups);
  }

  indirectParticles.particles[alloc].data = particle;
  return true;
}

#endif

#endif // PARTICLES_H_GLSL