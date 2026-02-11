#include "../../Resources.h.glsl"
#include "../../debug/DebugCommon.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(SSGIParams)
{
  FVOG_SHARED Texture2D gDepth;
  FVOG_SHARED Texture2D gNormal;
  FVOG_SHARED Texture2D gIlluminance;
  FVOG_SHARED Image2D outIlluminance;

  FVOG_UINT32 sliceCount;
  FVOG_UINT32 sampleCount;
  FVOG_FLOAT sampleRadius;
  FVOG_FLOAT hitThickness;
  FVOG_MAT4 view_from_world;
  FVOG_MAT4 world_from_view;
  FVOG_MAT4 clip_from_view;
  FVOG_MAT4 view_from_clip;
  FVOG_MAT4 world_from_clip;
  DebugDrawData debugDraw;
  FVOG_BOOL32 debugCapture;
};

#ifndef __cplusplus

#include "../../Math.h.glsl"
#include "../../Config.shared.h"

FVOG_DECLARE_ARGUMENTS(PushConstants)
{
  SSGIParams uniforms;
};






#define SSGI_MODE_SSILVB 0
#define SSGI_MODE_GTAO   1
#define SSGI_MODE_SSAO   2

#ifndef SSGI_MODE
#define SSGI_MODE 2
#endif

const float pi     = 3.14159265359;
const float twoPi  = 2.0 * pi;
const float halfPi = 0.5 * pi;

// https://cdrinmatane.github.io/posts/ssaovb-code/
const uint SSILVB_SectorCount = 32u;
uint SSILVB_UpdateSectors(float minHorizon, float maxHorizon, uint outBitfield)
{
  uint startBit        = uint(minHorizon * float(SSILVB_SectorCount));
  uint horizonAngle    = uint(ceil((maxHorizon - minHorizon) * float(SSILVB_SectorCount)));
  uint angleBit        = horizonAngle > 0u ? uint(0xFFFFFFFFu >> (SSILVB_SectorCount - horizonAngle)) : 0u;
  uint currentBitfield = angleBit << startBit;
  return outBitfield | currentBitfield;
}

