#include "Common.h"

#include "Frog.h"

vec3 phaseTex(float cosTheta)
{
  // [1, -1] -> [0, 1]
  float u = 1.0 - (cosTheta * .5 + .5);
  
  vec3 intensity = textureLod(uniforms.mieScattering, uniforms.linearSampler, u, 0).rgb;

  // limit intensity (hack)
  //return log2(1.0 + intensity);
  return intensity;// / (1.0 + intensity);
}

vec4 FogAtPoint(vec3 wPos)
{
  const vec3 ogPos = wPos;
  wPos -= vec3(50, 350, 50);
  // ground fog
  vec3 t = vec3(.2, 0.1, .3) * uniforms.time;
  //float d = max((snoise(vec4(wPos * 0.11 + t, t * 1.2)) + 0.5), 0.0);
  //d *= uniforms.groundFogDensity;
  float d = 0.2;

  // Fade out fog if too low or too high.
  d *= (1.0 - smoothstep(0, 100, wPos.y)) * (smoothstep(-15, 0, wPos.y));

  // Fade out fog if too far from center of the world.
  d *= 1.0 - smoothstep(0, 10, distance(abs(wPos.xz), vec2(0)) - 650);
  
  vec3 c = vec3(1, 1, 1); // base color

  const ivec2 wSize = textureSize(uniforms.globalSurfaceHeight, 0);
  //const ivec2 wPos2 = clamp(ivec2(wPos.xz), ivec2(0), wSize - 1);
  const ivec2 wPos2 = ivec2(ogPos.xz);
  if (all(greaterThanEqual(wPos2, ivec2(0))) && all(lessThan(wPos2, wSize)))
  {
    const vec2 uv = (ogPos.xz + 0.5) / (uniforms.voxels.dimensions.xz);
    const float height = textureLod(uniforms.globalSurfaceHeight, uniforms.linearSampler, uv, 0).x;
    const float fogginess = textureLod(uniforms.globalSurfaceFog, uniforms.linearSampler, uv, 0).x;
    d += 3 * (1 - smoothstep(3, 8, abs(height - ogPos.y))) * fogginess;
  }

  const ivec2 wPos3 = ivec2(ogPos);
  const vec3 uv2 = (ogPos + 0.5) / (uniforms.voxels.dimensions);
  if (all(greaterThanEqual(uv2, vec3(0))) && all(lessThan(uv2, vec3(1))))
  {
    const float fogginess = 10 * textureLod(uniforms.globalFog, uniforms.linearSampler, uv2, 0).x;
    d += fogginess;
  }

  for (int i = 0; i < uniforms.fogList.count; i++)
  {
    // TODO: figure out how color mixing works in participating media (probably an average of some sort).
    Vol_FogEmitter emitter = uniforms.fogList.emitters[i];
    const float density = emitter.density * (1 - smoothstep(emitter.radiusInner, emitter.radiusOuter, distance(emitter.position, ogPos)));
    d += density;
    
    /*
    vec3 frogPos = emitter.position;
    frog_sdfRet ret = frog_map(0.125 * (ogPos - frogPos));
    float froge = 1.0 - smoothstep(0.0, 0.05, ret.sdf);
    {
      c = mix(c, frog_idtocol(ret.id), froge);
      d += froge * 105.0;
    }
    */
  }

  d = max(0, d);

  return vec4(c, d * 0.0092);
}

vec4 DensityToLight(vec3 start, vec3 end, int steps)
{
  const vec3 dir = normalize(end - start);
  const float stepSize = distance(end, start) / steps;

  vec3 inScatteringAccum = vec3(0);
  float densityAccum = 0;

  for (float i = 0.5; i < steps; i++)
  {
    const vec3 curPos = start + dir * stepSize * i;

    vec4 cd = FogAtPoint(curPos);
    densityAccum += cd.w * stepSize;
  }

  return vec4(inScatteringAccum, densityAccum);
}

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
void main()
{
  ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);
  ivec3 targetDim = imageSize(uniforms.fogDensityVolumeRW);
  if (any(greaterThanEqual(gid, targetDim)))
  {
    return;
  }
  
  vx_Init(uniforms.voxels);

  vec3 uvw = (vec3(gid) + 0.5) / targetDim;

  // Apply our own curve by squaring the linear depth, then convert to inverted window-space Z and unproject it to get world position.
  float zInv = InvertDepthZO(uvw.z * uvw.z * uvw.z, uniforms.volumeNearPlane, uniforms.volumeFarPlane);
  vec3 wPos = UnprojectUVZO(zInv, uvw.xy, uniforms.invViewProjVolume);
  vec3 wPos2 = UnprojectUVZO(0.5, uvw.xy, uniforms.invViewProjVolume); // ||viewPos-wPos|| has poor precision and causes flickering when used to sample the phase function

  vec4 colorAndDensity = FogAtPoint(wPos);
  vec3 fogColor = colorAndDensity.rgb;
  float fogDensity = colorAndDensity.w;
  vec3 light = vec3(0);
  light = SampleAverageLuminance(wPos, uniforms.linearSampler, uniforms.ddgi);
  
  // Shadow
  //vec3 phase = vec3(phaseHG(0.5, dot(-normalize(uniforms.viewPos - wPos), globalUniforms.sky.sunDir)));
  vec3 phase = phaseTex(dot(-normalize(uniforms.viewPos - wPos2), globalUniforms.sky.sunDir));
  const vec3 transmittanceToSun = getTransmittanceAlongRay(globalUniforms.sky, globalUniforms.transmittanceLut, globalUniforms.linearSampler, globalUniforms.sky.sunDir, uniforms.viewPos);
  
  const float bottom_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
      wPos * M_TO_KM_SCALE + vec3(0, globalUniforms.sky.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
      globalUniforms.sky.sunDir,
      vec3(0.0),
      globalUniforms.sky.atmosphere_bottom
  );

  bool view_ray_intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;
#if 0 // Ray traced shadow
  vec3 sunVisibility = TraceSunRay(wPos, globalUniforms.sky.sunDir);
#else
  float sunVisibility = SampleCascadedShadowMap(wPos, globalUniforms.sunShadowMap);
#endif
  sunVisibility *= SampleCascadedBeerShadowMap(wPos, globalUniforms.beerShadowMap);
  vec3 skylight_internal = fogColor * sunVisibility * getAtmosphereAlongRay(globalUniforms.sky, globalUniforms.skyViewLut, globalUniforms.linearSampler, globalUniforms.sky.sunDir, wPos);
	vec3 sunlight_internal = sunVisibility * globalUniforms.sky.sunColor * globalUniforms.sky.sunBrightness * transmittanceToSun / solid_angle_mapping_PDF(radians(0.5));

  if (uniforms.sunSelfShadowSteps > 0)
  {
    float selfShadow = beer(DensityToLight(wPos + globalUniforms.sky.sunDir * uniforms.sunSelfShadowDist, wPos, uniforms.sunSelfShadowSteps).w);
    skylight_internal *= selfShadow;
    sunlight_internal *= selfShadow;
  }

  // Vibes-driven lerp between phase functions to smoothen abrupt lighting change when the sun is on the horizon.
  phase = mix(vec3(1 / (4 * M_PI)), phase, smoothstep(-0.03, 0.06, dot(globalUniforms.sky.sunDir, vec3(0, 1, 0))));
  //phase = vec3(1 / (4 * M_PI));
  light += phase * (float(!view_ray_intersects_ground) * sunlight_internal + skylight_internal);

  imageStore(uniforms.fogDensityVolumeRW, gid, vec4(light * fogColor * fogDensity, fogDensity));
}
