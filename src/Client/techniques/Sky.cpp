#include "Sky.h"
#include "Client/PipelineManager.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/Fvog/Texture2.h"
#include "Game/Assets.h"
#include "shaders/sky/SkyShared.h.glsl"

#include "vulkan/vulkan_core.h"

namespace Techniques
{
  namespace
  {
    class SkyImpl : public Sky
    {
    public:
      SkyImpl()
      {
        skyTransmittancePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "Sky Transmittance LUT Pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "sky/TransmittanceLUT.comp.glsl",
            },
        });

        skyMultiscatteringPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "Sky Multiscattering LUT Pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "sky/MultiscatteringLUT.comp.glsl",
            },
        });

        skyViewPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "Sky View LUT Pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "sky/SkyViewLUT.comp.glsl",
            },
        });
      }

      void EnsureResources(VkCommandBuffer cmd, const SkyResourceExtents& extents) override
      {
        auto ctx = Fvog::Context(cmd);
        EnsureTexture(ctx, transmittanceLut, extents.transmittanceLutExtent, Fvog::Format::R16G16B16A16_SFLOAT, "Sky: Transmittance LUT");
        EnsureTexture(ctx, multiscatteringLut, extents.multiscatteringLutExtent, Fvog::Format::R16G16B16A16_SFLOAT, "Sky: Multiple Scattering LUT");
        EnsureTexture(ctx, skyViewLut, extents.skyViewLutExtent, Fvog::Format::R16G16B16A16_SFLOAT, "Sky: Sky View LUT");

        if (!aerialPerspectiveLut || aerialPerspectiveLut->GetCreateInfo().extent != extents.aerialPerspectiveLutExtent)
        {
          aerialPerspectiveLut = Fvog::Texture({
            .viewType    = VK_IMAGE_VIEW_TYPE_3D,
            .format      = Fvog::Format::R16G16B16A16_SFLOAT,
            .extent      = extents.aerialPerspectiveLutExtent,
            .usage       = Fvog::TextureUsage::GENERAL,
          }, "Sky: Aerial Perspective LUT");
        }
      }

      void ComputeTransmittanceLut(VkCommandBuffer cmd, const SkyComputeTransmittanceLutParams& params) override
      {
        auto ctx = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Sky: compute transmittance LUT");

        ctx.BindComputePipeline(skyTransmittancePipeline.GetPipeline());
        ctx.SetPushConstants(TransmittancePush{
          .globalUniformsIndexTransmittance = params.globalUniformsBufferIndex,
          .transmittanceImage               = transmittanceLut.value().ImageView().GetImage2D(),
        });
        ctx.DispatchInvocations(transmittanceLut.value().GetCreateInfo().extent);
      }

      void ComputeMultiscatteringLut(VkCommandBuffer cmd, const SkyComputeMultiscatteringLutParams& params) override
      {
        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Sky: compute multiple scattering LUT");

        ctx.BindComputePipeline(skyMultiscatteringPipeline.GetPipeline());
        ctx.SetPushConstants(MultiscatteringPush{
          .globalUniformsIndexMultiscattering = params.globalUniformsBufferIndex,
          .transmittanceTexture               = transmittanceLut.value().ImageView().GetTexture2D(),
          .multiscatteringImage               = multiscatteringLut.value().ImageView().GetImage2D(),
        });
        ctx.DispatchInvocations(multiscatteringLut.value().GetCreateInfo().extent);
      }

      void ComputeSkyViewLut(VkCommandBuffer cmd, const SkyComputeSkyViewLutParams& params) override
      {
        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Sky: compute sky view LUT");

        ctx.BindComputePipeline(skyViewPipeline.GetPipeline());
        ctx.SetPushConstants(SkyViewPush{
          .globalUniformsIndexSkyView = params.globalUniformsBufferIndex,
          .transmittanceTexture       = transmittanceLut.value().ImageView().GetTexture2D(),
          .multiscatteringTexture     = multiscatteringLut.value().ImageView().GetTexture2D(),
          .skyViewImage               = skyViewLut.value().ImageView().GetImage2D(),
        });
        ctx.DispatchInvocations(skyViewLut.value().GetCreateInfo().extent);
      }

      void ComputeAerialPerspectiveLut([[maybe_unused]] VkCommandBuffer cmd, [[maybe_unused]] const SkyComputeAerialPerspectiveLutParams& params) override
      {
        ASSERT(false);
      }

      Fvog::Texture& GetTransmittanceLut() override
      {
        return transmittanceLut.value();
      }

      Fvog::Texture& GetMultiscatteringLut() override
      {
        return multiscatteringLut.value();
      }

      Fvog::Texture& GetSkyViewLut() override
      {
        return skyViewLut.value();
      }

      Fvog::Texture& GetAerialPerspectivelut() override
      {
        return aerialPerspectiveLut.value();
      }

    private:
      static void EnsureTexture(const Fvog::Context& ctx, std::optional<Fvog::Texture>& texture, Fvog::Extent2D extent, Fvog::Format format, std::string name)
      {
        if (!texture || Fvog::Extent2D(texture->GetCreateInfo().extent) != extent)
        {
          texture = Fvog::CreateTexture2D({extent.width, extent.height}, format, Fvog::TextureUsage::GENERAL, std::move(name));
          ctx.ImageBarrierDiscard(*texture, VK_IMAGE_LAYOUT_GENERAL);
          ctx.ClearTexture(*texture, {});
          ctx.Barrier();
        }
      }

      std::optional<Fvog::Texture> transmittanceLut;
      std::optional<Fvog::Texture> multiscatteringLut;
      std::optional<Fvog::Texture> skyViewLut;
      std::optional<Fvog::Texture> aerialPerspectiveLut;

      PipelineManager::ComputePipelineKey skyTransmittancePipeline;
      PipelineManager::ComputePipelineKey skyMultiscatteringPipeline;
      PipelineManager::ComputePipelineKey skyViewPipeline;
    };
  }

  std::unique_ptr<Sky> Sky::Create()
  {
    return std::make_unique<SkyImpl>();
  }
}