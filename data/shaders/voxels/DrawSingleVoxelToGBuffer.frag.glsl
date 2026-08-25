#include "DrawSingleVoxelToGBuffer.shared.h"

#ifndef __cplusplus
#include "GBuffer.h.glsl"

layout(location = 0) in vec3 i_posWS;
layout(location = 1) in flat int i_instanceIndex;
layout(location = 2) in vec3 i_posOS;

void main()
{
  DrawSingleVoxelInstance instance = pc.instances[i_instanceIndex];
  const mat3 object_from_world_normal = transpose(inverse(mat3(instance.object_from_world)));
  const mat3 world_from_object_normal = transpose(inverse(mat3(instance.world_from_object)));

  const vec3 normalOS = normalize(cross(dFdy(i_posOS), dFdx(i_posOS)));
  vec3 normalWS = normalize(world_from_object_normal * normalOS);
  
  g_voxels.bufferIdx               = pc.voxelDataBufferIdx;
  g_voxels.materialBufferIdx       = pc.voxelMaterialBufferIdx;
  g_voxels.time                    = pc.uniforms.time;
  GpuVoxelMaterial material        = voxelMaterialsBuffers[pc.voxelMaterialBufferIdx].materials[instance.voxel];

  vec3 albedo = {0, 0, 0};
  vec3 radiance = {0, 0, 0};

  HitSurfaceParameters hit = HitSurfaceParameters_init();
  float t = distance(pc.uniforms.cameraPos.xyz, i_posWS);
  
  hit.voxel = instance.voxel;
  hit.voxelPosition = ivec3(0);

  const vec3 uvw = i_posOS + 0.5 + normalOS * 1e-4;
  const vec2 texCoords = vx_GetTexCoords(normalOS, uvw);
  hit.positionWorld = uvw; // Hit position in *object + 0.5* space ([0, 1]), despite the name.
  hit.flatNormalWorld = normalOS;
  hit.texCoords = texCoords;

  if (bool(material.voxelFlags & VOXEL_IS_SUBGRID))
  {
    const vec3 rayOrigin = pc.uniforms.cameraPos.xyz;
    const vec3 rayEnd    = i_posWS;
    const vec3 rayDir    = normalize(object_from_world_normal * normalize(rayEnd - rayOrigin));

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
    if (!vx_TraceRaySubGrid(uvw * SUBGRIDS[subGridIndex].dimensions, rayDir, subGridIndex, init, cases, t, 100, hit, TRANSLUCENCY_MODE_ALL_OPAQUE))
    {
      if (hit.hitTranslucent)
      {
        // TODO: draw in a separate pass or atomically write to translucent images.
      }
      discard;
    }
    
    normalWS = (world_from_object_normal * hit.flatNormalWorld);
  }

  hit.positionWorld = (instance.world_from_object * vec4(hit.positionWorld - 0.5, 1)).xyz;
  const vec4 posClip = pc.uniforms.viewProj * vec4(hit.positionWorld, 1.0);
  gl_FragDepth = posClip.z / posClip.w;

  const vec3 posOSOld = (instance.object_from_world * vec4(hit.positionWorld, 1)).xyz;
  const vec3 posWSOld = (instance.world_from_object_old * vec4(posOSOld, 1)).xyz;
  const vec4 posClipOld = pc.uniforms.oldViewProjUnjittered * vec4(posWSOld, 1.0);
  const vec4 posClip2 = pc.uniforms.viewProjUnjittered * vec4(hit.positionWorld, 1.0);
  const vec2 motion = ((posClipOld.xy / posClipOld.w) - (posClip2.xy / posClip2.w)) * 0.5;
  albedo = GetHitAlbedo(hit);
  radiance += GetHitEmission(hit);

  const float reactive = 0;
  WriteGBuffer(normalWS, normalWS, radiance, motion, reactive);
}

#endif // !__cplusplus