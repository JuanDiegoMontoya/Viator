#version 460 core
#include "../../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(ModulateArgs)
{
  Texture2D albedo;
  Image2D illuminance;
}pc;

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  ivec2 targetDim = imageSize(pc.illuminance);
  if (any(greaterThanEqual(gid, targetDim)))
  {
    return;
  }

  vec3 albedo = texelFetch(pc.albedo, gid, 0).rgb;
  vec3 ambient = imageLoad(pc.illuminance, gid).rgb;

  imageStore(pc.illuminance, gid, vec4(ambient * albedo, 1.0));
}