#include "RayMarchedClouds.shared.h"

FVOG_DECLARE_ARGUMENTS(RayMarchedCloudsUpscalePushConstants)
{
  UpscaleCloudsGpuParams pc;
};

#ifndef __cplusplus

#include "../../Math.h.glsl"
#define KERNEL_3x3
#include "../../denoising/spatial/Kernels.h.glsl"
#include "../../denoising/spatial/Common.h.glsl"


// https://stackoverflow.com/questions/13501081/efficient-bicubic-filtering-code-in-glsl
vec4 cubic(float v)
{
  vec4 n  = vec4(1.0, 2.0, 3.0, 4.0) - v;
  vec4 s  = n * n * n;
  float x = s.x;
  float y = s.y - 4.0 * s.x;
  float z = s.z - 4.0 * s.y + 6.0 * s.x;
  float w = 6.0 - x - y - z;
  return vec4(x, y, z, w) * (1.0 / 6.0);
}

vec4 textureBicubic(Texture2D texture, Sampler linear, vec2 texCoords, int)
{
  vec2 texSize    = textureSize(texture, 0);
  vec2 invTexSize = 1.0 / texSize;

  texCoords = texCoords * texSize - 0.5;

  vec2 fxy = fract(texCoords);
  texCoords -= fxy;

  vec4 xcubic = cubic(fxy.x);
  vec4 ycubic = cubic(fxy.y);

  vec4 c = texCoords.xxyy + vec2(-0.5, +1.5).xyxy;

  vec4 s      = vec4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
  vec4 offset = c + vec4(xcubic.yw, ycubic.yw) / s;

  offset *= invTexSize.xxyy;

  vec4 sample0 = textureLod(texture, linear, offset.xz, 0);
  vec4 sample1 = textureLod(texture, linear, offset.yz, 0);
  vec4 sample2 = textureLod(texture, linear, offset.xw, 0);
  vec4 sample3 = textureLod(texture, linear, offset.yw, 0);

  float sx = s.x / (s.x + s.y);
  float sy = s.z / (s.z + s.w);

  return mix(mix(sample3, sample2, sx), mix(sample1, sample0, sx), sy);
}



float DepthWeight(float zVS_0, float zVS_1, float phi)
{
  return exp(-abs(zVS_0 - zVS_1) / phi);
}


bool DepthAwareBilerp(out vec4 value, Texture2D srcImage, Texture2D gDepth, float cDepthVS, vec2 reprojectedUV, vec2 jitterUV)
{
  const ivec2 srcResolution   = textureSize(srcImage, 0);
  const ivec2 depthResolution = textureSize(gDepth, 0);
  const vec2 srcToDepthRatio  = vec2(depthResolution) / srcResolution;
  const ivec2 bottomLeftPos   = ivec2(reprojectedUV.xy * srcResolution - 0.5);

  vec4 colors[2][2] = vec4[2][2](vec4[2](vec4(0), vec4(0)), vec4[2](vec4(0), vec4(0)));
  float valid[2][2] = float[2][2](float[2](0, 0), float[2](0, 0));
  int validCount    = 0;

  for (int y = 0; y <= 1; y++)
  {
    for (int x = 0; x <= 1; x++)
    {
      const ivec2 pos = bottomLeftPos + ivec2(x, y);
      if (any(lessThan(pos, ivec2(0))) || any(greaterThanEqual(pos, srcResolution)))
      {
        continue;
      }

      // The jittered low res position doesn't always correspond to an exact depth sample, so maybe bilerp is in order.
      const ivec2 depthSamplePos = ivec2((pos + 0.5 - jitterUV * srcResolution) * srcToDepthRatio);
      if (any(lessThan(depthSamplePos, ivec2(0))) || any(greaterThanEqual(depthSamplePos, depthResolution)))
      {
        continue;
      }

      const float oDepthVS = InfRevZ_To_ViewZ(texelFetch(gDepth, depthSamplePos, 0).x, pc.zNear);
      if (DepthWeight(cDepthVS, oDepthVS, 1) < 0.75)
      {
        continue;
      }

      validCount++;
      valid[x][y] = 1.0;
      colors[x][y] = texelFetch(srcImage, pos, 0);
    }
  }

  vec2 weight = fract(reprojectedUV.xy * srcResolution - 0.5);

  // Requiring at least 3 valid samples (instead of 1) is a hack to deal with a jitter bug (no pun intended)
  if (validCount > 0)
  {
    // Use weighted bilinear filter if any of its samples are valid
    float factor = max(0.01, Bilerp(valid[0][0], valid[0][1], valid[1][0], valid[1][1], weight));
    vec4 prevColor = Bilerp(colors[0][0], colors[0][1], colors[1][0], colors[1][1], weight) / factor;

    value = prevColor;
    return true;
  }

  value = vec4(0);
  return false;
}


