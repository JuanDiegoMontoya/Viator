#pragma once
#include "Client/Fvog/BasicTypes2.h"
#include "Client/Fvog/detail/VkFwd.h"
#include "shaders/sky/SkyParams.shared.h"

#include "glm/mat4x4.hpp"

#include <memory>

namespace Fvog
{
  class Texture;
}

namespace Techniques
{
  struct SkyResourceExtents
  {
    Fvog::Extent2D transmittanceLutExtent{};
    Fvog::Extent2D multiscatteringLutExtent{};
    Fvog::Extent2D skyViewLutExtent{};
    Fvog::Extent3D aerialPerspectiveLutExtent{};
  };

  struct SkyComputeTransmittanceLutParams
  {
    VkDeviceAddress globalUniformsPtr;
  };

  struct SkyComputeMultiscatteringLutParams
  {
    VkDeviceAddress globalUniformsPtr;
  };

  struct SkyComputeSkyViewLutParams
  {
    VkDeviceAddress globalUniformsPtr;
  };

  struct SkyComputeAerialPerspectiveLutParams
  {
    VkDeviceAddress globalUniformsPtr;
  };

  struct SkyGetSkyDataParams
  {
    SkyConfig skyConfig;
    float zNear{};
    float zFar{};
    float aspectRatio{};
    float fovy{};
    glm::mat4 view_from_world{};
  };

  class Sky
  {
  public:
    static std::unique_ptr<Sky> Create();

    virtual ~Sky() = default;

    // Ensures resources exist with the specified sizes.
    // Call at least once before invoking any of the Compute* functions.
    virtual void EnsureResources(VkCommandBuffer cmd, const SkyResourceExtents& extents)                              = 0;
    virtual SkyData GetSkyData(const SkyGetSkyDataParams& params)                                                     = 0;
    virtual void ComputeTransmittanceLut(VkCommandBuffer cmd, const SkyComputeTransmittanceLutParams& params)         = 0;
    virtual void ComputeMultiscatteringLut(VkCommandBuffer cmd, const SkyComputeMultiscatteringLutParams& params)     = 0;
    virtual void ComputeSkyViewLut(VkCommandBuffer cmd, const SkyComputeSkyViewLutParams& params)                     = 0;
    virtual void ComputeAerialPerspectiveLut(VkCommandBuffer cmd, const SkyComputeAerialPerspectiveLutParams& params) = 0;

    virtual Fvog::Texture& GetTransmittanceLut()               = 0;
    virtual Fvog::Texture& GetMultiscatteringLut()             = 0;
    virtual Fvog::Texture& GetSkyViewLut()                     = 0;
    virtual Fvog::Texture& GetAerialPerspectiveTransmittance() = 0;
    virtual Fvog::Texture& GetAerialPerspectiveScattering()    = 0;
  };
}