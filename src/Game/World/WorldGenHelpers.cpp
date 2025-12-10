#include "WorldGenHelpers.h"

#include "tracy/Tracy.hpp"

namespace WorldGen
{
  Core::DSP::Image<2, float> GenerateAndUpscale2D(const FastNoise::SmartNode<>& node, glm::ivec2 start, int seed, int inSideLength, int outSideLength, Core::DSP::Filter filter)
  {
    ZoneScoped;
    const int sideLength = (inSideLength == outSideLength) ? inSideLength : (inSideLength + 1);
    auto rawImage        = Core::DSP::Image<2, float>({sideLength, sideLength});

    {
      ZoneScopedN("GenUniformGrid2D");
      const int sideCount = (inSideLength == outSideLength) ? inSideLength : inSideLength + 1;
      node->GenUniformGrid2D(rawImage.Data(), start.x, start.y, sideCount, sideCount, seed);
    }

    if (inSideLength == outSideLength)
    {
      return rawImage;
    }

    {
      ZoneScopedN("Upscale 2D");
      auto outImage = Core::DSP::Image<2, float>({outSideLength, outSideLength});
      auto* out     = outImage.Data();
      int i         = 0;
      for (int y = 0; y < outSideLength; y++)
      for (int x = 0; x < outSideLength; x++)
      {
        const auto uv = (glm::vec2(x, y) + 0.5f) / (outSideLength + (float(outSideLength) / inSideLength));
        out[i++]      = rawImage.Sample({.filter = filter}, uv);
      }

      return outImage;
    }
  }

  
  // Generate inSideLength^3 chunk of noise, then upscale it to outSideLength^3 with Filter.
  // Note: this upscaling is actually considerably less efficient than simply generating the equivalent volume of noise,
  // except in the case of very complex noise graphs.
  Core::DSP::Image<3, float> GenerateAndUpscale3D(const FastNoise::SmartNode<>& node, glm::ivec3 start, int seed, int inSideLength, int outSideLength, Core::DSP::Filter filter)
  {
    ZoneScoped;
    const int sideLength = (inSideLength == outSideLength) ? inSideLength : (inSideLength + 1);
    auto rawImage        = Core::DSP::Image<3, float>({sideLength, sideLength, sideLength});

    {
      ZoneScopedN("GenUniformGrid3D");
      const int sideCount = (inSideLength == outSideLength) ? inSideLength : inSideLength + 1;
      node->GenUniformGrid3D(rawImage.Data(), start.x, start.y, start.z, sideCount, sideCount, sideCount, seed);
    }

    if (inSideLength == outSideLength)
    {
      return rawImage;
    }

    {
      ZoneScopedN("Upscale 3D");
      auto outImage = Core::DSP::Image<3, float>({outSideLength, outSideLength, outSideLength});
      auto* out     = outImage.Data();
      int i         = 0;
      for (int z = 0; z < outSideLength; z++)
      for (int y = 0; y < outSideLength; y++)
      for (int x = 0; x < outSideLength; x++)
      {
        const auto uv = (glm::vec3(x, y, z) + 0.5f) / (outSideLength + (float(outSideLength) / inSideLength));
        out[i++]      = rawImage.Sample({.filter = filter}, uv);
      }

      return outImage;
    }
  }
} // namespace