// Get indirect lighting and ambient occlusion with SSILVB or GTAO.
// If doSSILVB is false, then GTAO is executed instead and no indirect lighting is returned.
vec4 ExecuteSSILVB(const ivec2 positionSS, const SSGIParams args, bool doSSILVB)
{
  const vec2 screenSize = textureSize(args.gDepth, 0).xy;
  const vec2 uv = (positionSS + 0.5) / screenSize;

  const vec2 aspect     = screenSize.yx / screenSize.x;
  const vec3 normalWS   = normalize(texelFetch(args.gNormal, positionSS, 0).xyz);
  const vec3 normalVS   = (args.view_from_world * vec4(normalWS, 0.0)).xyz;
  const float cDepth    = texelFetch(args.gDepth, positionSS, 0).x;
  if (cDepth == FAR_DEPTH)
  {
    return vec4(0, 0, 0, 1);
  }
  const vec3 positionWS = UnprojectUV_ZO(cDepth, uv, args.world_from_clip);
  const vec3 positionVS = (args.view_from_world * vec4(positionWS, 1.0)).xyz;
  const vec3 cameraVS   = normalize(-positionVS);

  // SSILVB samples half-hemisphere slices, so it needs twice as much rotation as GTAO. 
  const float sliceRotation = (doSSILVB ? twoPi : pi) / (args.sliceCount);
  const float sampleScale = (-args.sampleRadius * args.clip_from_view[0][0]) / positionVS.z;
  // Minimum offset (in UV space) of samples to center texel.
  // Too-small values result in self-occlusion. Too-large values result in underdarkening in corners.
  const float sampleOffset = 0.1;
  const float jitter = Hash_IGN(positionSS.x, positionSS.y) - 0.5;
  //const float jitter = MM_Hash2(positionSS) - 0.5;

  const bool doDebugCapture = positionSS == ivec2(screenSize / 2) && args.debugCapture != 0;

  uint indirect    = 0u;
  float correction = 0;
  vec3 lighting    = vec3(0.0);
  float visibility = 0.0;

  // TODO: this may need to be <=, or start at 1.
  for (uint slice = 0; slice < args.sliceCount; slice++)
  {
    // Compute view-space slice directionVS.
    const float phi        = sliceRotation * (slice + jitter);                // GTAO line 5
    const vec2 omega       = vec2(cos(phi), sin(phi));                        // GTAO line 6
    const vec3 directionVS = vec3(omega.x, omega.y, 0.0); // directionV, d_i  // GTAO line 8
    
    // Rejection of directionVS from cameraVS: ortho vector FROM cameraVS TO directionVS.
    // Orthogonal to cameraVS.
    const vec3 orthoDirection = directionVS - dot(directionVS, cameraVS) * cameraVS; // GTAO line 9
    const vec3 axis           = cross(directionVS, cameraVS);                        // GTAO line 10
    const vec3 projNormal     = normalVS - axis * dot(normalVS, axis);               // GTAO line 11
    const float projLength    = length(projNormal);

    const float signN = sign(dot(orthoDirection, projNormal));                   // GTAO line 13
    const float cosN  = clamp(dot(projNormal, cameraVS) / projLength, 0.0, 1.0); // GTAO line 14
    const float n     = signN * acos(cosN);                                      // GTAO line 15

    // Bitmask b_i <- 0
    uint occlusion = 0u;

    // WIP broken SSILVB.
    // https://cybereality.com/screen-space-indirect-lighting-with-visibility-bitmask-improvement-to-gtao-ssao-real-time-ambient-occlusion-algorithm-glsl-shader-implementation/
    // https://github.com/cdrinmatane/SSRT3/blob/main/HDRP/Shaders/Resources/SSRTCS.compute
    // https://arxiv.org/pdf/2301.11376
    // https://www.shadertoy.com/view/dsGBzW
    if (doSSILVB)
    {
      // TODO: this may need to be <=, or start at 1.
      for (uint currentSample = 0; currentSample < args.sampleCount; currentSample++)
      {
        const float sampleStep  = (currentSample + jitter) / args.sampleCount + sampleOffset;
        vec2 sampleUV           = uv + sampleStep * sampleScale * omega * aspect * vec2(1, -1);
        const ivec2 sampleCoord = ivec2(sampleUV * screenSize);
        sampleUV = (sampleCoord + vec2(0.5)) / screenSize;
        if (any(lessThan(sampleCoord, ivec2(0))) || any(greaterThanEqual(sampleCoord, screenSize)))
        {
          break;
        }
        
        // Front sample s_f <- view-space positon at step j
        const vec3 samplePositionWS = UnprojectUV_ZO(texelFetch(args.gDepth, sampleCoord, 0).x, sampleUV, args.world_from_clip);
        const vec3 samplePositionVS = (args.view_from_world * vec4(samplePositionWS, 1.0)).xyz;
        const vec3 sampleNormalWS   = normalize(texelFetch(args.gNormal, sampleCoord, 0).xyz);
        const vec3 sampleNormalVS   = (args.view_from_world * vec4(sampleNormalWS, 0.0)).xyz;

        float sampleDist = distance(samplePositionWS, positionWS);
        if (sampleDist < 0.1)
        {
          continue;
        }
        
        if (sampleCoord == positionSS)
        {
          continue;
        }

        const vec3 sampleLight    = texelFetch(args.gIlluminance, sampleCoord, 0).rgb;
        const vec3 sampleDistance = samplePositionVS - positionVS;
        const float sampleLength  = length(sampleDistance);
        const vec3 sampleHorizon  = sampleDistance / sampleLength;

        vec2 frontBackHorizon;
        frontBackHorizon.x = dot(sampleHorizon, cameraVS);
        frontBackHorizon.y = dot(normalize(sampleDistance - cameraVS * args.hitThickness), cameraVS);

        frontBackHorizon = acos(frontBackHorizon);
        frontBackHorizon = clamp((frontBackHorizon + n + halfPi) / pi, 0.0, 1.0);

        indirect = SSILVB_UpdateSectors(frontBackHorizon.x, frontBackHorizon.y, 0u);

        // GI <- GI + (countbits(b_j & ~b_i) / N_b) * c_j(dot(n_p, l_j)) * dot(n_j, -l_j)
        lighting += (1.0 - float(bitCount(indirect & ~occlusion)) / float(SSILVB_SectorCount)) * sampleLight * clamp(dot(normalVS, sampleHorizon), 0.0, 1.0) *
                    clamp(dot(sampleNormalVS, -sampleHorizon), 0.0, 1.0);

        // b_i <- b_i | b_j
        occlusion |= indirect;

        if (doDebugCapture)
        {
          DebugLine line;
          line.aPosition = samplePositionWS;
          line.bPosition = samplePositionWS + sampleNormalWS * .1;
          line.aColor = vec4(1, 0, 1, 1);
          line.bColor = vec4(0, 1, 1, 1);
          TryPushDebugLine(args.debugDraw, line);

          //if (currentSample == 0)
          {
            //printf("occ: %u\n", bitCount(occlusion));
            //printf("lighting: %v3f\n", lighting);
          }
        }
      }

      // AO <- AO + 1 - countbits(b_i) / N_b
      visibility += 1.0 - float(bitCount(occlusion)) / float(SSILVB_SectorCount);
    }
    // https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
    // https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.hlsli
    else // Do GTAO
    {
      // https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.hlsli#L408-L410
      // Like XeGTAO, not using -1.
      //const float lowHorizonCos = -1;
      const float lowHorizonCos = cos(signN * halfPi + n);

      const float radiusMultiplier         = 1.0;
      const float effectFalloffRange       = 0.1;
      const float thinOccluderCompensation = 0.1;

      const float effectRadius = args.sampleRadius * radiusMultiplier;
      const float falloffRange = effectFalloffRange * effectRadius;

      const float falloffFrom = effectRadius * (1.0 - effectFalloffRange);
      const float falloffMul  = -1.0 / falloffRange;
      const float falloffAdd  = falloffFrom / falloffRange + 1.0;

      for (float side = 0; side < 2; side++)
      {
        float cHorizonCos = lowHorizonCos;

        for (uint currentSample = 0; currentSample < args.sampleCount; currentSample++)
        {
        #if 1
          const float sampleStep  = (currentSample + (MM_Hash2(positionSS) - .5) + jitter) / args.sampleCount + sampleOffset;
        #else
          const float sampleStep  = (currentSample + jitter) / args.sampleCount + sampleOffset;
        #endif
          vec2 sampleUV           = uv + (side * 2 - 1) * sampleStep * sampleScale * omega * aspect * vec2(1, -1);
          const ivec2 sampleCoord = ivec2(sampleUV * screenSize);

          // Snap texcoord to texel center to avoid artifacts caused by mismatch (e.g. when reconstructing position).
          // XeGTAO source notes that this introduces new (hopefully minor) artifacts from the sample no longer being on the slice.
          sampleUV = (sampleCoord + vec2(0.5)) / screenSize;
          if (any(lessThan(sampleCoord, ivec2(0))) || any(greaterThanEqual(sampleCoord, screenSize)))
          {
            break;
          }
          
          const float sampleDepth = texelFetch(args.gDepth, sampleCoord, 0).x;
          if (sampleDepth == FAR_DEPTH)
          {
            continue;
          }
          const vec3 samplePositionWS = UnprojectUV_ZO(sampleDepth, sampleUV, args.world_from_clip);
          const vec3 samplePositionVS = (args.view_from_world * vec4(samplePositionWS, 1.0)).xyz;
          const vec3 sampleDelta      = samplePositionVS - positionVS;
          const float sampleDistance  = length(sampleDelta);
          if (dot(sampleDelta, sampleDelta) < 0.03) // Discard samples that are really close to the center.
          {
            continue;
          }

          if (sampleCoord == positionSS)
          {
            continue;
          }

        #if 1 // Default range falloff
          const float rangeWeight = clamp(sampleDistance * falloffMul + falloffAdd, 0, 1);
        #else // XeGTAO's custom range falloff
          const float falloffBase = length(vec3(sampleDelta.xy, sampleDelta.z * (1 + thinOccluderCompensation)));
          const float rangeWeight = clamp(falloffBase * falloffMul + falloffAdd, 0, 1);
        #endif
          const vec3 sHorizonVS   = sampleDelta / sampleDistance;
          float sampleHorizonCos  = dot(sHorizonVS, cameraVS);
          sampleHorizonCos        = mix(lowHorizonCos, sampleHorizonCos, rangeWeight);

        #if 1 // No thickness heuristic
          cHorizonCos = max(cHorizonCos, sampleHorizonCos);
        #else // "Fast" thickness heuristic from XeGTAO
          cHorizonCos = mix(max(cHorizonCos, sampleHorizonCos), sampleHorizonCos, thinOccluderCompensation);
        #endif
          if (doDebugCapture)
          {
            DebugLine line;
            line.aPosition = samplePositionWS;
            line.bPosition = samplePositionWS + normalWS * .1;
            line.aColor    = vec4(1, 0, 1, 1);
            line.bColor    = vec4(0, 1, 1, 1);
            TryPushDebugLine(args.debugDraw, line);
          }
        }

          //cHorizonCos = max(cHorizonCos - 0.4, lowHorizonCos);
        const float h = n + clamp((2 * side - 1) * acos(cHorizonCos) - n, -halfPi, halfPi);
        visibility += projLength * (cosN + 2 * h * sin(n) - cos(2 * h - n)) / 4;
      }
    }

    correction += projLength * (n * sin(n) + cosN);

    // Display sectors of hemisphere slice.
    if (doDebugCapture)
    {
      const vec3 directionWS = (args.world_from_view * vec4(directionVS, 0.0)).xyz;
      const vec3 projNormalWS = (args.world_from_view * vec4(projNormal, 0.0)).xyz;
      const vec3 orthoDirectionWS = (args.world_from_view * vec4(orthoDirection, 0.0)).xyz;
      const vec3 cameraWS = (args.world_from_view * vec4(cameraVS, 0.0)).xyz;
      const vec3 axisWS = (args.world_from_view * vec4(axis, 0.0)).xyz;
      //TryPushDebugLine(args.debugDraw, positionWS, vec4(0, 1, 1, 1), positionWS + projNormalWS * 1, vec4(0, 1, 1, 1));
      //TryPushDebugLine(args.debugDraw, positionWS, vec4(0, 1, 0, 1), positionWS + orthoDirectionWS * 1, vec4(0, 1, 0, 1));
      //TryPushDebugLine(args.debugDraw, positionWS, vec4(0, 0, 1, 1), positionWS + axisWS * 1, vec4(1, 0, 0, 1));
      TryPushDebugLine(args.debugDraw, positionWS + cameraWS, vec4(0, 0, 1, 1), positionWS + directionWS * 1 + cameraWS, vec4(0, 1, 0, 1));
      TryPushDebugLine(args.debugDraw, positionWS + cameraWS, vec4(0, 0, 1, 1), positionWS - directionWS * 1 + cameraWS, vec4(0, 1, 0, 1));
      //TryPushDebugLine(args.debugDraw, positionWS, vec4(0, 0, 1, 1), positionWS + cameraWS * 1, vec4(0, 0, 1, 1));

      const vec3 rotator = cross(cameraWS, axisWS);
      const mat3 basis = mat3(cameraWS, rotator, orthoDirectionWS);
      //DrawDebugLineBasis(args.debugDraw, positionWS, basis);
      
      // Show individual sectors.
      for (int i = 0; i < SSILVB_SectorCount; i++)
      {
        const float theta = pi * float(i + 0.5) / SSILVB_SectorCount;
        const vec3 bitRay = cos(theta) * cameraWS + sin(theta) * rotator;
        vec4 color = vec4(1);
        //if ((occlusion & (1 << i)) == 0)
        {
          color = vec4(1, 0, 0, 1);
        }
        //TryPushDebugLine(args.debugDraw, positionWS, color, positionWS + bitRay, color);
      }
    }
  }

  if (doDebugCapture)
  {
    printf("correction: %f\n", correction);
  }

  //visibility /= args.sliceCount;
  visibility /= correction;
  lighting /= args.sliceCount;
    
  if (doDebugCapture)
  {
    printf("visibility: %f\n", visibility);
  }

  return vec4(lighting, visibility);
}


