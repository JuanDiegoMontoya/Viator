#pragma once
#include "Client/Fvog/BasicTypes2.h"
#include "shaders/voxels/Voxels.h.glsl"
#include "Client/Fvog/detail/VkFwd.h"

#include <memory>

class Scheduler;

namespace Techniques
{
  struct FoliageSSSRenderBeerShadowMapParams
  {
    Fvog::Extent2D shadowResolution;
    uint32_t numCascades;
    Voxels voxels;
    glm::vec3 playerPos; // the light will look at this
    glm::vec3 lightDirection;
    float frustumDepth;
    float baseFrustumSideLength;
  };

  class FoliageSSS
  {
  public:
    static std::unique_ptr<FoliageSSS> Create();

    virtual ~FoliageSSS() = default;
    virtual void RenderBeerShadowMap(Scheduler& scheduler, VkCommandBuffer cmd, const FoliageSSSRenderBeerShadowMapParams& params) = 0;
    virtual VkDeviceAddress GetCBSMInfoPtr() = 0;
  };
}