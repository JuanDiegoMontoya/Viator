#include "Sky.h"
#include "Client/PipelineManager.h"
#include "Client/Fvog/Device.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/Fvog/Texture2.h"
#include "Game/Assets.h"
#include "shaders/sky/SkyShared.h.glsl"

#include "glm/gtc/matrix_transform.hpp"

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

        aerialPerspectivePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "Aerial Perspective LUT Pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "sky/AerialPerspectiveLUT.comp.glsl",
            },
        });
      }

      void EnsureResources(VkCommandBuffer cmd, const SkyResourceExtents& extents) override
      {
        auto ctx = Fvog::Context(cmd);
        EnsureTexture(ctx, transmittanceLut, extents.transmittanceLutExtent, Fvog::Format::R16G16B16A16_SFLOAT, "Sky: Transmittance LUT");
        EnsureTexture(ctx, multiscatteringLut, extents.multiscatteringLutExtent, Fvog::Format::R16G16B16A16_SFLOAT, "Sky: Multiple Scattering LUT");
        EnsureTexture(ctx, skyViewLut, extents.skyViewLutExtent, Fvog::Format::R16G16B16A16_SFLOAT, "Sky: Sky View LUT");

        if (!aerialPerspectiveTransmittance || aerialPerspectiveTransmittance->GetCreateInfo().extent != extents.aerialPerspectiveLutExtent)
        {
          aerialPerspectiveTransmittance = Fvog::Texture({
            .viewType    = VK_IMAGE_VIEW_TYPE_3D,
            .format      = Fvog::Format::R16G16B16A16_SFLOAT,
            .extent      = extents.aerialPerspectiveLutExtent,
            .usage       = Fvog::TextureUsage::GENERAL,
          }, "Sky: Aerial Perspective Transmittance");
        }

        if (!aerialPerspectiveScattering || aerialPerspectiveScattering->GetCreateInfo().extent != extents.aerialPerspectiveLutExtent)
        {
          aerialPerspectiveScattering = Fvog::Texture(
            {
              .viewType = VK_IMAGE_VIEW_TYPE_3D,
              .format   = Fvog::Format::R16G16B16A16_SFLOAT,
              .extent   = extents.aerialPerspectiveLutExtent,
              .usage    = Fvog::TextureUsage::GENERAL,
            },
            "Sky: Aerial Perspective Scattering");
        }
      }

      SkyData GetSkyData(const SkyGetSkyDataParams& params) override
      {
        auto clip_from_view  = glm::perspectiveRH_ZO(params.fovy, params.aspectRatio, params.zNear, params.zFar);
        clip_from_view[1][1] *= -1;
        const auto clip_from_world = clip_from_view * params.view_from_world;
        const auto world_from_clip = glm::inverse(clip_from_world);

        return SkyData{
          .config = params.skyConfig,
          .luts =
            SkyLuts{
              .transmittanceLut               = transmittanceLut.value().ImageView().GetTexture2D(),
              .multiscatteringLut             = multiscatteringLut.value().ImageView().GetTexture2D(),
              .skyViewLut                     = skyViewLut.value().ImageView().GetTexture2D(),
              .aerialPerspectiveTransmittance = aerialPerspectiveTransmittance.value().ImageView().GetTexture3D(),
              .aerialPerspectiveScattering    = aerialPerspectiveScattering.value().ImageView().GetTexture3D(),
            },
          .ae_clip_from_world = clip_from_world,
          .ae_world_from_clip = world_from_clip,
          .ae_zNear           = params.zNear,
          .ae_zFar            = params.zFar,
        };
      }

      void ComputeTransmittanceLut(VkCommandBuffer cmd, const SkyComputeTransmittanceLutParams& params) override
      {
        auto ctx = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Compute transmittance LUT");

        auto gpuParams = Fvog::GetDevice().AllocTransient<TransmittanceGpuParams_t>();

        *gpuParams = {
          .uniforms           = params.globalUniformsPtr,
          .transmittanceImage = transmittanceLut.value().ImageView().GetImage2D(),
        };

        ctx.BindComputePipeline(skyTransmittancePipeline.GetPipeline());
        ctx.SetPushConstants(gpuParams);
        ctx.DispatchInvocations(transmittanceLut.value().GetCreateInfo().extent);
      }

      void ComputeMultiscatteringLut(VkCommandBuffer cmd, const SkyComputeMultiscatteringLutParams& params) override
      {
        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Compute multiple scattering LUT");

        auto gpuParams = Fvog::GetDevice().AllocTransient<MultiscatteringGpuParams_t>();

        *gpuParams = {
          .uniforms             = params.globalUniformsPtr,
          .transmittanceTexture = transmittanceLut.value().ImageView().GetTexture2D(),
          .multiscatteringImage = multiscatteringLut.value().ImageView().GetImage2D(),
        };

        ctx.BindComputePipeline(skyMultiscatteringPipeline.GetPipeline());
        ctx.SetPushConstants(gpuParams);
        ctx.DispatchInvocations(multiscatteringLut.value().GetCreateInfo().extent);
      }

      void ComputeSkyViewLut(VkCommandBuffer cmd, const SkyComputeSkyViewLutParams& params) override
      {
        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Compute sky view LUT");

        auto gpuParams = Fvog::GetDevice().AllocTransient<SkyViewGpuParams_t>();

        *gpuParams = {
          .uniforms               = params.globalUniformsPtr,
          .transmittanceTexture   = transmittanceLut.value().ImageView().GetTexture2D(),
          .multiscatteringTexture = multiscatteringLut.value().ImageView().GetTexture2D(),
          .skyViewImage           = skyViewLut.value().ImageView().GetImage2D(),
        };

        ctx.BindComputePipeline(skyViewPipeline.GetPipeline());
        ctx.SetPushConstants(gpuParams);
        ctx.DispatchInvocations(skyViewLut.value().GetCreateInfo().extent);
      }

      void ComputeAerialPerspectiveLut(VkCommandBuffer cmd, const SkyComputeAerialPerspectiveLutParams& params) override
      {
        ASSERT(aerialPerspectiveScattering->GetCreateInfo().extent == aerialPerspectiveTransmittance->GetCreateInfo().extent);
        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Compute aerial perspective LUT");

        auto gpuParams = Fvog::GetDevice().AllocTransient<AerialPerspectiveGpuParams_t>();

        *gpuParams = {
          .uniforms                       = params.globalUniformsPtr,
          .transmittanceTexture           = transmittanceLut.value().ImageView().GetTexture2D(),
          .multiscatteringTexture         = multiscatteringLut.value().ImageView().GetTexture2D(),
          .aerialPerspectiveTransmittance = aerialPerspectiveTransmittance.value().ImageView().GetImage3D(),
          .aerialPerspectiveScattering    = aerialPerspectiveScattering.value().ImageView().GetImage3D(),
        };

        ctx.BindComputePipeline(aerialPerspectivePipeline.GetPipeline());
        ctx.SetPushConstants(gpuParams);
        const auto extent = aerialPerspectiveScattering.value().GetCreateInfo().extent;
        //ctx.DispatchInvocations(extent.width, extent.height, 1); // TODO: 2D dispatch.
        ctx.DispatchInvocations(extent);
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

      Fvog::Texture& GetAerialPerspectiveTransmittance() override
      {
        return aerialPerspectiveTransmittance.value();
      }
      
      Fvog::Texture& GetAerialPerspectiveScattering() override
      {
        return aerialPerspectiveScattering.value();
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
      std::optional<Fvog::Texture> aerialPerspectiveTransmittance;
      std::optional<Fvog::Texture> aerialPerspectiveScattering;

      PipelineManager::ComputePipelineKey skyTransmittancePipeline;
      PipelineManager::ComputePipelineKey skyMultiscatteringPipeline;
      PipelineManager::ComputePipelineKey skyViewPipeline;
      PipelineManager::ComputePipelineKey aerialPerspectivePipeline;
    };
  }

  std::unique_ptr<Sky> Sky::Create()
  {
    return std::make_unique<SkyImpl>();
  }
}