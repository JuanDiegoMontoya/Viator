#include "ProbeCommon.shared.h"

layout(local_size_x = 128) in;

void main()
{
  // Check if this probe is on a newly-uncovered edge. If it is, set its validity to zero.
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridInfo[cascade].gridResolution.x * args.gridInfo[cascade].gridResolution.y * args.gridInfo[cascade].gridResolution.z;
  const int probeIndex = gid;
  
  if (probeIndex >= numProbes)
  {
    return;
  }

  const ivec3 offsetVelocity = args.gridInfo[cascade].gridOffset - args.gridInfo[cascade].oldGridOffset;
  const vec3 probePos = ProbeIndexToCoord(probeIndex, args.gridInfo[cascade].gridResolution);
  const vec3 probePosOldProbeSpace = probePos + offsetVelocity;

  const int stableProbeIndex = ProbeIndexToStableIndex(probeIndex, args.gridInfo[cascade]);

  if (any(lessThan(probePosOldProbeSpace, vec3(0))) || any(greaterThanEqual(probePosOldProbeSpace, args.gridInfo[cascade].gridResolution)))
  {
    probeInfosBuffers[args.gridInfo[cascade].probeInfosIndex].data[stableProbeIndex].validity = 0;
  }

  probeInfosBuffers[args.gridInfo[cascade].probeInfosIndex].data[stableProbeIndex].validity += 1.02;
}
