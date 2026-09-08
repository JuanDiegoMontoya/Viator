#include "FoliageSSS.h"

#include "MathUtilities.h"
#include "Client/Scheduler.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/Fvog/Texture2.h"
#include "Client/PipelineManager.h"
#include "Client/Fvog/Device.h"

#include "shaders/voxels/FoliageSSS.comp.glsl"

#include "Core/Assert2.h"
#include "Game/Assets.h"

#include "glm/gtc/epsilon.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <optional>
#include <ranges>

namespace
{
  class FoliageSSSImpl : public Techniques::FoliageSSS
  {
  public:
    FoliageSSSImpl()
    {
      cbsmPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "Foliage SSS CBSM Pipeline",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "voxels/FoliageSSS.comp.glsl",
          },
      });

      cbsmInfoBuffer.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR});
    }

    void RenderBeerShadowMap(Scheduler& scheduler, VkCommandBuffer cmd, const Techniques::FoliageSSSRenderBeerShadowMapParams& params) override
    {
      scheduler.AddPass("FoliageSSS_Prepare",
        [=, this]
        {
          EnsureTexture(cmd,
            meanExtinctionTexture,
            {
              .viewType    = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
              .format      = Fvog::Format::R16G16B16A16_SFLOAT,
              .extent      = Fvog::Extent3D{params.shadowResolution.width, params.shadowResolution.height, 1},
              .arrayLayers = params.numCascades,
            },
            "Foliage SSS Mean Extinction Texture");

          EnsureTexture(cmd,
            volumePositionTexture,
            {
              .viewType    = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
              .format      = Fvog::Format::R16G16B16A16_SFLOAT,
              .extent      = Fvog::Extent3D{params.shadowResolution.width, params.shadowResolution.height, 1},
              .arrayLayers = params.numCascades,
            },
            "Foliage SSS Volume Bounds (BSM) Texture");

          auto cbsmInfoCpu = FoliageCBSMInfoPtr_t{
            .cascades             = {},
            .meanExtinctionArray  = meanExtinctionTexture->ImageView().GetTexture2DArray(),
            .volumePositionArray  = volumePositionTexture->ImageView().GetTexture2DArray(),
            .meanExtinctionImages = meanExtinctionTexture->ImageView().GetImage2DArray(),
            .volumePositionImages = volumePositionTexture->ImageView().GetImage2DArray(),
            .numCascades          = params.numCascades,
            .frustumDepth         = params.frustumDepth,
          };

          const auto up = glm::epsilonEqual(abs(glm::dot(params.lightDirection, glm::vec3(0, 1, 0))), 1.0f, 1e-3f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
          const auto view_from_world = glm::lookAt(params.playerPos, params.playerPos + params.lightDirection, up);

          for (auto [i, cascade] : std::views::enumerate(cbsmInfoCpu.cascades))
          {
            const auto side                = static_cast<float>((params.baseFrustumSideLength / 2) * exp2(float(i)));
            const auto clip_from_view_temp = glm::ortho(-side, side, -side, side, -params.frustumDepth / 2, params.frustumDepth / 2);
            const auto clip_from_view      = Math::SnapProjectionToTexel(clip_from_view_temp, view_from_world, {params.shadowResolution.width, params.shadowResolution.height}, 1024);

            cascade.clip_from_world = clip_from_view * view_from_world;
            cascade.world_from_clip = glm::inverse(cascade.clip_from_world);
          }

          auto ctx  = Fvog::Context(cmd);
          ctx.TeenyBufferUpdate(cbsmInfoBuffer.value(), cbsmInfoCpu);
        });

      scheduler.AddPass("FoliageSSS",
        {"FoliageSSS_Prepare"},
        [=, this]
        {
          auto gpuParams = Fvog::GetDevice().AllocTransient<RenderFoliageSSSGpuParams_t>();

          *gpuParams = {
            .voxels = params.voxels,
            .cbsm   = GetCBSMInfoPtr(),
          };

          auto ctx = Fvog::Context(cmd);
          ctx.BindComputePipeline(cbsmPipeline.GetPipeline());
          const auto extent = meanExtinctionTexture->GetCreateInfo().extent;
          ctx.SetPushConstants(gpuParams);
          ctx.DispatchInvocations(extent.width, extent.height, meanExtinctionTexture->GetCreateInfo().arrayLayers);
        });
    }

    VkDeviceAddress GetCBSMInfoPtr() override
    { 
      return cbsmInfoBuffer.value().GetDeviceAddress();
    }

  private:
    static void EnsureTexture(VkCommandBuffer cmd, std::optional<Fvog::Texture>& texture, const Fvog::TextureCreateInfo& createInfo, std::string_view name)
    {
      if (texture.has_value() && texture->GetCreateInfo() == createInfo)
      {
        return;
      }

      texture = Fvog::Texture(createInfo, std::string(name));

      auto ctx = Fvog::Context(cmd);
      ctx.ImageBarrierDiscard(*texture, VK_IMAGE_LAYOUT_GENERAL);
      ctx.ClearTexture(*texture);
      ctx.Barrier();
    }

    std::optional<Fvog::Texture> meanExtinctionTexture;
    std::optional<Fvog::Texture> volumePositionTexture;
    std::optional<Fvog::TypedBuffer<FoliageCBSMInfoPtr_t>> cbsmInfoBuffer{};
    PipelineManager::ComputePipelineKey cbsmPipeline;
  };
} // namespace

std::unique_ptr<Techniques::FoliageSSS> Techniques::FoliageSSS::Create()
{
  return std::make_unique<FoliageSSSImpl>();
}