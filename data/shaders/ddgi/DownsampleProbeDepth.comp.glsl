#include "ProbeCommon.shared.h"

layout(local_size_x = 128) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);
  const int cascade = int(gl_GlobalInvocationID.z);

  const int numProbes = args.gridResolution.x * args.gridResolution.y * args.gridResolution.z;
  const int numTexels = args.probeDepthMomentsResolution.x * args.probeDepthMomentsResolution.y;
  const int probeIndex = gid / numTexels;

  if (probeIndex >= numProbes)
  {
    return;
  }

  // UV is calculated with depth moment sizes.
  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.probeDepthMomentsResolution);
  const vec2 sampleUv = (vec2(texelCoord) + 0.5) / args.probeDepthMomentsResolution;
  const vec2 babySampleUv = sampleUv / (imageSize(args.packedProbeRawDepth).xy / args.probeRadianceResolution);
  
  const ivec2 rawDepthTexelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeRawDepth).xy, args.probeRadianceResolution);
  const vec2 rawDepthUvOffset = vec2(rawDepthTexelOffset) / imageSize(args.packedProbeRadiance).xy;
  const float depth = textureLod(args.packedProbeRawDepthTex, args.linearSampler, vec3(rawDepthUvOffset + babySampleUv, cascade), 0).x;

  WriteToProbeWithBorder(args.packedProbeDepthMoments, cascade, probeIndex, args.probeDepthMomentsResolution, texelCoord, vec4(depth, depth * depth, 0, 0));
}