#pragma once
#include <vulkan/vulkan_core.h>
#include "Client/Fvog/Texture2.h"
#include "Client/Fvog/Pipeline2.h"
#include "Client/PipelineManager.h"
#include "Client/Scheduler.h"
#include "shaders/voxels/Voxels.h.glsl"
#include "shaders/ao/rtao/Upscale.comp.glsl"

#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"

#include <optional>

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
      int32_t numRays{8};
      float rayLength{2};
      uint32_t frameNumber{};

      glm::mat4 clip_from_view;
      glm::mat4 world_from_clip;
      glm::vec3 cameraPosWS;
      float stepWidth = 1;
      float phiNormal = 0.3f;
      float phiDepth = 0.2f;

      uint32_t upscaleFactor = 2;
    };

    void ComputeAO(Scheduler& scheduler, VkCommandBuffer commandBuffer, const ComputeParams& params);
    [[nodiscard]] Fvog::Texture& GetAOTexture();

  private:
    PipelineManager::ComputePipelineKey rtaoPipeline_;
    PipelineManager::ComputePipelineKey upscalePipeline_;
    std::optional<Fvog::Texture> aoLowResTexture_;
    std::optional<Fvog::Texture> aoOutputTexture_;
    Fvog::NDeviceBuffer<FilterParams_t> upscaleUniforms_;
    Fvog::Texture* aoTextureToReturn{};
  };
}
