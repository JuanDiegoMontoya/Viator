#include "ShadeDeferred.shared.h"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"
#include "../Config.shared.h"

#define uniforms perFrameUniformsBuffers[uniformBufferIndex]

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, imageSize(sceneColor))))
  {
    return;
  }

  vx_Init(voxels);
  
  const vec2 uv = (vec2(gid) + 0.5) / imageSize(sceneColor);
  
  const float depth = texelFetch(gBuffer.gDepth, gid, 0).x;
  const vec3 positionWorld = UnprojectUV_ZO(depth, uv, uniforms.invViewProj);

  uint valueToWrite = 0;

  const vec3 rayDir = CreateRay(uv, uniforms.invProj, uniforms.invView);
  const vec3 rayPos = positionWorld;
  
  const float effectDistance = 12 + 2 * MM_Hash2(uv);
  const float tMax = effectDistance - dot(rayDir, positionWorld - uniforms.cameraPos.xyz);
  // This shader will be invoked with a different voxel material buffer, so any hit will be a specially marked material.
  HitSurfaceParameters hit;
  //if (vx_TraceRayMultiLevel(rayPos, rayDir, tMax, hit))
  if (vx_TraceRaySimple(rayPos, rayDir, tMax, hit))
  {
    if (distance(positionWorld, hit.positionWorld) > 1e-3)
    {
      valueToWrite = 1;
    }
    else
    {
      valueToWrite = 2;
    }
  }

  imageStore(gBuffer.gSpecial, gid, uvec4(valueToWrite));
}
