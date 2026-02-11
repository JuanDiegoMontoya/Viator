#include "../../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(ModulateAlbedoArgs)
{
  FVOG_SHARED Texture2D inputDiffuseLuminance;
  FVOG_SHARED Texture2D inputAlbedo;
  FVOG_SHARED Texture2D inputDiffuseIlluminance;
  FVOG_SHARED Image2D outputDiffuseLuminance;
};

#ifndef __cplusplus

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  if (any(lessThan(gid, ivec2(0))) || any(greaterThanEqual(gid, textureSize(inputAlbedo, 0))))
  {
    return;
  }

  const vec4 luminance = texelFetch(inputDiffuseLuminance, gid, 0);
  const vec4 albedo = texelFetch(inputAlbedo, gid, 0);
  const vec4 illuminance = texelFetch(inputDiffuseIlluminance, gid, 0);
  imageStore(outputDiffuseLuminance, gid, luminance + albedo * illuminance);
  //imageStore(outputDiffuseLuminance, gid, illuminance);
  if (false)
  {
    if (isnan(illuminance.a) || isinf(illuminance.a))
      imageStore(outputDiffuseLuminance, gid, vec4(1, 0, 1, 1));
    else
      imageStore(outputDiffuseLuminance, gid, illuminance.aaaa);
  }
}

#endif // __cplusplus