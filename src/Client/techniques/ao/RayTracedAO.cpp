#include "RayTracedAO.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/RendererUtilities.h"
#include "Game/Assets.h"
#include "shaders/ao/rtao/RayTracedAO.comp.glsl"

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

namespace Techniques
{
  RayTracedAO::RayTracedAO() : upscaleUniforms_(1, "RTAO Upscale Uniforms")
  {
    rtaoPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
      .name             = "Ray Traced AO",
      .shaderModuleInfo = {.path = GetShaderDirectory() / "ao/rtao/RayTracedAO.comp.glsl"},
      .useMinSubgroupSize = true,
    });

    upscalePipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
      .name             = "Upscale Ray Traced AO",
      .shaderModuleInfo = {.path = GetShaderDirectory() / "ao/rtao/Upscale.comp.glsl"},
    });
  }

  Fvog::Texture& RayTracedAO::ComputeAO(VkCommandBuffer commandBuffer, const ComputeParams& params)
  {
    ASSERT(params.inputDepth);
    ASSERT(params.inputNormal);

    auto ctx = Fvog::Context(commandBuffer);

    if (!aoOutputTexture_ || Fvog::Extent2D(aoOutputTexture_->GetCreateInfo().extent) != params.outputSize)
    {
      aoOutputTexture_ = Fvog::CreateTexture2D(params.outputSize, Fvog::Format::R16_UNORM, Fvog::TextureUsage::GENERAL, "AO Texture");
    }

    const auto internalResolution = params.outputSize >> (params.upscaleFactor - 1);
    if (!aoLowResTexture_ || Fvog::Extent2D(aoLowResTexture_->GetCreateInfo().extent) != internalResolution)
    {
      aoLowResTexture_ = Fvog::CreateTexture2D(internalResolution, Fvog::Format::R16_UNORM, Fvog::TextureUsage::GENERAL, "AO low res Texture");
    }

    ctx.ImageBarrierDiscard(aoLowResTexture_.value(), VK_IMAGE_LAYOUT_GENERAL);
    ctx.ImageBarrierDiscard(aoOutputTexture_.value(), VK_IMAGE_LAYOUT_GENERAL);

    // Generate raw AO
    auto marker = ctx.MakeScopedDebugMarker("Ray Traced AO");
    ctx.BindComputePipeline(rtaoPipeline_.GetPipeline());
    ctx.SetPushConstants(RtaoArguments{
      .voxels      = params.voxels,
      .gDepth      = params.inputDepth->ImageView().GetTexture2D(),
      .gNormal     = params.inputNormal->ImageView().GetTexture2D(),
      .outputAo    = aoLowResTexture_.value().ImageView().GetImage2D(),
      .numRays     = uint32_t(params.numRays),
      .rayLength   = params.rayLength,
      .frameNumber = params.frameNumber,
    });
    ctx.DispatchInvocations(aoLowResTexture_->GetCreateInfo().extent);
    ctx.Barrier();

    if (params.upscaleFactor == 1)
    {
      return aoLowResTexture_.value();
    }

    // Upscale to output resolution
    ctx.BindComputePipeline(upscalePipeline_.GetPipeline());
    upscaleUniforms_.UpdateData(commandBuffer,
      FilterParams_t{
        .proj                     = params.clip_from_view,
        .invViewProj              = params.world_from_clip,
        .viewPos                  = params.cameraPosWS,
        .stepWidth                = params.stepWidth,
        .targetDim                = {aoOutputTexture_->GetCreateInfo().extent.width, aoOutputTexture_->GetCreateInfo().extent.height},
        .phiNormal                = params.phiNormal,
        .phiDepth                 = params.phiDepth,
        .rawAmbientOcclusion      = aoLowResTexture_->ImageView().GetTexture2D(),
        .gNormal                  = params.inputNormal->ImageView().GetTexture2D(),
        .gDepth                   = params.inputDepth->ImageView().GetTexture2D(),
        .upscaledAmbientOcclusion = aoOutputTexture_->ImageView().GetImage2D(),
      });
    ctx.SetPushConstants(FilterUniforms{
      .uniforms = upscaleUniforms_.GetDeviceBuffer().GetDeviceAddress(),
    });
    ctx.DispatchInvocations(aoOutputTexture_->GetCreateInfo().extent);
    ctx.Barrier();

    return aoOutputTexture_.value();
  }
} // namespace Techniques
