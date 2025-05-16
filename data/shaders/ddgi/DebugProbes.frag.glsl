#include "DebugProbesCommon.h.glsl"

layout(location = 0) in vec3 v_normal;
layout(location = 1) flat in int v_probeIndex;

layout(location = 0) out vec4 o_sceneColor; // Luminance/"radiance"

// If true, black is drawn everywhere except when very close to a texel.
const bool gShrinkTexels = false;
const float gShrunkTexelSize = 0.2;
const bool gUseNearestNeighbor = true;

void main()
{
  ivec3 swizzle = {0, 1, 2};
  float scale = 1;
  ivec2 probeImageSize = imageSize(args.ddgi.packedProbeRadiance);
  ivec2 probeGridResolution = args.ddgi.gridInfo.probeRadianceResolution;
  Texture2D tex = args.ddgi.packedProbeRadianceTex;
  if (args.debugMode == 2) // Irradiance
  {
    probeImageSize = imageSize(args.ddgi.packedProbeIrradiance);
    probeGridResolution = args.ddgi.gridInfo.probeIrradianceResolution;
    tex = args.ddgi.packedProbeIrradianceTex;
  }
  else if (args.debugMode == 3) // Raw depth
  {
    swizzle = ivec3(0, 0, 0);
    scale = 1 / (args.ddgi.gridInfo.baseGridScale * M_SQRT_3);
    probeImageSize = imageSize(args.ddgi.packedProbeRawDepth);
    probeGridResolution = args.ddgi.gridInfo.probeRadianceResolution;
    tex = args.ddgi.packedProbeRawDepthTex;
  }
  else if (args.debugMode == 4) // Depth moments
  {
    scale = 1 / (args.ddgi.gridInfo.baseGridScale * M_SQRT_3);
    probeImageSize = imageSize(args.ddgi.packedProbeDepthMoments);
    probeGridResolution = args.ddgi.gridInfo.probeDepthMomentsResolution;
    tex = args.ddgi.packedProbeDepthMomentsTex;
  }

  const ivec2 texelOffset = GetProbeTexelOffset(v_probeIndex, probeImageSize, probeGridResolution);

  const vec2 uvOffset = vec2(texelOffset) / probeImageSize;
  const vec2 uv = ProbeDirectionToUv(normalize(v_normal), v_probeIndex, probeImageSize, probeGridResolution);
  const vec2 texelPos = (uvOffset + uv) * textureSize(tex, 0);
  
  vec3 sampled;
  if (gUseNearestNeighbor)
  {
    sampled = texelFetch(tex, ivec2(texelPos), 0).rgb * scale;
  }
  else
  {
    sampled = textureLod(tex, args.samplerr, uvOffset + uv, 0).rgb * scale;
  }
  //sampled = vec3(uv * (probeImageSize / probeGridResolution), 0);
  const vec3 swizzled = {sampled[swizzle[0]], sampled[swizzle[1]], sampled[swizzle[2]]};
  if (!gShrinkTexels || length(fract(texelPos)) < gShrunkTexelSize)
  {
    o_sceneColor = vec4(swizzled, 1);
  }
  else
  {
    o_sceneColor = vec4(0, 0, 0, 1);
  }
}
