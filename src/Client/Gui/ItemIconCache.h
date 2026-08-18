#pragma once
#include "Game/ItemFwd.h"

#include "Client/Fvog/BasicTypes2.h"
#include "Client/Fvog/detail/VkFwd.h"

#include <memory>

// TODO: remove when real mesh cache is added.
#include "Client/GpuMesh.h"
#include <unordered_map>
#include <string>

namespace Fvog
{
  class Buffer;
  class ComputePipeline;
  class GraphicsPipeline;
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
    const Fvog::ComputePipeline* drawSingleVoxelPipeline{};
    const Fvog::GraphicsPipeline* drawMeshPipeline{};
    std::unordered_map<std::string, GpuMesh>* meshes{};
    std::unordered_map<std::string, Fvog::Texture>* textures{};
    Fvog::Buffer* voxelMaterialBuffer{};
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