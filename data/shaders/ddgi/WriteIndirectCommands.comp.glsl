#include "ProbeCommon.shared.h"

layout(local_size_x = 1) in;

void main()
{
  DispatchIndirectCommand wholeProbesCommand = {0, 1, 1};
  wholeProbesCommand.x = (args.probesToUpdate.size + DDGI_WORKGROUP_SIZE - 1) / DDGI_WORKGROUP_SIZE;
  args.wholeProbesIndirectCommand.data = wholeProbesCommand;

  DispatchIndirectCommand probeTexelsCommand = {0, 1, 1};
  probeTexelsCommand.x = ((args.probesToUpdate.size * args.probeRadianceResolution.x * args.probeRadianceResolution.y) + DDGI_WORKGROUP_SIZE - 1) / DDGI_WORKGROUP_SIZE;
  args.probeTexelsIndirectCommand.data = probeTexelsCommand;
}
