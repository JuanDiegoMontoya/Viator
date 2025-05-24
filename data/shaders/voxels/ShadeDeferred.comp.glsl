#include "ShadeDeferred.shared.h"
#include "../GlobalUniforms.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "../Hash.h.glsl"
#include "../Config.shared.h"

#define uniforms perFrameUniformsBuffers[uniformBufferIndex]

// When enabled, probes will be blended in a gamma-2 space to make gradients appear perceptually 
// smoother than a simple photometrically linear blend.
#define PERCEPTUAL_BLEND 1

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

  const vec3 albedo_internal = color_convert_src_to_dst(texelFetch(gAlbedo, gid, 0).rgb, 
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);
  const vec3 normal = normalize(texelFetch(gNormal, gid, 0).xyz);
  const float depth = texelFetch(gDepth, gid, 0).x;
  const vec3 positionWorld = UnprojectUV_ZO(depth, uv, uniforms.invViewProj);
  const vec3 viewDirWS = normalize(positionWorld - uniforms.cameraPos.xyz);

  // Hack for unlit objects to render properly.
  if (normal == vec3(0))
  {
    imageStore(sceneColor, gid, vec4(albedo_internal, 0.0));
    return;
  }

  const vec3 radiance_internal = color_convert_src_to_dst(texelFetch(gRadiance, gid, 0).rgb,
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);

  vec3 irradiance_internal = vec3(0);
  if (giMethod == 1)
  {
    irradiance_internal = color_convert_src_to_dst(texelFetch(gIndirectIlluminance, gid, 0).rgb,
      COLOR_SPACE_sRGB_LINEAR,
      internalColorSpace);
  }
  else if (giMethod == 2)
  {
    vec3 irradiance_internalNoShadow = vec3(0);
    const vec3 posProbeSpacePreMod = ((positionWorld - 0.5) / ddgi.gridInfo.baseGridScale) - ddgi.gridInfo.gridOffset;
    const vec3 posProbeSpace = mod(posProbeSpacePreMod, ddgi.gridInfo.gridResolution);
    const ivec3 minProbe = ivec3(floor(posProbeSpace));
    const ivec3 maxProbe = minProbe + 1;
    float sumWeights = 0;
    float sumWeightsNoShadow = 0;
    // Sample nearest 8 probes and apply trilinear weights.
    if (all(greaterThanEqual(posProbeSpacePreMod, vec3(0))) && all(lessThan(posProbeSpacePreMod, ddgi.gridInfo.gridResolution)))
    {
      //uint rng = PCG_Hash(gid.x + PCG_Hash(gid.y));

      for (int z = minProbe.z; z <= maxProbe.z; z++)
      for (int y = minProbe.y; y <= maxProbe.y; y++)
      for (int x = minProbe.x; x <= maxProbe.x; x++)
      {
        //const ivec3 p = ivec3(round(probeCoord));
        const vec3 probePos = vec3(x, y, z);
        const vec3 probePosWS = (probePos + ddgi.gridInfo.gridOffset) * ddgi.gridInfo.baseGridScale + 0.5;
        const float trilinearWeight = TrilinearWeight(probePos, posProbeSpace);

        // Give less weight to probes that lie below the plane of the shaded point.
        const vec3 dirToProbe = normalize(probePos - posProbeSpace);
        //const float backfaceWeight = max(1e-4, dot(dirToProbe, normal));
        const float backfaceWeight = square(max(1e-4, dot(dirToProbe, normal) * 0.5 + 0.5)) + 0.2; // Wrap shading term

        // Sample probe illuminance and depth moments.
        const int probeIndex = ProbeCoordToIndex(ivec3(probePos), ddgi.gridInfo.gridResolution);

        const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(ddgi.packedProbeIrradiance), ddgi.gridInfo.probeIrradianceResolution);
        const vec2 uvOffset = vec2(texelOffset) / imageSize(ddgi.packedProbeIrradiance);
        const vec2 uv = ProbeDirectionToUv(normal, probeIndex, imageSize(ddgi.packedProbeIrradiance), ddgi.gridInfo.probeIrradianceResolution);
        const vec3 illuminance = textureLod(ddgi.packedProbeIrradianceTex, samplerr, uvOffset + uv, 0).rgb;
        
        float shadowWeight = 1;
        const float normalBias = 0.45 * ddgi.gridInfo.baseGridScale;
        //const vec3 probeToPointBiasedWS = positionWorld - probePosWS + (normal + 3.0 * viewDirWS) * normalBias;
        const vec3 probeToPointBiasedWS = (positionWorld + normal * normalBias) - probePosWS;
        const vec3 dirToProbeBiased = normalize(-probeToPointBiasedWS);
        const float distToProbeWS = length(probeToPointBiasedWS);
        //const float distToProbeWS = length(probePos - posProbeSpace) * ddgi.gridInfo.baseGridScale;

#if 1
        const ivec2 texelOffset2 = GetProbeTexelOffset(probeIndex, imageSize(ddgi.packedProbeDepthMoments), ddgi.gridInfo.probeDepthMomentsResolution);
        const vec2 uvOffset2 = vec2(texelOffset2) / imageSize(ddgi.packedProbeDepthMoments);
        const vec2 uv2 = ProbeDirectionToUv(-dirToProbeBiased, probeIndex, imageSize(ddgi.packedProbeDepthMoments), ddgi.gridInfo.probeDepthMomentsResolution);
        const vec2 depthMoments = textureLod(ddgi.packedProbeDepthMomentsTex, samplerr, uvOffset2 + uv2, 0).xy;
        const float mean = depthMoments.x;
        const float mean2 = depthMoments.y;

        if (distToProbeWS > mean)
        {
          const float variance = abs(mean * mean - mean2);
          shadowWeight = variance / (variance + square(max(distToProbeWS - mean, 0.0)));
        }
#else // Regular shadow test.
        const ivec2 texelOffset2 = GetProbeTexelOffset(probeIndex, imageSize(ddgi.packedProbeRawDepth), ddgi.gridInfo.probeRadianceResolution);
        const vec2 uvOffset2 = vec2(texelOffset2) / imageSize(ddgi.packedProbeRawDepth);
        const vec2 uv2 = ProbeDirectionToUv(-dirToProbeBiased, probeIndex, imageSize(ddgi.packedProbeRawDepth), ddgi.gridInfo.probeRadianceResolution);
        const float rawDepth = textureLod(ddgi.packedProbeRawDepthTex, samplerr, uvOffset2 + uv2, 0).x;

        if (distToProbeWS > rawDepth)
        {
          shadowWeight = 0;
        }
#endif

        float weightNoTrilinear = backfaceWeight * shadowWeight;
        float weightNoTrilinearNoShadow = backfaceWeight;
        
        weightNoTrilinear = max(1e-5, weightNoTrilinear);
        weightNoTrilinearNoShadow = max(1e-5, weightNoTrilinearNoShadow);

        // Since tiny leaks are very visible, we crush tiny weights here while keeping the curve continuous.
        const float threshold = 0.2;
        if (weightNoTrilinear < threshold)
        {
          weightNoTrilinear *= square(weightNoTrilinear) / square(threshold);
        }
        if (weightNoTrilinearNoShadow < threshold)
        {
          weightNoTrilinearNoShadow *= square(weightNoTrilinearNoShadow) / square(threshold);
        }

        const float weight = trilinearWeight * weightNoTrilinear;
        const float weightNoShadow = trilinearWeight * weightNoTrilinearNoShadow;
#if PERCEPTUAL_BLEND
        irradiance_internal += weight * sqrt(albedo_internal * illuminance);
        irradiance_internalNoShadow += weightNoShadow * sqrt(albedo_internal * illuminance);
#else
        irradiance_internal += weight * albedo_internal * illuminance;
        irradiance_internalNoShadow += weightNoShadow * albedo_internal * illuminance;
#endif
        sumWeights += weight;
        sumWeightsNoShadow += weightNoShadow;
      }

      irradiance_internal /= sumWeights;
      irradiance_internalNoShadow /= sumWeightsNoShadow;

#if PERCEPTUAL_BLEND
      irradiance_internal *= irradiance_internal;
      irradiance_internalNoShadow *= irradiance_internalNoShadow;
#endif

      // TODO: Smooth blend (mix + smoothstep) when sumWeights is small.
      if (sumWeights < 1e-4)
      {
        irradiance_internal = irradiance_internalNoShadow;
      }
    }
  }

  // Shadow
  const vec3 sunDir = normalize(vec3(.7, 1, .3));
  const float NoL = max(0, dot(normal, sunDir));
	vec3 sunlight_internal = albedo_internal * NoL * TraceSunRay(positionWorld + normal * 1e-3, sunDir);

  imageStore(sceneColor, gid, vec4(sunlight_internal + radiance_internal + irradiance_internal, 1));
}
