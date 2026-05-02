#pragma once
#include "Client/Fvog/Texture2.h"
#include "Client/PipelineManager.h"
#include "Client/Scheduler.h"
#include "shaders/voxels/Voxels.h.glsl"
#include "shaders/voxels/RayTracedVoxelsShadow.frag.glsl"

#include "glm/vec3.hpp"

#include <optional>
#include <string_view>

namespace Techniques
{
  struct CascadedShadowMapRenderTerrainParams
  {
    Fvog::Extent2D shadowResolution;
    uint32_t numCascades;
    Voxels voxels;
    glm::vec3 playerPos; // the light will look at this
    glm::vec3 lightDirection;
    float frustumDepth;
    float baseFrustumSideLength;
  };

  class CascadedShadowMap
  {
  public:
    explicit CascadedShadowMap();

    void RenderTerrainShadowMap(Scheduler& scheduler, std::string_view suffix, VkCommandBuffer cmd, const CascadedShadowMapRenderTerrainParams& params);
    CascadedShadowMapInfoPtr GetShadowInfoBufferAddress();
    Fvog::Texture& GetShadowMapTexture();

  private:
    std::optional<Fvog::TypedBuffer<CascadedShadowMapInfoPtr_t>> shadowMapInfo_;
    std::optional<Fvog::TypedBuffer<RayTracedVoxelsShadowArgs_t>> shadowPassArguments_;
    std::optional<Fvog::Texture> shadowArrayTexture_;
    std::unique_ptr<std::optional<Fvog::TextureView>[]> shadowArrayTextureViews_;
    PipelineManager::GraphicsPipelineKey shadowPipeline_;

    constexpr static Fvog::Format shadowFormat = Fvog::Format::D16_UNORM;
  };
}