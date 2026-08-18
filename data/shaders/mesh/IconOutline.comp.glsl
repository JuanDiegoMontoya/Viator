#include "../Resources.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_2(IconOutlineParams)
{
  FVOG_SHARED Texture2D depth;
  FVOG_SHARED Image2D radianceSDR;
  FVOG_FLOAT farDepth;
  FVOG_INT32 width;
};

FVOG_DECLARE_ARGUMENTS(IconOutlineArgs)
{
  IconOutlineParams pc;
};

#ifndef __cplusplus

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 outResolution = imageSize(pc.radianceSDR);

  if (any(greaterThanEqual(gid, outResolution)))
  {
    return;
  }

  const float cDepth = texelFetch(pc.depth, gid, 0).x;

  if (cDepth != pc.farDepth)
  {
    return;
  }

  vec3 sumBorderRadiance = {0, 0, 0};
  uint countBorder = 0;

  for (int y = -pc.width; y <= pc.width; y++)
  {
    for (int x = -pc.width; x <= pc.width; x++)
    {
      const ivec2 samplePos = gid + ivec2(x, y);

      if (any(lessThan(samplePos, ivec2(0))) || any(greaterThanEqual(samplePos, outResolution)))
      {
        continue;
      }

      const float sDepth = texelFetch(pc.depth, samplePos, 0).x;
      
      if (sDepth == pc.farDepth)
      {
        continue;
      }

      sumBorderRadiance += imageLoad(pc.radianceSDR, samplePos).rgb;
      countBorder++;
    }
  }

  if (countBorder == 0)
  {
    return;
  }

  const vec3 meanBorderColor = sumBorderRadiance / float(countBorder);
  imageStore(pc.radianceSDR, gid, vec4(1 - meanBorderColor, 1));
}

#endif // !__cplusplus