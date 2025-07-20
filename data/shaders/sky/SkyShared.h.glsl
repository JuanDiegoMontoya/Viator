#ifndef SKY_SHARED_H
#define SKY_SHARED_H
#include "../GlobalUniforms.h.glsl"

#if defined(TRANSMITTANCE_LUT) || defined(__cplusplus)
FVOG_DECLARE_ARGUMENTS(TransmittancePush)
{
 FVOG_UINT32 globalUniformsIndexTransmittance;
 FVOG_SHARED Image2D transmittanceImage;
};
#endif // TRANSMITTANCE_LUT || __cplusplu

#if defined(TRANSMITTANCE_LUT)
#define uniforms perFrameUniformsBuffers[globalUniformsIndexTransmittance]
#endif // TRANSMITTANCE_LUT

#if defined(MULTISCATTERING_LUT) || defined(__cplusplus)
FVOG_DECLARE_ARGUMENTS(MultiscatteringPush)
{
 FVOG_UINT32 globalUniformsIndexMultiscattering;
 FVOG_SHARED Texture2D transmittanceTexture;
 FVOG_SHARED Sampler transmittanceSampler;
 FVOG_SHARED Image2D multiscatteringImage;
};
#endif // MULTISCATTERING_LUT || __cplusplus

#if defined(MULTISCATTERING_LUT)
#define uniforms perFrameUniformsBuffers[globalUniformsIndexMultiscattering]
#endif // MULTISCATTERING_LUT

#if defined(SKY_VIEW_LUT) || defined(__cplusplus)
FVOG_DECLARE_ARGUMENTS(SkyViewPush)
{
 FVOG_UINT32 globalUniformsIndexSkyView;
 FVOG_SHARED Texture2D transmittanceTexture;
 FVOG_SHARED Texture2D multiscatteringTexture;
 FVOG_SHARED Sampler multiscatteringTransmittanceSampler;
 FVOG_SHARED Image2D skyViewImage;
};
#endif // SKY_VIEW_LUT || __cplusplus

#if defined(SKY_VIEW_LUT)
#define uniforms perFrameUniformsBuffers[globalUniformsIndexSkyView]
#endif // SKY_VIEW_LUT

#endif // SKY_SHARED_H