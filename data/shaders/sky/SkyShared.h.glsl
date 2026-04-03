#ifndef SKY_SHARED_H
#define SKY_SHARED_H
#include "SkyParams.shared.h"
#include "../GlobalUniforms.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(TransmittanceGpuParams)
{
  GlobalUniformsPtr uniforms;
  FVOG_SHARED Image2D transmittanceImage;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(MultiscatteringGpuParams)
{
  GlobalUniformsPtr uniforms;
  FVOG_SHARED Texture2D transmittanceTexture;
  FVOG_SHARED Image2D multiscatteringImage;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(SkyViewGpuParams)
{
  GlobalUniformsPtr uniforms;
  FVOG_SHARED Texture2D transmittanceTexture;
  FVOG_SHARED Texture2D multiscatteringTexture;
  FVOG_SHARED Image2D skyViewImage;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(AerialPerspectiveGpuParams)
{
  GlobalUniformsPtr uniforms;
  FVOG_SHARED Texture2D transmittanceTexture;
  FVOG_SHARED Texture2D multiscatteringTexture;
  FVOG_SHARED Image3D aerialPerspectiveTransmittance;
  FVOG_SHARED Image3D aerialPerspectiveScattering;
};

#endif // SKY_SHARED_H