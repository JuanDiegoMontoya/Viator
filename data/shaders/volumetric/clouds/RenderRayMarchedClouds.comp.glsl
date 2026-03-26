#include "RayMarchedClouds.shared.h"

FVOG_DECLARE_ARGUMENTS(RayMarchedCloudsRenderPushConstants)
{
  RayMarchedCloudsRenderGpuParams pc;
};

#ifndef __cplusplus
#include "CloudDensity.h.glsl"
#include "../../Math.h.glsl"
#include "../../Hash.h.glsl"
#include "../../Config.shared.h"
#define VOLUMETRIC_NO_PUSH_CONSTANTS
#include "../Common.h"
#define globalUniforms2 perFrameUniformsBuffers[pc.globalUniformsIndex]

vec3 Quantize(vec3 v, float steps)
{
  return ivec3(v / steps) * steps;
}

float CloudDensityToPoint(vec3 start, vec3 end, int steps, float startOffset)
{
  const vec3 dir = normalize(end - start);
  const float stepSize = distance(end, start) / steps;

  float densityAccum = 0;

  for (float i = startOffset; i < steps; i++)
  {
    const vec3 curPos = start + dir * stepSize * i;
    densityAccum += CloudDensityAtPoint(curPos, globalUniforms2.time) * stepSize;
  }

  return densityAccum;
}

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 renderResolution = imageSize(pc.outRadianceTransmittance);
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const vec2 uv = (gid + 0.5) / renderResolution;

  if (any(lessThan(gid, ivec2(0))) || any(greaterThanEqual(gid, renderResolution)))
  {
    return;
  }

  const float depth = textureLod(pc.gDepth, pc.nearestSampler, uv - pc.jitterUV, 0).x;

  const vec3 rayEnd = UnprojectUV_ZO(depth == FAR_DEPTH ? 0.5 : depth, uv, pc.world_from_clip);
  const vec3 rayOrigin = UnprojectUV_ZO(NEAR_DEPTH, uv, pc.world_from_clip);
  //const vec3 rayOrigin = pc.cameraPosWS;
  const float maxLength = depth == FAR_DEPTH ? 1e30 : length(rayEnd - rayOrigin);
  const vec3 rayDir = normalize(rayEnd - rayOrigin);

  uint seed               = globalUniforms2.frameNumber;
  const ivec2 frameOffset = ivec2(PCG_RandU32(seed), PCG_RandU32(seed));
  const ivec2 noiseSize   = textureSize(globalUniforms2.blueNoise, 0);
  const ivec2 noiseCoord  = (gid + frameOffset) % noiseSize;
  const float jitter      = 0.5 + (texelFetch(globalUniforms2.blueNoise, noiseCoord, 0).x - 0.5);

  float accumDist = 0;
  float accumDensity = 0;
  vec3 accumScattering = vec3(0);
  vec3 prevPos = rayOrigin;
  float transmittance = 1;
  float hitT = 0;

  for (uint i = 0; i < pc.numRayMarchSteps; i++)
  {
    const float t = (i + jitter) * 10.0;
    const vec3 curPos = rayOrigin + rayDir * t;
    const float stepDist = distance(curPos, prevPos);
    accumDist += stepDist;
    if (accumDist > maxLength)
    {
      break;
    }

    {
      const vec3 transmittanceToSun = getTransmittanceAlongRay(globalUniforms2.sky, globalUniforms2.transmittanceLut, globalUniforms2.linearSampler, globalUniforms2.sky.sunDir, curPos);
      const float bottom_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
          curPos * M_TO_KM_SCALE + vec3(0, globalUniforms2.sky.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
          globalUniforms2.sky.sunDir,
          vec3(0.0),
          globalUniforms2.sky.atmosphere_bottom
      );
      const bool view_ray_intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;
      
      vec3 skylight_internal = getAtmosphereAlongRay(globalUniforms2.sky, globalUniforms2.skyViewLut, globalUniforms2.linearSampler, globalUniforms2.sky.sunDir, curPos);
      vec3 sunlight_internal = globalUniforms2.sky.sunColor * globalUniforms2.sky.sunBrightness * transmittanceToSun / solid_angle_mapping_PDF(radians(0.5));
      
      const float sunSelfShadowDist = 50;
      const int sunSelfShadowSteps = 1;
      if (sunSelfShadowSteps > 0)
      {
        const float densityToSun = CloudDensityToPoint(curPos + globalUniforms2.sky.sunDir * sunSelfShadowDist, curPos, sunSelfShadowSteps, jitter);
        float selfShadow = beer(densityToSun);
        selfShadow = SampleCascadedBeerShadowMap(curPos, globalUniforms2.beerShadowMap);
        skylight_internal *= selfShadow;
        sunlight_internal *= selfShadow;
      }
      const vec3 sunlight_total = float(!view_ray_intersects_ground) * sunlight_internal + skylight_internal;

      const float stepDensity = stepDist * CloudDensityAtPoint(curPos, globalUniforms2.time);
      
      accumDensity += stepDensity;
      transmittance = beer(accumDensity);
      vec3 indirect = vec3(0);
      //indirect = SampleAverageLuminance(curPos, globalUniforms2.linearSampler, pc.ddgi);
      const float LoV = dot(-pc.sunDirection, -rayDir);
      const float powderEffect = mix(1, powder(accumDensity), max(0, -LoV));
      const float commonTerm = powderEffect * transmittance * stepDensity;
      const float HACK_MAGIC_FACTOR = 10;
      const float phase = (phaseHG(0.8, LoV) + phaseHG(0.0, LoV)) / 2; // Artistic mixture of phase functions.
      accumScattering += HACK_MAGIC_FACTOR * commonTerm * sunlight_total * phase;
      accumScattering += commonTerm * indirect * uniform_sphere_PDF();

      if (transmittance < 0.9 && hitT == 0)
      {
        hitT = t;
      }

      // Small perf improvement when no basically more light can make it through.
      if (transmittance < 1e-3)
      {
        transmittance = 0;
        break;
      }
    }
    prevPos = curPos;
  }

  if (hitT == 0)
  {
    hitT = 1500;
  }

  // If hitT is too low (i.e. the player is in a cloud), then there will be horrible parallax.
  hitT = max(100, hitT);
  // However, we don't want hitT to go past real geometry.
  hitT = min(hitT, -InfRevZ_To_ViewZ(depth, pc.zNear));

  const vec3 hitPos     = rayOrigin + rayDir * hitT;
  const vec4 posClip    = pc.clip_from_world_unjittered * vec4(hitPos, 1.0);
  const vec4 posClipOld = pc.clip_from_world_old_unjittered * vec4(hitPos, 1.0);
  const vec2 motionUV   = ((posClipOld.xy / posClipOld.w) - (posClip.xy / posClip.w)) * 0.5;

  accumScattering = min(vec3(65500), accumScattering);
  transmittance = min(65500, transmittance);
  imageStore(pc.outRadianceTransmittance, gid, vec4(accumScattering, transmittance));
  imageStore(pc.outMotionVectors, gid, vec4(motionUV, 0, 0));
}
#endif // !__cplusplus