// Alchemy SSAO with a simple extension that adds indirect lighting.
vec4 ExecuteAlchemySSAO(uint numSamples, float delta, float R, float s, float k)
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const vec2 screenSize = textureSize(uniforms.gDepth, 0).xy;
  const vec2 uv2 = (gid + 0.5) / screenSize;

  const vec3 P = UnprojectUV_ZO(texelFetch(uniforms.gDepth, gid, 0).x, uv2, uniforms.world_from_clip);
  const vec3 N = texelFetch(uniforms.gNormal, gid, 0).xyz;
  const float d = -(uniforms.view_from_world * vec4(P, 1.0)).z; // camera-space depth
  const float c = 0.1 * R;

  vec3 lighting = vec3(0);
  float occlusion = 0.0;
  for (uint i = 0; i < numSamples; i++)
  {
    const float alpha = (float(i) + 0.5) / float(numSamples);
    const float h = (alpha * R) / d;
    const float theta = Hash_IGN(gid.x, gid.y) * 2.0 * M_PI * alpha * (7.0 * float(numSamples)) / 9.0;
    //const float theta = MM_Hash2(vec2(gid.x, gid.y)) * 2.0 * M_PI * alpha * (7.0 * float(numSamples)) / 9.0;
    vec2 uv = (uv2 + h * vec2(cos(theta), sin(theta)));
    const ivec2 texSize = textureSize(uniforms.gDepth, 0);
    const ivec2 samplePos = ivec2(uv * texSize);
    if (any(lessThan(samplePos, ivec2(0))) || any(greaterThanEqual(samplePos, texSize)))
    {
      break;
    }

    const vec3 Pi = UnprojectUV_ZO(texelFetch(uniforms.gDepth, samplePos, 0).x, uv, uniforms.world_from_clip); // sample world position
    const vec3 Ni = texelFetch(uniforms.gNormal, samplePos, 0).xyz;
    const vec3 Li = texelFetch(uniforms.gIlluminance, samplePos, 0).rgb;
    const float geometry = max(0.0, dot(Ni, P - Pi)) * max(0.0, dot(N, Pi - P));
    // Normally in RSM we clamp to something like 0.01 as it would be the primary source of indirect lighting, and we only want to prevent divisions by zero.
    // Here, however, a too-small minimum distance amplifies indirect lighting and makes interior corners glow unnaturally.
    const float d = max(distance(P, Pi), 0.1);

    const float di = -(uniforms.view_from_world * vec4(Pi, 1.0)).z; // world/view-space sample depth
    const vec3 omega_i = Pi - P;

    const float isInRadius = R > length(omega_i) ? 1 : 0;
    const float numerator = max(0.0, dot(N, omega_i) - delta * di) * isInRadius;
    const float denom = max(c * c, dot(omega_i, omega_i));

    // The parameters of the two dot products of `d` are un-normalized, so their lengths are accounted for here, on top of the normal d^2 attenuation.
    lighting += Li * geometry / (d * d * d * d);
    occlusion += numerator / denom;
  }

  occlusion *= (2.0 * M_PI * c) / numSamples;
  lighting *= (2.0 * M_PI * c) / numSamples;

  const float visibility = max(0.0, pow(1.0 - s * occlusion, k));

  return vec4(lighting, visibility);
}


layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  if (any(lessThan(gid, ivec2(0))) || any(greaterThanEqual(gid, imageSize(uniforms.outIlluminance))))
  {
    return;
  }

#if SSGI_MODE == SSGI_MODE_SSILVB
  imageStore(uniforms.outIlluminance, gid, ExecuteSSILVB(gid, uniforms, true));
#elif SSGI_MODE == SSGI_MODE_GTAO
  imageStore(uniforms.outIlluminance, gid, ExecuteSSILVB(gid, uniforms, false));
#elif SSGI_MODE == SSGI_MODE_SSAO
  imageStore(uniforms.outIlluminance, gid, ExecuteAlchemySSAO(uniforms.sampleCount, 0.001, uniforms.sampleRadius, 2.3, 1.4));
#endif
}

#endif // __cplusplus