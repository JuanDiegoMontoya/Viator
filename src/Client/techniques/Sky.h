#pragma once
#include "Client/Fvog/BasicTypes2.h"
#include "Client/Fvog/detail/VkFwd.h"

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
    uint32_t globalUniformsBufferIndex;
  };

  struct SkyComputeMultiscatteringLutParams
  {
    uint32_t globalUniformsBufferIndex;
    
  };

  struct SkyComputeSkyViewLutParams
  {
    uint32_t globalUniformsBufferIndex;

  };

  struct SkyComputeAerialPerspectiveLutParams
  {
    
  };

  class Sky
  {
  public:
    static std::unique_ptr<Sky> Create();

    virtual ~Sky() = default;

    // Ensures resources exist with the specified sizes.
    // Call at least once before invoking any of the Compute* functions.
    virtual void EnsureResources(VkCommandBuffer cmd, const SkyResourceExtents& extents)                              = 0;
    virtual void ComputeTransmittanceLut(VkCommandBuffer cmd, const SkyComputeTransmittanceLutParams& params)         = 0;
    virtual void ComputeMultiscatteringLut(VkCommandBuffer cmd, const SkyComputeMultiscatteringLutParams& params)     = 0;
    virtual void ComputeSkyViewLut(VkCommandBuffer cmd, const SkyComputeSkyViewLutParams& params)                     = 0;
    virtual void ComputeAerialPerspectiveLut(VkCommandBuffer cmd, const SkyComputeAerialPerspectiveLutParams& params) = 0;

    virtual Fvog::Texture& GetTransmittanceLut()     = 0;
    virtual Fvog::Texture& GetMultiscatteringLut()   = 0;
    virtual Fvog::Texture& GetSkyViewLut()           = 0;
    virtual Fvog::Texture& GetAerialPerspectivelut() = 0;
  };
}