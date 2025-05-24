#include "ProbeCommon.shared.h"

layout(local_size_x = 128) in;

void main()
{ 
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridInfo[cascade].gridResolution.x * args.gridInfo[cascade].gridResolution.y * args.gridInfo[cascade].gridResolution.z;
  const int numTexels = args.gridInfo[cascade].probeDepthMomentsResolution.x * args.gridInfo[cascade].probeDepthMomentsResolution.y;
  const int probeIndex = gid / numTexels;

  if (probeIndex >= numProbes)
  {
    return;
  }

  // UV is calculated with depth moment sizes.
  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.gridInfo[cascade].probeDepthMomentsResolution);
  const vec2 sampleUv = (vec2(texelCoord) + 0.5) / args.gridInfo[cascade].probeDepthMomentsResolution;
  const vec2 babySampleUv = sampleUv / (imageSize(args.packedProbeRawDepth).xy / args.gridInfo[cascade].probeRadianceResolution);
  
  const ivec2 rawDepthTexelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeRawDepth).xy, args.gridInfo[cascade].probeRadianceResolution);
  const vec2 rawDepthUvOffset = vec2(rawDepthTexelOffset) / imageSize(args.packedProbeRadiance).xy;
  const float depth = textureLod(args.packedProbeRawDepthTex, args.linearSampler, vec3(rawDepthUvOffset + babySampleUv, cascade), 0).x;

  WriteToProbeWithBorder(args.packedProbeDepthMoments, cascade, probeIndex, args.gridInfo[cascade].probeDepthMomentsResolution, texelCoord, vec4(depth, depth * depth, 0, 0));
}