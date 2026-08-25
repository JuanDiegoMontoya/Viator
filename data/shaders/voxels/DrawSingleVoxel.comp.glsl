#include "../Resources.h.glsl"
#include "Voxels.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(DrawSingleVoxelParams)
{
  FVOG_MAT4 view_from_clip;
  FVOG_MAT4 world_from_view;
  FVOG_VEC3 cameraPos;
  FVOG_UINT32 samples;
  FVOG_SHARED Image2D outRadianceSDR;

  voxel_t voxel;
  FVOG_UINT32 voxelDataBufferIdx;     // Subgrids
  FVOG_UINT32 voxelMaterialBufferIdx; // Materials
  FVOG_FLOAT time;
};

FVOG_DECLARE_ARGUMENTS(DrawSingleVoxelArgs)
{
  DrawSingleVoxelParams pc;
};

#ifndef __cplusplus

#include "../Math.h.glsl"
#include "../Color.h.glsl"

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 outResolution = imageSize(pc.outRadianceSDR);

  if (any(greaterThanEqual(gid, outResolution)))
  {
    return;
  }

  if (pc.voxel == 0)
  {
    return;
  }

  g_voxels.bufferIdx         = pc.voxelDataBufferIdx;
  g_voxels.materialBufferIdx = pc.voxelMaterialBufferIdx;
  g_voxels.time              = pc.time;
  GpuVoxelMaterial material  = voxelMaterialsBuffers[pc.voxelMaterialBufferIdx].materials[pc.voxel];

  vec4 sumRadianceSdr = vec4(0);
  for (int i = 0; i < pc.samples; i++)
  {
    const vec2 xi = Hammersley(i, pc.samples);
    const vec2 uv = (gid + xi - 0.5) / outResolution;

    const mat4 world_from_clip = pc.world_from_view * pc.view_from_clip;
    const vec3 rayPos = UnprojectUV_ZO(0.0, uv, world_from_clip);
    const vec3 rayEnd = UnprojectUV_ZO(1.0, uv, world_from_clip);
    const vec3 rayDir = normalize(rayEnd - rayPos);

    float t;
    vec3 normal;
    if (RayAABBIntersect2(vec3(0.5), vec3(0), rayPos, rayDir, t, normal))
    {
      const vec3 hitPos = rayPos + rayDir * t;
      
      HitSurfaceParameters hit = HitSurfaceParameters_init();
      hit.voxel = pc.voxel;
      hit.voxelPosition = ivec3(0);

      const vec3 uvw = hitPos + 0.5;
      const vec2 texCoords = vx_GetTexCoords(normal, uvw);
      hit.positionWorld = uvw;
      hit.flatNormalWorld = normal;
      hit.texCoords = texCoords;

      if (bool(material.voxelFlags & VOXEL_IS_SUBGRID))
      {
        vx_InitialDDAState init;
        init.deltaDist = 1.0 / abs(rayDir);
        init.S         = bvec3(step(0.0, rayDir));
        init.stepDir   = i8vec3(2 * i8vec3(init.S) - 1);
        
        vec3 sideDist = (vec3(init.S) - init.stepDir * fract(hit.positionWorld)) * init.deltaDist;
        bvec3 cases   = bvec3(sideDist);
        bvec4 conds = lessThanEqual(sideDist.xxyy, sideDist.yzzx);
        cases.x = conds.x && conds.y;
        cases.y = (!cases.x) && conds.z && conds.w;
        cases.z = (!cases.x) && (!cases.y);

        const uint subGridIndex = vx_GetSubGridIndex(material, ivec3(0));
        if (!vx_TraceRaySubGrid(uvw * SUBGRIDS[subGridIndex].dimensions, rayDir, subGridIndex, init, cases, t, 100, hit, TRANSLUCENCY_MODE_ALL))
        {
          if (hit.hitTranslucent)
          {
            const vec3 albedo = vec3(0.2, 0.6, 0.9);
            sumRadianceSdr += vec4(albedo * (1 - hit.transmission) * 2, 1.0);
          }
          continue;
        }
      }

      const vec3 albedo = GetHitAlbedo(hit);
      const float NoL = max(0, dot(hit.flatNormalWorld, normalize(vec3(1, 3, 2))));
      sumRadianceSdr += vec4(albedo * hit.transmission * NoL * 3, 1.0);
    }
  }

  const vec4 result = sumRadianceSdr / pc.samples;
  imageStore(pc.outRadianceSDR, gid, vec4(color_sRGB_OETF(result.rgb), result.a));
}

#endif // !__cplusplus