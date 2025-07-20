#pragma once
#include <vulkan/vulkan_core.h>
#include "Client/Fvog/Texture2.h"
#include "Client/Fvog/Pipeline2.h"
#include "Client/PipelineManager.h"
#include "shaders/voxels/Voxels.h.glsl"

#include <glm/mat4x4.hpp>
#include <optional>

namespace Fvog
{
  class Device;
  class Tlas;
}

namespace Techniques
{
  class RayTracedAO
  {
  public:
    RayTracedAO();

    struct ComputeParams
    {
      Voxels voxels;
      Fvog::Texture* inputDepth{};
      Fvog::Texture* inputNormal{};
      Fvog::Extent2D outputSize;
      uint32_t numRays{1};
      float rayLength{2};
      uint32_t frameNumber{};

      // TODO: scale factor and denoising params
    };

    [[nodiscard]] Fvog::Texture& ComputeAO(VkCommandBuffer commandBuffer, const ComputeParams& params);

  private:
    PipelineManager::ComputePipelineKey rtaoPipeline_;
    std::optional<Fvog::Texture> aoTexture_;
  };
}
