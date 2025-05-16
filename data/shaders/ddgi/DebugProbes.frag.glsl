#include "DebugProbesCommon.h.glsl"

layout(location = 0) in vec3 v_normal;
layout(location = 1) flat in int v_probeIndex;

layout(location = 0) out vec4 o_sceneColor; // Luminance/"radiance"

void main()
{
  ivec2 probeImageSize = imageSize(args.ddgi.packedProbeRadiance);
  ivec2 probeGridResolution = args.ddgi.gridInfo.probeRadianceResolution;
  Texture2D tex = args.ddgi.packedProbeRadianceTex;
  if (args.debugMode == 2)
  {
    probeImageSize = imageSize(args.ddgi.packedProbeIrradiance);
    probeGridResolution = args.ddgi.gridInfo.probeIrradianceResolution;
    tex = args.ddgi.packedProbeIrradianceTex;
  }

  // TODO: Select between radiance vs irradiance probe sizes based on debug mode.
  const ivec2 probeGridSize2d = probeImageSize / probeGridResolution;
  const ivec2 texelOffset = GetProbeTexelOffset(v_probeIndex, probeImageSize, probeGridResolution);

  const vec2 uvOffset = vec2(texelOffset) / probeImageSize;
  const vec2 uv = ProbeDirectionToUv(normalize(v_normal), v_probeIndex, probeImageSize, probeGridResolution);
  const ivec2 texel = ivec2(uv * (2 + probeGridResolution));

  //o_sceneColor = vec4(v_normal * .5 + .5, 1);
  //o_sceneColor = vec4(imageLoad(args.ddgi.packedProbeRadiance, texelOffset + texel).rgb, 1);
  //o_sceneColor = vec4(imageLoad(args.ddgi.packedProbeRadiance, ivec2((uv + uvOffset) * imageSize(args.ddgi.packedProbeRadiance))).rgb, 1);
  o_sceneColor = vec4(textureLod(tex, args.samplerr, uvOffset + uv, 0).rgb, 1);
}
