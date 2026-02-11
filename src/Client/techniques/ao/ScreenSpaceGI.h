#pragma once
#include "Client/PipelineManager.h"
#include "Client/Fvog/Texture2.h"
#include "shaders/ao/ssgi/ScreenSpaceGI.comp.glsl"

namespace Techniques
{
  struct ScreenSpaceGIDispatchInfo
  {
    Fvog::Texture* inputAlbedo{};
    Fvog::Texture* inputDepth{};
    Fvog::Texture* inputNormal{};
    Fvog::Texture* inputDiffuseLuminance{};
    Fvog::Extent2D outputSize;
    uint32_t sliceCount  = 8;
    uint32_t sampleCount = 8;
    float sampleRadius   = 4;
    float hitThickness   = 0.5f;

    uint32_t frameNumber{};

    glm::mat4 view_from_world;
    glm::mat4 clip_from_view;
    bool debugCapture;
    VkDeviceAddress debugDraw;

    //float stepWidth = 1;
    //float phiNormal = 0.3f;
    //float phiDepth  = 0.2f;

    //uint32_t upscaleFactor = 2;
  };

  class ScreenSpaceGI
  {
  public:
    ScreenSpaceGI();

    [[nodiscard]] Fvog::Texture& Dispatch(VkCommandBuffer cmd, const ScreenSpaceGIDispatchInfo& params);

  private:
    PipelineManager::ComputePipelineKey ssgiPipeline_;
    PipelineManager::ComputePipelineKey modulateAlbedoPipeline_;
    Fvog::NDeviceBuffer<SSGIParams_t> uniforms_;
    std::optional<Fvog::Texture> diffuseIlluminance_;
    std::optional<Fvog::Texture> outputLuminance_;
  };
}