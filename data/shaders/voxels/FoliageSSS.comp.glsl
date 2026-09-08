#include "Voxels.h.glsl"
#include "FoliageSSS.shared.h"

FVOG_DECLARE_BUFFER_REFERENCE_2(RenderFoliageSSSGpuParams)
{
  Voxels voxels;
  FoliageCBSMInfoPtr cbsm;
};

FVOG_DECLARE_ARGUMENTS(RenderFoliageSSSArgs)
{
  RenderFoliageSSSGpuParams pc;
};

#ifndef __cplusplus
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const int cascadeIndex = int(gl_GlobalInvocationID.z);
  const ivec2 outResolution = imageSize(pc.cbsm.meanExtinctionImages).xy;

  if (any(greaterThanEqual(gid, outResolution)))
  {
    return;
  }

  vx_Init(pc.voxels);

  const vec2 uv = (gid + 0.5) / outResolution.xy;

  const vec3 rayPos = UnprojectUV_ZO(0.0, uv, pc.cbsm.cascades[cascadeIndex].world_from_clip);
  const vec3 rayEnd = UnprojectUV_ZO(1.0, uv, pc.cbsm.cascades[cascadeIndex].world_from_clip);
  const vec3 rayDir = normalize(rayEnd - rayPos);
  const float maxT  = pc.cbsm.frustumDepth;

  float opaqueDepth = maxT;
  HitSurfaceParameters hit;
  if (vx_TraceRayMultiLevel(rayPos, rayDir, distance(rayPos, rayEnd), hit, TRANSLUCENCY_MODE_ALL))
  {
    opaqueDepth = distance(rayPos, hit.positionWorld);
  }

  if (hit.hitTranslucent)
  {
    // Transmission at any depth can be reconstructed with exp(-depth * meanExtinction).
    const vec3 meanExtinction = log(max(vec3(1e-4), hit.transmission)) / -max(1e-4, distance(hit.firstTranslucentHitT, hit.lastTranslucentHitT));

    imageStore(pc.cbsm.meanExtinctionImages, ivec3(gid, cascadeIndex), vec4(meanExtinction, 0));
    imageStore(pc.cbsm.volumePositionImages, ivec3(gid, cascadeIndex), vec4(hit.firstTranslucentHitT, hit.lastTranslucentHitT, opaqueDepth, 0));
  }
  else
  {
    imageStore(pc.cbsm.meanExtinctionImages, ivec3(gid, cascadeIndex), vec4(0));
    // It's necessary to store something real here for translucent start/end because it may be linearly interpolated.
    imageStore(pc.cbsm.volumePositionImages, ivec3(gid, cascadeIndex), vec4(0, opaqueDepth, opaqueDepth, 0));
  }
}
#endif // !__cplusplus