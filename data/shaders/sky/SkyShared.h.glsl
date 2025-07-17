#ifndef SKY_SHARED_H
#define SKY_SHARED_H
#include "../GlobalUniforms.h.glsl"

FVOG_DECLARE_ARGUMENTS(TransmittancePush)
{
 FVOG_UINT32 globalUniformsIndex;
 FVOG_SHARED Image2D transmittanceImage;
};

#define uniforms perFrameUniformsBuffers[globalUniformsIndex]

#endif // SKY_SHARED_H