// Find valid samples in neighborhood using depth weight.
vec4 BilateralUpscale(Texture2D srcImage, Texture2D gDepth, ivec2 gid, float cDepthVS, ivec2 outResolution, vec2 jitterUV)
{
  const ivec2 sourceDim = textureSize(srcImage, 0);
  const vec2 outToSrcRatio = vec2(sourceDim) / outResolution;
  const vec2 srcToOutRatio = 1.0 / outToSrcRatio;

  const vec2 dstUv = (gid + 0.5) / outResolution;
  const vec2 sourceUv = dstUv + jitterUV;
  const ivec2 sourcePos = ivec2(sourceUv * sourceDim + 0.);

  vec4 accumValue = vec4(0);
  float accumWeight = 0;
  for (int col = 0; col < kWidth; col++)
  {
    for (int row = 0; row < kWidth; row++)
    {
      const ivec2 offset = ivec2(row - kRadius, col - kRadius);
      
      const ivec2 posSrc = sourcePos + offset;
      const vec2 posSrcUv = (posSrc + 0.5) / sourceDim;
      const ivec2 posDst = ivec2((posSrcUv - jitterUV) * outResolution);

      if (any(greaterThanEqual(posSrc, sourceDim)) || any(lessThan(posSrc, ivec2(0))))
      {
        continue;
      }

      if (any(greaterThanEqual(posDst, outResolution)) || any(lessThan(posDst, ivec2(0))))
      {
        continue;
      }

      const float kernelWeight = kernel[row][col];

      const vec4 oImageSample = texelFetch(srcImage, posSrc, 0);

      // TODO: If jitterUV is (0, 0), then occasionally we will sample the wrong location here, which causes some edges to render incorrectly.
      // I suspect the error is in RenderRayMarchedClouds.comp.glsl, not here.
      const float oDepthVS = InfRevZ_To_ViewZ(texelFetch(gDepth, posDst, 0).x, pc.zNear);

      const float weight = DepthWeight(cDepthVS, oDepthVS, 1);
  
      accumValue  += oImageSample * weight * kernelWeight;
      accumWeight += weight * kernelWeight;
    }
  }

  if (accumWeight >= 0.0001)
  {
    return accumValue / accumWeight;
  }

  //return vec4(500, 0, 500, 0);
  // Nearest-neighbor fallback.
  //return texelFetch(srcImage, sourcePos, 0);
  return textureLod(srcImage, pc.linearSampler, (sourcePos + 0.5) / sourceDim, 0);
}

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 outResolution = imageSize(pc.outCloudRadianceTransmittance);
  const vec2 uv = (gid + 0.5) / outResolution;

  if (any(greaterThanEqual(gid, outResolution)))
  {
    return;
  }

  const float depthVS = InfRevZ_To_ViewZ(texelFetch(pc.inHighResDepth, gid, 0).x, pc.zNear);

  const vec2 uvForLowRes = uv + pc.jitterUV;
  const vec2 motionUV = textureLod(pc.inLowResCloudMotionVectors, pc.linearSampler, uv, 0).xy;
  const vec2 uvForPrev = uv + motionUV;

  // Sample history. Reject depth discontinuities.
  const vec4 historySample = textureLod(pc.inOldCloudRadianceTransmittance, pc.linearSampler, uvForPrev, 0);


  vec4 currentSample;
  //currentSample = textureLod(pc.inLowResCloudRadianceTransmittance, pc.linearSampler, uvForLowRes, 0);
  //currentSample = BilateralUpscale(pc.inLowResCloudRadianceTransmittance, pc.inHighResDepth, gid, depthVS, outResolution, pc.jitterUV);
  //currentSample = texelFetch(pc.inLowResCloudRadianceTransmittance, ivec2(uvForLowRes * textureSize(pc.inLowResCloudRadianceTransmittance, 0)), 0);
  if (!DepthAwareBilerp(currentSample, pc.inLowResCloudRadianceTransmittance, pc.inHighResDepth, depthVS, uvForLowRes, pc.jitterUV))
  {
    //currentSample = textureLod(pc.inLowResCloudRadianceTransmittance, pc.linearSampler, uvForLowRes, 0);
    currentSample = BilateralUpscale(pc.inLowResCloudRadianceTransmittance, pc.inHighResDepth, gid, depthVS, outResolution, pc.jitterUV);
  }

  // Reject history when off-screen.
  const float historyWeight = 0.05 * float(all(greaterThanEqual(uvForPrev, vec2(0))) && all(lessThan(uvForPrev, vec2(1))));
  const vec4 blendedSample = mix(currentSample, historySample, historyWeight);

  imageStore(pc.outCloudRadianceTransmittance, gid, blendedSample);
}

#endif // !__cplusplus