#pragma once
#include "Fvog/Buffer2.h"
#include "Fvog/Texture2.h"
#include "PipelineManager.h"
#include "PlayerHead.h"
#include "Game/Voxel/Grid.h"
#include "debug/Shapes.h"
#include "techniques/denoising/spatial/Bilateral.h"
#include "techniques/Bloom.h"
#include "techniques/AutoExposure.h"
#include "techniques/volumetric/FroxelFog.h"
#include "techniques/ao/RayTracedAO.h"
#include "techniques/ao/ScreenSpaceGI.h"
#include "techniques/shadows/CascadedShadowMaps.h"
#include "techniques/volumetric/RayMarchedClouds.h"
#include "techniques/Sky.h"
#include "techniques/Particles.h"
#include "shaders/Light.h.glsl"
#include "shaders/voxels/Voxels.h.glsl"
#include "shaders/ddgi/ProbeCommon.shared.h"
#include "shaders/post/TonemapAndDither.shared.h"
#include "shaders/GlobalUniforms.h.glsl"
#include "shaders/sky/SkyShared.h.glsl"
#include "shaders/voxels/ShadeDeferred.shared.h"
#include "shaders/debug/DebugCommon.h.glsl"
#include "Game/CVar.h"

#ifdef FROGRENDER_FSR2_ENABLE
  #include "src/ffx-fsr2-api/ffx_fsr2.h"
  #include "src/ffx-fsr2-api/vk/ffx_fsr2_vk.h"
#endif

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "glm/trigonometric.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/handle.hpp"
#include "entt/entity/registry.hpp"
#include "entt/meta/fwd.hpp"

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
    glm::mat4 worldFromObjectOld;
    VkDeviceAddress vertexBuffer;
    glm::vec3 tint;
  };
} // namespace "Temp"

class VoxelRenderer
{
public:
  explicit VoxelRenderer(PlayerHead* head);
  ~VoxelRenderer();

  void CreateRenderingMaterials(const World& world);

  // TODO: This really needs to be refactored into a dedicated cached resource abstraction.
  Fvog::Texture& GetOrEmplaceCachedTexture(const std::string& name, bool srgb);

  void RegisterParticleArchetype(std::string name, const Game2::Render::ParticleArchetype& archetype);
  void SpawnParticles(std::span<const Game2::Render::Particle> particles);
  void SpawnParticleArchetypes(std::span<const Game2::Render::ParticleArchetypeSpawnInfo> archetypeSpawnInfos);

private:
  bool needsHeightmapInit = false; // TODO: TEMP

  void InitGui();
  void LoadGameSettings();
  void DrawEntityHelper(entt::registry& world, entt::entity entity, const struct Hierarchy* h);

  enum class EditorMode
  {
    Entities,
    Items,
    Blocks,
  };
  void ShowEditor(DeltaTime dt, World& world, EditorMode mode);
  bool ShowSettingsWindow(World& world);
  void OnFramebufferResize(uint32_t newWidth, uint32_t newHeight);
  void OnRender(DeltaTime dt, World& world, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex);
  void RenderGame(DeltaTime dt, World& world, VkCommandBuffer commandBuffer);
  void OnGui(DeltaTime dt, World& world, VkCommandBuffer commandBuffer);

  struct Frame
  {
    // G-buffer
    std::optional<Fvog::Texture> gAlbedo;
    std::optional<Fvog::Texture> gNormal;
    std::optional<Fvog::Texture> gRadiance;
    std::optional<Fvog::Texture> gIlluminance;
    std::optional<Fvog::Texture> gIlluminancePingPong; // Used in denoising.
    std::optional<Fvog::Texture> gSpecial;
    std::optional<Fvog::Texture> gDepth;
    std::optional<Fvog::Texture> gDepthPrev;
    std::optional<Fvog::Texture> gMotion;
    std::optional<Fvog::Texture> gReactiveMask;

