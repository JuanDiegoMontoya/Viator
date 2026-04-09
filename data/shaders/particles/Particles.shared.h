#ifndef PARTICLES_H_GLSL
#define PARTICLES_H_GLSL

#include "../Resources.h.glsl"

struct Particle
{
  FVOG_SHARED Texture2D baseColorTexture;
  FVOG_VEC4 baseColorFactor;
  FVOG_VEC3 position;
  FVOG_VEC3 velocity;
  FVOG_VEC3 acceleration;
  FVOG_BOOL32 isSolid; // Collides with voxels.
  FVOG_BOOL32 spawnParticleOnHit;
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

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticleArchetypeList)
{
  FVOG_INT32 size;
  ParticleArchetypePtr archetypes;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(IntList)
{
  FVOG_INT32 size;
  IntPtr values;
};

#ifndef __cplusplus
#define PARTICLE_UPDATE_LOCAL_SIZE 128
#endif

#endif // PARTICLES_H_GLSL