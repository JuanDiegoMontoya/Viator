#include "Common.h"

#include "Frog.h"

float snoise(vec4 v);

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

  vec4 colorAndDensity = FogAtPoint(wPos);
  vec3 fogColor = colorAndDensity.rgb;
  float fogDensity = colorAndDensity.w;
  vec3 light = vec3(0);
  light = SampleAverageLuminance(wPos, uniforms.linearSampler, uniforms.ddgi);
  
  // Shadow
  //vec3 phase = vec3(phaseHG(0.5, dot(-normalize(uniforms.viewPos - wPos), globalUniforms.sky.sunDir)));
  vec3 phase = phaseTex(dot(-normalize(uniforms.viewPos - wPos), globalUniforms.sky.sunDir));
  const vec3 transmittanceToSun = getTransmittanceAlongRay(globalUniforms.sky, globalUniforms.transmittanceLut, globalUniforms.linearSampler, globalUniforms.sky.sunDir, uniforms.viewPos);
  
  const float bottom_atmosphere_intersection_distance = ray_sphere_intersect_nearest(
      wPos * M_TO_KM_SCALE + vec3(0, globalUniforms.sky.atmosphere_bottom + BASE_HEIGHT_OFFSET, 0),
      globalUniforms.sky.sunDir,
      vec3(0.0),
      globalUniforms.sky.atmosphere_bottom
  );

  bool view_ray_intersects_ground = bottom_atmosphere_intersection_distance >= 0.0;
#if 0 // Ray traced shadow
  const vec3 sunVisibility = TraceSunRay(wPos, globalUniforms.sky.sunDir);
#else
  const float sunVisibility = SampleCascadedShadowMap(wPos, globalUniforms.sunShadowMap);
#endif
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




//	Simplex 4D Noise
//	by Ian McEwan, Ashima Arts
vec4 permute(vec4 x){return mod(((x*34.0)+1.0)*x, 289.0);}
float permute(float x){return floor(mod(((x*34.0)+1.0)*x, 289.0));}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
float taylorInvSqrt(float r){return 1.79284291400159 - 0.85373472095314 * r;}

vec4 grad4(float j, vec4 ip)
{
  const vec4 ones = vec4(1.0, 1.0, 1.0, -1.0);
  vec4 p,s;

  p.xyz = floor( fract (vec3(j) * ip.xyz) * 7.0) * ip.z - 1.0;
  p.w = 1.5 - dot(abs(p.xyz), ones.xyz);
  s = vec4(lessThan(p, vec4(0.0)));
  p.xyz = p.xyz + (s.xyz*2.0 - 1.0) * s.www; 

  return p;
}

float snoise(vec4 v)
{
  const vec2  C = vec2( 0.138196601125010504,  // (5 - sqrt(5))/20  G4
                        0.309016994374947451); // (sqrt(5) - 1)/4   F4
  // First corner
  vec4 i  = floor(v + dot(v, C.yyyy) );
  vec4 x0 = v -   i + dot(i, C.xxxx);

  // Other corners

  // Rank sorting originally contributed by Bill Licea-Kane, AMD (formerly ATI)
  vec4 i0;

  vec3 isX = step( x0.yzw, x0.xxx );
  vec3 isYZ = step( x0.zww, x0.yyz );
  //  i0.x = dot( isX, vec3( 1.0 ) );
  i0.x = isX.x + isX.y + isX.z;
  i0.yzw = 1.0 - isX;

  //  i0.y += dot( isYZ.xy, vec2( 1.0 ) );
  i0.y += isYZ.x + isYZ.y;
  i0.zw += 1.0 - isYZ.xy;

  i0.z += isYZ.z;
  i0.w += 1.0 - isYZ.z;

  // i0 now contains the unique values 0,1,2,3 in each channel
  vec4 i3 = clamp( i0, 0.0, 1.0 );
  vec4 i2 = clamp( i0-1.0, 0.0, 1.0 );
  vec4 i1 = clamp( i0-2.0, 0.0, 1.0 );

  //  x0 = x0 - 0.0 + 0.0 * C 
  vec4 x1 = x0 - i1 + 1.0 * C.xxxx;
  vec4 x2 = x0 - i2 + 2.0 * C.xxxx;
  vec4 x3 = x0 - i3 + 3.0 * C.xxxx;
  vec4 x4 = x0 - 1.0 + 4.0 * C.xxxx;

  // Permutations
  i = mod(i, 289.0); 
  float j0 = permute( permute( permute( permute(i.w) + i.z) + i.y) + i.x);
  vec4 j1 = permute( permute( permute( permute (
             i.w + vec4(i1.w, i2.w, i3.w, 1.0 ))
           + i.z + vec4(i1.z, i2.z, i3.z, 1.0 ))
           + i.y + vec4(i1.y, i2.y, i3.y, 1.0 ))
           + i.x + vec4(i1.x, i2.x, i3.x, 1.0 ));
  // Gradients
  // ( 7*7*6 points uniformly over a cube, mapped onto a 4-octahedron.)
  // 7*7*6 = 294, which is close to the ring size 17*17 = 289.

  vec4 ip = vec4(1.0/294.0, 1.0/49.0, 1.0/7.0, 0.0) ;

  vec4 p0 = grad4(j0,   ip);
  vec4 p1 = grad4(j1.x, ip);
  vec4 p2 = grad4(j1.y, ip);
  vec4 p3 = grad4(j1.z, ip);
  vec4 p4 = grad4(j1.w, ip);

  // Normalise gradients
  vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
  p0 *= norm.x;
  p1 *= norm.y;
  p2 *= norm.z;
  p3 *= norm.w;
  p4 *= taylorInvSqrt(dot(p4,p4));

  // Mix contributions from the five corners
  vec3 m0 = max(0.6 - vec3(dot(x0,x0), dot(x1,x1), dot(x2,x2)), 0.0);
  vec2 m1 = max(0.6 - vec2(dot(x3,x3), dot(x4,x4)            ), 0.0);
  m0 = m0 * m0;
  m1 = m1 * m1;
  return 49.0 * ( dot(m0*m0, vec3( dot( p0, x0 ), dot( p1, x1 ), dot( p2, x2 )))
               + dot(m1*m1, vec2( dot( p3, x3 ), dot( p4, x4 ) ) ) ) ;

}