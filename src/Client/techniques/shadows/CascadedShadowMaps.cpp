#include "CascadedShadowMaps.h"

#include "Client/Fvog/Rendering2.h"
#include "Game/Assets.h"
#include "shaders/Config.shared.h"

#include "glm/gtc/epsilon.hpp"
#include "glm/gtc/matrix_transform.hpp"

Techniques::CascadedShadowMap::CascadedShadowMap()
{
  shadowPipeline_ = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Shadow voxel pipeline",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "Voxels/RayTracedVoxelsShadow.frag.glsl",
      },
    .state =
      {
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
        .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = VK_COMPARE_OP_ALWAYS},
        .renderTargetFormats =
          {
            .depthAttachmentFormat = shadowFormat,
          },
      },
  });

  shadowPassArguments_.emplace(Fvog::TypedBufferCreateInfo{}, "Shadow map arguments");
  shadowMapInfo_.emplace(Fvog::TypedBufferCreateInfo{}, "Shadow map info");
}

Fvog::Texture& Techniques::CascadedShadowMap::RenderTerrainShadowMap(VkCommandBuffer cmd, const CascadedShadowMapRenderTerrainParams& params)
{
  auto ctx = Fvog::Context(cmd);
  auto marker = ctx.MakeScopedDebugMarker("Render cascaded shadow map");

  if (!shadowArrayTexture_ || Fvog::Extent2D(shadowArrayTexture_->GetCreateInfo().extent) != params.shadowResolution ||
      params.numCascades != shadowArrayTexture_->GetCreateInfo().arrayLayers)
  {
    shadowArrayTexture_ = Fvog::CreateTexture2DArray({params.shadowResolution.width, params.shadowResolution.height},
      params.numCascades,
      shadowFormat,
      Fvog::TextureUsage::ATTACHMENT_READ_ONLY,
      "Shadow map");

    ctx.ImageBarrierDiscard(shadowArrayTexture_.value(), VK_IMAGE_LAYOUT_GENERAL);

    shadowArrayTextureViews_ = std::make_unique<std::optional<Fvog::TextureView>[]>(params.numCascades);

    for (uint32_t i = 0; i < params.numCascades; i++)
    {
      shadowArrayTextureViews_[i] = Fvog::TextureView(shadowArrayTexture_.value(),
        {
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = shadowFormat,
          .subresourceRange =
            VkImageSubresourceRange{
              .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
              .baseMipLevel   = 0,
              .levelCount     = VK_REMAINING_MIP_LEVELS,
              .baseArrayLayer = i,
              .layerCount     = 1,
            },
        },
        "Shadow Cascade View");
    }
  }

  auto args = RayTracedVoxelsShadowArgs_t{
    .voxels = params.voxels,
    .shadow = GetShadowInfoBufferAddress(),
  };

  auto shadow = CascadedShadowMapInfoPtr_t{
    .shadowMapArray = shadowArrayTexture_.value().ImageView().GetTexture2DArray(),
    .numCascades    = params.numCascades,
  };

  const auto up = glm::epsilonEqual(abs(glm::dot(params.lightDirection, glm::vec3(0, 1, 0))), 1.0f, 1e-3f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
  const auto view_from_world = glm::lookAt(params.playerPos, params.playerPos + params.lightDirection, up);

  for (uint32_t i = 0; i < params.numCascades; i++)
  {
    const auto side             = (params.baseFrustumSideLength / 2) * exp2(float(i));
    const auto clip_from_view   = glm::ortho(-side, side, -side, side, -params.frustumDepth / 2, params.frustumDepth / 2);
    shadow.cascades[i].clip_from_world = clip_from_view * view_from_world;
    shadow.cascades[i].world_from_clip = glm::inverse(shadow.cascades[i].clip_from_world);
  }
  ctx.TeenyBufferUpdate(shadowMapInfo_.value(), shadow);
  ctx.TeenyBufferUpdate(shadowPassArguments_.value(), args);

  ctx.SetPushConstants(shadowPassArguments_->GetDeviceAddress());
  for (uint32_t i = 0; i < params.numCascades; i++)
  {
    ctx.BeginRendering({
      .name            = "Shadow cascade",
      .depthAttachment =
        Fvog::RenderDepthStencilAttachment{
          .texture = shadowArrayTextureViews_[i].value(),
          .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        },
    });
    ctx.BindGraphicsPipeline(shadowPipeline_.GetPipeline());
    ctx.SetPushConstants(i, offsetof(RTVSMPushConstants, cascadeIndex));
    ctx.Draw(3, 1, 0, 0);
    ctx.EndRendering();
  }

  return shadowArrayTexture_.value();
}

CascadedShadowMapInfoPtr Techniques::CascadedShadowMap::GetShadowInfoBufferAddress()
{
  return shadowMapInfo_->GetDeviceAddress();
}