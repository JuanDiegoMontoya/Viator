#include "PerPixelPathtracer.shared.h"
#include "../Config.shared.h"

#define uniforms perFrameUniformsBuffers[uniformBufferIndex]

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, imageSize(gIndirectIrradiance))))
  {
    return;
  }

  vx_Init(voxels);
  
  const vec2 uv = (vec2(gid) + 0.5) / imageSize(gIndirectIrradiance);

  const vec3 normal = texelFetch(gNormal, gid, 0).xyz;
  const float depth = texelFetch(gDepth, gid, 0).x;
  const vec3 positionWorld = UnprojectUV_ZO(depth, uv, uniforms.invViewProj);
  vec3 indirectIlluminance = {0, 0, 0};

  if (depth == FAR_DEPTH || normal == vec3(0))
  {
    imageStore(gIndirectIrradiance, gid, vec4(0));
    return;
  }

  indirectIlluminance += TraceIndirectLighting(gid, positionWorld + normal * 1e-3, normal, samples, bounces, noiseTexture);

  imageStore(gIndirectIrradiance, gid, vec4(indirectIlluminance, 0));
}
