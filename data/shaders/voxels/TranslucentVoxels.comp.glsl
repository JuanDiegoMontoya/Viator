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

  const float translucentHitT = imageLoad(uniforms.gBuffer.gDepthTranslucent, gid).x;

  vx_Init(voxels);
  
  const vec2 uv = (vec2(gid) + 0.5) / imageSize(sceneColor);

  const float rayLength = 1e-3;
  const vec3 rayDir = CreateRay(uv, uniforms.invProj, uniforms.invView);
  const vec3 rayPos = uniforms.cameraPos.xyz + rayDir * (translucentHitT - rayLength);
  
  vec3 albedo = vec3(0);
  vec3 normal = vec3(0);

  HitSurfaceParameters hit;
  if (translucentHitT < 1e10 && vx_TraceRaySimple(rayPos, rayDir, 2 * rayLength, hit, TRANSLUCENCY_MODE_FIRST_TRANSLUCENT_ONLY))
  {
    albedo = GetHitAlbedo(hit);
    normal = hit.flatNormalWorld;
  }

  imageStore(uniforms.gBuffer.gAlbedoTranslucent, gid, vec4(albedo, 0));
  imageStore(uniforms.gBuffer.gNormalTranslucent, gid, vec4(normal, 0));
}
