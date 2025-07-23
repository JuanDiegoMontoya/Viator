#pragma once
#include "Fvog/Buffer2.h"
#include "Fvog/Texture2.h"
#include "PipelineManager.h"
#include "PlayerHead.h"
#include "Game/TwoLevelGrid.h"
#include "debug/Shapes.h"
#include "techniques/denoising/spatial/Bilateral.h"
#include "techniques/Bloom.h"
#include "techniques/AutoExposure.h"
#include "techniques/volumetric/FroxelFog.h"
#include "shaders/Light.h.glsl"
#include "shaders/voxels/Voxels.h.glsl"
#include "shaders/ddgi/ProbeCommon.shared.h"
#include "shaders/post/TonemapAndDither.shared.h"
#include "shaders/GlobalUniforms.h.glsl"

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "entt/entity/entity.hpp"

#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string>

namespace Temp
{
  FVOG_DECLARE_ARGUMENTS(PushConstants)
  {
    Voxels voxels;
    FVOG_UINT32 uniformBufferIndex;
  };

  FVOG_DECLARE_ARGUMENTS(DebugTextureArguments)
  {
    FVOG_UINT32 textureIndex;
    FVOG_UINT32 samplerIndex;
  };

  FVOG_DECLARE_ARGUMENTS(MeshArgs)
  {
    VkDeviceAddress objects;
    VkDeviceAddress frame;
  };

  struct BillboardInstance
  {
    glm::vec3 position;
    glm::vec2 scale;
    glm::vec4 leftColor;
    glm::vec4 rightColor;
    float middle;
  };

  struct BillboardSpriteInstance
  {
    glm::vec3 position;
    glm::vec2 scale;
    glm::vec3 tint;
    shared::Texture2D texture;
  };

  struct ObjectUniforms
  {
    glm::mat4 worldFromObject;
    VkDeviceAddress vertexBuffer;
    glm::vec3 tint;
  };
} // namespace "Temp"

class VoxelRenderer
{
public:

  explicit VoxelRenderer(PlayerHead* head, World& world);
  ~VoxelRenderer();

  void CreateRenderingMaterials(std::span<const std::unique_ptr<BlockDefinition>> blockDefinitions);

private:

  void InitGui();
  void LoadGameSettings();
  void ShowEditor(DeltaTime dt, World& world);
  bool ShowSettingsWindow(World& world);
  void OnFramebufferResize(uint32_t newWidth, uint32_t newHeight);
  void OnRender(double dt, World& world, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex);
  void RenderGame(double dt, World& world, VkCommandBuffer commandBuffer);
  void OnGui(DeltaTime dt, World& world, VkCommandBuffer commandBuffer);

  Fvog::Texture& GetOrEmplaceCachedTexture(const std::string& name, bool srgb);

  struct Frame
  {
    // G-buffer
    std::optional<Fvog::Texture> sceneAlbedo;
    constexpr static Fvog::Format sceneAlbedoFormat = Fvog::Format::R8G8B8A8_SRGB;
    std::optional<Fvog::Texture> sceneNormal;
    constexpr static Fvog::Format sceneNormalFormat = Fvog::Format::R16G16B16A16_SNORM; // TODO: should be oct
    std::optional<Fvog::Texture> sceneRadiance;
    std::optional<Fvog::Texture> sceneIlluminance;
    std::optional<Fvog::Texture> sceneIlluminancePingPong; // Used in denoising.
    constexpr static Fvog::Format sceneIlluminanceFormat = Fvog::Format::R16G16B16A16_SFLOAT;
    std::optional<Fvog::Texture> sceneSpecial;
    constexpr static Fvog::Format sceneSpecialFormat = Fvog::Format::R8_UINT;

    // Pre-tonemap
    std::optional<Fvog::Texture> sceneColor;
    std::optional<Fvog::Texture> sceneColorBloomScratch;
    constexpr static Fvog::Format sceneColorFormat = Fvog::Format::R16G16B16A16_SFLOAT;

    // Post-tonemap. Format allows for HDR display support.
    std::optional<Fvog::Texture> sceneColorTonemapped;
    constexpr static Fvog::Format sceneColorTonemappedFormat = Fvog::Format::R16G16B16A16_SFLOAT;
    std::optional<Fvog::Texture> sceneDepth;
    constexpr static Fvog::Format sceneDepthFormat = Fvog::Format::D32_SFLOAT;
  };
  Frame frame;

  Techniques::Bilateral bilateral_;
  Techniques::AutoExposure autoExposure_;
  Techniques::Bloom bloom_;
  Fvog::NDeviceBuffer<GlobalUniforms> perFrameUniforms;
  PipelineManager::GraphicsPipelineKey voxelsPipeline;
  PipelineManager::GraphicsPipelineKey meshPipeline;
  PipelineManager::GraphicsPipelineKey debugTexturePipeline;
  PipelineManager::GraphicsPipelineKey debugLinesPipeline;
  PipelineManager::GraphicsPipelineKey billboardsPipeline;
  PipelineManager::GraphicsPipelineKey billboardSpritesPipeline;

