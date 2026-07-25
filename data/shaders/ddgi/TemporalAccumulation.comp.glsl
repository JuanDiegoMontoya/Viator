#include "ProbeCommon.shared.h"
#include "../sky/SkyUtil.h.glsl"
#include "../Color.h.glsl"

#define uniforms perFrameUniformsBuffers[args.globalUniformsIndex]

layout(local_size_x = 128, local_size_y = 1) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridResolution.x * args.gridResolution.y * args.gridResolution.z;
  const int numTexels = args.probeRadianceResolution.x * args.probeRadianceResolution.y;
  const int probeIndex = gid / numTexels;
  const int texelIndex = gid % numTexels;

  if (probeIndex >= numProbes)
  {
    return;
  }

  vx_Init(args.voxels);

  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.probeRadianceResolution);
  
  const int stableProbeIndex = ProbeIndexToStableIndex(probeIndex, cascade, args);
  
  const ProbeData probeData = probeInfosBuffers(args.gridInfo[cascade].probeInfosIndex).data[stableProbeIndex];
  const float validity = probeData.validity;
  const float age = probeData.age;
  //const float alpha = max(0.01, 1.0 / float(age));
  const float alpha = args.minTemporalAlpha;

  const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, imageSize(args.packedProbeRadiance).xy, args.probeRadianceResolution);
  const vec3 radianceRaw = imageLoad(args.packedProbeRadianceRaw, ivec3(texelOffset + texelCoord, cascade)).rgb;
  const vec3 oldRadiance = imageLoad(args.packedProbeRadiance, ivec3(texelOffset + texelCoord, cascade)).rgb;

  const vec3 newRadiance = mix(oldRadiance, radianceRaw, alpha);
  WriteToProbeWithBorder(args.packedProbeRadiance, cascade, stableProbeIndex, args.probeRadianceResolution, texelCoord, vec4(newRadiance, 0));
}
