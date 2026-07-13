#pragma once
#include "Client/Fvog/detail/VkFwd.h"

#include "glm/vec3.hpp"

#include <memory>

class Scheduler;

namespace Fvog
{
  class Buffer;
}

namespace Techniques
{
  struct LightGridParams
  {
    uint32_t numCascades{};
    glm::ivec3 cascadeDimensions{};
    float baseGridScale{};
    uint32_t maxLightsPerCell{};
    uint32_t lightIndicesPerCascade{};
  };

  struct LightGridUpdateInfo
  {
    LightGridParams gridParams;
    uint32_t numLights{};
    VkDeviceAddress lightsBuffer{};
    glm::vec3 cameraPosition{};
  };

  class LightGrid
  {
  public:
    static [[nodiscard]] std::unique_ptr<LightGrid> Create();

    virtual ~LightGrid() = default;

    // Returns a device pointer to the cascaded light grid structure.
    virtual [[nodiscard]] VkDeviceAddress Update(VkCommandBuffer cmd, Scheduler& scheduler, const LightGridUpdateInfo& info) = 0;
  };
}