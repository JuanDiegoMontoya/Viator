#include "DebugProbesCommon.h.glsl"

layout(location = 0) in vec3 v_normal;
layout(location = 1) flat in int v_probeIndex;
layout(location = 2) flat in int v_cascade;

layout(location = 0) out vec4 o_sceneColor; // Luminance/"radiance"

// If true, black is drawn everywhere except when very close to a texel.
const bool gShrinkTexels = false;
const float gShrunkTexelSize = 0.2;
const bool gUseNearestNeighbor = true;

void main()
{
  const int stableProbeIndex = ProbeIndexToStableIndex(v_probeIndex, args.ddgi.gridInfo[v_cascade]);
  if (args.debugMode == 5) // Validity
  {
    o_sceneColor.rgb = vec3(min(10, args.ddgi.gridInfo[v_cascade].probes.data[stableProbeIndex].validity / 100));
    o_sceneColor.a = 1;
    return;
  }
  ivec3 swizzle = {0, 1, 2};
  float scale = 1;
  ivec2 probeImageSize = imageSize(args.ddgi.packedProbeRadiance).xy;
  ivec2 probeGridResolution = args.ddgi.gridInfo[v_cascade].probeRadianceResolution;
  Texture2DArray tex = args.ddgi.packedProbeRadianceTex;
  if (args.debugMode == 2) // Irradiance
  {
    probeImageSize = imageSize(args.ddgi.packedProbeIrradiance).xy;
    probeGridResolution = args.ddgi.gridInfo[v_cascade].probeIrradianceResolution;
    tex = args.ddgi.packedProbeIrradianceTex;
  }
  else if (args.debugMode == 3) // Raw depth
  {
    swizzle = ivec3(0, 0, 0);
    scale = 1 / (args.ddgi.gridInfo[v_cascade].baseGridScale * M_SQRT_3);
    probeImageSize = imageSize(args.ddgi.packedProbeRawDepth).xy;
    probeGridResolution = args.ddgi.gridInfo[v_cascade].probeRadianceResolution;
    tex = args.ddgi.packedProbeRawDepthTex;
  }
  else if (args.debugMode == 4) // Depth moments
  {
    scale = 1 / (args.ddgi.gridInfo[v_cascade].baseGridScale * M_SQRT_3);
    probeImageSize = imageSize(args.ddgi.packedProbeDepthMoments).xy;
    probeGridResolution = args.ddgi.gridInfo[v_cascade].probeDepthMomentsResolution;
    tex = args.ddgi.packedProbeDepthMomentsTex;
  }

  const ivec2 texelOffset = GetProbeTexelOffset(stableProbeIndex, probeImageSize, probeGridResolution);

  const vec2 uvOffset = vec2(texelOffset) / probeImageSize;
  const vec2 uv = ProbeDirectionToUv(normalize(v_normal), stableProbeIndex, probeImageSize, probeGridResolution);
  const vec2 texelPos = (uvOffset + uv) * textureSize(tex, 0).xy;
  
  vec3 sampled;
  if (gUseNearestNeighbor)
  {
    sampled = texelFetch(tex, ivec3(texelPos, v_cascade), 0).rgb * scale;
  }
  else
  {
    sampled = textureLod(tex, args.samplerr, vec3(uvOffset + uv, v_cascade), 0).rgb * scale;
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