    constexpr static Fvog::Format sceneAlbedoFormat      = Fvog::Format::R8G8B8A8_SRGB;
    constexpr static Fvog::Format sceneNormalFormat      = Fvog::Format::R16G16B16A16_SNORM; // TODO: should be oct
    constexpr static Fvog::Format sceneIlluminanceFormat = Fvog::Format::R16G16B16A16_SFLOAT;
    constexpr static Fvog::Format sceneSpecialFormat     = Fvog::Format::R8_UINT;
    constexpr static Fvog::Format sceneDepthFormat       = Fvog::Format::D32_SFLOAT;
    constexpr static Fvog::Format sceneMotionFormat      = Fvog::Format::R16G16_SFLOAT;
    constexpr static Fvog::Format gReactiveMaskFormat    = Fvog::Format::R8_UNORM;

    std::optional<Fvog::Texture> gTransmission;
    std::optional<Fvog::Texture> gAlbedoTranslucent;
    std::optional<Fvog::Texture> gNormalTranslucent;
    std::optional<Fvog::Texture> gDepthTranslucent;

    // Pre-tonemap
    std::optional<Fvog::Texture> sceneColorInternalRes;
    std::optional<Fvog::Texture> sceneColorOutputRes;
    std::optional<Fvog::Texture> sceneColorBloomScratch;
    constexpr static Fvog::Format sceneColorFormat = Fvog::Format::R16G16B16A16_SFLOAT;

    // Post-tonemap. Format allows for HDR display support.
    std::optional<Fvog::Texture> sceneColorTonemapped;
    constexpr static Fvog::Format sceneColorTonemappedFormat = Fvog::Format::R16G16B16A16_SFLOAT;
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
  PipelineManager::ComputePipelineKey translucentVoxelsPipeline;
  PipelineManager::ComputePipelineKey shadeDeferredPipeline;
  PipelineManager::ComputePipelineKey perPixelPathtracerPipeline;
  PipelineManager::ComputePipelineKey tonemapPipeline;

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
  std::optional<Fvog::TypedBuffer<GBuffer_t>> gBufferBuffer;

  // Sky
  std::unique_ptr<Techniques::Sky> sky_ = Techniques::Sky::Create();
  SkyConfig skyParameters = InitSkyConfig();

  Fvog::TypedBuffer<float> exposureBuffer;
  Fvog::NDeviceBuffer<shared::TonemapUniforms> tonemapUniformBuffer;
  shared::TonemapUniforms tonemapUniforms{};
  std::unordered_map<std::string, Fvog::Texture> stringToTexture;
  PlayerHead* head_;
  entt::handle selectedHandle;

  // DDGI
  struct DDGI
  {
    // static constexpr Fvog::Format radianceFormat = Fvog::Format::B10G11R11_UFLOAT;
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

  DDGIDebugView ddgiDebugView_           = DDGIDebugView::None;
  float ddgiDebugProbeSize_              = 0.25f;
  bool ddgiDebugPauseUpdates_            = false;
  bool ddgiDebugFreezeGrid_              = false; // Pauses only grid movement- probes still update.
  int ddgiDebugShowOnlyThisCascade_      = -1;    // <0: show all cascades
  bool ddgiDebugShowCascadeIndexAsColor_ = false;

  Techniques::FroxelFog fog_;
  // Resources needed for froxel fog
  std::optional<Fvog::Texture> inScatteringAndTransmittanceVolume;
  std::optional<Fvog::Texture> fogColorAndDensityVolume;
  int32_t sunSelfShadowSteps = 5;
  float sunSelfShadowDist    = 100;

  Techniques::RayTracedAO ao_;
  bool enableAo_ = true;
  std::optional<Fvog::Texture> whiteTexture_;
  Techniques::RayTracedAO::ComputeParams aoParams_{};

  Techniques::ScreenSpaceGI ssgi_;
  bool enableSsgi_ = false;
  Techniques::ScreenSpaceGIDispatchInfo ssgiParams_{};

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

