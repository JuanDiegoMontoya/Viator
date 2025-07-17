#pragma once
#include "Client/Fvog/Texture2.h"
#include "Client/Fvog/Pipeline2.h"
#include "Client/Fvog/Buffer2.h"
#include "Client/PipelineManager.h"

#include "shaders/volumetric/Common.h"

namespace Techniques
{
  class FroxelFog
  {
  public:
    explicit FroxelFog();

    void UpdateUniforms(VkCommandBuffer commandBuffer, const VolumetricUniforms& uniforms);

    void InjectFog(VkCommandBuffer commandBuffer, const Fvog::Texture& fogDensityVolume);

    void MarchVolume(VkCommandBuffer commandBuffer, const Fvog::Texture& fogDensityVolume, const Fvog::Texture& inScatteringAndTransmittanceVolume);

    void ApplyDeferred(VkCommandBuffer commandBuffer,
      const Fvog::Texture& inSceneRadiance,
      const Fvog::Texture& gDepth,
      const Fvog::Texture& outSceneRadiance,
      const Fvog::Texture& inScatteringAndTransmittanceVolume);

  private:
    std::optional<PipelineManager::ComputePipelineKey> accumulateDensityPipeline;
    std::optional<PipelineManager::ComputePipelineKey> marchVolumePipeline;
    std::optional<PipelineManager::ComputePipelineKey> applyDeferredPipeline;
    std::optional<Fvog::NDeviceBuffer<VolumetricUniforms>> uniformBuffer;
    std::optional<Fvog::Texture> scatteringTexture;
  };
} // namespace Techniques
