#pragma once
#include "Core/Image.h"

#include "FastNoise/FastNoise.h"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"

namespace WorldGen
{
  Core::DSP::Image<2, float> GenerateAndUpscale2D(const FastNoise::SmartNode<>& node,
    glm::ivec2 start,
    int seed,
    int inSideLength,
    int outSideLength,
    Core::DSP::Filter filter);

  Core::DSP::Image<3, float> GenerateAndUpscale3D(const FastNoise::SmartNode<>& node,
    glm::ivec3 start,
    int seed,
    int inSideLength,
    int outSideLength,
    Core::DSP::Filter filter);
}