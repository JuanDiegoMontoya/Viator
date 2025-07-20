#include "../../Resources.h.glsl"
#include "../../voxels/Voxels.h.glsl"

#ifndef __cplusplus
#include "../../Math.h.glsl"
#include "../../Utility.h.glsl"
#include "../../Config.shared.h"
#include "../../Hash.h.glsl"
#else
using namespace shared;
#endif

FVOG_DECLARE_ARGUMENTS(RtaoArguments)
{
  Voxels voxels;
  Texture2D gDepth;
  Texture2D gNormal;
  Image2D outputAo;
  FVOG_UINT32 numRays;
  FVOG_FLOAT rayLength;
  FVOG_UINT32 frameNumber;
};

#ifndef __cplusplus

layout(local_size_x = 8, local_size_y = 8) in;

void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, imageSize(outputAo))))
  {
    return;
  }

  vx_Init(voxels);

  const vec2 uv = (vec2(gid) + 0.5) / imageSize(outputAo);

  const float depth = texelFetch(gDepth, gid, 0).x;

  if (depth == FAR_DEPTH)
  {
    return;
  }

  const vec3 normal = texelFetch(gNormal, gid, 0).xyz;

  const vec3 rayOrigin = UnprojectUV_ZO(depth, uv, v_globalUniforms.invViewProj);

  uint randState = PCG_Hash(frameNumber + PCG_Hash(gid.y + PCG_Hash(gid.x)));
  vec2 noise = vec2(PCG_RandFloat(randState, 0, 1), PCG_RandFloat(randState, 0, 1));

  float visibility = 0;
  for (uint i = 0; i < numRays; i++)
  {
    const vec2 xi = fract(noise + Hammersley(i, numRays));
    const vec3 rayDir = map_to_unit_hemisphere_cosine_weighted(xi, normal);

    // Miss, increase visibility
    HitSurfaceParameters hit;
    if (!vx_TraceRayMultiLevel(rayOrigin + normal * 1e-3, rayDir, 4, hit) || distance(hit.positionWorld, rayOrigin) > rayLength)
    {
      // cos_theta * lambertian(1) / pdf
      // cosine weighted sampling pdf = cos_theta / pi
      // lambertian = c / pi
      // The terms cancel out so we're left with only visibility, which is handled by this branch.
      visibility += 1.0;
    }
  }

  if (numRays == 0)
  {
    imageStore(outputAo, gid, vec4(1));
    return;
  }

  const float ao = visibility / numRays;
  imageStore(outputAo, gid, vec4(ao));
}

#endif // !__cplusplus