  float sunElevation  = 0.5f;
  float sunAzimuth    = 0.3f;
  glm::vec3 sunColor  = glm::vec3(1.0f, 0.94f, 0.91f);
  float sunBrightness = 100'000;

  std::optional<Fvog::Texture> globalSurfaceHeightImage;
  std::optional<Fvog::Texture> globalSurfaceFogImage;
  std::optional<Fvog::Texture> globalFogImage;

  // GPU debug drawing
  enum class GPUDebugMesh
  {
    Circle,
    Semicircle,
    Rectangle,
  };
  // std::optional<Fvog::TypedBuffer<Fvog::DrawIndirectCommand>> debugMeshIndirectCommands;
  // std::optional<Fvog::TypedBuffer<,>> debugMeshInstanceData;
  std::optional<Fvog::TypedBuffer<DebugDrawData_t>> debugRenderingInfo;
  std::optional<Fvog::TypedBuffer<DebugAabb>> debugAabbBuffer;
  std::optional<Fvog::TypedBuffer<DebugRect>> debugRectBuffer;
  std::optional<Fvog::TypedBuffer<DebugLine>> debugLineBuffer;
  bool debugClearGpuPrimtives = true;

  Game2::AutoCVar<Game2::cvar_float> enableSunShadowPass =
    {"r.sun.csm.enablePass", "- Controls whether the sun shadow pass is enabled", 1, {}, {}, Game2::CVarFlagBits::CHEAT | Game2::CVarFlagBits::ARCHIVE};
  glm::ivec2 sunShadowResolution   = {512, 512};
  float sunShadowFrustumSideLength = 100;
  float sunShadowFrustumDepth      = 1000;
  int sunShadowNumCascades         = 5;
  Techniques::CascadedShadowMap sunShadowMap_;

  glm::ivec2 skyShadowResolution   = {256, 256};
  float skyShadowFrustumSideLength = 100;
  float skyShadowFrustumDepth      = 1000;
  int skyShadowNumCascades         = 3;
  Techniques::CascadedShadowMap skyShadowMap_;

  uint32_t renderInternalWidth{};
  uint32_t renderInternalHeight{};
  uint32_t renderOutputWidth{};
  uint32_t renderOutputHeight{};

#ifdef FROGRENDER_FSR2_ENABLE
  // FSR 2
  Game2::AutoCVar<Game2::cvar_float> fsr2Enable = {"r.fsr2.enable",
    "- If true, FSR 2 (TAAU) pass will be performed",
    1,
    {},
    {},
    Game2::CVarFlagBits::ARCHIVE,
    [this](std::string_view, Game2::cvar_float) { head_->shouldResizeNextFrame = true; }};
  bool fsr2FirstInit                            = true;
  float fsr2Sharpness                           = 0;
  float fsr2Ratio                               = 1.5f; // FFX_FSR2_QUALITY_MODE_QUALITY
  FfxFsr2Context fsr2Context{};
  std::unique_ptr<char[]> fsr2ScratchMemory;
#else
  Game2::AutoCVar<Game2::cvar_float> fsr2Enable =
    {"r.fsr2.enable", "- If true, FSR 2 (TAAU) pass will be performed (NOTE: FSR 2 is disabled in this build)", 0, 0, 0, Game2::CVarFlagBits::ARCHIVE};
#endif

  Game2::AutoCVar_float cameraNearPlane   = {"r.camera.nearPlane", "- Near plane of the viewer's camera.", 0.1f, 0.01f, {}, Game2::CVarFlagBits::ARCHIVE};
  Game2::AutoCVar_float cameraFovyRadians = {"r.camera.fovy",
    "- Vertical field of view, in radians, of the viewer's camera.",
    glm::radians(65.0f),
    glm::radians(1.0f),
    glm::radians(160.0f),
    Game2::CVarFlagBits::ARCHIVE};

  glm::mat4 clip_from_world_unjittered_old = glm::mat4(1);
  glm::mat4 clip_from_world_old            = glm::mat4(1);
  glm::mat4 clip_from_view_old             = glm::mat4(1);
  glm::mat4 view_from_world_old            = glm::mat4(1);
  double time                              = 0;

