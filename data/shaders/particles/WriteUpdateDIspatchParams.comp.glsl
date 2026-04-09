#include "Particles.shared.h"
#include "../BasicTypes.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(DispatchIndirectCommandPtr)
{
  DispatchIndirectCommand data;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(WriteUpdateDispatchParamsGpuParams)
{
  DispatchIndirectCommandPtr dispatchCommand;
  IntList liveParticleList;
};

#ifndef __cplusplus

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
  WriteUpdateDispatchParamsGpuParams pc;
};

layout(local_size_x = 1) in;
void main()
{
  const int groups = (pc.liveParticleList.size + PARTICLE_UPDATE_LOCAL_SIZE - 1) / PARTICLE_UPDATE_LOCAL_SIZE;
  pc.dispatchCommand.data = DispatchIndirectCommand(groups, 1, 1);
}

#endif // !__cplusplus