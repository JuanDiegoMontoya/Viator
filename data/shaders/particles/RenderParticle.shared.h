#ifndef RENDER_PARTICLE_SHARED_H
#define RENDER_PARTICLE_SHARED_H

#include "../GlobalUniforms.h.glsl"
#include "Particles.shared.h"

FVOG_DECLARE_BUFFER_REFERENCE_2(ParticleRenderGpuParams)
{
  GlobalUniformsPtr uniforms;
  ParticleList particleList;
  IntList liveParticles;
};

FVOG_DECLARE_ARGUMENTS(ParticleRenderArgs)
{
  ParticleRenderGpuParams pc;
};

#endif // RENDER_PARTICLE_SHARED_H