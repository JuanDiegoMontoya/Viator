#include "ItemIconCache.h"

#include "Game/World.h"
#include "Game/Globals.h"
#include "Game/Block.h"
#include "Game/Voxel/Grid.h"
#include "shaders/voxels/DrawSingleVoxel.comp.glsl"
#include "Client/Fvog/Buffer2.h"
#include "Client/Fvog/Device.h"
#include "Client/Fvog/Rendering2.h"
#include "Client/Fvog/Texture2.h"
#include "Client/Fvog/detail/Hash2.h"
#include "Game/Item.h"

#include "spdlog/spdlog.h"

#include <unordered_map>
#include <utility>

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
      }

      void SetRenderContext(const ItemIconRenderContext& render) override
      {
        render_ = render;
      }

      Fvog::Texture* GetOrEmplaceIcon(World& world, const ItemIconParams& iconParams) override
      {
        const auto key = CacheKey{.iconParams = iconParams, .time = 0};
        auto it = iconCache_.find(key);
        if (it == iconCache_.end())
        {
          const auto name = Item::GetName(world, iconParams.item);
          spdlog::debug("Rendering icon for item {}. Resolution: ({}, {}). Samples: {}", name, iconParams.extent.width, iconParams.extent.height, iconParams.samples);

          auto tex = Fvog::CreateTexture2D({iconParams.extent.width, iconParams.extent.height}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::GENERAL, name);
          auto res = iconCache_.try_emplace(key, std::make_shared<Fvog::Texture>(std::move(tex)));
          ASSERT(res.second);
          it = res.first;
          if (!RenderIcon(world, iconParams.item, iconParams.samples, *it->second))
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
              if (!RenderIcon(world, iconParams.item, iconParams.samples, *it->second))
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
          // TODO
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
        const auto view_from_world2 = glm::lookAt(cameraPos, glm::vec3(0), glm::vec3(0, 1, 0));
        const auto clip_from_view2  = glm::ortho(-bound, bound, -bound, bound, -15.0f, 15.0f);

        auto paramsBuffer = Fvog::GetDevice().AllocTransient<DrawSingleVoxelParams_t>();

        *paramsBuffer = DrawSingleVoxelParams_t{
          .view_from_clip         = glm::inverse(clip_from_view2),
          .world_from_view        = glm::inverse(view_from_world2),
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
      };

      struct CacheKeyHash
      {
        std::size_t operator()(const CacheKey& k) const noexcept
        {
          auto hashed = std::make_tuple(k.iconParams.item, k.iconParams.extent.width, k.iconParams.extent.height, k.iconParams.samples, k.time);
          return Fvog::detail::hashing::hash<decltype(hashed)>{}(hashed);
        }
      };

      std::unordered_map<CacheKey, std::shared_ptr<Fvog::Texture>, CacheKeyHash> iconCache_;
      ItemIconRenderContext render_{};

      constexpr static CacheKey errorKey = {.iconParams = {.item = entt::null}};
    };
  } // namespace

  std::unique_ptr<ItemIconCache> ItemIconCache::Create()
  {
    return std::make_unique<ItemIconCacheImpl>();
  }
} // namespace Gui