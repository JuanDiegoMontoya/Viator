#include "RayMarchedClouds.shared.h"
#include "../../sky/SkyParams.shared.h"

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
    densityAccum += CloudDensityAtPoint(curPos, globalUniforms2.weatherParams) * stepSize;
  }

  return densityAccum;
}

int Cloud_GetRayStartsAndEndsForSphereClouds(in vec3 rayOrigin,
  in vec3 rayDirection,
  in WeatherGpuParams weather,
  out float ray1StartT,
  out float ray1EndT,
  out float ray2StartT,
  out float ray2EndT)
{
  const float atmosphereBottom = Cloud_GetAtmosphereBottom(weather.earthSizeFactor);
  const vec3 atmosphereOffset = Cloud_GetAtmosphereOffset(weather.earthSizeFactor);
  
  ray1StartT = ray_sphere_intersect_nearest(
    rayOrigin + atmosphereOffset,
    rayDirection,
    vec3(0),
    atmosphereBottom + weather.cloudBottomAltitude
  );

  ray1EndT = ray_sphere_intersect_nearest(
    rayOrigin + atmosphereOffset,
    rayDirection,
    vec3(0),
    atmosphereBottom + weather.cloudBottomAltitude + weather.cloudHeight
  );

  const float cameraRayToEarth = ray_sphere_intersect_nearest(
    rayOrigin * M_TO_KM_SCALE + vec3(0, globalUniforms2.sky.config.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
    rayDirection,
    vec3(0.0),
    globalUniforms2.sky.config.atmosphere_bottom
  );

  const float altitude = length(rayOrigin + atmosphereOffset);
  const float cloudBottom = atmosphereBottom + weather.cloudBottomAltitude;
  const float cloudTop = cloudBottom + weather.cloudHeight;

  if (altitude < cloudBottom && cameraRayToEarth >= 0)
  {
    return 0;
  }
  // Ray started inside cloud layer, looking down (no endT).
  if (ray1StartT >= 0 && ray1EndT < 0)
  {
    //endT = 1000;
    //startT = 0;
  }
  // Ray started inside cloud layer.
  if (altitude >= cloudBottom && altitude < cloudTop)
  {
    if (ray1StartT >= 0 && ray1EndT < 0) // Looking down
    {
      ray1EndT = ray1StartT;
    }
    ray1StartT = 0;
  }
  // Kill rays that start below cloud layer and look at ground.
  if (altitude < cloudBottom && cameraRayToEarth >= 0)
  {
    ray1EndT = -1;
    //endT = max(10, endT);
  }
  // Inside cloud layer, looking down.
  if (altitude >= cloudBottom && altitude < cloudTop && cameraRayToEarth >= 0)
  {
    if (ray1StartT >= 0)
    {
      ray1EndT = 1000;
    }
  }
  if (altitude > cloudTop)
  {
    // Looking down, swap start and end.
    if (ray1StartT >= 0 && ray1EndT >= 0)
    {
      float temp = ray1StartT;
      ray1StartT = ray1EndT;
      ray1EndT = temp;
    }
    // Ray is "skimming" the cloud layer (intersecting just the top boundary)
    if (ray1StartT < 0 && ray1EndT >= 0)
    {
      const float secondHit = ray_sphere_intersect_nearest(
        rayOrigin + rayDirection * (ray1EndT + 1e-3),
        rayDirection,
        vec3(0),
        atmosphereBottom + weather.cloudBottomAltitude + weather.cloudHeight
      );
      ray1StartT = ray1EndT;
      ray1EndT = secondHit;
      //endT = cameraRayToEarth * 1000 * earthSizeFactor;
    }
  }

  ray2StartT = ray_sphere_intersect_nearest(
    rayOrigin + rayDirection * (ray1EndT + 1e-3),
    rayDirection,
    vec3(0),
    atmosphereBottom + weather.cloudBottomAltitude
  );

  ray2EndT = ray_sphere_intersect_nearest(
    rayOrigin + rayDirection * (ray1EndT + 1e-3),
    rayDirection,
    vec3(0),
    atmosphereBottom + weather.cloudBottomAltitude + weather.cloudHeight
  );

  // Detect situation in which a second ray march will be needed.
  if (cameraRayToEarth < 0 && ray2StartT >= 0 && ray2EndT >= 0)
  {
    return 2;
  }
  return 1;
}

bool Cloud_GetRayStartAndEndForPlaneClouds(in vec3 rayOrigin, in vec3 rayDirection, in WeatherGpuParams weather, out float rayStart, out float rayEnd)
{
  if (abs(rayDirection.y) < 1e-3)
  {
    rayDirection.y = 1e-3;
  }

  const float cloudBottom = weather.cloudBottomAltitude;
  const float cloudTop = cloudBottom + weather.cloudHeight;

  rayStart = (cloudBottom - rayOrigin.y) / rayDirection.y;
  rayEnd = (cloudTop - rayOrigin.y) / rayDirection.y;

  // Outside cloud layer, looking away from it.
  if (rayStart < 0 && rayEnd < 0)
  {
    return false;
  }

  // Inside cloud layer.
  if (rayOrigin.y >= cloudBottom && rayOrigin.y <= cloudTop)
  {
    // Looking down.
    if (rayStart > 0)
    {
      rayEnd = rayStart;
    }
    rayStart = 0;
  }

  // Remaining case: above cloud layer, looking down.
  if (rayOrigin.y >= cloudTop)
  {
    const float temp = rayStart;
    rayStart = rayEnd;
    rayEnd = temp;
  }

  return true;
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

  WeatherGpuParams weather = globalUniforms2.weatherParams;
  float rayStartT[2];
  float rayEndT[2];
  //const int numRays = Cloud_GetRayStartsAndEndsForSphereClouds(rayOrigin, rayDir, weather, rayStartT[0], rayEndT[0], rayStartT[1], rayEndT[1]);
  const int numRays = int(Cloud_GetRayStartAndEndForPlaneClouds(rayOrigin, rayDir, weather, rayStartT[0], rayEndT[0]));

  const float atmosphereBottom = Cloud_GetAtmosphereBottom(weather.earthSizeFactor);
  const vec3 atmosphereOffset = Cloud_GetAtmosphereOffset(weather.earthSizeFactor);

  float transmittance = 1;
  float accumDensity = 0;
  vec3 accumScattering = vec3(0);
  float hitT = 0;
  
  for (int r = 0; r < numRays; r++)
  {
    float actualRayLength = abs(rayEndT[r] - rayStartT[r]);
    // Very long ray steps will overestimate transmittance (which is computed before scattering) 
    // leading to darkening when inside the cloud layer. Simple fix: limit the ray length in that case.
    if (rayOrigin.y > weather.cloudBottomAltitude && rayOrigin.y < weather.cloudBottomAltitude + weather.cloudHeight)
    {
      actualRayLength = min(20000, actualRayLength);
    }
    const uint numSteps = uint(mix(pc.numRayMarchStepsMin, pc.numRayMarchStepsMax, smoothstep(pc.distForMinRayStepCount, pc.distForMaxRayStepCount, actualRayLength)));
    //const uint numSteps = pc.numRayMarchSteps;

    float accumDist = 0;
    vec3 prevPos = rayOrigin;

    for (uint i = 0; i < numSteps; i++)
    {
      // Skew the ray step distribution nearer to the viewer.
      const float alpha = pow((i + jitter) / numSteps, 4);
      const float t = rayStartT[r] + alpha * actualRayLength;
      vec3 curPos = rayOrigin + rayDir * t;
      const vec3 spherical = Cloud_CartesianToSpherical(curPos + atmosphereOffset);
      const float equatorToPoleDistance = 10000000;
      const float angleToCartesianFactor = equatorToPoleDistance * weather.earthSizeFactor / (M_PI / 2);
      //curPos.x = angleToCartesianFactor * spherical.g;
      //curPos.z = angleToCartesianFactor * spherical.b;
      //curPos.y = spherical.r - atmosphereBottom; // Uncomment if using spherical clouds.
      const float stepDist = distance(curPos, prevPos);
      accumDist += stepDist;
      if (accumDist > maxLength)
      {
        break;
      }

      {
        const vec3 transmittanceToSun = Sky_GetTransmittanceAlongRay(globalUniforms2.sky, globalUniforms2.sky.config.sunDir, curPos);
        const float bottom_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
            curPos * M_TO_KM_SCALE + vec3(0, globalUniforms2.sky.config.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
            globalUniforms2.sky.config.sunDir,
            vec3(0.0),
            globalUniforms2.sky.config.atmosphere_bottom
        );
        const bool view_ray_intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;
        
        vec3 skylight_internal = Sky_GetScatteringAlongRay(globalUniforms2.sky, globalUniforms2.sky.config.sunDir, curPos);
        vec3 sunlight_internal = globalUniforms2.sky.config.sunColor * globalUniforms2.sky.config.sunBrightness * transmittanceToSun / solid_angle_mapping_PDF(radians(0.5));
        
        const float sunSelfShadowDist = 100;
        const int sunSelfShadowSteps = 1;
        if (sunSelfShadowSteps > 0)
        {
          const float densityToSun = CloudDensityToPoint(curPos + globalUniforms2.sky.config.sunDir * sunSelfShadowDist, curPos, sunSelfShadowSteps, 0.5);
          float selfShadow = beer(densityToSun);
          selfShadow = SampleCascadedBeerShadowMap(curPos, globalUniforms2.beerShadowMap);
          skylight_internal *= selfShadow;
          sunlight_internal *= selfShadow;
        }
        const vec3 sunlight_total = float(!view_ray_intersects_ground) * sunlight_internal + skylight_internal;

        const float stepDensity = stepDist * CloudDensityAtPoint(curPos, globalUniforms2.weatherParams);
        
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

        if (transmittance < 0.1 && hitT == 0)
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
  }

  // If the ray missed, for the sake of calculating decent motion vectors, guess a distance.
  if (hitT == 0)
  {
    hitT = 1500;
  }

  // If hitT is too low (i.e. the player is in a cloud), then there will be horrible parallax.
  hitT = max(100, hitT);
  // However, we don't want hitT to go past real geometry.
  hitT = min(hitT, -InfRevZ_To_ViewZ(depth, pc.zNear));

  const vec3 hitPos     = rayOrigin + rayDir * hitT;
  const vec3 hitPosOld  = hitPos + vec3(weather.windVelocity.x, 0, weather.windVelocity.y) * globalUniforms2.dt;
  const vec4 posClip    = pc.clip_from_world_unjittered * vec4(hitPos, 1.0);
  const vec4 posClipOld = pc.clip_from_world_old_unjittered * vec4(hitPosOld, 1.0);
  const vec2 motionUV   = ((posClipOld.xy / posClipOld.w) - (posClip.xy / posClip.w)) * 0.5;

  if (hitT < 1e10)
  {
    vec3 trans2;
    vec3 scattering2;
    Sky_GetAerialPerspective(globalUniforms2.sky, hitPos, trans2, scattering2);
    accumScattering = accumScattering * trans2 + scattering2;
  }

  accumScattering = min(vec3(65500), accumScattering);
  transmittance = min(65500, transmittance);
  imageStore(pc.outRadianceTransmittance, gid, vec4(accumScattering, transmittance));
  imageStore(pc.outMotionVectors, gid, vec4(motionUV, 0, 0));
}
#endif // !__cplusplus