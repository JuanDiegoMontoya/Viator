#include "RayTracedVoxelsShadowCommon.h.glsl"
#include "../Resources.h.glsl"
#include "Voxels.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(RayTracedVoxelsShadowArgs)
{
  Voxels voxels;
  CascadedShadowMapInfoPtr shadow;
};

FVOG_DECLARE_ARGUMENTS(RTVSMPushConstants)
{
  RayTracedVoxelsShadowArgs args;
  FVOG_UINT32 cascadeIndex;
};

#ifndef __cplusplus
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Config.shared.h"

#include "GBuffer.h.glsl"

layout(location = 0) in vec2 uv;

#define uniforms perFrameUniformsBuffers[uniformBufferIndex]

void main()
{
  vx_Init(args.voxels);

  const vec3 rayEnd = UnprojectUV_ZO(0.5, uv, args.shadow.cascades[cascadeIndex].world_from_clip);
  const vec3 rayPos = UnprojectUV_ZO(0.0, uv, args.shadow.cascades[cascadeIndex].world_from_clip);
  const vec3 rayDir = normalize(rayEnd - rayPos);

  const ivec2 gid = ivec2(gl_FragCoord.xy);

  HitSurfaceParameters hit;
  if (vx_TraceRayMultiLevel(rayPos, rayDir, 1024, hit, TRANSLUCENCY_MODE_ALL))
  {
    const vec4 posClip = args.shadow.cascades[cascadeIndex].clip_from_world * vec4(hit.positionWorld, 1.0);
    gl_FragDepth = posClip.z / posClip.w;
  }
  else
  {
    gl_FragDepth = 1.0;
  }
}
#endif // !__cplusplus