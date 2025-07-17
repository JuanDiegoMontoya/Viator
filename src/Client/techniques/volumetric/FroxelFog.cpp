#include "FroxelFog.h"
#include "PCG.h"
#include "Game/Assets.h"
#include "Core/Assert2.h"
#include "Client/Fvog/Rendering2.h"

#include "glm/common.hpp"
#include "tracy/Tracy.hpp"

#include <fstream>
#include <numbers>

namespace
{
  float UniformSpherePDF()
  {
    return 1.0f / (4.0f * std::numbers::pi_v<float>);
  }

  glm::vec3 MapToUnitSphere(glm::vec2 uv)
  {
    float cosTheta = 2.0f * uv.x - 1.0f;
    float phi      = 2.0f * std::numbers::pi_v<float> * uv.y;
    float sinTheta = cosTheta >= 1 ? 0 : sqrt(1.0f - cosTheta * cosTheta);
    float sinPhi   = sin(phi);
    float cosPhi   = cos(phi);

    return {sinTheta * cosPhi, cosTheta, sinTheta * sinPhi};
  }
}

namespace Techniques
{
  FroxelFog::FroxelFog()
  {
    ZoneScoped;
    accumulateDensityPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "Inject Fog (Froxel fog)",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "volumetric/CellLightingAndDensity.comp.glsl",
        },
    });

    marchVolumePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "March Volume (Froxel fog)",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "volumetric/MarchVolume.comp.glsl",
        },
    });

    applyDeferredPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
        .name = "Apply Deferred (Froxel fog)",
        .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
            .stage = Fvog::PipelineStage::COMPUTE_SHADER,
            .path  = GetShaderDirectory() / "volumetric/ApplyVolumetricsDeferred.comp.glsl",
        },
    });

    // Load the normalized MiePlot generated scattering data.
    // This texture is used if a flag is set in marchVolume.comp.glsl.
    std::ifstream file{GetDataDirectory() / "textures/fog_mie_data.txt"};

    std::vector<glm::vec4> data;
    data.reserve(500);

    while (file.peek() != EOF)
    {
      std::string fs0, fs1, fs2;
      std::getline(file, fs0);
      std::getline(file, fs1);
      std::getline(file, fs2);

      float red   = std::stof(fs0);
      float green = std::stof(fs1);
      float blue  = std::stof(fs2);

      data.emplace_back(red, green, blue, 0.0f);
    }

    ASSERT(!data.empty());

    glm::dvec4 sum = {};
    for (const auto& e : data)
    {
      sum += e * 180.0f;
    }
    sum /= data.size();

    //printf("sum: %f, %f, %f\n", sum.r, sum.g, sum.b);

    auto eval = [&](const glm::vec3& dir)
    {
      auto cosine = glm::clamp(glm::dot(dir, {0, 0, 1}), -1.0f, 1.0f);
      // return vec3(phaseHG(0.9f, uv));
      // return vec3(phaseSchlick(0.9f, uv));
      auto radians = acos(cosine);
      auto ic      = radians / glm::pi<float>();
      auto tc      = ic * (data.size() - 1);
      auto left    = (size_t)floor(tc);
      auto right   = (size_t)ceil(tc);
      auto alpha   = glm::fract(tc);
      return mix(data[left], data[right], alpha);
    };

    uint32_t seed = PCG::Hash(7);

    constexpr int samples = 1'000'000;
    glm::dvec4 estimate   = {};
    for (int i = 0; i < samples; i++)
    {
      const auto xi = glm::vec2(PCG::RandFloat(seed), PCG::RandFloat(seed));
      estimate += eval(MapToUnitSphere(xi)) / UniformSpherePDF();
    }
    estimate /= samples;

    // printf("estimate: %f, %f, %f\n", estimate.r, estimate.g, estimate.b);

    for (auto& c : data)
    {
      c /= estimate;
    }
#if 0 // Re-integrate to see how close we got last time
    glm::dvec3 estimate2 = {};
    for (int i = 0; i < samples; i++)
    {
      const auto xi = glm::vec2(PCG_RandFloat(seed), PCG_RandFloat(seed));
      estimate2 += eval(MapToUnitSphere(xi)) / UniformSpherePDF();
    }
    estimate2 /= samples;

    printf("estimate2: %f, %f, %f\n", estimate2.r, estimate2.g, estimate2.b);