  std::unique_ptr<Techniques::RayMarchedClouds> rayMarchedClouds_ = Techniques::RayMarchedClouds::Create();
  Game2::AutoCVar_float cloudsUpscaleRatio                        = {
    "r.clouds.renderScale",
    "- The fraction of the render resolution to render clouds, prior to upscaling.",
    0.25f,
    0.01f,
    1.0f,
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudsNumRayMarchStepsMin = {
    "r.clouds.rayMarch.minSteps",
    "- The minimum number of steps to take in the ray marching pass when rendering clouds.",
    10,
    1,
    {},
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudsNumRayMarchStepsMax = {
    "r.clouds.rayMarch.maxSteps",
    "- The maximum number of steps to take in the ray marching pass when rendering clouds.",
    200,
    1,
    {},
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudsDistForMinRayStepCount = {
    "r.clouds.rayMarch.distForMinSteps",
    "- The ray length required to to take the minimum number of steps when rendering clouds.",
    50,
    1,
    {},
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudsDistForMaxRayStepCount = {
    "r.clouds.rayMarch.distForMaxSteps",
    "- The ray length required to to take the maximum number of steps when rendering clouds.",
    1000,
    1,
    {},
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudCbsmResolution = {
    "r.clouds.cbsm.resolution",
    "- The resolution of Beer shadow map cascades used for cloud shadows.",
    256,
    1,
    2048,
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudCbsmNumCascades = {
    "r.clouds.cbsm.cascades",
    "- The number of cascades for the Beer shadow map.",
    4,
    1,
    8,
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudCbsmRayMarchSteps = {
    "r.clouds.cbsm.rayMarch.steps",
    "- The number of steps to take when rendering the Beer shadow map.",
    50,
    1,
    1000,
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudCbsmFrustumDepth = {
    "r.clouds.cbsm.frustum.depth",
    "- The length of the frustum used to render the Beer shadow map.",
    2000,
    100,
    10000,
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudCbsmFrustumSideLength = {
    "r.clouds.cbsm.frustum.width",
    "- The width of the frustum used to render the Beer shadow map.",
    100,
    10,
    10000,
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudCbsmHistoryWeight = {
    "r.clouds.cbsm.historyWeight",
    "- Weight of history sample when filtering Beer shadow map. 0 means no temporal accumulation.",
    0.8f,
    0,
    1,
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float cloudCbsmJitterScale = {
    "r.clouds.cbsm.jitterScale",
    "- This scale is applied when jittering ray offsets for rendering the Beer shadow map.",
    1,
    0,
    1,
    Game2::CVarFlagBits::ARCHIVE,
  };

  Game2::AutoCVar_float skyEnableAerialPerspective = {
    "r.sky.aerialPerspective",
    "- WIP sky \"fog\" effect that makes distant objects and clouds blend smoothly with the sky.",
    0,
    0,
    1,
    Game2::CVarFlagBits::ARCHIVE,
  };

  bool enableWeatherOverride_ = false;
  WeatherGpuParams_t weather_{
    .cloudBottomAltitude        = 480.0f,
    .cloudBottomFalloffDistance = 20.0f,
    .cloudHeight                = 150.0f,
    .cloudCoverage              = 1.0f,
    .cloudDensity               = 0.2f,
    .cloudFrequency             = 1.0f / 150.0f,
    .windVelocity               = {},
    .cloudHorizontalOffset      = {},
    .cloudTemporalOffset        = {},
    .earthSizeFactor            = 1.0f / 10.0f,
  };

  std::unique_ptr<Techniques::Particles> particles_;
};

struct ImFont;

namespace GuiHelper
{
  bool DrawComponent(World& world, entt::entity entity, entt::meta_any instance, entt::meta_custom custom, bool readonly, int& guiId);
  ImFont* GetStandardFont();
  ImFont* GetMonospaceFont();
}
