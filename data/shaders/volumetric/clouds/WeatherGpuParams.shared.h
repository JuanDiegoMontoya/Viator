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
};

#endif // WEATHER_GPU_PARAMS_H