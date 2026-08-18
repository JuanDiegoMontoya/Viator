#include "../Resources.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(SpriteIconParams)
{
  FVOG_SHARED Texture2D sprite;
  FVOG_SHARED Image2D radianceSDR;
  FVOG_VEC3 tint;
};

FVOG_DECLARE_ARGUMENTS(SpriteIconArgs)
{
  SpriteIconParams pc;
};

#ifndef __cplusplus

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, imageSize(pc.radianceSDR))))
  {
    return;
  }

  const vec2 uv = (gid + 0.5) / imageSize(pc.radianceSDR);

  vec4 outColor = textureLod(pc.sprite, gNearestClampSampler, uv, 0);
  outColor.rgb *= pc.tint;

  imageStore(pc.radianceSDR, gid, outColor);
}

#endif // !__cplusplus