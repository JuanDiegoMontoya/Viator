#include "RayMarchedClouds.h"

#include "Client/PipelineManager.h"
#include "Client/Fvog/Device.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/Fvog/Texture2.h"
#include "Game/Assets.h"

#include "shaders/volumetric/clouds/RenderRayMarchedClouds.comp.glsl"

#include "glm/integer.hpp"
#include "imgui.h"

#include <optional>

namespace Techniques
{
  namespace // Helpers yoinked from ffx_fsr2.cpp
  {
    float halton(int32_t index, int32_t base)
    {
      float f = 1.0f, result = 0.0f;

      for (int32_t currentIndex = index; currentIndex > 0;)
      {
        f /= (float)base;
        result       = result + f * (float)(currentIndex % base);
        currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
      }

      return result;
    }

    int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
    {
      const float basePhaseCount     = 8.0f;
      const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
      return jitterPhaseCount;
    }

    glm::vec2 GetJitterOffsetSS(int32_t index, int32_t phaseCount)
    {
      ASSERT(phaseCount > 0);
      const float x = halton((index % phaseCount) + 1, 2) - 0.5f;
      const float y = halton((index % phaseCount) + 1, 3) - 0.5f;
      return {x, y};
    }

    glm::vec2 GetJitterOffsetUV(uint32_t frameIndex, uint32_t renderInternalWidth, uint32_t renderInternalHeight, uint32_t renderOutputWidth)
    {
      const auto jitter = GetJitterOffsetSS(frameIndex, GetJitterPhaseCount(renderInternalWidth, renderOutputWidth));
      return {jitter.x / static_cast<float>(renderInternalWidth), jitter.y / static_cast<float>(renderInternalHeight)};
    }
  }

  namespace
  {
    class RayMarchedCloudsImpl : public RayMarchedClouds
    {
    public:
      RayMarchedCloudsImpl()
      {
        renderCloudsPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
          .name             = "Render clouds pipeline",
          .shaderModuleInfo = {.path = GetShaderDirectory() / "volumetric/clouds/RenderRayMarchedClouds.comp.glsl"},
        });

        upscaleCloudsPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
          .name             = "Upscale clouds pipeline",
          .shaderModuleInfo = {.path = GetShaderDirectory() / "volumetric/clouds/TemporalUpscaleClouds.comp.glsl"},
        });

        compositeCloudsPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
          .name             = "Composite clouds pipeline",
          .shaderModuleInfo = {.path = GetShaderDirectory() / "volumetric/clouds/CompositeClouds.comp.glsl"},
        });
      }

      void Render(VkCommandBuffer cmd, const RayMarchedCloudsRenderParams& params) override
      {
        ASSERT(params.gDepth);
        auto ctx = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Clouds: render");

        EnsureTexture(ctx,
          lowResCloudRadianceTransmittance_,
          {params.renderWidth, params.renderHeight},
          Fvog::Format::R16G16B16A16_SFLOAT,
          "Low Res Cloud Radiance & Transmittance");

        EnsureTexture(ctx,
          lowResCloudMotionVectors_,
          {params.renderWidth, params.renderHeight},
          Fvog::Format::R16G16_SFLOAT,
          "Low Res Cloud Motion Vectors");

        auto jitterNDC = 2.0f * GetJitterOffsetUV(params.frameNumber, params.renderWidth, params.renderHeight, params.upscaleWidth);
        //auto jitterNDC = glm::vec2(rng.RandFloat(-1, 1), rng.RandFloat(-1, 1)) / glm::vec2(params.renderWidth, params.renderHeight);

#if 0   // 
        static bool overrideJitter = false;
        static glm::vec2 jitterOverride{};
        ImGui::Checkbox("Override jitter", &overrideJitter);
        ImGui::InputFloat2("Jitter override", &jitterOverride[0]);
        if (overrideJitter)
        {
          jitterNDC = jitterOverride / glm::vec2(params.renderWidth, params.renderHeight);
        }
#endif

        currentJitterNDC     = jitterNDC;

        const auto jitterMatrix            = glm::translate(glm::mat4(1), glm::vec3(jitterNDC, 0));
        const auto clip_from_view_jittered = jitterMatrix * params.clip_from_view;
        const auto clip_from_world         = clip_from_view_jittered * params.view_from_world;
        const auto world_from_clip         = glm::inverse(clip_from_world);

        auto gpuParams = Fvog::GetDevice().AllocTransient<RayMarchedCloudsRenderGpuParams_t>();

        *gpuParams = {
          .gDepth                         = params.gDepth->ImageView().GetTexture2D(),
          .outMotionVectors               = lowResCloudMotionVectors_.value().ImageView().GetImage2D(),
          .outRadianceTransmittance       = lowResCloudRadianceTransmittance_.value().ImageView().GetImage2D(),
          .nearestSampler                 = Fvog::Sampler({.magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST}).GetResourceHandle().index,
          .world_from_clip                = world_from_clip,
          .clip_from_world                = clip_from_world,
          .clip_from_world_old            = params.clip_from_view_old * params.view_from_world_old,
          .clip_from_world_unjittered     = params.clip_from_view * params.view_from_world,
          .clip_from_world_old_unjittered = clip_from_view_old_unjittered * params.view_from_world_old,
          .numRayMarchSteps               = params.numRayMarchSteps,
          .jitterUV                       = jitterNDC / 2.0f,
          .sunDirection                   = params.sunDirection,
          .sunIntensity                   = params.sunIntensity,
          .globalUniformsIndex            = params.globalUniformsIndex,
          .ddgi                           = params.ddgi,
          .zNear                          = params.zNear,
        };

        const auto& outputResolution = params.gDepth->GetCreateInfo().extent;
        ctx.BindComputePipeline(renderCloudsPipeline_.GetPipeline());
        ctx.SetPushConstants(gpuParams);
        ctx.DispatchInvocations(outputResolution.width, outputResolution.height, 1);

        clip_from_view_old = clip_from_view_jittered;
        clip_from_view_old_unjittered = params.clip_from_view;
      }

      void Upscale(VkCommandBuffer cmd, const RayMarchedCloudsUpscaleParams& params) override
      {
        ASSERT(params.gDepth);
        ASSERT(lowResCloudRadianceTransmittance_.has_value());
        ASSERT(lowResCloudMotionVectors_.has_value());
        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Clouds: upscale");
        
        EnsureTexture(ctx,
          highResCloudRadianceTransmittance_,
          {params.upscaleWidth, params.upscaleHeight},
          Fvog::Format::R16G16B16A16_SFLOAT,
          "High Res Cloud Radiance & Transmittance");

        EnsureTexture(ctx,
          highResCloudRadianceTransmittanceHistory_,
          {params.upscaleWidth, params.upscaleHeight},
          Fvog::Format::R16G16B16A16_SFLOAT,
          "High Res Cloud Radiance & Transmittance 2");

        std::swap(highResCloudRadianceTransmittance_, highResCloudRadianceTransmittanceHistory_);

        auto gpuParams = Fvog::GetDevice().AllocTransient<UpscaleCloudsGpuParams_t>();

        *gpuParams = {
          .inLowResCloudRadianceTransmittance = lowResCloudRadianceTransmittance_->ImageView().GetTexture2D(),
          .inLowResCloudMotionVectors         = lowResCloudMotionVectors_.value().ImageView().GetTexture2D(),
          .inOldCloudRadianceTransmittance    = highResCloudRadianceTransmittanceHistory_.value().ImageView().GetTexture2D(),
          .inHighResDepth                     = params.gDepth->ImageView().GetTexture2D(),
          .inHighResDepthPrev                 = params.gDepthPrev->ImageView().GetTexture2D(),
          .outCloudRadianceTransmittance      = highResCloudRadianceTransmittance_.value().ImageView().GetImage2D(),
          .linearSampler = Fvog::Sampler({
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          }),
          .jitterUV = 0.5f * currentJitterNDC,
          .zNear = params.zNear,
        };

        ctx.BindComputePipeline(upscaleCloudsPipeline_.GetPipeline());
        ctx.SetPushConstants(gpuParams);
        ctx.DispatchInvocations(params.upscaleWidth, params.upscaleHeight, 1);
      }

      void Composite(VkCommandBuffer cmd, const RayMarchedCloudsCompositeParams& params) override
      {
        ASSERT(params.gRadianceIn);
        ASSERT(params.gRadianceOut);
        ASSERT(highResCloudRadianceTransmittance_.has_value());

        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Clouds: composite");

        const auto& outputResolution = params.gRadianceOut->GetCreateInfo().extent;

        ctx.BindComputePipeline(compositeCloudsPipeline_.GetPipeline());
        auto gpuParams = Fvog::GetDevice().AllocTransient<RayMarchedCloudsCompositeGpuParams_t>();
        *gpuParams     = {
              .inOpaqueRadiance             = params.gRadianceIn->ImageView().GetTexture2D(),
              .inCloudRadianceTransmittance = highResCloudRadianceTransmittance_->ImageView().GetTexture2D(),
              .outRadiance                  = params.gRadianceOut->ImageView().GetImage2D(),
        };
        ctx.SetPushConstants(gpuParams);
        ctx.DispatchInvocations(outputResolution.width, outputResolution.height, 1);
      }

    private:
      static void EnsureTexture(Fvog::Context ctx, std::optional<Fvog::Texture>& texture, Fvog::Extent2D extent, Fvog::Format format, std::string name)
      {
        if (!texture || Fvog::Extent2D(texture->GetCreateInfo().extent) != extent)
        {
          texture = Fvog::CreateTexture2D({extent.width, extent.height}, format, Fvog::TextureUsage::GENERAL, std::move(name));
          ctx.ImageBarrierDiscard(*texture, VK_IMAGE_LAYOUT_GENERAL);
          ctx.ClearTexture(*texture, {});
          ctx.Barrier();
        }
      }

      PipelineManager::ComputePipelineKey renderCloudsPipeline_;
      PipelineManager::ComputePipelineKey upscaleCloudsPipeline_;
      PipelineManager::ComputePipelineKey compositeCloudsPipeline_;
      std::optional<Fvog::Texture> lowResCloudRadianceTransmittance_;
      std::optional<Fvog::Texture> lowResCloudMotionVectors_;
      std::optional<Fvog::Texture> highResCloudRadianceTransmittance_;
      std::optional<Fvog::Texture> highResCloudRadianceTransmittanceHistory_;

      glm::vec2 currentJitterNDC{};
      glm::mat4 clip_from_view_old = glm::mat4(1);
      glm::mat4 clip_from_view_old_unjittered = glm::mat4(1);
    };
  }

  std::unique_ptr<RayMarchedClouds> RayMarchedClouds::Create()
  {
    return std::make_unique<RayMarchedCloudsImpl>();
  }
}