#ifndef SKY_SHARED_H
#define SKY_SHARED_H
#include "../GlobalUniforms.h.glsl"

#if defined(TRANSMITTANCE_LUT) || defined(__cplusplus)
FVOG_DECLARE_ARGUMENTS(TransmittancePush)
{
 FVOG_UINT32 globalUniformsIndexTransmittance;
 FVOG_SHARED Image2D transmittanceImage;
};
#endif // TRANSMITTANCE_LUT || __cplusplus

#if defined(TRANSMITTANCE_LUT)
#define uniforms perFrameUniformsBuffers[globalUniformsIndexTransmittance]
#endif // TRANSMITTANCE_LUT

#if defined(MULTISCATTERING_LUT) || defined(__cplusplus)
FVOG_DECLARE_ARGUMENTS(MultiscatteringPush)
{
 FVOG_UINT32 globalUniformsIndexMultiscattering;
 FVOG_SHARED Texture2D transmittanceTexture;
 FVOG_SHARED Image2D multiscatteringImage;
};
#endif // MULTISCATTERING_LUT || __cplusplus

#if defined(MULTISCATTERING_LUT)
#define uniforms perFrameUniformsBuffers[globalUniformsIndexMultiscattering]
#endif // MULTISCATTERING_LUT

#if defined(SKY_VIEW_LUT) || defined(__cplusplus)
FVOG_DECLARE_ARGUMENTS(SkyViewPush)
{
 FVOG_UINT32 globalUniformsIndex;
 FVOG_SHARED Texture2D transmittanceTexture;
 FVOG_SHARED Texture2D multiscatteringTexture;
 FVOG_SHARED Image2D skyViewImage;
};
#endif // SKY_VIEW_LUT || __cplusplus

#if defined(AERIAL_PERSPECTIVE_LUT) || defined(__cplusplus)
FVOG_DECLARE_ARGUMENTS(AerialPerspectivePush)
{
  FVOG_UINT32 globalUniformsIndex;
  FVOG_MAT4 world_from_clip;
  FVOG_SHARED Texture2D transmittanceTexture;
  FVOG_SHARED Texture2D multiscatteringTexture;
  FVOG_SHARED Image3D aerialPerspectiveTransmittance;
  FVOG_SHARED Image3D aerialPerspectiveScattering;
};
#endif // AERIAL_PERSPECTIVE_LUT || __cplusplus

#if defined(SKY_VIEW_LUT) || defined(AERIAL_PERSPECTIVE_LUT)
#define uniforms perFrameUniformsBuffers[globalUniformsIndex]
#endif // SKY_VIEW_LUT

#endif // SKY_SHARED_H