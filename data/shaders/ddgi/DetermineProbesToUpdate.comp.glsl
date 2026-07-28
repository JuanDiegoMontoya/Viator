#include "ProbeCommon.shared.h"

#define uniforms perFrameUniformsBuffers[args.globalUniformsIndex]

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

  const int priority = probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].priority;
  const float chance = float(priority * args.probeUpdateBudget) / float(args.sumProbePriority);

  uint seed = PCG_Hash(uniforms.frameNumber) ^ PCG_Hash(stableProbeIndex);
  if (PCG_RandFloat(seed) <= chance)
  {
    const uint myIndex = atomicAdd(args.probesToUpdate.size, 1);
    if (myIndex < args.probesToUpdate.capacity)
    {
      args.probesToUpdate.values[myIndex].data = EncodeCascadeAndStableProbeIndex(cascade, stableProbeIndex);
    }

    if (subgroupElect())
    {
      atomicMin(args.probesToUpdate.size, args.probesToUpdate.capacity);
    }
  }
}
