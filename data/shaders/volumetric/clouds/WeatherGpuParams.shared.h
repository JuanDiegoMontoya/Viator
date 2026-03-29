#ifndef WEATHER_GPU_PARAMS_H
#define WEATHER_GPU_PARAMS_H

#include "../../Resources.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(WeatherGpuParams)
{
  FVOG_FLOAT cloudBottomAltitude;        // 480
  FVOG_FLOAT cloudBottomFalloffDistance; // 20
  FVOG_FLOAT cloudHeight;                // 150
  FVOG_FLOAT cloudCoverage;
  FVOG_FLOAT cloudDensity;               // 0.1-1
  FVOG_FLOAT cloudFrequency;             // 1/150
  FVOG_VEC2 windVelocity;
  FVOG_VEC2 cloudHorizontalOffset;
  FVOG_FLOAT cloudTemporalOffset;
  // Earth scale for curving clouds (experimental).
  FVOG_FLOAT earthSizeFactor;            // ~ 1/100
};

#ifndef __cplusplus
float Cloud_GetAtmosphereBottom(float earthSizeFactor)
{
  return 6360.0 * 1000.0 * earthSizeFactor;
}

vec3 Cloud_GetAtmosphereOffset(float earthSizeFactor)
{
  const float atmosphereBottom = Cloud_GetAtmosphereBottom(earthSizeFactor);
  return vec3(0, atmosphereBottom, 0);
}
#endif // !__cplusplus

#endif // WEATHER_GPU_PARAMS_H