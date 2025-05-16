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

  const vec3 albedo_internal = color_convert_src_to_dst(texelFetch(gAlbedo, gid, 0).rgb, 
    COLOR_SPACE_sRGB_LINEAR,
    internalColorSpace);
  const vec3 normal = normalize(texelFetch(gNormal, gid, 0).xyz);
  const float depth = texelFetch(gDepth, gid, 0).x;
  const vec3 positionWorld = UnprojectUV_ZO(depth, uv, uniforms.invViewProj);

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
    const vec3 normalBias = normal * 0;
    const vec3 posProbeSpace = (positionWorld + normalBias - 0.5) / ddgi.gridInfo.baseGridScale;
    const ivec3 minProbe = ivec3(floor(posProbeSpace));
    const ivec3 maxProbe = minProbe + 1;
    float sumWeights = 0;
    // Sample nearest 8 probes and apply trilinear weights.
    if (all(greaterThanEqual(posProbeSpace, vec3(0))) && all(lessThan(posProbeSpace, ddgi.gridInfo.gridResolution)))
    {
      //uint rng = PCG_Hash(gid.x + PCG_Hash(gid.y));

      for (int z = minProbe.z; z <= maxProbe.z; z++)
      for (int y = minProbe.y; y <= maxProbe.y; y++)
      for (int x = minProbe.x; x <= maxProbe.x; x++)
      {
        //const ivec3 p = ivec3(round(probeCoord));
        const vec3 probePos = vec3(x, y, z);
        const float trilinearWeight = TrilinearWeight(probePos, posProbeSpace);

        // Give less weight to probes that lie below the plane of the shaded point.
        const vec3 dirToProbe = normalize(probePos - posProbeSpace);
        const float backfaceWeight = dot(dirToProbe, normal) * 0.5 + 0.5;

        // Sample probe
        const int probeIndex = ProbeCoordToIndex(ivec3(probePos), ddgi.gridInfo.gridResolution);
        const ivec2 texelOffset = GetProbeTexelOffset(probeIndex, imageSize(ddgi.packedProbeIrradiance), ddgi.gridInfo.probeIrradianceResolution);
        const vec2 uvOffset = vec2(texelOffset) / imageSize(ddgi.packedProbeIrradiance);
        const vec2 uv = ProbeDirectionToUv(normal, probeIndex, imageSize(ddgi.packedProbeIrradiance), ddgi.gridInfo.probeIrradianceResolution);
        const vec3 illuminance = textureLod(ddgi.packedProbeIrradianceTex, samplerr, uvOffset + uv, 0).rgb;

        const float weight = max(EPSILON, trilinearWeight * backfaceWeight);
        irradiance_internal += albedo_internal * weight * illuminance;
        sumWeights += weight;
      }

      // If an occluded probe contributes nothing, then boost the other probes' contributions.
      if (sumWeights > 1e-3)
      {
        irradiance_internal /= sumWeights;
      }
    }
  }

  // Shadow
  const vec3 sunDir = normalize(vec3(.7, 1, .3));
  const float NoL = max(0, dot(normal, sunDir));
	vec3 sunlight_internal = albedo_internal * NoL * TraceSunRay(positionWorld + normal * 1e-3, sunDir);

  imageStore(sceneColor, gid, vec4(sunlight_internal + radiance_internal + irradiance_internal, 1));
}
