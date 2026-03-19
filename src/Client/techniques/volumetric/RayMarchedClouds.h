#pragma once
#include "Client/Fvog/detail/VkFwd.h"

#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"

#include <memory>

namespace Fvog
{
  class Texture;
}

namespace Techniques
{
  struct RayMarchedCloudsRenderParams
  {
    Fvog::Texture* gDepth{};
    uint32_t renderWidth{};
    uint32_t renderHeight{};
    uint32_t upscaleWidth{};
    glm::mat4 clip_from_view{};
    glm::mat4 view_from_world{};
    glm::mat4 clip_from_view_old{};
    glm::mat4 view_from_world_old{};
    uint32_t numRayMarchSteps{};
    glm::vec3 sunDirection{};
    glm::vec3 sunIntensity{};
    uint32_t globalUniformsIndex{};
    VkDeviceAddress ddgi{};
    uint32_t frameNumber;
  };

  struct RayMarchedCloudsUpscaleParams
  {
    uint32_t upscaleWidth{};
    uint32_t upscaleHeight{};
  };

  struct RayMarchedCloudsCompositeParams
  {
    Fvog::Texture* gRadianceIn{};
    Fvog::Texture* gRadianceOut{};
  };

  class RayMarchedClouds
  {
  public:
    virtual ~RayMarchedClouds() = default;
    static std::unique_ptr<RayMarchedClouds> Create();

    virtual void Render(VkCommandBuffer cmd, const RayMarchedCloudsRenderParams& params) = 0;
    virtual void Upscale(VkCommandBuffer cmd, const RayMarchedCloudsUpscaleParams& params) = 0;
    virtual void Composite(VkCommandBuffer cmd, const RayMarchedCloudsCompositeParams& params) = 0;
  };
}