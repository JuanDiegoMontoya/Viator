#ifndef SKY_PARAMS_SHARED_H
#define SKY_PARAMS_SHARED_H
#include "../Resources.h.glsl"

#define PROFILE_LAYER_COUNT 2
// An atmosphere layer density which can be calculated as:
//   density = exp_term * exp(exp_scale * h) + linear_term * h + constant_term,
struct DensityProfileLayer
{
  FVOG_FLOAT const_term;
  FVOG_FLOAT exp_scale;
  FVOG_FLOAT exp_term;
  FVOG_FLOAT layer_width;
  FVOG_FLOAT lin_term;
};

struct SkyConfig
{
  FVOG_VEC3 sunDir;
  FVOG_VEC3 sunColor;
  FVOG_FLOAT sunBrightness;

  FVOG_FLOAT atmosphere_bottom;
  FVOG_FLOAT atmosphere_top;
  
  FVOG_VEC3 mie_scattering;
  FVOG_VEC3 mie_extinction;
  FVOG_FLOAT mie_scale_height;
  FVOG_FLOAT mie_phase_function_g;
  DensityProfileLayer mie_density[PROFILE_LAYER_COUNT];
  
  FVOG_VEC3 rayleigh_scattering;
  FVOG_FLOAT rayleigh_scale_height;
  DensityProfileLayer rayleigh_density[PROFILE_LAYER_COUNT];
  
  FVOG_VEC3 absorption_extinction;
  DensityProfileLayer absorption_density[PROFILE_LAYER_COUNT];
};

struct SkyLuts
{
  FVOG_SHARED Texture2D transmittanceLut;
  FVOG_SHARED Texture2D multiscatteringLut;
  FVOG_SHARED Texture2D skyViewLut;
  FVOG_SHARED Texture3D aerialPerspectiveTransmittance;
  FVOG_SHARED Texture3D aerialPerspectiveScattering;
};

struct SkyData
{
  SkyConfig config;
  SkyLuts luts;
  FVOG_MAT4 ae_clip_from_world;
  FVOG_MAT4 ae_world_from_clip;
  FVOG_FLOAT ae_zNear;
  FVOG_FLOAT ae_zFar;
};

#ifdef __cplusplus
inline SkyConfig InitSkyConfig()
{
  SkyConfig params;
  params.absorption_density[0] = { 0.0f, 0.2f, 0.00182376393f, 35.0f, 0.0f };
  params.absorption_density[1] = { 0.0f, -0.06666666666f, 20.6245170027f, 65.0f, 0.0f };
  params.absorption_extinction = { 0.00229072f,  0.00154036f,  0.0f};
  params.atmosphere_bottom = 6360.0f;
  params.atmosphere_top = 6460.0f;

  params.mie_density[0] = { 0.0f, -0.8333333134651184f, 1.0f, 100.0f, 0.0f};
  params.mie_density[1] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; 
  params.mie_extinction = { 0.00443999981507659f, 0.00443999981507659f, 0.00443999981507659f};
  params.mie_phase_function_g = 0.800000011920929f;
  params.mie_scale_height = 1.2000000476837158f;
  params.mie_scattering = { 0.003996000159531832f, 0.003996000159531832f, 0.003996000159531832f },

  params.rayleigh_density[0] = { 0.0f, -0.125f, 1.0f, 100.0f, 0.0f };
  params.rayleigh_density[1] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  params.rayleigh_scale_height = 8.696f;
  params.rayleigh_scattering = { 0.006604931f, 0.012344918f, 0.029412623f };
  
  return params;
}

#else
#include "../Math.h.glsl"

float Sky_EncodeAerialPerspectiveNdcZ(SkyData sky, float z_ndc)
{
  z_ndc = pow(z_ndc, 3);
  z_ndc = InvertDepthZO(z_ndc, sky.ae_zNear, sky.ae_zFar);
  return z_ndc;
}

float Sky_DecodeAerialPerspectiveNdcZ(SkyData sky, float z_ndc)
{
  z_ndc = LinearizeDepthZO(z_ndc, sky.ae_zNear, sky.ae_zFar);
  z_ndc = pow(z_ndc, 1.0 / 3);
  return z_ndc;
}

bool Sky_GetAerialPerspective(SkyData sky, vec3 positionWS, out vec3 transmittance, out vec3 scattering)
{
  const vec4 pos_ae_clip = sky.ae_clip_from_world * vec4(positionWS, 1);
  const vec3 pos_ae_ndc = pos_ae_clip.xyz / pos_ae_clip.w;
  const float z = Sky_DecodeAerialPerspectiveNdcZ(sky, pos_ae_ndc.z);
  vec3 pos_ae_uv = vec3(pos_ae_ndc.xy * 0.5 + 0.5, z);

  transmittance = textureLod(sky.luts.aerialPerspectiveTransmittance, gLinearClampSampler, pos_ae_uv, 0).rgb;
  scattering    = textureLod(sky.luts.aerialPerspectiveScattering, gLinearClampSampler, pos_ae_uv, 0).rgb;

  return true;
}
#endif // __cplusplus

#endif // SKY_PARAMS_SHARED_H