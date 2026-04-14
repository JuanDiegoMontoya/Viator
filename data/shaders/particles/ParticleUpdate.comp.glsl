#include "Particles.shared.h"
#include "../GlobalUniforms.h.glsl"
#include "../voxels/Voxels.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticlesUpdateGpuParams)
{
  ParticleList particles;
  ParticleVector indirectParticles;
  DispatchIndirectCommandPtr spawnIndirectDispatchCommand;
  IntList thisFrameLiveParticles;
  IntList nextFrameLiveParticles;
  IntList freeParticles;
  GlobalUniformsPtr uniforms;
  ParticleArchetypePtr archetypes;
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

  vx_Init(VoxelsPtr(pc.uniforms.voxelsPtr).data);

  const int particleIndex = pc.thisFrameLiveParticles.values[gid].data;
  Particle particle = pc.particles.particles[particleIndex].data;

  const float dt = pc.uniforms.dt;
  
  bool alreadyUpdatedPosition = false;
  bool hadCollision = false;

  // TODO: Update other attributes.
  if (bool(particle.isSolid))
  {
#if 0 // Simple block collision logic with incorrect reflection vector.
    if (vx_GetSolidAt(particle.position))
    {
      particle.velocity *= -0.75;
      particle.position += particle.velocity * dt;
      hadCollision = true;
    }
#else // More expensive collision logic that uses a short ray trace. Computes accurate reflection vector.
    const float speed = length(particle.velocity);
    if (speed > 1e-3)
    {
      const vec3 rayDir = particle.velocity / speed;
      HitSurfaceParameters hit;
      if (vx_TraceRaySimple(particle.position, rayDir, speed * dt, hit))
      {
        particle.velocity = reflect(rayDir, normalize(hit.flatNormalWorld)) * speed;
        ASSERT(abs(length(particle.velocity) - speed) < 1e-3);

        const float distToHit = distance(particle.position, hit.positionWorld);
        const float remainingDist = speed * dt - distToHit;
        particle.position = hit.positionWorld;
        particle.position += particle.velocity * remainingDist * dt;

        alreadyUpdatedPosition = true;
        hadCollision = true;
      }
    }
#endif
  
    if (hadCollision && bool(particle.spawnParticleOnHit))
    { 
      uint seed = PCG_Hash(gid) ^ PCG_Hash(pc.uniforms.frameNumber);
      Particle newParticle = Particle_CreateFromArchetype(seed, pc.archetypes[particle.particleArchetypeToSpawnOnHit].data);
      newParticle.position += particle.position;
      SpawnParticleIndirect(newParticle, pc.indirectParticles, pc.spawnIndirectDispatchCommand);
    }
  }

  particle.positionOld = particle.position;
  particle.velocity += particle.acceleration * dt;

  if (!alreadyUpdatedPosition)
  {
    particle.position += particle.velocity * dt;
  }

  particle.lifeRemaining -= dt;

  if (particle.lifeRemaining > 0)
  {
    const int index = atomicAdd(pc.nextFrameLiveParticles.size, 1);
    pc.nextFrameLiveParticles.values[index].data = particleIndex;
    pc.particles.particles[particleIndex].data = particle;
  }
  else
  {
    pc.particles.particles[particleIndex].data.lifeRemaining = 0;
  }
}

#endif // !__cplusplus