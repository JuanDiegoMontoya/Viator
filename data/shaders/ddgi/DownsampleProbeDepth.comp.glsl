#include "ProbeCommon.shared.h"

layout(local_size_x = DDGI_WORKGROUP_SIZE) in;

void main()
{
  const int gid = int(gl_GlobalInvocationID.x);

  const int numTexels       = args.probeRadianceResolution.x * args.probeRadianceResolution.y;
  const int probeIndexIndex = gid / numTexels;

  if (probeIndexIndex >= args.probesToUpdate.size)
  {
    return;
  }

  const uint encoded = args.probesToUpdate.values[probeIndexIndex].data;
  int cascade;
  int probeIndex;
  DecodeCascadeAndProbeIndex(encoded, cascade, probeIndex);

  // UV is calculated with depth moment sizes.
  const ivec2 texelCoord = GetWorkTexelCoord(gid, args.probeDepthMomentsResolution);
  const vec2 sampleUv = (vec2(texelCoord) + 0.5) / args.probeDepthMomentsResolution;
  const vec2 babySampleUv = sampleUv / (imageSize(args.packedProbeDepth).xy / args.probeRadianceResolution);
  
  const ivec2 rawDepthTexelOffset = GetProbeTexelOffset(probeIndex, imageSize(args.packedProbeDepth).xy, args.probeRadianceResolution);
  const vec2 rawDepthUvOffset = vec2(rawDepthTexelOffset) / imageSize(args.packedProbeRadiance).xy;
  const float depth = textureLod(args.packedProbeDepthTex, args.linearSampler, vec3(rawDepthUvOffset + babySampleUv, cascade), 0).x;

  WriteToProbeWithBorder(args.packedProbeDepthMoments, cascade, probeIndex, args.probeDepthMomentsResolution, texelCoord, vec4(depth, depth * depth, 0, 0));
}