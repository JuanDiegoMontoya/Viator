#include "ItemIconCache.h"

#include "Client/PipelineManager.h"
#include "Game/World.h"
#include "Game/Globals.h"
#include "Game/Block.h"
#include "Game/Voxel/Grid.h"
#include "shaders/voxels/DrawSingleVoxel.comp.glsl"
#include "shaders/mesh/MeshColorIcon.shared.h"
#include "shaders/mesh/IconOutline.comp.glsl"
#include "shaders/mesh/SpriteIcon.comp.glsl"
#include "Client/Fvog/Buffer2.h"
#include "Client/Fvog/Device.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/Fvog/Texture2.h"
#include "Client/Fvog/detail/Hash2.h"
#include "Core/Defer.h"
#include "Game/Assets.h"
#include "Game/Item.h"

#include "spdlog/spdlog.h"
#include "volk.h"
#include "Client/Fvog/detail/Common.h"

#include <unordered_map>
#include <utility>
#include <format>
#include <queue>

namespace Gui
{
  namespace
  {
    class ItemIconCacheImpl : public ItemIconCache
    {
    public:
      ItemIconCacheImpl()
      {
        EmplaceErrorIcon();

        outlinePipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "Render icon outline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "mesh/IconOutline.comp.glsl",
            },
        });

        spritePipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "Render sprite icon",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "mesh/SpriteIcon.comp.glsl",
            },
        });
      }

      void SetRenderContext(const ItemIconRenderContext& render) override
      {
        render_ = render;
      }

      Fvog::Texture* GetOrEmplaceIcon(World& world, const ItemIconParams& iconParams) override
      {
        const auto key = CacheKey{.iconParams = iconParams, .time = 0, .extent = render_.extent, .samples = render_.samples};
        auto it = iconCache_.find(key);
        if (it == iconCache_.end())
        {
          const auto name = Item::GetName(world, iconParams.item);
          spdlog::debug("Rendering icon for item {}. Resolution: ({}, {}). Samples: {}", name, render_.extent.width, render_.extent.height, render_.samples);

          const auto levels = 1 + uint32_t(std::ceil(std::log2((float)glm::max(render_.extent.width, render_.extent.height))));
          auto tex = Fvog::CreateTexture2DMip({render_.extent.width, render_.extent.height}, Fvog::Format::R8G8B8A8_UNORM, levels, Fvog::TextureUsage::GENERAL, name);
          auto res = iconCache_.try_emplace(key, std::make_shared<Fvog::Texture>(std::move(tex)));
          ASSERT(res.second);
          it = res.first;
          if (!RenderIcon(world, iconParams.item, render_.samples, *it->second))
          {
            it->second = iconCache_.at(errorKey);
          }
        }
        else
        {
          // Determine if the icon needs to be updated due to animation.
          const auto& itemReg = world.globals->itemRegistry->GetRegistry();
          if (const auto* bp = itemReg.try_get<const Item::Component::Block>(entt::entity(iconParams.item)))
          {
            const auto& blockReg = world.globals->blockRegistry->GetRegistry();
            if (blockReg.all_of<Block::Component::RenderAsAnimatedSubGrid>(entt::entity(bp->voxel)))
            {
              if (!RenderIcon(world, iconParams.item, render_.samples, *it->second))
              {
                it->second = iconCache_.at(errorKey);
              }
            }
          }
        }

        return it->second.get();
      }

      void Clear() override
      {
        iconCache_.clear();
        EmplaceErrorIcon();
      }

    private:
      bool RenderIcon(World& world, ItemId item, uint32_t samples, Fvog::Texture& iconTexture) const
      {
        const auto& itemReg = world.globals->itemRegistry->GetRegistry();
        if (const auto* bp = itemReg.try_get<const Item::Component::Block>(entt::entity(item)))
        {
          RenderBlockIcon(world, bp->voxel, samples, iconTexture);
          return true;
        }
        
        if (const auto* sp = itemReg.try_get<const Item::Component::MaterializeAsSprite>(entt::entity(item)))
        {
          RenderSpriteIcon(world, sp->tag, sp->tint, iconTexture);
          return true;
        }

        if (const auto* mp = itemReg.try_get<const Item::Component::MaterializeAsMeshEntity>(entt::entity(item)))
        {
          RenderMeshIcon(world, item, iconTexture);
          return true;
        }

        return false;
      }

      void RenderBlockIcon(World& world, BlockId block, uint32_t samples, Fvog::Texture& iconTexture) const
      {
        auto ctx = Fvog::Context(render_.cmd);

        const auto name = Block::GetName(world, block);
        auto marker2 = ctx.MakeScopedDebugMarker(name.c_str());

        const auto cameraPos        = glm::vec3(1, 1, 1);
        const auto bound            = 0.8f;
        const auto view_from_world = glm::lookAt(cameraPos, glm::vec3(0), glm::vec3(0, 1, 0));
        const auto clip_from_view  = glm::ortho(-bound, bound, -bound, bound, -15.0f, 15.0f);

        auto paramsBuffer = Fvog::GetDevice().AllocTransient<DrawSingleVoxelParams_t>();

        *paramsBuffer = DrawSingleVoxelParams_t{
          .view_from_clip         = glm::inverse(clip_from_view),
          .world_from_view        = glm::inverse(view_from_world),
          .cameraPos              = cameraPos,
          .samples                = samples,
          .outRadianceSDR         = iconTexture.ImageView().GetImage2D(),
          .voxel                  = voxel_t(block),
          .voxelDataBufferIdx     = world.globals->grid->Buffer().GetGpuBuffer().GetResourceHandle().index,
          .voxelMaterialBufferIdx = render_.voxelMaterialBuffer->GetResourceHandle().index,
          .time                   = static_cast<float>(render_.time),
        };

        ctx.ImageBarrierDiscard(iconTexture, VK_IMAGE_LAYOUT_GENERAL);
        ctx.SetPushConstants(paramsBuffer);
        ctx.BindComputePipeline(*render_.drawSingleVoxelPipeline);
        ctx.DispatchInvocations(iconTexture.GetCreateInfo().extent.width, iconTexture.GetCreateInfo().extent.height, 1);

        GenerateMipmap(iconTexture);
      }

      void RenderSpriteIcon([[maybe_unused]] World& world, const std::string& sprite, glm::vec3 tint, Fvog::Texture& iconTexture) const
      {
        auto& srcTexture = render_.textures->at(sprite);
        auto ctx = Fvog::Context(render_.cmd);
        auto marker = ctx.MakeScopedDebugMarker(std::format("Rendering tinted sprite icon {}", sprite).c_str());

        auto gpuParams = Fvog::GetDevice().AllocTransient<SpriteIconParams_t>();

        *gpuParams = {
          .sprite      = srcTexture.ImageView().GetTexture2D(),
          .radianceSDR = iconTexture.ImageView().GetImage2D(),
          .tint        = tint,
        };

        ctx.ImageBarrierDiscard(iconTexture, VK_IMAGE_LAYOUT_GENERAL);
        ctx.BindComputePipeline(spritePipeline_.GetPipeline());
        ctx.SetPushConstants(gpuParams);
        ctx.DispatchInvocations(iconTexture.GetCreateInfo().extent);

        GenerateMipmap(iconTexture);
      }

      void RenderMeshIcon(World& world, ItemId item, Fvog::Texture& iconTexture) const
      {
        const auto entity = Item::Materialize(world, item);

        auto _ = Defer([&] { Item::Dematerialize(world, item, entity); });
        
        auto ctx = Fvog::Context(render_.cmd);

        const auto name = Item::GetName(world, item);

        const auto extent = VkExtent2D{iconTexture.GetCreateInfo().extent.width, iconTexture.GetCreateInfo().extent.height};
        auto depthTexture = Fvog::CreateTexture2D(extent, Fvog::Format::D16_UNORM, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Temporary depth texture for icon");

        ctx.ImageBarrierDiscard(iconTexture, VK_IMAGE_LAYOUT_GENERAL);
        ctx.ImageBarrierDiscard(depthTexture, VK_IMAGE_LAYOUT_GENERAL);

        const auto colorAttachment = Fvog::RenderColorAttachment{
          .texture    = iconTexture.ImageView(),
          .loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .clearValue = Fvog::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f),
        };

        const auto depthAttachment = Fvog::RenderDepthStencilAttachment{
          .texture    = depthTexture.ImageView(),
          .loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .clearValue = {.depth = 1.0f},
        };

        ctx.BeginRendering(Fvog::RenderInfo{
          .name             = std::format("Render mesh icon for {}", name).c_str(),
          .colorAttachments = std::span{&colorAttachment, 1},
          .depthAttachment  = depthAttachment,
        });

        ctx.BindGraphicsPipeline(*render_.drawMeshPipeline);

        auto averagePosition = glm::vec3{};

        {
          auto sumPosition = glm::vec3(0);
          auto entityCount = 0;

          auto entityQueue = std::queue<ItemId>();
          entityQueue.push(entity);
          while (!entityQueue.empty())
          {
            const auto nextEntity = entityQueue.front();
            entityQueue.pop();

            if (const auto* hp = world.GetRegistry().try_get<const Hierarchy>(nextEntity))
            {
              for (auto child : hp->children)
              {
                entityQueue.push(child);
              }
            }

            if (const auto* gt = world.GetRegistry().try_get<const GlobalTransform>(nextEntity))
            {
              sumPosition += gt->position;
              entityCount++;
            }
          }

          averagePosition = sumPosition / (float)entityCount;
        }

        auto renderQueue = std::queue<ItemId>();

        renderQueue.push(entity);

        while (!renderQueue.empty())
        {
          const auto entityToRender = renderQueue.front();
          renderQueue.pop();

          if (const auto* hp = world.GetRegistry().try_get<const Hierarchy>(entityToRender))
          {
            for (auto child : hp->children)
            {
              renderQueue.push(child);
            }
          }

          if (const auto* mp = world.GetRegistry().try_get<const Mesh>(entityToRender))
          {
            auto mod = Item::Component::IconModifiers{};
            if (const auto* ip = world.globals->itemRegistry->GetRegistry().try_get<const Item::Component::IconModifiers>(entt::entity(item)))
            {
              mod = *ip;
            }

            const auto world_from_object = glm::angleAxis(mod.deltaRoll, glm::vec3{0, 0, 1}) * glm::angleAxis(mod.deltaYaw, glm::vec3{0, 1, 0}) *
                                           glm::angleAxis(mod.deltaPitch, glm::vec3{1, 0, 0});

            const auto cameraPos       = glm::vec3(1, .5f, 1) + averagePosition + mod.deltaCameraPosition;
            const auto bound           = 0.6f + mod.deltaBound;
            const auto view_from_world = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.5f, 0.0f) + mod.deltaLookAtPosition, glm::vec3(0, 1, 0));
            const auto clip_from_view  = glm::ortho(-bound, bound, -bound, bound, -15.0f, 15.0f);

            auto tint = glm::vec3(1);
            if (const auto* tp = world.GetRegistry().try_get<const Tint>(entityToRender))
            {
              tint = tp->color;
            }

            const auto& mesh = render_.meshes->at(mp->name);

            auto gpuParams = Fvog::GetDevice().AllocTransient<DrawMeshColorIconParams_t>();

            *gpuParams = {
              .clip_from_world   = clip_from_view * view_from_world,
              .world_from_object = glm::mat4_cast(world_from_object),
              .vertexBuffer      = mesh.vertexBuffer->GetDeviceAddress(),
              .tint              = tint,
              .cameraPos         = cameraPos,
            };

            ctx.SetPushConstants(gpuParams);
            ctx.BindIndexBuffer(mesh.indexBuffer.value(), 0, VK_INDEX_TYPE_UINT32);
            ctx.DrawIndexed((uint32_t)mesh.indices.size(), 1, 0, 0, 0);
          }
        }

        ctx.EndRendering();

        ctx.Barrier();

        {
          auto marker2 = ctx.MakeScopedDebugMarker("Render outline");

          auto gpuParams = Fvog::GetDevice().AllocTransient<IconOutlineParams_t>();

          *gpuParams = {
            .depth       = depthTexture.ImageView().GetTexture2D(),
            .radianceSDR = iconTexture.ImageView().GetImage2D(),
            .farDepth    = 1.0f,
            .width       = 1,
          };

          ctx.BindComputePipeline(outlinePipeline_.GetPipeline());
          ctx.SetPushConstants(gpuParams);
          ctx.DispatchInvocations(iconTexture.GetCreateInfo().extent);
        }

        GenerateMipmap(iconTexture);
      }

      void GenerateMipmap(Fvog::Texture& iconTexture) const
      {
        auto ctx = Fvog::Context(render_.cmd);

        for (uint32_t i = 1; i < iconTexture.GetCreateInfo().mipLevels; i++)
        {
          ctx.Barrier();

          const int prevWidth  = iconTexture.GetCreateInfo().extent.width >> (i - 1);
          const int prevHeight = iconTexture.GetCreateInfo().extent.width >> (i - 1);
          const int curWidth   = iconTexture.GetCreateInfo().extent.width >> i;
          const int curHeight  = iconTexture.GetCreateInfo().extent.width >> i;

          auto region = VkImageBlit2{
            .sType          = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = i - 1, .baseArrayLayer = 0, .layerCount = 1},
            .srcOffsets     = {},
            .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = i, .baseArrayLayer = 0, .layerCount = 1},
            .dstOffsets     = {},
          };

          region.srcOffsets[1] = {prevWidth, prevHeight, 1};
          region.dstOffsets[1] = {curWidth, curHeight, 1};

          vkCmdBlitImage2(render_.cmd,
            Fvog::detail::Address(VkBlitImageInfo2{
              .sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
              .srcImage       = iconTexture.Image(),
              .srcImageLayout = *iconTexture.currentLayout,
              .dstImage       = iconTexture.Image(),
              .dstImageLayout = *iconTexture.currentLayout,
              .regionCount    = 1,
              .pRegions       = &region,
              .filter         = VK_FILTER_LINEAR,
            }));
        }
      }

      void EmplaceErrorIcon()
      {
        // Checkerboard error texture.
        constexpr uint32_t width  = 16;
        constexpr uint32_t height = 16;
        auto res                  = iconCache_.try_emplace(errorKey,
          std::make_shared<Fvog::Texture>(Fvog::CreateTexture2D({width, height}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::GENERAL, "Error icon")));
        ASSERT(res.second, "Error icon already present!");
        auto buffer = Fvog::TypedBuffer<glm::u8vec4>({.count = width * height, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE});
        auto* pBuf  = buffer.GetMappedMemory();

        for (uint32_t y = 0; y < height; y++)
        {
          for (uint32_t x = 0; x < width; x++)
          {
            glm::u8vec4 color = {0, 0, 0, 255};

            if ((x + y) % 2 == 1)
            {
              color = {255, 0, 255, 255};
            }

            pBuf[y * width + x] = color;
          }
        }

        Fvog::GetDevice().ImmediateSubmit(
          [&](VkCommandBuffer cmd)
          {
            auto ctx = Fvog::Context(cmd);
            ctx.ImageBarrierDiscard(*res.first->second, VK_IMAGE_LAYOUT_GENERAL);
            ctx.CopyBufferToTexture(buffer, *res.first->second, {.extent = {width, height}});
          });
      }

      struct CacheKey
      {
        bool operator==(const CacheKey&) const = default;

        ItemIconParams iconParams;
        double time{}; // For animated items and blocks.
        Fvog::Extent2D extent;
        uint32_t samples;
      };

      struct CacheKeyHash
      {
        std::size_t operator()(const CacheKey& k) const noexcept
        {
          auto hashed = std::make_tuple(k.iconParams.item, k.extent.width, k.extent.height, k.samples, k.time);
          return Fvog::detail::hashing::hash<decltype(hashed)>{}(hashed);
        }
      };

      std::unordered_map<CacheKey, std::shared_ptr<Fvog::Texture>, CacheKeyHash> iconCache_;
      ItemIconRenderContext render_{};
      PipelineManager::ComputePipelineKey outlinePipeline_;
      PipelineManager::ComputePipelineKey spritePipeline_;

      constexpr static CacheKey errorKey = {.iconParams = {.item = entt::null}};
    };
  } // namespace

  std::unique_ptr<ItemIconCache> ItemIconCache::Create()
  {
    return std::make_unique<ItemIconCacheImpl>();
  }
} // namespace Gui