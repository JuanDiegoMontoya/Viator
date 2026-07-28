#include "ProbeCommon.shared.h"

layout(local_size_x = DDGI_WORKGROUP_SIZE) in;

void main()
{
  // Check if this probe is on a newly-uncovered edge. If it is, set its validity to zero.
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridResolution.x * args.gridResolution.y * args.gridResolution.z;
  const int probeIndex = gid;
  
  if (probeIndex >= numProbes)
  {
    return;
  }

  const int stableProbeIndex = ProbeIndexToStableIndex(probeIndex, cascade, args);

  int priority = 0;
  priority += max(0, args.probeBaseAgePriority - args.probeAgeFactor * probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].age);
  priority += args.probeFrequencyFactor * probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].queryCount;
  priority = max(priority, args.probeMinPriority);
  priority *= int(probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].validity);
  probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].priority = priority;
  atomicAdd(args.sumProbePriority, priority);

  probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].queryCount = 0;
}
