#include "ProbeCommon.shared.h"

layout(local_size_x = 128) in;

void main()
{ 
  const int gid = int(gl_GlobalInvocationID.x);

  const int numProbes = args.gridInfo.gridResolution.x * args.gridInfo.gridResolution.y * args.gridInfo.gridResolution.z;
  const int numTexels = args.gridInfo.probeDepthMomentsResolution.x * args.gridInfo.probeDepthMomentsResolution.y;
  const int probeIndex = gid / numTexels;

  if (probeIndex >= numProbes)
  {
    return;
  }

  // UV is calculated with depth moment sizes.
  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.gridInfo.probeDepthMomentsResolution);
  const vec2 sampleUv = (vec2(texelCoord) + 0.5) / args.gridInfo.probeDepthMomentsResolution;
  const vec2 babySampleUv = sampleUv / (imageSize(args.packedProbeRawDepth) / args.gridInfo.probeRadianceResolution);
  
  const ivec2 rawDepthTexelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeRawDepth), args.gridInfo.probeRadianceResolution);
  const vec2 rawDepthUvOffset = vec2(rawDepthTexelOffset) / imageSize(args.packedProbeRadiance);
  const float depth = textureLod(args.packedProbeRawDepthTex, args.linearSampler, rawDepthUvOffset + babySampleUv, 0).x;

  WriteToProbeWithBorder(args.packedProbeDepthMoments, probeIndex, args.gridInfo.probeDepthMomentsResolution, texelCoord, vec4(depth, depth * depth, 0, 0));
}