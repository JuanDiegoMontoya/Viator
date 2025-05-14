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
  const vec3 normal = texelFetch(gNormal, gid, 0).xyz;
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
    const vec3 probeCoord = (positionWorld - 0.5) / ddgi.gridInfo.baseGridScale;
    const ivec3 minProbe = ivec3(floor(probeCoord));
    const ivec3 maxProbe = minProbe + 1;
    float sumWeights = 0;
    // Sample nearest 8 probes and apply trilinear weights.
    if (all(greaterThanEqual(probeCoord, vec3(0))) && all(lessThan(probeCoord, ddgi.gridInfo.gridResolution)))
    {
      //uint rng = PCG_Hash(gid.x + PCG_Hash(gid.y));

      for (int z = minProbe.z; z <= maxProbe.z; z++)
      for (int y = minProbe.y; y <= maxProbe.y; y++)
      for (int x = minProbe.x; x <= maxProbe.x; x++)
      {
        //const ivec3 p = ivec3(round(probeCoord));
        const vec3 p = vec3(x, y, z);
        //const vec3 p = vec3(minProbe);
        const float weight = TrilinearWeight(p, probeCoord);
        if (weight == 0)
        {
          //continue;
        }
        
        //const vec3 dir = normalize(p - probeCoord);
        const vec3 dir = normal;

        // Sample probe
        const ivec3 sampledProbe = ivec3(p);
        const int probeIndex = (sampledProbe.z * ddgi.gridInfo.gridResolution.x * ddgi.gridInfo.gridResolution.y) + (sampledProbe.y * ddgi.gridInfo.gridResolution.x) + sampledProbe.x;

        const ivec2 probeGridSize2d = imageSize(ddgi.packedProbeRadiance) / ddgi.gridInfo.probeRadianceResolution;
        const ivec2 texelOffset = ddgi.gridInfo.probeRadianceResolution * ivec2(
          probeIndex % probeGridSize2d.x,
          probeIndex / probeGridSize2d.x
        );

        const vec2 uvOffset = vec2(texelOffset) / imageSize(ddgi.packedProbeRadiance);
        const vec2 uv = (Vec3ToOct(normalize(dir)) * .5 + .5) / (imageSize(ddgi.packedProbeRadiance) / ddgi.gridInfo.probeRadianceResolution);
        const ivec2 texel = ivec2(uv * ddgi.gridInfo.probeRadianceResolution);

        const vec3 illuminance = textureLod(ddgi.packedProbeIrradianceTex, samplerr, uvOffset + uv, 0).rgb;
        if (illuminance == vec3(0))
        {
          continue;
        }
        irradiance_internal += albedo_internal * weight * illuminance;
        sumWeights += weight;
      }
      // If an occluded probe contributes nothing, then boost the other probes' contributions.
      irradiance_internal /= sumWeights;
    }
  }

  // Shadow
  const vec3 sunDir = normalize(vec3(.7, 1, .3));
  const float NoL = max(0, dot(normal, sunDir));
	vec3 sunlight_internal = albedo_internal * NoL * TraceSunRay(positionWorld + normal * 1e-3, sunDir);

  imageStore(sceneColor, gid, vec4(sunlight_internal + radiance_internal + irradiance_internal, 1));
}
