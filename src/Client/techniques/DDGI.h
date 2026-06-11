#pragma once
#include "Client/Fvog/BasicTypes2.h"
#include "Client/Fvog/detail/VkFwd.h"
#include "shaders/voxels/Voxels.h.glsl"

#include "glm/vec3.hpp"

#include <memory>

struct GpuMesh;
class Scheduler;
struct DDGIProbeGridInfo;

namespace Fvog
{
  class GraphicsPipeline;
}

namespace Techniques
{
  struct DDGIInitParams
  {
    const DDGIProbeGridInfo* probeGridInfo;
    Fvog::Format sceneColorFormat{};
    Fvog::Format sceneDepthFormat{};
  };

  struct DDGIUpdateParams
  {
    glm::vec3 position{};
    Voxels voxels{};
    uint32_t shadingColorSpace{};
    shared::Texture2D noiseTexture{};
    uint32_t globalUniformsIndex{};
    bool showCascadeIndexAsColor{};
    shared::Sampler linearClampSampler{};
    bool debugFreezeGrid{};
    float baseGridScale{};
  };

  enum class DDGIDebugMode : uint32_t
  {
    None,
    Luminance,
    Illuminance,
    RawDepth,
    DepthMoments,
    Validity,
    AverageLuminance,
  };

  struct DDGIRenderDebugProbesParams
  {
    const GpuMesh* mesh{};
    int singleCascadeToShow{};
    uint32_t globalUniformsIndex{};
    shared::Sampler linearClampSampler{};
    DDGIDebugMode mode{};
    float probeSize{};
  };

  class DDGI
  {
  public:
    static std::unique_ptr<DDGI> Create(const DDGIInitParams& params);

    virtual ~DDGI() = default;

    virtual void Update(Scheduler& scheduler, VkCommandBuffer cmd, const DDGIUpdateParams& params) = 0;

    virtual VkDeviceAddress GetArgsBufferAddress() = 0;

    virtual void RenderDebugProbes(VkCommandBuffer cmd, const DDGIRenderDebugProbesParams& params) = 0;
  };
}