#endif
    scatteringTexture = Fvog::Texture(Fvog::TextureCreateInfo{
      .viewType    = VK_IMAGE_VIEW_TYPE_1D,
      .format      = Fvog::Format::R32G32B32A32_SFLOAT, // TODO: temp
      .extent      = {static_cast<uint32_t>(data.size()), 1, 1},
      .mipLevels   = 1,
      .arrayLayers = 1,
    });

    scatteringTexture->UpdateImageSLOW({
      .extent = {static_cast<uint32_t>(data.size())},
      .data   = data.data(),
    });
  }

  void FroxelFog::UpdateUniforms(VkCommandBuffer commandBuffer, const VolumetricUniforms& uniforms)
  {
    if (!uniformBuffer)
    {
      uniformBuffer.emplace(1, "Froxel Fog Uniforms");
    }
    auto uniforms2 = uniforms;
    uniforms2.mieScattering = scatteringTexture->ImageView().GetTexture1D();
    uniformBuffer->UpdateData(commandBuffer, uniforms2);
  }

  void FroxelFog::InjectFog(VkCommandBuffer commandBuffer, const Fvog::Texture& fogDensityVolume)
  {
    ASSERT(fogDensityVolume.GetCreateInfo().viewType == VK_IMAGE_VIEW_TYPE_3D);

    auto ctx = Fvog::Context(commandBuffer);
    auto _ = ctx.MakeScopedDebugMarker("Inject fog");
    ctx.Barrier();
    ctx.BindComputePipeline(accumulateDensityPipeline->GetPipeline());
    ctx.SetPushConstants(uint32_t(uniformBuffer->GetDeviceBuffer().GetResourceHandle().index));
    ctx.DispatchInvocations(fogDensityVolume.GetCreateInfo().extent);
  }

  void FroxelFog::MarchVolume(VkCommandBuffer commandBuffer, const Fvog::Texture& fogDensityVolume, const Fvog::Texture& inScatteringAndTransmittanceVolume)
  {
    ASSERT(fogDensityVolume.GetCreateInfo().viewType == VK_IMAGE_VIEW_TYPE_3D);
    ASSERT(inScatteringAndTransmittanceVolume.GetCreateInfo().viewType == VK_IMAGE_VIEW_TYPE_3D);

    auto ctx = Fvog::Context(commandBuffer);
    auto _   = ctx.MakeScopedDebugMarker("March volume");
    ctx.Barrier();
    ctx.BindComputePipeline(marchVolumePipeline->GetPipeline());
    ctx.SetPushConstants(uint32_t(uniformBuffer->GetDeviceBuffer().GetResourceHandle().index));
    ctx.DispatchInvocations(inScatteringAndTransmittanceVolume.GetCreateInfo().extent.width, inScatteringAndTransmittanceVolume.GetCreateInfo().extent.height, 1);
  }

  void FroxelFog::ApplyDeferred(VkCommandBuffer commandBuffer,
    const Fvog::Texture& inSceneRadiance,
    const Fvog::Texture& gDepth,
    const Fvog::Texture& outSceneRadiance,
    const Fvog::Texture& inScatteringAndTransmittanceVolume)
  {
    ASSERT(inScatteringAndTransmittanceVolume.GetCreateInfo().viewType == VK_IMAGE_VIEW_TYPE_3D);
    ASSERT(outSceneRadiance.GetCreateInfo().extent == inSceneRadiance.GetCreateInfo().extent && outSceneRadiance.GetCreateInfo().extent == gDepth.GetCreateInfo().extent);
    
    auto ctx = Fvog::Context(commandBuffer);
    auto _   = ctx.MakeScopedDebugMarker("Apply deferred");
    ctx.Barrier();
    ctx.BindComputePipeline(applyDeferredPipeline->GetPipeline());
    ctx.SetPushConstants(uint32_t(uniformBuffer->GetDeviceBuffer().GetResourceHandle().index));
    ctx.DispatchInvocations(outSceneRadiance.GetCreateInfo().extent);
  }
} // namespace Techniques
