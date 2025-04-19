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

  const uint samples = 1;
  const uint bounces = 2;
  indirectIlluminance += TraceIndirectLighting(gid, positionWorld + normal * 1e-3, normal, samples, bounces, noiseTexture);

  // Technically direct lighting, but since it's noisy it'll be lumped in with the indirect.
  if (g_voxels.numLights > 0)
  {
    uint randState = PCG_Hash(gid.y + PCG_Hash(gid.x));
    // Local light NEE
    const uint lightIndex = PCG_RandU32(randState) % g_voxels.numLights;
    const float lightPdf = 1.0 / g_voxels.numLights;
    GpuLight light = lightsBuffers[g_voxels.lightBufferIdx].lights[lightIndex];

    const float visibility = GetPunctualLightVisibility(positionWorld + normal * 1e-3, lightIndex);
    if (visibility > 0)
    {
      Surface surface;
      surface.albedo = vec3(1); // Albedo modulation happens after denoising, so for shading we just assume it's 1. This is valid as long as the BRDF is isotropic.
      surface.normal = normal;
      surface.position = positionWorld;
      indirectIlluminance += visibility * EvaluatePunctualLightLambert(light, surface, COLOR_SPACE_sRGB_LINEAR) / lightPdf;
    }
  }

  imageStore(gIndirectIrradiance, gid, vec4(indirectIlluminance, 0));
}
