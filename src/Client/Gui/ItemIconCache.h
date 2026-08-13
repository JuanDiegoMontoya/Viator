#pragma once
#include "Game/ItemFwd.h"

#include "Client/Fvog/BasicTypes2.h"
#include "Client/Fvog/detail/VkFwd.h"

#include <memory>

namespace Fvog
{
  class Buffer;
  class ComputePipeline;
  class Texture;
}

class World;

namespace Gui
{
  struct ItemIconParams
  {
    bool operator==(const ItemIconParams&) const = default;

    ItemId item;
  };

  struct ItemIconRenderContext
  {
    VkCommandBuffer cmd;
    const Fvog::ComputePipeline* drawSingleVoxelPipeline;
    const Fvog::ComputePipeline* drawMeshPipeline;
    Fvog::Buffer* voxelMaterialBuffer;
    double time;
    Fvog::Extent2D extent;
    uint32_t samples;
  };

  class ItemIconCache
  {
  public:
    static std::unique_ptr<ItemIconCache> Create();

    virtual ~ItemIconCache() = default;

    virtual void SetRenderContext(const ItemIconRenderContext& render) = 0;

    virtual Fvog::Texture* GetOrEmplaceIcon(World& world, const ItemIconParams& params) = 0;

    virtual void Clear() = 0;
  };
}