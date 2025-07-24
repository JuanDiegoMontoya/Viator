#include "../../Resources.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(FilterParams)
{
  FVOG_MAT4 proj;
  FVOG_MAT4 invViewProj;
  FVOG_VEC3 viewPos;
  FVOG_FLOAT stepWidth;
  FVOG_IVEC2 targetDim;
  FVOG_FLOAT phiNormal;
  FVOG_FLOAT phiDepth;

  FVOG_SHARED Texture2D rawAmbientOcclusion;
  FVOG_SHARED Texture2D gNormal;
  FVOG_SHARED Texture2D gDepth;
  FVOG_SHARED Image2D upscaledAmbientOcclusion;
};

FVOG_DECLARE_ARGUMENTS(FilterUniforms)
{
  FilterParams uniforms;
};

#ifndef __cplusplus
#include "../../Math.h.glsl"

//#define KERNEL_3x3
#define KERNEL_5x5
#include "../../denoising/spatial/Kernels.h.glsl"
#include "../../denoising/spatial/Common.h.glsl"

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(gid, uniforms.targetDim)))
  {
    return;
  }

  const ivec2 sourceDim = textureSize(uniforms.rawAmbientOcclusion, 0);

  // gbuffer samples taken at center (output texel)
  const vec3 cNormal = texelFetch(uniforms.gNormal, gid, 0).xyz;
  const float cDepth = texelFetch(uniforms.gDepth, gid, 0).x;

  const vec2 uv = (vec2(gid) + 0.5) / uniforms.targetDim;
  const vec3 point = UnprojectUV_ZO(0.1, uv, uniforms.invViewProj);
  const vec3 rayDir = normalize(point - uniforms.viewPos);

  const vec2 ratio = vec2(sourceDim) / uniforms.targetDim;
  const ivec2 sourcePos = ivec2(gid * ratio);

  float accumAmbientOcclusion = 0;
  float accumWeight = 0;

  // 3x3 bilateral filter to find valid samples
  for (int col = 0; col < kWidth; col++)
  {
    for (int row = 0; row < kWidth; row++)
    {
      const ivec2 offset = ivec2(row - kRadius, col - kRadius);
      const ivec2 posSrc = sourcePos + offset;
      const ivec2 posDst = gid + offset;
      
      if (any(greaterThanEqual(posSrc, sourceDim)) || any(lessThan(posSrc, ivec2(0))))
      {
        continue;
      }

      if (any(greaterThanEqual(posDst, uniforms.targetDim)) || any(lessThan(posDst, ivec2(0))))
      {
        continue;
      }

      const float kernelWeight = kernel[row][col];

      const float oAmbientOcclusion = texelFetch(uniforms.rawAmbientOcclusion, posSrc, 0).x;
      const vec3 oNormal = texelFetch(uniforms.gNormal, posDst, 0).xyz;
      const float oDepth = texelFetch(uniforms.gDepth, posDst, 0).x;

      const float normalWeight = NormalWeight(oNormal, cNormal, uniforms.phiNormal);
      const float depthWeight = DepthWeight(oDepth, cDepth, cNormal, rayDir, uniforms.proj, uniforms.phiDepth);
      
      const float weight = normalWeight * depthWeight;
      accumAmbientOcclusion += oAmbientOcclusion * weight * kernelWeight;
      accumWeight += weight * kernelWeight;
    }
  }

  if (accumWeight >= 0.0001)
  {
    imageStore(uniforms.upscaledAmbientOcclusion, gid, vec4(accumAmbientOcclusion / accumWeight, 0, 0, 0));
  }
  else
  {
    float center = texelFetch(uniforms.rawAmbientOcclusion, sourcePos, 0).x;
    imageStore(uniforms.upscaledAmbientOcclusion, gid, vec4(center, 0, 0, 0));
  }
}
#endif // !__cplusplus