  PipelineManager::ComputePipelineKey spelunkerEffectPipeline;
  PipelineManager::ComputePipelineKey shadeDeferredPipeline;
  PipelineManager::ComputePipelineKey perPixelPathtracerPipeline;
  PipelineManager::ComputePipelineKey tonemapPipeline;

  PipelineManager::ComputePipelineKey skyTransmittancePipeline;
  PipelineManager::ComputePipelineKey skyMultiscatteringPipeline;
  PipelineManager::ComputePipelineKey skyViewPipeline;

  std::optional<Fvog::NDeviceBuffer<Temp::ObjectUniforms>> meshUniformz;
  std::optional<Fvog::NDeviceBuffer<Debug::Line>> lineVertexBuffer;
  std::optional<Fvog::NDeviceBuffer<GpuLight>> lightBuffer;
  std::optional<Fvog::NDeviceBuffer<Temp::BillboardInstance>> billboardInstanceBuffer;
  std::optional<Fvog::NDeviceBuffer<Temp::BillboardSpriteInstance>> billboardSpriteInstanceBuffer;
  std::optional<Fvog::Buffer> voxelMaterialBuffer;
  std::optional<Fvog::Buffer> voxelMaterialBufferSpelunker;
  std::optional<Fvog::Texture> noiseTexture;
  std::optional<Fvog::Texture> tonyMcMapfaceLut;
  std::optional<Fvog::Texture> backgroundTexture;
  std::optional<Fvog::Texture> transmittanceLut;
  std::optional<Fvog::Texture> multiscatteringLut;
  std::optional<Fvog::Texture> skyViewLut;
  std::optional<Fvog::TextureView> transmittanceLutView;
  std::optional<Fvog::TextureView> multiscatteringLutView;
  std::optional<Fvog::TextureView> skyViewLutView;

  Fvog::TypedBuffer<float> exposureBuffer;
  Fvog::NDeviceBuffer<shared::TonemapUniforms> tonemapUniformBuffer;
  shared::TonemapUniforms tonemapUniforms{};
  std::unordered_map<std::string, Fvog::Texture> stringToTexture;
  PlayerHead* head_;
  entt::entity selectedEntity = entt::null;

  // DDGI
  struct DDGI
  {
    //static constexpr Fvog::Format radianceFormat = Fvog::Format::B10G11R11_UFLOAT;
    static constexpr Fvog::Format radianceFormat = Fvog::Format::R32G32B32A32_SFLOAT; // TODO: TEMP until quantization with smaller formats is dealt with.
    std::optional<Fvog::NDeviceBuffer<DDGIArgs>> argsBuffer;
    std::optional<Fvog::Texture> packedProbeRadiance;
    std::optional<Fvog::Texture> packedProbeRawDepth; // Same resolution as radiance
    std::optional<Fvog::Texture> packedProbeIrradiance;
    std::optional<Fvog::Texture> packedProbeDepthMoments; // Filtered depth and depth^2
    std::unique_ptr<std::optional<Fvog::TypedBuffer<ProbeData>>[]> probeDataBuffers;
    DDGIArgs args{};
    PipelineManager::ComputePipelineKey traceRaysPipeline;
    PipelineManager::ComputePipelineKey convolveIrradiancePipeline;
    PipelineManager::ComputePipelineKey downsampleDepthPipeline;
    PipelineManager::ComputePipelineKey resetNewProbesPipeline;
    PipelineManager::GraphicsPipelineKey debugProbesPipeline;
  } ddgi;

  // Successive cascades will have 2x the scale of the previous.
  void InitDDGI(const DDGIProbeGridInfo& probeGridInfo);

  enum class DDGIDebugView : uint32_t
  {
    None,
    Luminance,
    Illuminance,
    RawDepth,
    DepthMoments,
    Validity,
    AverageLuminance,
  };

  DDGIDebugView ddgiDebugView_ = DDGIDebugView::None;
  float ddgiDebugProbeSize_    = 0.25f;
  bool ddgiDebugPauseUpdates_  = false;
  bool ddgiDebugFreezeGrid_    = false; // Pauses only grid movement- probes still update.
  int ddgiDebugShowOnlyThisCascade_ = -1; // <0: show all cascades
  bool ddgiDebugShowCascadeIndexAsColor_ = false;

  Techniques::FroxelFog fog_;
  // Resources needed for froxel fog
  std::optional<Fvog::Texture> inScatteringAndTransmittanceVolume;
  std::optional<Fvog::Texture> fogColorAndDensityVolume;

  enum class GIMethod
  {
    None,
    PerPixelPathTracing,
    DDGI,
  };

  GIMethod giMethod_ = GIMethod::DDGI;

  int32_t pathTracerSamples = 1;
  int32_t pathTracerBounces = 2;
  bool enableBloom          = true;
  bool debugDisableFog      = false;

  float sunElevation = 0.5f;
  float sunAzimuth   = 0.3f;
  glm::vec3 sunColor = glm::vec3(1.0f, 0.94f, 0.91f);
  float sunBrightness = 100'000;
};
