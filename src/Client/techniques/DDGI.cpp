#include "DDGI.h"

#include "Client/PipelineManager.h"
#include "Client/Fvog/Texture2.h"
#include "Client/Scheduler.h"
#include "Client/Fvog/Device.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/GpuMesh.h"
#include "Game/Assets.h"

#include "shaders/Config.shared.h"
#include "shaders/ddgi/ProbeCommon.shared.h"
#include "shaders/ddgi/DebugProbesCommon.h.glsl"

#include "tracy/Tracy.hpp"

namespace Techniques
{
  class DDGIImpl final : public DDGI
  {
  public:
    explicit DDGIImpl(const DDGIInitParams& params)
  {
    ZoneScoped;
    ASSERT(params.probeGridInfo);
    const auto& probeGridInfo = *params.probeGridInfo;
    ASSERT(probeGridInfo.probeRadianceResolution.x > 0);
    ASSERT(probeGridInfo.probeRadianceResolution.x == probeGridInfo.probeRadianceResolution.y);
    traceRaysPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "DDGI Trace Luminance",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "ddgi/TraceProbes.comp.glsl",
        },
      .useMinSubgroupSize = true,
    });

    convolveIrradiancePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "DDGI Convolve Illuminance",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "ddgi/ConvolveIrradiance.comp.glsl",
        },
    });

    downsampleDepthPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "DDGI Downsample Probe Depth",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "ddgi/DownsampleProbeDepth.comp.glsl",
        },
    });

    resetNewProbesPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "Reset New Probes",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "ddgi/ResetNewProbes.comp.glsl",
        },
    });

    debugProbesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
      .name = "Debug Probes",
      .vertexModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::VERTEX_SHADER,
          .path  = GetShaderDirectory() / "ddgi/DebugProbes.vert.glsl",
        },
      .fragmentModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
          .path  = GetShaderDirectory() / "ddgi/DebugProbes.frag.glsl",
        },
      .state =
        {
          .rasterizationState = {.cullMode = VK_CULL_MODE_BACK_BIT},
          .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = FVOG_COMPARE_OP_NEARER_OR_EQUAL},
          .renderTargetFormats =
            {
              .colorAttachmentFormats = {{params.sceneColorFormat}},
              .depthAttachmentFormat  = params.sceneDepthFormat,
            },
        },
    });


    args.gridInfo[0] = probeGridInfo;
    for (int i = 0; i < DDGI_NUM_CASCADES; i++)
    {
      args.gridInfo[i] = args.gridInfo[0];
    }
    argsBuffer.emplace(1, "DDGI Arguments");
    const auto numProbes = probeGridInfo.gridResolution.x * probeGridInfo.gridResolution.y * probeGridInfo.gridResolution.z;

    probeDataBuffers = std::make_unique<decltype(probeDataBuffers)::element_type[]>(DDGI_NUM_CASCADES);
    for (int i = 0; i < DDGI_NUM_CASCADES; i++)
    {
      probeDataBuffers[i].emplace(Fvog::TypedBufferCreateInfo{uint32_t(numProbes)}, std::format("Probe Data (cascade {})", i));
    }

    Fvog::GetDevice().ImmediateSubmit(
      [&](VkCommandBuffer cmd)
      {
        for (int i = 0; i < DDGI_NUM_CASCADES; i++)
        {
          probeDataBuffers[i]->FillData(cmd);
        }
      });

    // Probe sizes are dilated to include a 1-texel border.
    const auto width1  = (2 + probeGridInfo.probeRadianceResolution.x) * std::ceil(std::sqrt(float(numProbes)));
    const auto height1 = (2 + probeGridInfo.probeRadianceResolution.x) * std::ceil(numProbes * (2 + probeGridInfo.probeRadianceResolution.x) / width1);
    packedProbeRadiance =
      Fvog::CreateTexture2DArray({uint32_t(width1), uint32_t(height1)}, DDGI_NUM_CASCADES, radianceFormat, Fvog::TextureUsage::GENERAL, "DDGI Probe Radiance");
    packedProbeRawDepth =
      Fvog::CreateTexture2DArray({uint32_t(width1), uint32_t(height1)}, DDGI_NUM_CASCADES, Fvog::Format::R32_SFLOAT, Fvog::TextureUsage::GENERAL, "DDGI Probe Raw Depth");

    const auto width2  = (2 + probeGridInfo.probeIrradianceResolution.x) * std::ceil(std::sqrt(float(numProbes)));
    const auto height2 = (2 + probeGridInfo.probeIrradianceResolution.x) * std::ceil(numProbes * (2 + probeGridInfo.probeIrradianceResolution.x) / width2);
    packedProbeIrradiance =
      Fvog::CreateTexture2DArray({uint32_t(width2), uint32_t(height2)}, DDGI_NUM_CASCADES, radianceFormat, Fvog::TextureUsage::GENERAL, "DDGI Probe Irradiance");

    const auto width3  = (2 + probeGridInfo.probeDepthMomentsResolution.x) * std::ceil(std::sqrt(float(numProbes)));
    const auto height3 = (2 + probeGridInfo.probeDepthMomentsResolution.x) * std::ceil(numProbes * (2 + probeGridInfo.probeDepthMomentsResolution.x) / width2);
    packedProbeDepthMoments = Fvog::CreateTexture2DArray({uint32_t(width3), uint32_t(height3)},
      DDGI_NUM_CASCADES,
      Fvog::Format::R32G32_SFLOAT,
      Fvog::TextureUsage::GENERAL,
      "DDGI Probe Depth Moments");

    Fvog::GetDevice().ImmediateSubmit(
      [&](VkCommandBuffer cmd)
      {
        auto ctx = Fvog::Context(cmd);
        ctx.ImageBarrierDiscard(packedProbeRadiance.value(), VK_IMAGE_LAYOUT_GENERAL);
        ctx.ImageBarrierDiscard(packedProbeIrradiance.value(), VK_IMAGE_LAYOUT_GENERAL);
        ctx.ImageBarrierDiscard(packedProbeRawDepth.value(), VK_IMAGE_LAYOUT_GENERAL);
        ctx.ImageBarrierDiscard(packedProbeDepthMoments.value(), VK_IMAGE_LAYOUT_GENERAL);
        ctx.ClearTexture(packedProbeRadiance.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
        ctx.ClearTexture(packedProbeIrradiance.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
        ctx.ClearTexture(packedProbeRawDepth.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
        ctx.ClearTexture(packedProbeDepthMoments.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
      });
  }

  void Update(Scheduler& scheduler, VkCommandBuffer cmd, const DDGIUpdateParams& params) override
  {
    scheduler.AddPass("DdgiUpdateArguments",
      [=]
      {
        // Successive cascades are 2x the scale of the previous.
        args.gridInfo[0].baseGridScale = params.baseGridScale;
        for (int i = 0; i < DDGI_NUM_CASCADES; i++)
        {
          args.gridInfo[i].baseGridScale = args.gridInfo[0].baseGridScale * float(glm::exp2(i));
        }

        DDGIProbeGridInfo tempGridInfos[DDGI_NUM_CASCADES];
        for (int i = 0; i < DDGI_NUM_CASCADES; i++)
        {
          if (!params.debugFreezeGrid)
          {
            args.gridInfo[i].probeInfosIndex = probeDataBuffers[i].value().GetResourceHandle().index;
            args.gridInfo[i].oldGridOffset   = args.gridInfo[i].gridOffset;
            const auto offset = 1.0f + (params.position - glm::vec3(glm::vec3(args.gridInfo[i].gridResolution) * args.gridInfo[i].baseGridScale / 2.0f)) /
                                         args.gridInfo[i].baseGridScale;
            args.gridInfo[i].gridOffset         = glm::floor(offset);
            args.gridInfo[i].gridOffsetFraction = glm::fract(offset);
          }
          tempGridInfos[i] = args.gridInfo[i];
        }
        args = DDGIArgs{
          .voxels                  = params.voxels,
          .internalColorSpace      = params.shadingColorSpace,
          .noiseTexture            = params.noiseTexture,
          .samples                 = 1,
          .bounces                 = 2,
          .globalUniformsIndex     = params.globalUniformsIndex,
          .showCascadeIndexAsColor = params.showCascadeIndexAsColor,
          //.gridInfo                   = ddgi.args.gridInfo,
          .packedProbeRadiance        = packedProbeRadiance->ImageView().GetImage2DArray(),
          .packedProbeIrradiance      = packedProbeIrradiance->ImageView().GetImage2DArray(),
          .packedProbeRawDepth        = packedProbeRawDepth->ImageView().GetImage2DArray(),
          .packedProbeDepthMoments    = packedProbeDepthMoments->ImageView().GetImage2DArray(),
          .packedProbeRadianceTex     = packedProbeRadiance->ImageView().GetTexture2DArray(),
          .packedProbeIrradianceTex   = packedProbeIrradiance->ImageView().GetTexture2DArray(),
          .packedProbeRawDepthTex     = packedProbeRawDepth->ImageView().GetTexture2DArray(),
          .packedProbeDepthMomentsTex = packedProbeDepthMoments->ImageView().GetTexture2DArray(),
          .linearSampler              = params.linearClampSampler,
        };

        for (int i = 0; i < DDGI_NUM_CASCADES; i++)
        {
          args.gridInfo[i] = tempGridInfos[i];
        }

        argsBuffer->UpdateData(cmd, args);
      });

    scheduler.AddPass("DdgiResetNewProbes",
      {"DdgiUpdateArguments"},
      [=]
      {
        auto ctx             = Fvog::Context(cmd);
        const auto numProbes = args.gridInfo[0].gridResolution.x * args.gridInfo[0].gridResolution.y * args.gridInfo[0].gridResolution.z;
        ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
        ctx.BindComputePipeline(resetNewProbesPipeline.GetPipeline());
        ctx.DispatchInvocations(numProbes, 1, DDGI_NUM_CASCADES);
      });

    scheduler.AddPass("DdgiTraceRays",
      {"DdgiResetNewProbes", "ShadowMaps", "AllSky"},
      [=]
      {
        // As long as probe validity is unused here, a barrier is not needed.
        auto ctx          = Fvog::Context(cmd);
        const auto extent = packedProbeRadiance->GetCreateInfo().extent;
        ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
        ctx.BindComputePipeline(traceRaysPipeline.GetPipeline());
        ctx.DispatchInvocations(extent.width * extent.height, 1, DDGI_NUM_CASCADES); // TODO: caculate extent based on number of live probes instead of image size.
      });

    scheduler.AddPass("DdgiConvolveIrradiance",
      {"DdgiTraceRays"},
      [=]
      {
        auto ctx          = Fvog::Context(cmd);
        const auto extent = packedProbeRadiance->GetCreateInfo().extent;
        ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
        ctx.BindComputePipeline(convolveIrradiancePipeline.GetPipeline());
        ctx.DispatchInvocations(extent.width * extent.height, 1, DDGI_NUM_CASCADES);
      });

    scheduler.AddPass("DdgiDownsampleDepth",
      {"DdgiTraceRays"},
      [=]
      {
        auto ctx          = Fvog::Context(cmd);
        const auto extent = packedProbeRadiance->GetCreateInfo().extent;
        ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
        ctx.BindComputePipeline(downsampleDepthPipeline.GetPipeline());
        ctx.DispatchInvocations(extent.width * extent.height, 1, DDGI_NUM_CASCADES);
      });

    scheduler.AddPass("DDGI", {"DdgiDownsampleDepth", "DdgiConvolveIrradiance"}, nullptr);
  }

  VkDeviceAddress GetArgsBufferAddress() override
  {
    return argsBuffer.value().GetDeviceBuffer().GetDeviceAddress();
  }

  void RenderDebugProbes(VkCommandBuffer cmd, const DDGIRenderDebugProbesParams& params) override
  {
    ASSERT(params.mesh);
    auto ctx = Fvog::Context(cmd);
    ctx.BindGraphicsPipeline(debugProbesPipeline.GetPipeline());
    for (int cascade = 0; cascade < DDGI_NUM_CASCADES; cascade++)
    {
      if (params.singleCascadeToShow < 0 || params.singleCascadeToShow == cascade)
      {
        ctx.SetPushConstants(DebugProbesArguments{
          .vertexBuffer        = params.mesh->vertexBuffer.value().GetDeviceAddress(),
          .ddgi                = GetArgsBufferAddress(),
          .globalUniformsIndex = params.globalUniformsIndex,
          .samplerr            = params.linearClampSampler,
          .debugMode           = uint32_t(params.mode),
          .probeSize           = params.probeSize,
          .cascade             = cascade,
        });
        ctx.BindIndexBuffer(params.mesh->indexBuffer.value(), 0, VK_INDEX_TYPE_UINT32);
        const auto& res = args.gridInfo[cascade].gridResolution;
        ctx.DrawIndexed(uint32_t(params.mesh->indices.size()), res.x * res.y * res.z, 0, 0, 0);
      }
    } 
  }

  private:
    // static constexpr Fvog::Format radianceFormat = Fvog::Format::B10G11R11_UFLOAT;
    static constexpr Fvog::Format radianceFormat = Fvog::Format::R32G32B32A32_SFLOAT; // TODO: TEMP until quantization with smaller formats is dealt with.
    std::optional<Fvog::NDeviceBuffer<DDGIArgs>> argsBuffer;
    std::optional<Fvog::Texture> packedProbeRadiance;
    std::optional<Fvog::Texture> packedProbeRawDepth; // Same resolution as radiance
    std::optional<Fvog::Texture> packedProbeIrradiance;
    std::optional<Fvog::Texture> packedProbeDepthMoments; // Filtered depth and depth^2
    std::unique_ptr<std::optional<Fvog::TypedBuffer<ProbeData>>[]> probeDataBuffers;
    DDGIArgs args{};
    PipelineManager::ComputePipelineKey traceRaysPipeline;
    PipelineManager::ComputePipelineKey convolveIrradiancePipeline;
    PipelineManager::ComputePipelineKey downsampleDepthPipeline;
    PipelineManager::ComputePipelineKey resetNewProbesPipeline;
    PipelineManager::GraphicsPipelineKey debugProbesPipeline;
  };

  std::unique_ptr<DDGI> DDGI::Create(const DDGIInitParams& params)
  {
    return std::make_unique<DDGIImpl>(params);
  }
}