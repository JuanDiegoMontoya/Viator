#ifndef CLOUD_DENSITY_H
#define CLOUD_DENSITY_H

#include "../../Hash.h.glsl"

float CloudDensityAtPoint(vec3 positionWS, float time)
{
  //positionWS.x += 25 * time;
  const float height = 500;
  const float gradientLower = smoothstep(height - 20, height, positionWS.y);
  const float gradientUpper = 1 - clamp((positionWS.y - height) / 150, 0, 1);
  const float gradient = gradientLower * gradientUpper;
  if (gradient < 1e-3) return 0;
  return gradient * max(0.0, Simplex_Fbm(positionWS / 150, 7)) / 1;
}

#endif // CLOUD_DENSITY_H