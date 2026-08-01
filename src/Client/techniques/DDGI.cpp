#include "DDGI.h"

#include "Client/Fvog/Device.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/Fvog/Texture2.h"
#include "Client/GpuMesh.h"
#include "Client/PipelineManager.h"
#include "Client/Scheduler.h"
#include "Game/Assets.h"
#include "glm/gtx/component_wise.inl"

#include "shaders/Config.shared.h"
#include "shaders/ddgi/DebugProbesCommon.h.glsl"
#include "shaders/ddgi/ProbeCommon.shared.h"

#include "tracy/Tracy.hpp"

namespace Techniques
{
  class DDGIImpl final : public DDGI
  {
  public:
    explicit DDGIImpl(const DDGIInitParams& params)
    {
      ZoneScoped;
      traceRaysPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Trace Luminance",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/TraceProbes.comp.glsl",
          },
        .useMinSubgroupSize = true,
      });

      temporalAccumulationPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Temporal Accumulation",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/TemporalAccumulation.comp.glsl",
          },
      });

      convolveIrradiancePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Convolve Illuminance",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/ConvolveIrradiance.comp.glsl",
          },
      });

      computeAverageRadiancePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Compute Average Radiance",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/ComputeAverageRadiance.comp.glsl",
          },
      });

      downsampleDepthPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Downsample Probe Depth",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/DownsampleProbeDepth.comp.glsl",
          },
      });

      resetNewProbesPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Reset New Probes",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/ResetNewProbes.comp.glsl",
          },
      });

      computeProbePriorityPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Compute Probe Priority",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/ComputeProbePriority.comp.glsl",
          },
      });

      determineProbesToUpdatePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Determine Probes to Update",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/DetermineProbesToUpdate.comp.glsl",
          },
      });

      writeIndirectCommandsPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "[DDGI] Write Indirect Commands",
        .shaderModuleInfo =
          PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "ddgi/WriteIndirectCommands.comp.glsl",
          },
      });

      debugProbesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
        .name = "[DDGI] Debug Probes",
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
    }

    void Update(Scheduler& scheduler, VkCommandBuffer cmd, const DDGIUpdateParams& params) override
    {
      if (params.gridSetup.probeRadianceResolution != args.probeRadianceResolution ||
          params.gridSetup.probeIrradianceResolution != args.probeIrradianceResolution ||
          params.gridSetup.probeDepthMomentsResolution != args.probeDepthMomentsResolution ||
          params.gridSetup.gridResolution != args.gridResolution ||
          (int)params.gridSetup.probeUpdateBudget != args.probeUpdateBudget)
      {
        CreateResources(params.gridSetup);
      }

      scheduler.AddPass("DdgiUpdateArguments",
        {"LightGrid"},
        [=, this]
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
              const auto offset =
                1.0f + (params.position - glm::vec3(glm::vec3(args.gridResolution) * args.gridInfo[i].baseGridScale / 2.0f)) / args.gridInfo[i].baseGridScale;
              args.gridInfo[i].gridOffset         = glm::floor(offset);
              args.gridInfo[i].gridOffsetFraction = offset - glm::floor(offset);
            }
            tempGridInfos[i] = args.gridInfo[i];
          }
          args = DDGIArgs{
            .voxels                  = params.voxels,
            .internalColorSpace      = params.shadingColorSpace,
            .noiseTexture            = params.noiseTexture,
            .globalUniformsIndex     = params.globalUniformsIndex,
            .showCascadeIndexAsColor = params.showCascadeIndexAsColor,
            .minTemporalAlpha        = params.minTemporalAlpha,
            .fastMinTemporalAlpha    = params.fastTemporalAlpha,
            .varianceClipGamma       = params.varianceClipGamma,
            .convolveTemporalAlpha   = params.convolveTemporalAlpha,
            //.gridInfo                   = ddgi.args.gridInfo,
            .packedProbeRadiance         = packedProbeRadiance->ImageView().GetImage2DArray(),
            .packedProbeRadianceRaw      = packedProbeRadianceRaw->ImageView().GetImage2DArray(),
            .packedProbeFastRadianceLuminance = packedProbeFastRadianceLuminance->ImageView().GetImage2DArray(),
            .packedProbeIrradiance       = packedProbeIrradiance->ImageView().GetImage2DArray(),
            .packedProbeIrradianceRaw    = packedProbeIrradianceRaw->ImageView().GetImage2DArray(),
            .packedProbeDepth            = packedProbeDepth->ImageView().GetImage2DArray(),
            .packedProbeDepthMoments     = packedProbeDepthMoments->ImageView().GetImage2DArray(),
            .packedProbeRadianceTex      = packedProbeRadiance->ImageView().GetTexture2DArray(),
            .packedProbeRadianceRawTex   = packedProbeRadianceRaw->ImageView().GetTexture2DArray(),
            .packedProbeFastRadianceLuminanceTex = packedProbeFastRadianceLuminance->ImageView().GetTexture2DArray(),
            .packedProbeIrradianceTex    = packedProbeIrradiance->ImageView().GetTexture2DArray(),
            .packedProbeIrradianceRawTex = packedProbeIrradianceRaw->ImageView().GetTexture2DArray(),
            .packedProbeDepthTex         = packedProbeDepth->ImageView().GetTexture2DArray(),
            .packedProbeDepthMomentsTex  = packedProbeDepthMoments->ImageView().GetTexture2DArray(),
            .linearSampler               = params.linearClampSampler,
            .probeRadianceResolution     = params.gridSetup.probeRadianceResolution,
            .probeIrradianceResolution   = params.gridSetup.probeIrradianceResolution,
            .probeDepthMomentsResolution = params.gridSetup.probeDepthMomentsResolution,
            .gridResolution              = params.gridSetup.gridResolution,
            
            .wholeProbesIndirectCommand = wholeProbesIndirectCommand->GetDeviceAddress(),
            .probeTexelsIndirectCommand = probeTexelsIndirectCommand->GetDeviceAddress(),
            .probesToUpdate             = probesToUpdateVec->GetDeviceAddress(),
            .probeUpdateBudget          = (int)params.gridSetup.probeUpdateBudget,
            .probeBaseAgePriority       = 1000,
            .probeAgeFactor             = 100,
            .probeFrequencyFactor       = 1,
            .probeMinPriority           = 100,
            .sumProbePriority           = 0,
          };

          for (int i = 0; i < DDGI_NUM_CASCADES; i++)
          {
            args.gridInfo[i] = tempGridInfos[i];
          }

          argsBuffer->UpdateData(cmd, args);

          auto ctx = Fvog::Context(cmd);
          ctx.TeenyBufferUpdate(probesToUpdateVec.value(),
            UIntVector_t{
              0,
              (int)params.gridSetup.probeUpdateBudget,
              probesToUpdateData->GetDeviceAddress(),
            });
        });

      scheduler.AddPass("DdgiResetNewProbes",
        {"DdgiUpdateArguments"},
        [=, this]
        {
          auto ctx             = Fvog::Context(cmd);
          const auto numProbes = args.gridResolution.x * args.gridResolution.y * args.gridResolution.z;
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(resetNewProbesPipeline.GetPipeline());
          ctx.DispatchInvocations(numProbes, 1, DDGI_NUM_CASCADES);
          ctx.TeenyBufferUpdate(argsBuffer->GetDeviceBuffer(), 0, offsetof(DDGIArgs, sumProbePriority));
        });

      scheduler.AddPass("DdgiComputeProbePriority",
        {"DdgiResetNewProbes"},
        [=, this]
        {
          auto ctx             = Fvog::Context(cmd);
          const auto numProbes = args.gridResolution.x * args.gridResolution.y * args.gridResolution.z;
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(computeProbePriorityPipeline.GetPipeline());
          ctx.DispatchInvocations(numProbes, 1, DDGI_NUM_CASCADES);
        });

      scheduler.AddPass("DdgiDetermineProbesToUpdate",
        {"DdgiComputeProbePriority"},
        [=, this]
        {
          auto ctx             = Fvog::Context(cmd);
          const auto numProbes = args.gridResolution.x * args.gridResolution.y * args.gridResolution.z;
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(determineProbesToUpdatePipeline.GetPipeline());
          ctx.DispatchInvocations(numProbes, 1, DDGI_NUM_CASCADES);
        });

      scheduler.AddPass("DdgiWriteIndirectCommands",
        {"DdgiDetermineProbesToUpdate"},
        [=, this]
        {
          auto ctx = Fvog::Context(cmd);
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(writeIndirectCommandsPipeline.GetPipeline());
          ctx.DispatchInvocations(1, 1, 1);
        });

      scheduler.AddPass("DdgiTraceRays",
        {"DdgiWriteIndirectCommands", "DdgiResetNewProbes", "ShadowMaps", "AllSky"},
        [=, this]
        {
          auto ctx = Fvog::Context(cmd);
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(traceRaysPipeline.GetPipeline());
          ctx.DispatchIndirect(probeTexelsIndirectCommand.value());
        });

      scheduler.AddPass("DdgiTemporalAccumulation",
        {"DdgiTraceRays"},
        [=, this]
        {
          auto ctx = Fvog::Context(cmd);
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(temporalAccumulationPipeline.GetPipeline());
          ctx.DispatchIndirect(probeTexelsIndirectCommand.value());
        });

      scheduler.AddPass("DdgiComputeAverageRadiance",
        {"DdgiTemporalAccumulation"},
        [=, this]
        {
          auto ctx = Fvog::Context(cmd);
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(computeAverageRadiancePipeline.GetPipeline());
          ctx.DispatchIndirect(wholeProbesIndirectCommand.value());
        });

      scheduler.AddPass("DdgiConvolveIrradiance",
        {"DdgiTemporalAccumulation"},
        [=, this]
        {
          auto ctx = Fvog::Context(cmd);
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(convolveIrradiancePipeline.GetPipeline());
          ctx.DispatchIndirect(probeTexelsIndirectCommand.value());
        });

      scheduler.AddPass("DdgiDownsampleDepth",
        {"DdgiTraceRays"},
        [=, this] 
        {
          auto ctx = Fvog::Context(cmd);
          ctx.SetPushConstants(argsBuffer->GetDeviceBuffer().GetDeviceAddress());
          ctx.BindComputePipeline(downsampleDepthPipeline.GetPipeline());
          ctx.DispatchIndirect(probeTexelsIndirectCommand.value());
        });

      scheduler.AddPass("DDGI", {"DdgiDownsampleDepth", "DdgiConvolveIrradiance", "DdgiComputeAverageRadiance"}, nullptr);
    }

    VkDeviceAddress GetArgsBufferAddress() override
    {
      return argsBuffer.has_value() ? argsBuffer->GetDeviceBuffer().GetDeviceAddress() : 0;
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
          const auto res = args.gridResolution;
          ctx.DrawIndexed(uint32_t(params.mesh->indices.size()), res.x * res.y * res.z, 0, 0, 0);
        }
      }
    }

  private:
    void CreateResources(const DDGIGridSetup& gridSetup)
    {
      ZoneScoped;
      ASSERT(gridSetup.probeRadianceResolution.x > 0);
      ASSERT(gridSetup.probeRadianceResolution.x == gridSetup.probeRadianceResolution.y);

      argsBuffer.emplace(1, "DDGI Arguments");
      const auto numProbes = gridSetup.gridResolution.x * gridSetup.gridResolution.y * gridSetup.gridResolution.z;

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

      constexpr auto usage = Fvog::TextureUsage::GENERAL;
      constexpr auto cascades = DDGI_NUM_CASCADES;

      // Probe sizes are dilated to include a 1-texel border.
      const auto width1  = uint32_t((2 + gridSetup.probeRadianceResolution.x) * std::ceil(std::sqrt(float(numProbes))));
      const auto height1 = uint32_t((2 + gridSetup.probeRadianceResolution.x) * std::ceil(float(numProbes) * (2 + gridSetup.probeRadianceResolution.x) / float(width1)));
      ASSERT(int(width1 * height1) / glm::compMul(gridSetup.probeRadianceResolution + 2) >= numProbes);
      packedProbeRadiance = Fvog::CreateTexture2DArray({width1, height1}, cascades, radianceFormat, usage, "DDGI Probe Radiance");
      packedProbeRadianceRaw = Fvog::CreateTexture2DArray({width1, height1}, cascades, radianceFormat, usage, "DDGI Probe Raw Radiance");
      packedProbeFastRadianceLuminance = Fvog::CreateTexture2DArray({width1, height1}, cascades, Fvog::Format::R32_SFLOAT, usage, "DDGI Probe Fast Radiance Luminance");
      packedProbeDepth = Fvog::CreateTexture2DArray({width1, height1}, cascades, Fvog::Format::R32_SFLOAT, usage, "DDGI Probe Depth");

      const auto width2  = uint32_t((2 + gridSetup.probeIrradianceResolution.x) * std::ceil(std::sqrt(float(numProbes))));
      const auto height2 = uint32_t((2 + gridSetup.probeIrradianceResolution.x) * std::ceil(float(numProbes) * (2 + gridSetup.probeIrradianceResolution.x) / float(width2)));
      ASSERT(int(width2 * height2) / glm::compMul(gridSetup.probeIrradianceResolution + 2) >= numProbes);
      packedProbeIrradiance = Fvog::CreateTexture2DArray({width2, height2}, cascades, radianceFormat, usage, "DDGI Probe Irradiance");
      packedProbeIrradianceRaw = Fvog::CreateTexture2DArray({width2, height2}, cascades, radianceFormat, usage, "DDGI Probe Raw Irradiance");

      const auto width3 = uint32_t((2 + gridSetup.probeDepthMomentsResolution.x) * std::ceil(std::sqrt(float(numProbes))));
      const auto height3 = uint32_t((2 + gridSetup.probeDepthMomentsResolution.x) * std::ceil(float(numProbes) * (2 + gridSetup.probeDepthMomentsResolution.x) / float(width3)));
      ASSERT(int(width3 * height3) / glm::compMul(gridSetup.probeDepthMomentsResolution + 2) >= numProbes);
      packedProbeDepthMoments = Fvog::CreateTexture2DArray({width3, height3}, cascades, Fvog::Format::R32G32_SFLOAT, usage, "DDGI Probe Depth Moments");

      wholeProbesIndirectCommand.emplace(Fvog::TypedBufferCreateInfo{.count = 1, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR});
      probeTexelsIndirectCommand.emplace(Fvog::TypedBufferCreateInfo{.count = 1, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR});
      probesToUpdateData.emplace(Fvog::TypedBufferCreateInfo{.count = gridSetup.probeUpdateBudget, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR});
      probesToUpdateVec.emplace(Fvog::TypedBufferCreateInfo{.count = 1, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR});

      Fvog::GetDevice().ImmediateSubmit(
        [&](VkCommandBuffer cmd)
        {
          auto ctx = Fvog::Context(cmd);
          probesToUpdateData->FillData(cmd);
          ctx.TeenyBufferUpdate(probesToUpdateVec.value(),
            UIntVector_t{
              .size     = 0,
              .capacity = (int)gridSetup.probeUpdateBudget,
              .values   = probesToUpdateData.value().GetDeviceAddress(),
            });
          ctx.ImageBarrierDiscard(packedProbeRadiance.value(), VK_IMAGE_LAYOUT_GENERAL);
          ctx.ImageBarrierDiscard(packedProbeRadianceRaw.value(), VK_IMAGE_LAYOUT_GENERAL);
          ctx.ImageBarrierDiscard(packedProbeFastRadianceLuminance.value(), VK_IMAGE_LAYOUT_GENERAL);
          ctx.ImageBarrierDiscard(packedProbeIrradiance.value(), VK_IMAGE_LAYOUT_GENERAL);
          ctx.ImageBarrierDiscard(packedProbeIrradianceRaw.value(), VK_IMAGE_LAYOUT_GENERAL);
          ctx.ImageBarrierDiscard(packedProbeDepth.value(), VK_IMAGE_LAYOUT_GENERAL);
          ctx.ImageBarrierDiscard(packedProbeDepthMoments.value(), VK_IMAGE_LAYOUT_GENERAL);
          ctx.ClearTexture(packedProbeRadiance.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
          ctx.ClearTexture(packedProbeRadianceRaw.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
          ctx.ClearTexture(packedProbeFastRadianceLuminance.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
          ctx.ClearTexture(packedProbeIrradiance.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
          ctx.ClearTexture(packedProbeIrradianceRaw.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
          ctx.ClearTexture(packedProbeDepth.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
          ctx.ClearTexture(packedProbeDepthMoments.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
        });
    }

    // static constexpr Fvog::Format radianceFormat = Fvog::Format::B10G11R11_UFLOAT;
    static constexpr Fvog::Format radianceFormat = Fvog::Format::R32G32B32A32_SFLOAT; // TODO: TEMP until quantization with smaller formats is dealt with.
    std::optional<Fvog::NDeviceBuffer<DDGIArgs>> argsBuffer;
    std::optional<Fvog::Texture> packedProbeRadiance;
    std::optional<Fvog::Texture> packedProbeRadianceRaw;
    std::optional<Fvog::Texture> packedProbeFastRadianceLuminance;
    std::optional<Fvog::Texture> packedProbeDepth; // Same resolution as radiance
    std::optional<Fvog::Texture> packedProbeIrradiance;
    std::optional<Fvog::Texture> packedProbeIrradianceRaw;
    std::optional<Fvog::Texture> packedProbeDepthMoments; // Filtered depth and depth^2
    std::unique_ptr<std::optional<Fvog::TypedBuffer<ProbeData>>[]> probeDataBuffers;
    std::optional<Fvog::TypedBuffer<FVOG_UINT32>> probesToUpdateData;
    std::optional<Fvog::TypedBuffer<UIntVector_t>> probesToUpdateVec;
    std::optional<Fvog::TypedBuffer<DispatchIndirectCommand>> wholeProbesIndirectCommand;
    std::optional<Fvog::TypedBuffer<DispatchIndirectCommand>> probeTexelsIndirectCommand;
    DDGIArgs args{};
    PipelineManager::ComputePipelineKey traceRaysPipeline;
    PipelineManager::ComputePipelineKey temporalAccumulationPipeline;
    PipelineManager::ComputePipelineKey convolveIrradiancePipeline;
    PipelineManager::ComputePipelineKey computeAverageRadiancePipeline;
    PipelineManager::ComputePipelineKey downsampleDepthPipeline;
    PipelineManager::ComputePipelineKey resetNewProbesPipeline;
    PipelineManager::ComputePipelineKey computeProbePriorityPipeline;
    PipelineManager::ComputePipelineKey determineProbesToUpdatePipeline;
    PipelineManager::ComputePipelineKey writeIndirectCommandsPipeline;
    PipelineManager::GraphicsPipelineKey debugProbesPipeline;
  };

  std::unique_ptr<DDGI> DDGI::Create(const DDGIInitParams& params)
  {
    return std::make_unique<DDGIImpl>(params);
  }
} // namespace Techniques





#include "doctest.h"

TEST_CASE("DDGIHelpers")
{
  SUBCASE("Cascade and stable probe index encoding and decoding")
  {
    FVOG_INT32 cascade{};
    FVOG_INT32 index{};

    const auto encoded0 = EncodeCascadeAndProbeIndex(0, 0);
    DecodeCascadeAndProbeIndex(encoded0, cascade, index);
    CHECK_EQ(cascade, 0);
    CHECK_EQ(index, 0);

    const auto encoded1 = EncodeCascadeAndProbeIndex(5, 1000);
    DecodeCascadeAndProbeIndex(encoded1, cascade, index);
    CHECK_EQ(cascade, 5);
    CHECK_EQ(index, 1000);

    const auto encoded2 = EncodeCascadeAndProbeIndex(0xF, 0x0FFF'FFFF);
    DecodeCascadeAndProbeIndex(encoded2, cascade, index);
    CHECK_EQ(cascade, 0xF);
    CHECK_EQ(index, 0x0FFF'FFFF);
  }
}