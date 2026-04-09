#include "Particles.shared.h"
#include "../BasicTypes.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(DrawIndirectCommandPtr)
{
  DrawIndirectCommand data;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(WriteRenderParticleCommandGpuParams)
{
  DrawIndirectCommandPtr drawCommand;
  IntList liveParticleList;
};

#ifndef __cplusplus

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
  WriteRenderParticleCommandGpuParams pc;
};

layout(local_size_x = 1) in;
void main()
{
  DrawIndirectCommand command;
  command.vertexCount   = pc.liveParticleList.size * 6;
  command.instanceCount = 1;
  command.firstVertex   = 0;
  command.firstInstance = 0;
  pc.drawCommand.data   = command;
}

#endif // !__cplusplus