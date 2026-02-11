#include "vulkan/vulkan_core.h"
#include "ScreenSpaceGI.h"

#include "Client/Fvog/Rendering2.h"
#include "Game/Assets.h"
#include "shaders/ao/ssgi/ModulateAlbedo.comp.glsl"

Techniques::ScreenSpaceGI::ScreenSpaceGI()
{
  ssgiPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
    .name             = "SSILVB",
    .shaderModuleInfo = {.path = GetShaderDirectory() / "ao/ssgi/ScreenSpaceGI.comp.glsl"},
  });

  modulateAlbedoPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
    .name             = "Modulate Albedo",
    .shaderModuleInfo = {.path = GetShaderDirectory() / "ao/ssgi/ModulateAlbedo.comp.glsl"},
  });
}

Fvog::Texture& Techniques::ScreenSpaceGI::Dispatch(VkCommandBuffer cmd, const ScreenSpaceGIDispatchInfo& params)
{
  ASSERT(params.inputDepth);
  ASSERT(params.inputNormal);
  ASSERT(params.inputDiffuseLuminance);

  auto ctx    = Fvog::Context(cmd);
  auto marker = ctx.MakeScopedDebugMarker("Screen-space GI");

  if (!diffuseIlluminance_ || Fvog::Extent2D(diffuseIlluminance_->GetCreateInfo().extent) != params.outputSize)
  {
    diffuseIlluminance_ = Fvog::CreateTexture2D(params.outputSize, Fvog::Format::R16G16B16A16_SFLOAT, Fvog::TextureUsage::GENERAL, "SSGI Diffuse Illuminance");
    ctx.ImageBarrierDiscard(diffuseIlluminance_.value(), VK_IMAGE_LAYOUT_GENERAL);
  }

  if (!outputLuminance_ || Fvog::Extent2D(outputLuminance_->GetCreateInfo().extent) != params.outputSize)
  {
    outputLuminance_ = Fvog::CreateTexture2D(params.outputSize, Fvog::Format::R16G16B16A16_SFLOAT, Fvog::TextureUsage::GENERAL, "SSGI Output Luminance");
    ctx.ImageBarrierDiscard(outputLuminance_.value(), VK_IMAGE_LAYOUT_GENERAL);
  }

  {
    auto marker2 = ctx.MakeScopedDebugMarker("Sample lighting");
    uniforms_.UpdateData(cmd,
     SSGIParams_t{
       .gDepth          = params.inputDepth->ImageView().GetTexture2D(),
       .gNormal         = params.inputNormal->ImageView().GetTexture2D(),
       .gIlluminance    = params.inputDiffuseLuminance->ImageView().GetTexture2D(),
       .outIlluminance  = diffuseIlluminance_->ImageView().GetImage2D(),
       .sliceCount      = params.sliceCount,
       .sampleCount     = params.sampleCount,
       .sampleRadius    = params.sampleRadius,
       .hitThickness    = params.hitThickness,
       .view_from_world = params.view_from_world,
       .world_from_view = glm::inverse(params.view_from_world),
       .clip_from_view  = params.clip_from_view,
       .view_from_clip  = glm::inverse(params.clip_from_view),
       .world_from_clip = glm::inverse(params.clip_from_view * params.view_from_world),
       .debugDraw       = params.debugDraw,
       .debugCapture    = params.debugCapture,
   });
    ctx.SetPushConstants(uniforms_.GetDeviceBuffer().GetDeviceAddress());
    ctx.BindComputePipeline(ssgiPipeline_.GetPipeline());
    ctx.DispatchInvocations(outputLuminance_->GetCreateInfo().extent);
  }

  ctx.Barrier();

  {
    auto marker2 = ctx.MakeScopedDebugMarker("Modulate albedo");
    ctx.SetPushConstants(ModulateAlbedoArgs{
      .inputDiffuseLuminance   = params.inputDiffuseLuminance->ImageView().GetTexture2D(),
      .inputAlbedo             = params.inputAlbedo->ImageView().GetTexture2D(),
      .inputDiffuseIlluminance = diffuseIlluminance_->ImageView().GetTexture2D(),
      .outputDiffuseLuminance  = outputLuminance_->ImageView().GetImage2D(),
    });
    ctx.BindComputePipeline(modulateAlbedoPipeline_.GetPipeline());
    ctx.DispatchInvocations(outputLuminance_->GetCreateInfo().extent);
  }

  return outputLuminance_.value();
}