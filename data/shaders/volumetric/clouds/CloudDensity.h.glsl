#ifndef CLOUD_DENSITY_H
#define CLOUD_DENSITY_H

#include "../../Hash.h.glsl"
#include "../../Math.h.glsl"
#include "WeatherGpuParams.shared.h"

float CloudDensityAtPoint(vec3 positionWS, WeatherGpuParams params)
{
  positionWS.xz += params.cloudHorizontalOffset;
  const float height = params.cloudBottomAltitude;
  const float gradientLower = smoothstep(height, height + params.cloudBottomFalloffDistance, positionWS.y);
  const float gradientUpper = 1 - clamp((positionWS.y - height) / params.cloudHeight, 0, 1);
  const float gradient = gradientLower * gradientUpper;
  if (gradient < 1e-3) return 0;
  const float raw = Remap(Simplex_Fbm(positionWS * params.cloudFrequency, 7), -1, 1, -1.0 / params.cloudCoverage, 1);
  return gradient * max(0.0, raw) * params.cloudDensity;
}

#endif // CLOUD_DENSITY_H