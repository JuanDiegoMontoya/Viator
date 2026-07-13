#include "LightGrid.h"
#include "Client/Fvog/Buffer2.h"
#include "Client/Fvog/Texture2.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/Fvog/Pipeline2.h"
#include "Client/PipelineManager.h"
#include "Client/Scheduler.h"
#include "Client/Fvog/Device.h"
#include "Game/Assets.h"

#include "shaders/light_grid/LightGridCommon.shared.h"
#include "shaders/light_grid/ScatterLightsToCells.comp.glsl"
#include "shaders/light_grid/AllocateCellLists.comp.glsl"

#include <array>
#include <optional>
#include <format>

namespace Techniques
{
  namespace
  {
    class LightGridImpl : public LightGrid
    {
    public:
      LightGridImpl()
      {
        scatterLightsToCellsPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "Scatter Lights to Cells",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "light_grid/ScatterLightsToCells.comp.glsl",
            },
        });

        allocateCellListsPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "Allocate Cell Light Lists",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "light_grid/AllocateCellLists.comp.glsl",
            },
        });
      }

      VkDeviceAddress Update(VkCommandBuffer cmd, Scheduler& scheduler, const LightGridUpdateInfo& info) override
      {
        ASSERT(info.gridParams.numCascades <= LIGHT_GRID_MAX_CASCADES);
        DEBUG_ASSERT(all(greaterThan(info.gridParams.cascadeDimensions, glm::ivec3(0))));

        if (info.gridParams.cascadeDimensions != cascadedLightGrid.params.cascadeDimensions ||
            info.gridParams.lightIndicesPerCascade != cascadedLightGrid.params.lightIndicesPerCascade)
        {
          CreatePerGridResources(info.gridParams.cascadeDimensions, info.gridParams.lightIndicesPerCascade, info.gridParams.numCascades);
        }

        cascadedLightGrid.params = info.gridParams;

        auto cpuCascadedLightGrid = CascadedLightGrid_t{
          .numCascades       = info.gridParams.numCascades,
          .cascadeDimensions = info.gridParams.cascadeDimensions,
          .maxLightsPerCell  = (int)info.gridParams.maxLightsPerCell,
        };

        for (uint32_t i = 0; i < info.gridParams.numCascades; i++)
        {
          auto lightIndicesVec = Fvog::GetDevice().AllocTransient<IntVector_t>();
          auto& grid           = cascadedLightGrid.grids[i];

          *lightIndicesVec = {
            .size     = 0,
            .capacity = (int32_t)info.gridParams.lightIndicesPerCascade,
            .values   = grid.lightIndices->GetDeviceAddress(),
          };

          const auto scale              = info.gridParams.baseGridScale * (1 << i);
          cpuCascadedLightGrid.grids[i] = {
            .cellLightListOffset            = grid.cellLightListOffset->ImageView().GetUImage3D(),
            .cellLightListCount             = grid.cellLightListCount->ImageView().GetUImage3D(),
            .cellLightListCountIntermediate = grid.cellLightListCountIntermediate->ImageView().GetUImage3D(),
            .gridScale                      = scale,
            .positionOffset                 = glm::ivec3(glm::floor(info.cameraPosition / scale)) - info.gridParams.cascadeDimensions / 2,
            .lightIndices                   = lightIndicesVec.ptr,
          };
        }

        auto gpuCascadedLightGrid = Fvog::GetDevice().AllocTransient<CascadedLightGrid_t>();
        std::memcpy(&*gpuCascadedLightGrid, &cpuCascadedLightGrid, sizeof(cpuCascadedLightGrid));

        scheduler.AddPass("ResetLightCounts",
          [=, this]
          {
            auto ctx = Fvog::Context(cmd);
            for (uint32_t i = 0; i < cascadedLightGrid.params.numCascades; i++)
            {
              auto& grid = cascadedLightGrid.grids[i];
              ctx.ClearTexture(grid.cellLightListCount.value());
              ctx.ClearTexture(grid.cellLightListCountIntermediate.value());
            }
          });

        scheduler.AddPass("CellLightCounts",
          {"ResetLightCounts"},
          [=, this]
          {
            auto ctx  = Fvog::Context(cmd);
            auto args = Fvog::GetDevice().AllocTransient<ScatterLightsToCellsGpuParams_t>();

            *args = {
              .lightCount        = info.numLights,
              .lights            = info.lightsBuffer,
              .cascadedLightGrid = gpuCascadedLightGrid.ptr,
              .cameraPosition    = info.cameraPosition,
              .isSecondPass      = 0,
            };

            ctx.BindComputePipeline(scatterLightsToCellsPipeline.GetPipeline());
            ctx.SetPushConstants(args);
            ctx.DispatchInvocations(info.numLights, 1, info.gridParams.numCascades);
          });

        scheduler.AddPass("AllocateCellLists",
          {"CellLightCounts"},
          [=, this]
          {
            auto ctx = Fvog::Context(cmd);

            ctx.BindComputePipeline(allocateCellListsPipeline.GetPipeline());
            for (uint32_t i = 0; i < cascadedLightGrid.params.numCascades; i++)
            {
              auto args = Fvog::GetDevice().AllocTransient<AllocateCellListsGpuParams_t>();

              *args = {
                .cascadedLightGrid = gpuCascadedLightGrid.ptr,
                .cascade           = i,
              };

              const auto dims = cascadedLightGrid.params.cascadeDimensions;
              ctx.SetPushConstants(args);
              ctx.DispatchInvocations(dims.x, dims.y, dims.z);
            }
          });

        // The same as CellLightCounts, except `isSecondPass` is set to 1, which informs the 
        // shader to write light indices to previously-allocated per-cell lists.
        scheduler.AddPass("WriteCellLightIndices",
          {"AllocateCellLists"},
          [=, this]
          {
            auto ctx  = Fvog::Context(cmd);
            auto args = Fvog::GetDevice().AllocTransient<ScatterLightsToCellsGpuParams_t>();

            *args = {
              .lightCount        = info.numLights,
              .lights            = info.lightsBuffer,
              .cascadedLightGrid = gpuCascadedLightGrid.ptr,
              .cameraPosition    = info.cameraPosition,
              .isSecondPass      = 1,
            };

            ctx.BindComputePipeline(scatterLightsToCellsPipeline.GetPipeline());
            ctx.SetPushConstants(args);
            ctx.DispatchInvocations(info.numLights, 1, info.gridParams.numCascades);
          });

        scheduler.AddPass("LightGrid", {"WriteCellLightIndices"}, nullptr);

        return gpuCascadedLightGrid.ptr;
      }

    private:
      void CreatePerGridResources(glm::ivec3 gridDimensions, uint32_t indicesPerCascade, uint32_t numCascades)
      {
        cascadedLightGrid.grids = {};

        const auto extent = Fvog::Extent3D{(uint32_t)gridDimensions.x, (uint32_t)gridDimensions.y, (uint32_t)gridDimensions.z};

        for (uint32_t i = 0; i < numCascades; i++)
        {
          auto& grid = cascadedLightGrid.grids[i];

          grid.cellLightListOffset = Fvog::Texture(
            {
              .viewType = VK_IMAGE_VIEW_TYPE_3D,
              .format   = Fvog::Format::R32_UINT,
              .extent   = extent,
              .usage    = Fvog::TextureUsage::GENERAL,
            },
            std::format("[Light Grid] Cascade {} light list offset", i));

          grid.cellLightListCount = Fvog::Texture(
            {
              .viewType = VK_IMAGE_VIEW_TYPE_3D,
              .format   = Fvog::Format::R32_UINT,
              .extent   = extent,
              .usage    = Fvog::TextureUsage::GENERAL,
            },
            std::format("[Light Grid] Cascade {} light list count", i));

          grid.cellLightListCountIntermediate = Fvog::Texture(
            {
              .viewType = VK_IMAGE_VIEW_TYPE_3D,
              .format   = Fvog::Format::R32_UINT,
              .extent   = extent,
              .usage    = Fvog::TextureUsage::GENERAL,
            },
            std::format("[Light Grid] Cascade {} light list count intermediate", i));

          grid.lightIndices.emplace(
            Fvog::TypedBufferCreateInfo{
              .count = indicesPerCascade,
              .flag  = Fvog::BufferFlagThingy::NO_DESCRIPTOR,
            },
            std::format("[Light Grid] Cascade {} indices", i));
        }

        Fvog::GetDevice().ImmediateSubmit([=, this](VkCommandBuffer cmd) {
          auto ctx = Fvog::Context(cmd);
            for (uint32_t i = 0; i < numCascades; i++)
            {
              ctx.ImageBarrierDiscard(cascadedLightGrid.grids[i].cellLightListOffset.value(), VK_IMAGE_LAYOUT_GENERAL);
              ctx.ImageBarrierDiscard(cascadedLightGrid.grids[i].cellLightListCount.value(), VK_IMAGE_LAYOUT_GENERAL);
              ctx.ImageBarrierDiscard(cascadedLightGrid.grids[i].cellLightListCountIntermediate.value(), VK_IMAGE_LAYOUT_GENERAL);
            }
        });
      }

      struct LightGrid
      {
        std::optional<Fvog::Texture> cellLightListOffset;
        std::optional<Fvog::Texture> cellLightListCount;
        std::optional<Fvog::Texture> cellLightListCountIntermediate;
        std::optional<Fvog::TypedBuffer<uint32_t>> lightIndices;
      };

      struct CascadedLightGrid
      {
        std::array<LightGrid, LIGHT_GRID_MAX_CASCADES> grids{};
        LightGridParams params{};
      };

      CascadedLightGrid cascadedLightGrid;

      PipelineManager::ComputePipelineKey scatterLightsToCellsPipeline;
      PipelineManager::ComputePipelineKey allocateCellListsPipeline;
    };
  } // namespace

  std::unique_ptr<LightGrid> LightGrid::Create()
  {
    return std::make_unique<LightGridImpl>();
  }
} // namespace Techniques