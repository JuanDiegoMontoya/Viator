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

float DepthWeight(float zVS_0, float zVS_1, float phi)
{
  return exp(-abs(zVS_0 - zVS_1) / phi);
}

bool DepthAwareBilerp(out vec4 value, Texture2D srcImage, Texture2D gDepth, float cDepthVS, vec2 reprojectedUV, vec2 jitterUV, int minValid)
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
      if (DepthWeight(cDepthVS, oDepthVS, 50) < 0.75)
      {
        continue;
      }

      validCount++;
      valid[x][y] = 1.0;
      colors[x][y] = texelFetch(srcImage, pos, 0);
    }
  }

  const vec2 weight = fract(reprojectedUV.xy * srcResolution - 0.5);

  // Use weighted bilinear filter if any samples are valid.
  if (validCount >= minValid)
  {
    const float factor = max(1e-3, Bilerp(valid[0][0], valid[0][1], valid[1][0], valid[1][1], weight));
    const vec4 prevColor = Bilerp(colors[0][0], colors[0][1], colors[1][0], colors[1][1], weight) / factor;

    value = prevColor;
    return true;
  }

  value = vec4(0);
  return false;
}


// Find valid samples in neighborhood using depth weight.
vec4 BilateralUpscale(Texture2D srcImage, Texture2D gDepth, ivec2 gid, float cDepthVS, ivec2 outResolution, vec2 jitterUV, vec2 motionUV)
{
  const ivec2 sourceDim = textureSize(srcImage, 0);
  const vec2 outToSrcRatio = vec2(sourceDim) / outResolution;
  const vec2 srcToOutRatio = 1.0 / outToSrcRatio;

  const vec2 dstUv = (gid + 0.5) / outResolution + motionUV;
  const vec2 sourceUv = dstUv + jitterUV;
  const ivec2 sourcePos = ivec2(sourceUv * sourceDim);

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

  return textureLod(srcImage, pc.linearSampler, (sourcePos + 0.5) / sourceDim, 0);
}

// Selects the vector with the greater length.
vec2 maxLen(vec2 a, vec2 b)
{
  if (length(a) > length(b))
  {
    return a;
  }
  return b;
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

  const ivec2 srcResolution = textureSize(pc.inLowResCloudRadianceTransmittance, 0);
  const vec2 srcToOutRatio = vec2(outResolution) / srcResolution;
  const vec2 outToSrcRatio = 1.0 / srcToOutRatio;

  const float depthVS = InfRevZ_To_ViewZ(texelFetch(pc.inHighResDepth, gid, 0).x, pc.zNear);

  const vec2 uvForLowRes = uv + pc.jitterUV;
  const vec2 motionUV = textureLod(pc.inLowResCloudMotionVectors, pc.linearSampler, uvForLowRes, 0).xy;
  const vec2 uvForPrev = uv + motionUV;

  float historyWeight = 0.9;
  
  if (true)
  {
    // Preserve high frequency signal by reducing history weight when the nearest low res sample coincides with the current output sample.
    const ivec2 nearestLowResSample  = ivec2(gid * outToSrcRatio);
    const vec2 nearestLowResSampleUV = (nearestLowResSample + 0.5) / srcResolution - pc.jitterUV;
    const vec2 nearestLowResSampleSS = nearestLowResSampleUV * outResolution;
    const float distToLowResSample   = distance(nearestLowResSampleSS, uv * outResolution);
    historyWeight = clamp(smoothstep(0.02 * srcToOutRatio.x, 0.125 * srcToOutRatio.x, distToLowResSample), 0.5, 0.95);
  }

  vec4 currentSample;
  if (!DepthAwareBilerp(currentSample, pc.inLowResCloudRadianceTransmittance, pc.inHighResDepth, depthVS, uvForLowRes, pc.jitterUV, 1))
  {
    currentSample = BilateralUpscale(pc.inLowResCloudRadianceTransmittance, pc.inHighResDepth, gid, depthVS, outResolution, pc.jitterUV, vec2(0));
  }

  vec4 historySample;
  // Require four valid samples to avoid certain artifacts, which isn't ideal.
  if (!DepthAwareBilerp(historySample, pc.inOldCloudRadianceTransmittance, pc.inHighResDepthPrev, depthVS, uvForPrev, vec2(0), 4))
  {
    //historySample = BilateralUpscale(pc.inOldCloudRadianceTransmittance, pc.inHighResDepthPrev, gid, depthVS, outResolution, vec2(0), motionUV);
    historyWeight = 0.0;
  }

  // Neighborhood clamping
  if (historyWeight > 0)
  {
    float minLum = 1e30;
    float maxLum = -1e30;
    float minTrans = 1e30;
    float maxTrans = -1e30;
    const int radius = 1;
    for (int y = -radius; y <= radius; y++)
    for (int x = -radius; x <= radius; x++)
    {
      const vec2 offset = vec2(x, y) / srcResolution;
      const vec2 sampleUv = uvForLowRes + offset;
      const vec4 scatteringTransmittance = textureLod(pc.inLowResCloudRadianceTransmittance, gNearestClampSampler, sampleUv, 0);
      const float lum = Luminance(scatteringTransmittance.rgb);
      minLum = min(minLum, lum);
      maxLum = max(maxLum, lum);
      minTrans = min(minTrans, scatteringTransmittance.a);
      maxTrans = max(maxTrans, scatteringTransmittance.a);
    }
    const float historyLum = max(1e-3, Luminance(historySample.rgb));
    if (historyLum < minLum)
    {
      historySample.rgb = historySample.rgb / historyLum * minLum;
    }
    if (historyLum > maxLum)
    {
      historySample.rgb = historySample.rgb / historyLum * maxLum;
    }
    historySample.a = max(minTrans, historySample.a);
    historySample.a = min(maxTrans, historySample.a);
  }

  // Reject history when off-screen.
  historyWeight *= float(all(greaterThanEqual(uvForPrev, vec2(0))) && all(lessThan(uvForPrev, vec2(1))));
  const vec4 blendedSample = mix(currentSample, historySample, historyWeight);

  imageStore(pc.outCloudRadianceTransmittance, gid, blendedSample);
}

#endif // !__cplusplus