#include "DebugProbesCommon.h.glsl"

layout(location = 0) in vec3 v_normal;
layout(location = 1) flat in int v_probeIndex;

layout(location = 0) out vec4 o_sceneColor; // Luminance/"radiance"

void main()
{
  const ivec2 probeGridSize2d = imageSize(args.ddgi.packedProbeRadiance) / args.ddgi.gridInfo.probeRadianceResolution;
  const ivec2 texelOffset = args.ddgi.gridInfo.probeRadianceResolution * ivec2(
    v_probeIndex % probeGridSize2d.x,
    v_probeIndex / probeGridSize2d.x
  );

  const vec2 uvOffset = vec2(texelOffset) / imageSize(args.ddgi.packedProbeRadiance);
  const vec2 uv = (Vec3ToOct(normalize(v_normal)) * .5 + .5) / (imageSize(args.ddgi.packedProbeRadiance) / args.ddgi.gridInfo.probeRadianceResolution);
  const ivec2 texel = ivec2(uv * args.ddgi.gridInfo.probeRadianceResolution);

  //o_sceneColor = vec4(v_normal * .5 + .5, 1);
  //o_sceneColor = vec4(imageLoad(args.ddgi.packedProbeRadiance, texelOffset + texel).rgb, 1);
  Texture2D tex = args.ddgi.packedProbeRadianceTex;
  if (args.debugMode == 2)
  {
    tex = args.ddgi.packedProbeIrradianceTex;
  }
  o_sceneColor = vec4(textureLod(tex, args.samplerr, uvOffset + uv, 0).rgb, 1);
  //o_sceneColor = vec4(OctToVec3(vec2(1)), 1);
}
