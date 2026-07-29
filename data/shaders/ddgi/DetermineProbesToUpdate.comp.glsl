#include "ProbeCommon.shared.h"

#define uniforms perFrameUniformsBuffers[args.globalUniformsIndex]

layout(local_size_x = DDGI_WORKGROUP_SIZE) in;

// https://stackoverflow.com/a/27952689
uint hash_combine(uint lhs, uint rhs )
{
  lhs ^= rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
  return lhs;
}

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

  uint seed = hash_combine(PCG_Hash(uniforms.frameNumber), PCG_Hash(stableProbeIndex));
  if (PCG_RandFloat(seed) <= chance)
  {
    const uint myIndex = atomicAdd(args.probesToUpdate.size, 1);
    if (myIndex < args.probesToUpdate.capacity)
    {
      args.probesToUpdate.values[myIndex].data = EncodeCascadeAndProbeIndex(cascade, probeIndex);

      probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].age =
        min(255, 1 + probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex].age);
    }

    if (subgroupElect())
    {
      atomicMin(args.probesToUpdate.size, args.probesToUpdate.capacity);
    }
  }
}
