#include "VoxelRenderer.h"

#include "volk.h"

#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Assets.h"
#include "Game/Item.h"
#include "Game/Block.h"
#include "MathUtilities.h"
#include "PipelineManager.h"
#include "Fvog/Device.h"
#include "Fvog/Rendering2.h"
#include "Fvog/detail/Common.h"
#include "Core/Assert2.h"
#include "Core/Image.h"
#include "Game/VoxLoader.h"
#include "Game/Globals.h"

#include "shaders/Config.shared.h"
#include "shaders/voxels/PerPixelPathtracer.shared.h"
#include "shaders/voxels/ShadeDeferred.shared.h"
#include "shaders/voxels/Voxels.h.glsl"
#include "shaders/ddgi/DebugProbesCommon.h.glsl"

#ifdef JPH_DEBUG_RENDERER
#include "Game/Physics/DebugRenderer.h"
#endif

#include "Fvog/detail/ApiToEnum2.h"

#include "tiny_obj_loader.h"
#include "tracy/Tracy.hpp"
#include "tracy/TracyVulkan.hpp"
#include "stb_image.h"
#include "spdlog/spdlog.h"

#include <format>
#include <memory>
#include <numeric>
#include <type_traits>

namespace
{
  using index_t = uint32_t;

  struct Vertex
  {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec3 color{};
  };

  struct GpuMesh
  {
    std::optional<Fvog::TypedBuffer<Vertex>> vertexBuffer;
    std::optional<Fvog::TypedBuffer<index_t>> indexBuffer;
    std::vector<Vertex> vertices;
    std::vector<index_t> indices;
  };

  GpuMesh LoadObjFile(const std::filesystem::path& path)
  {
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path.string()))
    {
      //std::cout << "TinyObjReader error: " << reader.Error() << '\n';
      throw std::runtime_error("Failed to parse obj");
    }

    if (!reader.Warning().empty())
    {
      //std::cout << "TinyObjReader warning: " << reader.Warning() << '\n';
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    // auto& materials = reader.GetMaterials();

    auto mesh = GpuMesh{};

    // Loop over shapes
    for (const auto& shape : shapes)
    {
      // Loop over faces(polygon)
      size_t index_offset = 0;
      for (const auto& fv : shape.mesh.num_face_vertices)
      {
        // Loop over vertices in the face.
        for (size_t v = 0; v < fv; v++)
        {
          auto vertex = Vertex{};

          // access to vertex
          tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
          tinyobj::real_t vx   = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
          tinyobj::real_t vy   = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
          tinyobj::real_t vz   = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

          vertex.position = {vx, vy, vz};

          // Check if `normal_index` is zero or positive. negative = no normal data
          if (idx.normal_index >= 0)
          {
            tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
            tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
            tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];
            vertex.normal      = {nx, ny, nz};
          }

          //// Check if `texcoord_index` is zero or positive. negative = no texcoord data
          // if (idx.texcoord_index >= 0)
          //{
          //   tinyobj::real_t tx = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
          //   tinyobj::real_t ty = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
          //   vertex.texcoord = { tx, ty };
          // }

          // Optional: vertex colors
          tinyobj::real_t red   = attrib.colors[3 * size_t(idx.vertex_index) + 0];
          tinyobj::real_t green = attrib.colors[3 * size_t(idx.vertex_index) + 1];
          tinyobj::real_t blue  = attrib.colors[3 * size_t(idx.vertex_index) + 2];
          vertex.color          = {red, green, blue};

          mesh.vertices.push_back(vertex);
        }
        index_offset += fv;
      }
    }

    mesh.indices = std::vector<index_t>(mesh.vertices.size());
    std::iota(mesh.indices.begin(), mesh.indices.end(), 0);
    
    mesh.indexBuffer.emplace(Fvog::TypedBufferCreateInfo{.count = (uint32_t)mesh.indices.size(), .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE});
    mesh.vertexBuffer.emplace(Fvog::TypedBufferCreateInfo{.count = (uint32_t)mesh.vertices.size(), .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE});
    memcpy(mesh.indexBuffer->GetMappedMemory(), mesh.indices.data(), mesh.indices.size() * sizeof(index_t));
    memcpy(mesh.vertexBuffer->GetMappedMemory(), mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));

    return mesh;
  }

  [[nodiscard]] Fvog::Texture LoadImageFile(const std::filesystem::path& path, bool srgb)
  {
    stbi_set_flip_vertically_on_load(true);
    int x            = 0;
    int y            = 0;
    const auto pixels = stbi_load(path.string().c_str(), &x, &y, nullptr, 4);
    ASSERT(pixels);
    const auto format = srgb ? Fvog::Format::R8G8B8A8_SRGB : Fvog::Format::R8G8B8A8_UNORM;
    auto texture = Fvog::CreateTexture2D({static_cast<uint32_t>(x), static_cast<uint32_t>(y)}, format, Fvog::TextureUsage::READ_ONLY, path.string());
    texture.UpdateImageSLOW({
      .extent = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
      .data   = pixels,
    });
    stbi_image_free(pixels);

    return texture;
  }

  [[nodiscard]] Fvog::Texture LoadTonyMcMapfaceTexture()
  {
    stbi_set_flip_vertically_on_load(false);
    int x{};
    int y{};
    auto* pixels = stbi_load((GetTextureDirectory() / "tony_mcmapface/lut.png").string().c_str(), &x, &y, nullptr, 4);
    if (!pixels)
    {
      throw std::runtime_error("Texture not found");
    }

    constexpr uint32_t dim = 48;
    if (x != dim || y != dim * dim) // Image should be a column of 48x48 images
    {
      throw std::runtime_error("Texture had invalid dimensions");
    }
    auto texture = Fvog::Texture(
      {
        .viewType = VK_IMAGE_VIEW_TYPE_3D,
        .format   = Fvog::Format::R8G8B8A8_UNORM,
        .extent   = {dim, dim, dim},
        .usage    = Fvog::TextureUsage::READ_ONLY,
      },
      "Tony McMapface LUT");

    for (uint32_t i = 0; i < dim; i++)
    {
      texture.UpdateImageSLOW({
        .level       = 0,
        .offset      = {0, 0, i},
        .extent      = {dim, dim, 1},
        .data        = pixels + (i * 4 * dim * dim),
        .rowLength   = 0,
        .imageHeight = 0,
      });
    }

    stbi_image_free(pixels);
    return texture;
  }

  FVOG_DECLARE_ARGUMENTS(DebugLinesPushConstants)
  {
    FVOG_UINT32 vertexBufferIndex;
    FVOG_UINT32 globalUniformsIndex;
    FVOG_UINT32 useGpuVertexBuffer;
  };

  FVOG_DECLARE_ARGUMENTS(BillboardPushConstants)
  {
    FVOG_UINT32 billboardsIndex;
    FVOG_UINT32 globalUniformsIndex;
    FVOG_VEC3 cameraRight;
    FVOG_VEC3 cameraUp;
    shared::Sampler texSampler;
  };

  std::unordered_map<std::string, GpuMesh> g_meshes;

  glm::vec2 GetJitterOffset([[maybe_unused]] uint32_t frameIndex,
    [[maybe_unused]] uint32_t renderInternalWidth,
    [[maybe_unused]] uint32_t renderInternalHeight,
    [[maybe_unused]] uint32_t renderOutputWidth)
  {
#ifdef FROGRENDER_FSR2_ENABLE
    float jitterX{};
    float jitterY{};
    ffxFsr2GetJitterOffset(&jitterX, &jitterY, frameIndex, ffxFsr2GetJitterPhaseCount(renderInternalWidth, renderOutputWidth));
    return {2.0f * jitterX / static_cast<float>(renderInternalWidth), 2.0f * jitterY / static_cast<float>(renderInternalHeight)};
#else
    return {0, 0};
#endif
  }
} // namespace

VoxelRenderer::VoxelRenderer(PlayerHead* head) : head_(head)
{
  ZoneScoped;
  
  g_meshes.emplace("frog", LoadObjFile(GetAssetDirectory() / "models/frog.obj"));
  g_meshes.emplace("ar15", LoadObjFile(GetAssetDirectory() / "models/frog.obj"));
  //g_meshes.emplace("tracer", LoadObjFile(GetAssetDirectory() / "models/tracer.obj"));
  g_meshes.emplace("cube", LoadObjFile(GetAssetDirectory() / "models/cube.obj"));
  g_meshes.emplace("spear", LoadObjFile(GetAssetDirectory() / "models/spear.obj"));
  g_meshes.emplace("pickaxe", LoadObjFile(GetAssetDirectory() / "models/pickaxe.obj"));
  g_meshes.emplace("axe", LoadObjFile(GetAssetDirectory() / "models/axe.obj"));
  g_meshes.emplace("torch", LoadObjFile(GetAssetDirectory() / "models/torch.obj"));
  g_meshes.emplace("mushroom", LoadObjFile(GetAssetDirectory() / "models/mushroom.obj"));
  g_meshes.emplace("player", LoadObjFile(GetAssetDirectory() / "models/player.obj"));
  //g_meshes.emplace("ant", LoadObjFile(GetAssetDirectory() / "models/ant.obj"));
  g_meshes.emplace("sword", LoadObjFile(GetAssetDirectory() / "models/sword.obj"));
  g_meshes.emplace("bow", LoadObjFile(GetAssetDirectory() / "models/bow.obj"));
  g_meshes.emplace("arrow", LoadObjFile(GetAssetDirectory() / "models/arrow.obj"));

  head_->renderCallback_ = [this](float dt, World& world, VkCommandBuffer cmd, uint32_t swapchainImageIndex) { OnRender(dt, world, cmd, swapchainImageIndex); };
  head_->framebufferResizeCallback_ = [this](uint32_t newWidth, uint32_t newHeight) { OnFramebufferResize(newWidth, newHeight); };
  head_->guiCallback_ = [this](DeltaTime dt, World& world, VkCommandBuffer cmd) { OnGui(dt, world, cmd); };

  const auto gBufferFormats = std::vector{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneMotionFormat, Frame::gReactiveMaskFormat};

  voxelsPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Render voxels",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "Voxels/SimpleRayTracer.frag.glsl",
      },
    .state =
      {
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
        .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = FVOG_COMPARE_OP_NEARER_OR_EQUAL},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = gBufferFormats,
            .depthAttachmentFormat = Frame::sceneDepthFormat,
          },
      },
  });
  
  meshPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Render meshes",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "Voxels/Forward.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "Voxels/Forward.frag.glsl",
      },
    .state =
      {
        .rasterizationState = {.cullMode = VK_CULL_MODE_BACK_BIT},
        .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = FVOG_COMPARE_OP_NEARER},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = gBufferFormats,
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  debugTexturePipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug Texture",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "Texture.frag.glsl",
      },
    .state =
      {
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {Fvog::detail::VkToFormat(head_->swapchainFormat_.format)},
          },
      },
  });

  debugLinesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug Lines",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "debug/Debug.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "debug/VertexColor.frag.glsl",
      },
    .state =
      {
        .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST},
        .rasterizationState =
          {
            .cullMode                = VK_CULL_MODE_NONE,
            .depthBiasEnable         = true,
            .depthBiasConstantFactor = 1,
            .lineWidth               = 2,
          },
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = true,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = gBufferFormats,
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  billboardsPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Billboards",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "Billboard.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "Billboard.frag.glsl",
      },
    .state =
      {
        .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
        .rasterizationState =
          {
            .cullMode = VK_CULL_MODE_NONE,
          },
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = true,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = gBufferFormats,
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  billboardSpritesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Billboard Sprites",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "BillboardSprite.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "BillboardSprite.frag.glsl",
      },
    .state =
      {
        .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
        .rasterizationState =
          {
            .cullMode = VK_CULL_MODE_NONE,
          },
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = true,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = gBufferFormats,
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  spelunkerEffectPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Spelunker Effect",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "voxels/SpelunkerPotionEffect.comp.glsl",
      },
  });

  translucentVoxelsPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Translucent Voxels",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "voxels/TranslucentVoxels.comp.glsl",
      },
  });

  shadeDeferredPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Shade Deferred",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "voxels/ShadeDeferred.comp.glsl",
      },
  });

  perPixelPathtracerPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Per-Pixel Pathtracer",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "voxels/PerPixelPathtracer.comp.glsl",
      },
    .useMinSubgroupSize = true,
  });

  tonemapPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Tonemap and Dither",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "post/TonemapAndDither.comp.glsl",
      },
  });

  noiseTexture = LoadImageFile(GetTextureDirectory() / "bluenoise256.png", false);
  tonyMcMapfaceLut = LoadTonyMcMapfaceTexture();
  backgroundTexture = LoadImageFile(GetTextureDirectory() / "background.png", false);

  tonemapUniforms.tonemapper                = 1;
  tonemapUniforms.shadingInternalColorSpace = COLOR_SPACE_sRGB_LINEAR;
  tonemapUniforms.enableDithering           = 1;

  InitGui();

  InitDDGI({
    .probeRadianceResolution     = {16, 16},
    .probeIrradianceResolution   = {10, 10},
    .probeDepthMomentsResolution = {10, 10},
    .gridResolution              = {10, 10, 10}, // TODO: FIXME: certain sizes (such as 12^3) have unexpected black probes.
    .baseGridScale               = 2,
  });

  whiteTexture_ = Fvog::CreateTexture2D({1, 1}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::READ_ONLY, "1x1 White Texture");
  constexpr auto whiteUnorm8 = glm::u8vec4(255, 255, 255, 255);
  whiteTexture_->UpdateImageSLOW({.extent = {1, 1, 1}, .data = &whiteUnorm8});

  // Initialize froxel fog resources
  inScatteringAndTransmittanceVolume = Fvog::Texture(
    {
      .viewType = VK_IMAGE_VIEW_TYPE_3D,
      .format   = Fvog::Format::R16G16B16A16_SFLOAT,
      .extent   = {160, 90, 256},
      .usage    = Fvog::TextureUsage::GENERAL,
    },
    "In-scattering and transmittance volume");
  fogColorAndDensityVolume = Fvog::Texture(
    {
      .viewType = VK_IMAGE_VIEW_TYPE_3D,
      .format   = Fvog::Format::R16G16B16A16_SFLOAT,
      .extent   = {160, 90, 256},
      .usage    = Fvog::TextureUsage::GENERAL,
    },
    "Fog color and density volume");

  gBufferBuffer.emplace();

  Fvog::GetDevice().ImmediateSubmit([this](VkCommandBuffer cmd) { exposureBuffer.UpdateDataExpensive(cmd, 0.0f); });

  debugRenderingInfo.emplace(Fvog::TypedBufferCreateInfo{}, "Debug Rendering Info");
  debugAabbBuffer.emplace(Fvog::TypedBufferCreateInfo{.count = 100'000}, "Debug AABB Buffer");
  debugRectBuffer.emplace(Fvog::TypedBufferCreateInfo{.count = 100'000}, "Debug Rect Buffer");
  debugLineBuffer.emplace(Fvog::TypedBufferCreateInfo{.count = 100'000}, "Debug Line Buffer");

  OnFramebufferResize(head_->windowFramebufferWidth, head_->windowFramebufferHeight);
}

VoxelRenderer::~VoxelRenderer()
{
  ZoneScoped;
  g_meshes.clear();
  vkDeviceWaitIdle(Fvog::GetDevice().device_);

  Fvog::GetDevice().FreeUnusedResources();

#if FROGRENDER_FSR2_ENABLE
  if (!fsr2FirstInit)
  {
    ffxFsr2ContextDestroy(&fsr2Context);
  }
#endif
}

void VoxelRenderer::CreateRenderingMaterials(const World& world)
{
  const auto& blocks = *world.globals->blockRegistry;
  const auto& bReg   = blocks.GetRegistry();

  auto voxelMaterials = std::vector<GpuVoxelMaterial>();
  auto voxelMaterialsSpelunker = std::vector<GpuVoxelMaterial>();

  auto orderedBlocks = std::vector<entt::entity>();
  orderedBlocks.reserve(bReg.view<entt::entity>().size());
  for (auto block : bReg.view<entt::entity>())
  {
    orderedBlocks.push_back(block);
  }
  std::ranges::sort(orderedBlocks);

  // Translate block definitions to GPU materials, then upload.
  for (auto block : orderedBlocks)
  {
    auto gpuMat = GpuVoxelMaterial{};
    gpuMat.density = -1; // Negative density = solid voxel

    if (const auto* p = bReg.try_get<const Block::Component::RenderAsTexturedCube>(block))
    {
      for (auto& face : gpuMat.faces)
      {
        face.baseColorFactor = p->material.baseColorFactor;
        face.emissionFactor  = p->material.emissionFactor;
        if (p->material.randomizeTexcoordRotation)
        {
          face.materialFlags |= FACE_RANDOMIZE_TEXCOORDS_ROTATION;
        }
        if (p->material.baseColorTexture)
        {
          face.materialFlags |= FACE_HAS_BASE_COLOR_TEXTURE;
          face.baseColorTexture = GetOrEmplaceCachedTexture(*p->material.baseColorTexture, true).ImageView().GetTexture2D();
        }
        if (p->material.emissionTexture)
        {
          face.materialFlags |= FACE_HAS_EMISSION_TEXTURE;
          face.emissionTexture = GetOrEmplaceCachedTexture(*p->material.emissionTexture, true).ImageView().GetTexture2D();
        }
      }
    }

    if (const auto* p = bReg.try_get<const Block::Component::RenderAsTexturedCube2>(block))
    {
      for (int i = 0; i < 6; i++)
      {
        auto& face = gpuMat.faces[i];

        face.texcoordsQuarterTurns = p->faces[i].texcoordsQuarterTurns;
        face.baseColorFactor = p->faces[i].baseColorFactor;
        face.emissionFactor  = p->faces[i].emissionFactor;
        if (p->faces[i].randomizeTexcoordRotation)
        {
          face.materialFlags |= FACE_RANDOMIZE_TEXCOORDS_ROTATION;
        }
        if (p->faces[i].baseColorTexture)
        {
          face.materialFlags |= FACE_HAS_BASE_COLOR_TEXTURE;
          face.baseColorTexture = GetOrEmplaceCachedTexture(*p->faces[i].baseColorTexture, true).ImageView().GetTexture2D();
        }
        if (p->faces[i].emissionTexture)
        {
          face.materialFlags |= FACE_HAS_EMISSION_TEXTURE;
          face.emissionTexture = GetOrEmplaceCachedTexture(*p->faces[i].emissionTexture, true).ImageView().GetTexture2D();
        }
      }
    }

    if (!Block::IsVisible(world, BlockId(block)))
    {
      gpuMat.voxelFlags |= VOXEL_IS_INVISIBLE;
    }

    if (auto subGrids = Block::GetSubGrids(world, BlockId(block)); subGrids.size() == 1)
    {
      const auto* subGrid = subGrids.front();
      // Check if all subvoxels are the same
      bool allSame   = true;
      auto lastSubVoxel = subGrid->grid[0];
      for (int i = 1; i < subGrid->dimensions.x * subGrid->dimensions.y * subGrid->dimensions.z; i++)
      {
        if (subGrid->grid[i] != lastSubVoxel)
        {
          allSame = false;
          break;
        }
      }

      if (allSame)
      {
        spdlog::debug("Subgrid for block {} is homogeneous, converting to full voxel.", Block::GetName(world, BlockId(block)));
        if (lastSubVoxel == Voxel::SubVoxel::Air)
        {
          gpuMat.voxelFlags |= VOXEL_IS_INVISIBLE;
        }
        else
        {
          const auto subGridMat = subGrid->materials[size_t(lastSubVoxel) - 1];
          gpuMat.density = subGridMat.density;
          for (auto& face : gpuMat.faces)
          {
            face.baseColorFactor = subGridMat.colorSrgb;
            face.emissionFactor  = subGridMat.emissionSrgb;
          }
        }
      }
      else
      {
        gpuMat.voxelFlags |= VOXEL_IS_SUBGRID;
        gpuMat.subGridOrAnimatedSubGridInfoIndex = subGrid->myIndexINTERNAL;
      }
    }
    else if (subGrids.size() > 1)
    {
      ASSERT(subGrids.size() <= 8);
      gpuMat.voxelFlags |= VOXEL_IS_SUBGRID;
      gpuMat.voxelFlags |= VOXEL_IS_ANIMATED_SUBGRID;
      gpuMat.subGridOrAnimatedSubGridInfoIndex = world.globals->grid->Materials()[(int)block].animatedSubGridInfoIndex.value();
    }

    voxelMaterials.emplace_back(gpuMat);

    if (!bReg.all_of<Block::Component::Valuable>(block))
    {
      gpuMat.voxelFlags |= VOXEL_IS_INVISIBLE;
    }
    voxelMaterialsSpelunker.emplace_back(gpuMat);
  }

  voxelMaterialBuffer = Fvog::Buffer({.size = voxelMaterials.size() * sizeof(GpuVoxelMaterial), .flag = Fvog::BufferFlagThingy::NONE}, "Voxel Material Buffer");
  voxelMaterialBufferSpelunker = Fvog::Buffer({.size = voxelMaterialsSpelunker.size() * sizeof(GpuVoxelMaterial), .flag = Fvog::BufferFlagThingy::NONE}, "Voxel Material Buffer Ex");
  Fvog::GetDevice().ImmediateSubmit([&](VkCommandBuffer cmd)
  {
    voxelMaterialBuffer->UpdateDataExpensive(cmd, std::span(voxelMaterials));
    voxelMaterialBufferSpelunker->UpdateDataExpensive(cmd, std::span(voxelMaterialsSpelunker));
  });

  needsHeightmapInit = true;
}

void VoxelRenderer::OnFramebufferResize(uint32_t newWidth, uint32_t newHeight)
{
  ZoneScoped;

  #ifdef FROGRENDER_FSR2_ENABLE
  // create FSR 2 context
  if (fsr2Enable.Get() != 0)
  {
    if (!fsr2FirstInit)
    {
      // TODO: get rid of this stinky
      vkDeviceWaitIdle(Fvog::GetDevice().device_);
      ffxFsr2ContextDestroy(&fsr2Context);
    }

    fsr2FirstInit        = false;
    renderInternalWidth  = static_cast<uint32_t>(newWidth / fsr2Ratio);
    renderInternalHeight = static_cast<uint32_t>(newHeight / fsr2Ratio);
    FfxFsr2ContextDescription contextDesc{
      .flags = FFX_FSR2_ENABLE_DEBUG_CHECKING | FFX_FSR2_ENABLE_AUTO_EXPOSURE | FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR2_ENABLE_DEPTH_INFINITE |
               FFX_FSR2_ENABLE_DEPTH_INVERTED,
      .maxRenderSize = {renderInternalWidth, renderInternalHeight},
      .displaySize   = {newWidth, newHeight},
      .device        = ffxGetDeviceVK(Fvog::GetDevice().device_),
      .fpMessage     = [](FfxFsr2MsgType type, const wchar_t* message)
      {
        char buffer[256]{};
        if (wcstombs_s(nullptr, buffer, sizeof(buffer), message, sizeof(buffer)) == 0)
        {
          spdlog::log(type == FFX_FSR2_MESSAGE_TYPE_WARNING ? spdlog::level::warn : spdlog::level::err, "[FSR2] {}", buffer);
        }
      },
    };

    auto scratchMemorySize = ffxFsr2GetScratchMemorySizeVK(Fvog::GetDevice().physicalDevice_, vkEnumerateDeviceExtensionProperties);
    fsr2ScratchMemory      = std::make_unique<char[]>(scratchMemorySize);
    ffxFsr2GetInterfaceVK(&contextDesc.callbacks,
      fsr2ScratchMemory.get(),
      scratchMemorySize,
      Fvog::GetDevice().physicalDevice_,
      vkGetDeviceProcAddr,
      vkGetPhysicalDeviceMemoryProperties,
      vkGetPhysicalDeviceProperties2,
      vkGetPhysicalDeviceFeatures2,
      vkEnumerateDeviceExtensionProperties,
      vkGetPhysicalDeviceProperties);
    ffxFsr2ContextCreate(&fsr2Context, &contextDesc);
  }
  else
#endif
  {
    renderInternalWidth  = newWidth;
    renderInternalHeight = newHeight;
  }

  renderOutputWidth  = newWidth;
  renderOutputHeight = newHeight;

  const auto internalExtent = VkExtent2D{renderInternalWidth, renderInternalHeight};
  const auto outputExtent   = VkExtent2D{newWidth, newHeight};

  frame.gAlbedo               = Fvog::CreateTexture2D(internalExtent, Frame::sceneAlbedoFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene albedo");
  frame.gNormal               = Fvog::CreateTexture2D(internalExtent, Frame::sceneNormalFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene normal");
  frame.gRadiance             = Fvog::CreateTexture2D(internalExtent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene radiance");
  frame.gIlluminance          = Fvog::CreateTexture2D(internalExtent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene illuminance");
  frame.gIlluminancePingPong  = Fvog::CreateTexture2D(internalExtent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene illuminance 2");
  frame.gDepth                = Fvog::CreateTexture2D(internalExtent, Frame::sceneDepthFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene depth");
  frame.gDepthPrev            = Fvog::CreateTexture2D(internalExtent, Frame::sceneDepthFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene depth 2");
  frame.sceneColorInternalRes = Fvog::CreateTexture2D(internalExtent, Frame::sceneColorFormat, Fvog::TextureUsage::GENERAL, "Scene color (render res)");
  frame.gSpecial              = Fvog::CreateTexture2D(internalExtent, Frame::sceneSpecialFormat, Fvog::TextureUsage::GENERAL, "Scene special");
  frame.gMotion               = Fvog::CreateTexture2D(internalExtent, Frame::sceneMotionFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene motion");
  frame.gReactiveMask         = Fvog::CreateTexture2D(internalExtent, Frame::gReactiveMaskFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene reactive mask");

  frame.gTransmission      = Fvog::CreateTexture2D(internalExtent, Fvog::Format::B10G11R11_UFLOAT, Fvog::TextureUsage::GENERAL, "Scene transmission");
  frame.gAlbedoTranslucent = Fvog::CreateTexture2D(internalExtent, Fvog::Format::B10G11R11_UFLOAT, Fvog::TextureUsage::GENERAL, "Scene albedo (translucent)");
  frame.gNormalTranslucent = Fvog::CreateTexture2D(internalExtent, Frame::sceneNormalFormat, Fvog::TextureUsage::GENERAL, "Scene normal (translucent)");
  frame.gDepthTranslucent  = Fvog::CreateTexture2D(internalExtent, Fvog::Format::R32_SFLOAT, Fvog::TextureUsage::GENERAL, "Scene depth (translucent)");

  frame.sceneColorOutputRes = Fvog::CreateTexture2D(outputExtent, Frame::sceneColorFormat, Fvog::TextureUsage::GENERAL, "Scene color (output res)");
  frame.sceneColorBloomScratch = Fvog::CreateTexture2DMip({outputExtent.width / 2, outputExtent.height / 2}, Frame::sceneColorFormat, 8, Fvog::TextureUsage::GENERAL, "Scene color (bloom scratch buffer)");

  frame.sceneColorTonemapped = Fvog::CreateTexture2D(outputExtent, Frame::sceneColorTonemappedFormat, Fvog::TextureUsage::GENERAL, "Scene color tonemapped");
}

void VoxelRenderer::OnRender([[maybe_unused]] double dt, World& world, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
  ZoneScoped;
  TracyVkZone(head_->tracyVkContext_, commandBuffer, "OnRender");

  if (head_->shouldResizeNextFrame)
  {
    OnFramebufferResize(head_->windowFramebufferWidth, head_->windowFramebufferHeight);
    head_->shouldResizeNextFrame = false;
  }

  auto ctx = Fvog::Context(commandBuffer);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);

  if (Fvog::GetDevice().supportsRayTracing)
  {
    vkCmdBindDescriptorSets(commandBuffer,
      VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
      Fvog::GetDevice().defaultPipelineLayout,
      0,
      1,
      &Fvog::GetDevice().descriptorSet_,
      0,
      nullptr);
  }

  bool sceneWasRendered = false;
  if (auto gameState = world.globals->game->gameState;
    gameState == GameState::GAME || gameState == GameState::PAUSED || gameState == GameState::PAUSED_SETTINGS)
  {
    ctx.Barrier();
    RenderGame(dt, world, commandBuffer);
    ctx.Barrier();
    sceneWasRendered = true;
  }

  ctx.ImageBarrier(head_->swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  const auto nearestSampler = Fvog::Sampler({
    .magFilter     = VK_FILTER_NEAREST,
    .minFilter     = VK_FILTER_NEAREST,
    .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .maxAnisotropy = 0,
  });

  //const auto renderArea = VkRect2D{.offset = {}, .extent = {renderOutputWidth, renderOutputHeight}};
  const auto renderArea = VkRect2D{.offset = {}, .extent = {head_->windowFramebufferWidth, head_->windowFramebufferHeight}};
  vkCmdBeginRendering(commandBuffer,
    Fvog::detail::Address(VkRenderingInfo{
      .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea           = renderArea,
      .layerCount           = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments    = Fvog::detail::Address(VkRenderingAttachmentInfo{
           .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
           .imageView   = head_->swapchainImageViews_[swapchainImageIndex],
           .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
           .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
           .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
           .clearValue  = {.color = VkClearColorValue{.float32 = {0, 0, 1, 1}}},
      }),
    }));

  //vkCmdSetViewport(commandBuffer, 0, 1, Fvog::detail::Address(VkViewport{0, 0, (float)renderOutputWidth, (float)renderOutputHeight, 0, 1}));
  vkCmdSetViewport(commandBuffer, 0, 1, Fvog::detail::Address(VkViewport{0, 0, (float)head_->windowFramebufferWidth, (float)head_->windowFramebufferHeight, 0, 1}));
  vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
  ctx.BindGraphicsPipeline(debugTexturePipeline.GetPipeline());
  auto pushConstants = Temp::DebugTextureArguments{
    .textureIndex = sceneWasRendered ? frame.sceneColorTonemapped->ImageView().GetSampledResourceHandle().index
                                     : backgroundTexture.value().ImageView().GetSampledResourceHandle().index,
    .samplerIndex = nearestSampler.GetResourceHandle().index,
  };

  ctx.SetPushConstants(pushConstants);
  ctx.Draw(3, 1, 0, 0);

  vkCmdEndRendering(commandBuffer);

  ctx.ImageBarrier(head_->swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void VoxelRenderer::RenderGame([[maybe_unused]] double dt, World& world, VkCommandBuffer commandBuffer)
{
  auto ctx = Fvog::Context(commandBuffer);

  if (world.globals->grid->numTopLevelBricks_ > 0)
  {
    ZoneScopedN("Flush buffer and image writes");
    auto& grid = *world.globals->grid;
    grid.Buffer().FlushWritesToGPU(commandBuffer);

    if (needsHeightmapInit)
    {
      const auto extent        = Fvog::Extent3D{(uint32_t)grid.Dimensions().x, (uint32_t)grid.Dimensions().z};
      globalSurfaceHeightImage = Fvog::CreateTexture2D({extent.width, extent.height}, Fvog::Format::R32_SFLOAT, Fvog::TextureUsage::READ_ONLY, "Global surface height");
      globalSurfaceFogImage = Fvog::CreateTexture2D({extent.width, extent.height}, Fvog::Format::R32_SFLOAT, Fvog::TextureUsage::READ_ONLY, "Global surface fog");

      globalSurfaceHeightImage->UpdateImage(commandBuffer, {.extent = extent, .data = world.globals->globalSurfaceHeight->Data()});
      globalSurfaceFogImage->UpdateImage(commandBuffer, {.extent = extent, .data = world.globals->globalSurfaceFog->Data()});
    }

    if (needsHeightmapInit || world.globals->globalFogNeedsUpdate)
    {
      const auto size3   = world.globals->globalFog->Size();
      const auto extent3 = Fvog::Extent3D{(uint32_t)size3.x, (uint32_t)size3.y, (uint32_t)size3.z};
      globalFogImage = Fvog::Texture(
        Fvog::TextureCreateInfo{
          .viewType = VK_IMAGE_VIEW_TYPE_3D,
          .format   = Fvog::Format::R32_SFLOAT,
          .extent   = extent3,
          .usage    = Fvog::TextureUsage::READ_ONLY,
        },
        "Global fog");
      globalFogImage->UpdateImage(commandBuffer, {.extent = extent3, .data = world.globals->globalFog->Data()});
    }

    needsHeightmapInit = false;
    world.globals->globalFogNeedsUpdate = false;
  }

  if (debugClearGpuPrimtives)
  {
    ctx.TeenyBufferUpdate(debugRenderingInfo.value(),
      DebugDrawData_t{
        .aabbDrawCommand =
          DrawIndirectCommand{
            .vertexCount   = 14,
            .instanceCount = 0,
            .firstVertex   = 0,
            .firstInstance = 0,
          },
        .rectDrawCommand =
          DrawIndirectCommand{
            .vertexCount   = 4,
            .instanceCount = 0,
            .firstVertex   = 0,
            .firstInstance = 0,
          },
        .lineDrawCommand =
          DrawIndirectCommand{
            .vertexCount   = 2,
            .instanceCount = 0,
            .firstVertex   = 0,
            .firstInstance = 0,
          },
        .maxAabbCount = debugAabbBuffer->Size(),
        .maxRectCount = debugRectBuffer->Size(),
        .maxLineCount = debugLineBuffer->Size(),
        .aabbs        = debugAabbBuffer->GetDeviceAddress(),
        .rects        = debugRectBuffer->GetDeviceAddress(),
        .lines        = debugLineBuffer->GetDeviceAddress(),
      });
  }

  ctx.TeenyBufferUpdate(gBufferBuffer.value(),
    GBuffer_t{
      .gAlbedo              = frame.gAlbedo->ImageView().GetTexture2D(),
      .gDepth               = frame.gDepth->ImageView().GetTexture2D(),
      .gNormal              = frame.gNormal->ImageView().GetTexture2D(),
      .gRadiance            = frame.gRadiance->ImageView().GetTexture2D(),
      .gIndirectIlluminance = frame.gIlluminance->ImageView().GetTexture2D(),
      .gSpecial             = frame.gSpecial->ImageView().GetUImage2D(),
      .gTransmission        = frame.gTransmission->ImageView().GetImage2D(),
      .gAlbedoTranslucent   = frame.gAlbedoTranslucent->ImageView().GetImage2D(),
      .gNormalTranslucent   = frame.gNormalTranslucent->ImageView().GetImage2D(),
      .gDepthTranslucent    = frame.gDepthTranslucent->ImageView().GetImage2D(),
    });

  tonemapUniformBuffer.UpdateData(commandBuffer, tonemapUniforms);

  const auto nearestSampler = Fvog::Sampler({
    .magFilter     = VK_FILTER_NEAREST,
    .minFilter     = VK_FILTER_NEAREST,
    .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .maxAnisotropy = 0,
  });

  const auto linearClampSampler = Fvog::Sampler({
    .magFilter    = VK_FILTER_LINEAR,
    .minFilter    = VK_FILTER_LINEAR,
    .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
  });

  auto voxelSampler = Fvog::Sampler(
    {
      .magFilter     = VK_FILTER_NEAREST,
      .minFilter     = VK_FILTER_NEAREST,
      .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .maxAnisotropy = 16,
    },
    "Voxel Sampler");

  ctx.Barrier();

  auto player   = entt::entity(entt::null);
  auto viewMat  = glm::mat4(1);
  auto position = glm::vec3();
  for (auto&& [entity, inputLook, transform] : world.GetRegistry().view<const InputLookState, const GlobalTransform, const LocalPlayer>().each())
  {
    player = entity;
    position = transform.position;
    auto rotationQuat = transform.rotation;
    if (const auto* renderTransform = world.GetRegistry().try_get<const RenderTransform>(entity))
    {
      // Because the player has its own variable delta pitch and yaw updates, we only care about smoothing positions here
      position = renderTransform->transform.position;
    }
    auto rotation = glm::mat4_cast(glm::inverse(rotationQuat));
    viewMat       = glm::translate(rotation, -position);
  }

  const auto view_from_world = viewMat;
  // const auto clip_from_view = Math::InfReverseZPerspectiveRH(cameraFovyRadians, aspectRatio, cameraNearPlane);
  const auto aspectRatio               = (float)head_->windowFramebufferWidth / head_->windowFramebufferHeight;
  const auto frameNumber               = (uint32_t)Fvog::GetDevice().frameNumber;
  const auto jitterOffset              = fsr2Enable.Get() != 0 ? GetJitterOffset(frameNumber, renderInternalWidth, renderInternalHeight, renderOutputWidth) : glm::vec2{};
  const auto jitterMatrix              = glm::translate(glm::mat4(1), glm::vec3(jitterOffset, 0));
  const auto clip_from_view_unjittered = Math::InfReverseZPerspectiveRH((float)cameraFovyRadians.Get(), aspectRatio, (float)cameraNearPlane.Get());
  const auto clip_from_view            = jitterMatrix * clip_from_view_unjittered;
  const auto clip_from_world_unjittered = clip_from_view_unjittered * view_from_world;
  const auto clip_from_world           = clip_from_view * view_from_world;

  ctx.ImageBarrierDiscard(frame.gAlbedo.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.gNormal.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.gIlluminance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.gRadiance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.gDepth.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.gReactiveMask.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(*frame.gAlbedoTranslucent, VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(*frame.gNormalTranslucent, VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(*frame.gDepthTranslucent, VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(*frame.gTransmission, VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(*frame.gMotion, VK_IMAGE_LAYOUT_GENERAL);

  const auto& sunInfo = world.globals->game->sunInfo;
  sunElevation        = sunInfo.timeOfDay * glm::pi<float>() - glm::pi<float>();
  sunAzimuth          = sunInfo.azimuth;

  skyParameters.sunDir = Math::SphericalToCartesian(sunElevation, sunAzimuth);
  skyParameters.sunColor = sunColor;
  skyParameters.sunBrightness = sunBrightness; // Intended to be used with solid_angle_mapping_PDF(radians(0.5))

  sky_->EnsureResources(commandBuffer,
    {
      .transmittanceLutExtent     = {256, 64},
      .multiscatteringLutExtent   = {32, 32},
      .skyViewLutExtent           = {256, 192},
      .aerialPerspectiveLutExtent = {190, 90, 128},
    });

  auto weatherGpuParams = Fvog::GetDevice().AllocTransient<WeatherGpuParams_t>();
  *weatherGpuParams = weather_;

  perFrameUniforms.UpdateData(commandBuffer,
    GlobalUniforms{
      .viewProj               = clip_from_world,
      .oldViewProj            = frameNumber == 1 ? clip_from_world : clip_from_world_old,
      .oldViewProjUnjittered  = frameNumber == 1 ? clip_from_world_unjittered : clip_from_world_unjittered_old,
      .viewProjUnjittered     = clip_from_world_unjittered,
      .invViewProj            = glm::inverse(clip_from_world),
      .proj                   = clip_from_view,
      .invProj                = glm::inverse(clip_from_view),
      .view                   = view_from_world,
      .invView                = glm::inverse(view_from_world),
      .cameraPos              = glm::vec4(position, 0),
      .meshletCount           = 0,
      .maxIndices             = 0,
      .bindlessSamplerLodBias = 0,
      .flags                  = 0,
      .alphaHashScale         = 0,
      .frameNumber            = uint32_t(Fvog::GetDevice().frameNumber),
      .sky                    = skyParameters,
      .skyViewLut             = sky_->GetSkyViewLut().ImageView().GetTexture2D(),
      .transmittanceLut       = sky_->GetTransmittanceLut().ImageView().GetTexture2D(),
      .aerialPerspectiveTransmittance = sky_->GetAerialPerspectiveTransmittance().ImageView().GetTexture3D(),
      .aerialPerspectiveScattering = sky_->GetAerialPerspectiveScattering().ImageView().GetTexture3D(),
      .ae_clip_from_world = sky_->GetAerialPerspectiveClipFromWorld(), // TODO: this matrix lags one frame behind the viewer
      .linearSampler          = linearClampSampler,
      .gBuffer                = gBufferBuffer->GetDeviceAddress(),
      .debugDraw              = debugRenderingInfo.value().GetDeviceAddress(),
      .blueNoise              = noiseTexture->ImageView().GetTexture2D(),
      .sunShadowMap           = cascadedShadowMap_.GetShadowInfoBufferAddress(),
      .beerShadowMap          = rayMarchedClouds_->GetCascadedBeerShadowMapInfoPtr(),
      .weatherParams          = weatherGpuParams.ptr,
      .time                   = static_cast<float>(time += dt),
      .dt                     = static_cast<float>(dt),
    });

  ctx.ImageBarrierDiscard(sky_->GetTransmittanceLut(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(sky_->GetMultiscatteringLut(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(sky_->GetSkyViewLut(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(sky_->GetAerialPerspectiveTransmittance(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(sky_->GetAerialPerspectiveScattering(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL);

  {
    auto marker2 = ctx.MakeScopedDebugMarker("Sky");
    sky_->ComputeTransmittanceLut(commandBuffer, {.globalUniformsBufferIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index});
    ctx.Barrier();
    sky_->ComputeMultiscatteringLut(commandBuffer, {.globalUniformsBufferIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index});
    ctx.Barrier();
    sky_->ComputeSkyViewLut(commandBuffer, {.globalUniformsBufferIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index});
    sky_->ComputeAerialPerspectiveLut(commandBuffer,
      {
        .globalUniformsBufferIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .zNear = 100,
        .zFar  = 500'000,
        .aspectRatio = aspectRatio,
        .fovy = (float)cameraFovyRadians.Get(),
        .view_from_world = view_from_world,
      });
  }

  auto drawCalls       = std::vector<GpuMesh*>();
  auto meshUniformzVec = std::vector<Temp::ObjectUniforms>();
  for (auto&& [entity, transform, mesh] : world.GetRegistry().view<const GlobalTransform, const Mesh>().each())
  {
    if (world.GetRegistry().all_of<DoNotRenderIfAncestorIsLocalPlayer>(entity) && world.AncestorHasComponent<LocalPlayer>(entity))
    {
      continue;
    }
    GlobalTransform actualTransform = transform;
    GlobalTransform oldTransform = transform;
    if (auto* renderTransform = world.GetRegistry().try_get<const RenderTransform>(entity))
    {
      actualTransform = renderTransform->transform;
      oldTransform    = renderTransform->prevTransform;
    }
    auto worldFromObject = glm::translate(glm::mat4(1), actualTransform.position) * glm::mat4_cast(actualTransform.rotation) *
                           glm::scale(glm::mat4(1), glm::vec3(actualTransform.scale));
    auto worldFromObjectOld =
      glm::translate(glm::mat4(1), oldTransform.position) * glm::mat4_cast(oldTransform.rotation) * glm::scale(glm::mat4(1), glm::vec3(oldTransform.scale));
    auto& gpuMesh = g_meshes[mesh.name];
    auto tint     = glm::vec3(1);
    if (auto* tp = world.GetRegistry().try_get<const Tint>(entity))
    {
      tint = tp->color;
    }
    meshUniformzVec.emplace_back(worldFromObject, worldFromObjectOld, gpuMesh.vertexBuffer->GetDeviceAddress(), tint);
    drawCalls.emplace_back(&gpuMesh);
  }

  auto billboards = std::vector<Temp::BillboardInstance>();
  for (auto&& [entity, transform, health] : world.GetRegistry().view<const RenderTransform, const Health>(entt::exclude<LocalPlayer>).each())
  {
    billboards.emplace_back(Temp::BillboardInstance{
      .position   = transform.transform.position + glm::vec3(0, world.GetHeight(entity) / 2.0f + 0.25f, 0),
      .scale      = {0.5f, 0.1f},
      .leftColor  = {0, 1, 0, 1},
      .rightColor = {1, 0, 0, 1},
      .middle     = health.hp / health.maxHp,
    });
  }

  auto billboardSprites = std::vector<Temp::BillboardSpriteInstance>();
  for (auto&& [entity, transform, billboardSprite] : world.GetRegistry().view<const RenderTransform, const Billboard>().each())
  {
    auto tint = glm::vec3(1);
    if (auto* tp = world.GetRegistry().try_get<const Tint>(entity))
    {
      tint = tp->color;
    }
    billboardSprites.emplace_back(transform.transform.position,
      glm::vec2(transform.transform.scale),
      tint,
      GetOrEmplaceCachedTexture(billboardSprite.name, true).ImageView().GetTexture2D());
  }

  auto lights = std::vector<GpuLight>();
  for (auto&& [entity, light, transform] : world.GetRegistryRaw().view<GpuLight, const GlobalTransform>().each())
  {
    light.position  = transform.position;
    light.direction = GetForward(transform.rotation);
    if (const auto* rt = world.GetRegistry().try_get<const RenderTransform>(entity))
    {
      light.position  = rt->transform.position;
      light.direction = GetForward(rt->transform.rotation);
    }
    light.colorSpace = COLOR_SPACE_sRGB_LINEAR;
    lights.emplace_back(light);
  }

  if (world.globals->grid->numTopLevelBricks_ > 0)
  {
    auto lines           = std::vector<Debug::Line>();
    const auto& ecsLines = world.globals->debugLines;
    lines.insert(lines.end(), ecsLines.begin(), ecsLines.end());
#ifdef JPH_DEBUG_RENDERER
    const auto* debugRenderer = dynamic_cast<const Physics::DebugRenderer*>(JPH::DebugRenderer::sInstance);
    assert(debugRenderer);
    auto physicsLines = debugRenderer->GetLines();
    lines.insert(lines.end(), physicsLines.begin(), physicsLines.end());
#endif
    if (!meshUniformzVec.empty())
    {
      if (!meshUniformz || meshUniformz->Size() < meshUniformzVec.size() * sizeof(Temp::ObjectUniforms))
      {
        meshUniformz.emplace((uint32_t)meshUniformzVec.size(), "Mesh Uniforms");
      }
      meshUniformz->UpdateData(commandBuffer, meshUniformzVec);
    }

    if (!lines.empty())
    {
      if (!lineVertexBuffer || lineVertexBuffer->Size() < lines.size() * sizeof(Debug::Line))
      {
        lineVertexBuffer.emplace((uint32_t)lines.size(), "Debug Lines");
      }
      lineVertexBuffer->UpdateData(commandBuffer, lines);
    }

    if (!billboards.empty())
    {
      if (!billboardInstanceBuffer || billboardInstanceBuffer->Size() < billboards.size() * sizeof(Temp::BillboardInstance))
      {
        billboardInstanceBuffer.emplace((uint32_t)billboards.size(), "Billboards");
      }
      billboardInstanceBuffer->UpdateData(commandBuffer, billboards);
    }

    if (!lights.empty())
    {
      if (!lightBuffer || lightBuffer->Size() < lights.size() * sizeof(GpuLight))
      {
        lightBuffer.emplace((uint32_t)lights.size(), "Lights");
      }
      lightBuffer->UpdateData(commandBuffer, lights);
    }

    if (!billboardSprites.empty())
    {
      if (!billboardSpriteInstanceBuffer || billboardSpriteInstanceBuffer->Size() < billboardSprites.size() * sizeof(Temp::BillboardSpriteInstance))
      {
        billboardSpriteInstanceBuffer.emplace((uint32_t)billboardSprites.size(), "Billboard Sprites");
      }
      billboardSpriteInstanceBuffer->UpdateData(commandBuffer, billboardSprites);
    }

    auto& grid        = *world.globals->grid;
    const auto voxels = Voxels{
      .topLevelBricksDims         = grid.TopLevelBricksDims(),
      .topLevelBrickPtrsBaseIndex = grid.TopLevelBrickPtrsBaseIndex(),
      .dimensions                 = grid.Dimensions(),
      .bufferIdx                  = grid.Buffer().GetGpuBuffer().GetResourceHandle().index,
      .materialBufferIdx          = voxelMaterialBuffer->GetResourceHandle().index,
      .voxelSampler               = voxelSampler,
      .numLights                  = (uint32_t)lights.size(),
      .lightBufferIdx             = lights.empty() ? 0 : lightBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .globalUniformsIndex        = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
    };

    if (enableSunShadowPass.Get() != 0)
    {
      const auto playerPosition = world.GetRegistry().get<const RenderTransform>(player).transform.position;
      const auto sunDirection   = -Math::SphericalToCartesian(sunElevation, sunAzimuth);
      ctx.Barrier();
      cascadedShadowMap_.RenderTerrainShadowMap(commandBuffer,
        {
          .shadowResolution      = {uint32_t(sunShadowResolution.x), uint32_t(sunShadowResolution.y)},
          .numCascades           = uint32_t(sunShadowNumCascades),
          .voxels                = voxels,
          .playerPos             = playerPosition,
          .lightDirection        = sunDirection,
          .frustumDepth          = sunShadowFrustumDepth,
          .baseFrustumSideLength = sunShadowFrustumSideLength,
        });

      rayMarchedClouds_->RenderBeerShadowMap(commandBuffer,
        {
          .globalUniforms          = perFrameUniforms.GetDeviceBuffer().GetDeviceAddress(),
          .renderWidth             = static_cast<uint32_t>(cloudCbsmResolution.Get()),
          .renderHeight            = static_cast<uint32_t>(cloudCbsmResolution.Get()),
          .numCascades             = static_cast<uint32_t>(cloudCbsmNumCascades.Get()),
          .sunPosition             = playerPosition,
          .sunDirection            = sunDirection,
          .numRayMarchSteps        = static_cast<uint32_t>(cloudCbsmRayMarchSteps.Get()),
          .frustumDepth            = static_cast<float>(cloudCbsmFrustumDepth.Get()),
          .baseFrustumSideLength   = static_cast<float>(cloudCbsmFrustumSideLength.Get()),
          .time                    = static_cast<float>(time),
          .historyWeight           = static_cast<float>(cloudCbsmHistoryWeight.Get()),
          .jitterScale             = static_cast<float>(cloudCbsmJitterScale.Get()),
        });
      ctx.Barrier();
    }

    // DDGI- good candidate for async compute or overlapped work.
    if (giMethod_ == GIMethod::DDGI && !ddgiDebugPauseUpdates_)
    {
      auto marker = ctx.MakeScopedDebugMarker("DDGI");

      // Successive cascades are 2x the scale of the previous.
      for (int i = 0; i < DDGI_NUM_CASCADES; i++)
      {
        ddgi.args.gridInfo[i].baseGridScale = ddgi.args.gridInfo[0].baseGridScale * float(glm::exp2(i));
      }

      DDGIProbeGridInfo tempGridInfos[DDGI_NUM_CASCADES];
      for (int i = 0; i < DDGI_NUM_CASCADES; i++)
      {
        if (!ddgiDebugFreezeGrid_)
        {
          ddgi.args.gridInfo[i].probeInfosIndex = ddgi.probeDataBuffers[i].value().GetResourceHandle().index;
          ddgi.args.gridInfo[i].oldGridOffset   = ddgi.args.gridInfo[i].gridOffset;
          const auto offset = 1.0f + (position - glm::vec3(glm::vec3(ddgi.args.gridInfo[i].gridResolution) * ddgi.args.gridInfo[i].baseGridScale / 2.0f)) /
                                       ddgi.args.gridInfo[i].baseGridScale;
          ddgi.args.gridInfo[i].gridOffset         = glm::floor(offset);
          ddgi.args.gridInfo[i].gridOffsetFraction = glm::fract(offset);
        }
        tempGridInfos[i] = ddgi.args.gridInfo[i];
      }
      ddgi.args = DDGIArgs{
        .voxels                     = voxels,
        .internalColorSpace         = tonemapUniforms.shadingInternalColorSpace,
        .noiseTexture               = noiseTexture->ImageView().GetTexture2D(),
        .samples                    = 1,
        .bounces                    = 2,
        .globalUniformsIndex        = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .showCascadeIndexAsColor    = ddgiDebugShowCascadeIndexAsColor_,
        //.gridInfo                   = ddgi.args.gridInfo,
        .packedProbeRadiance        = ddgi.packedProbeRadiance->ImageView().GetImage2DArray(),
        .packedProbeIrradiance      = ddgi.packedProbeIrradiance->ImageView().GetImage2DArray(),
        .packedProbeRawDepth        = ddgi.packedProbeRawDepth->ImageView().GetImage2DArray(),
        .packedProbeDepthMoments    = ddgi.packedProbeDepthMoments->ImageView().GetImage2DArray(),
        .packedProbeRadianceTex     = ddgi.packedProbeRadiance->ImageView().GetTexture2DArray(),
        .packedProbeIrradianceTex   = ddgi.packedProbeIrradiance->ImageView().GetTexture2DArray(),
        .packedProbeRawDepthTex     = ddgi.packedProbeRawDepth->ImageView().GetTexture2DArray(),
        .packedProbeDepthMomentsTex = ddgi.packedProbeDepthMoments->ImageView().GetTexture2DArray(),
        .linearSampler              = linearClampSampler,
      };

      for (int i = 0; i < DDGI_NUM_CASCADES; i++)
      {
        ddgi.args.gridInfo[i] = tempGridInfos[i];
      }

      ddgi.argsBuffer->UpdateData(commandBuffer, ddgi.args);

      ctx.SetPushConstants(ddgi.argsBuffer->GetDeviceBuffer().GetDeviceAddress());

      ctx.BindComputePipeline(ddgi.resetNewProbesPipeline.GetPipeline());
      const auto numProbes = ddgi.args.gridInfo[0].gridResolution.x * ddgi.args.gridInfo[0].gridResolution.y * ddgi.args.gridInfo[0].gridResolution.z;
      ctx.DispatchInvocations(numProbes, 1, DDGI_NUM_CASCADES);

      // As long as probe validity is unused here, a barrier is not needed.
      ctx.BindComputePipeline(ddgi.traceRaysPipeline.GetPipeline());
      const auto extent = ddgi.packedProbeRadiance->GetCreateInfo().extent;
      ctx.DispatchInvocations(extent.width * extent.height, 1, DDGI_NUM_CASCADES); // TODO: caculate extent based on number of live probes instead of image size.

      ctx.Barrier();

      ctx.BindComputePipeline(ddgi.convolveIrradiancePipeline.GetPipeline());
      ctx.DispatchInvocations(extent.width * extent.height, 1, DDGI_NUM_CASCADES);

      // No barrier is needed here
      ctx.BindComputePipeline(ddgi.downsampleDepthPipeline.GetPipeline());
      ctx.DispatchInvocations(extent.width * extent.height, 1, DDGI_NUM_CASCADES);
    }

    auto albedoAttachment = Fvog::RenderColorAttachment{
      .texture = frame.gAlbedo.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto normalAttachment = Fvog::RenderColorAttachment{
      .texture = frame.gNormal.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto radianceAttachment = Fvog::RenderColorAttachment{
      .texture = frame.gRadiance.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto motionAttachment = Fvog::RenderColorAttachment{
      .texture = frame.gMotion.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto reactiveMaskAttachment = Fvog::RenderColorAttachment{
      .texture = frame.gReactiveMask.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    Fvog::RenderColorAttachment colorAttachments[] = {albedoAttachment, normalAttachment, radianceAttachment, motionAttachment, reactiveMaskAttachment};
    auto depthAttachment = Fvog::RenderDepthStencilAttachment{
      .texture = frame.gDepth.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .clearValue = {.depth = FAR_DEPTH},
    };

    ctx.BeginRendering({
      .name             = "Render voxels",
      .colorAttachments = colorAttachments,
      .depthAttachment  = depthAttachment,
    });
    {
      // Voxels
      TracyVkZone(head_->tracyVkContext_, commandBuffer, "Voxels");
      ctx.BindGraphicsPipeline(voxelsPipeline.GetPipeline());
      ctx.SetPushConstants(Temp::PushConstants{
        .voxels = voxels,
        .uniformBufferIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
      });
      ctx.Draw(3, 1, 0, 0);
    }
    {
      // Meshes
      TracyVkZone(head_->tracyVkContext_, commandBuffer, "Meshes");
      ctx.BindGraphicsPipeline(meshPipeline.GetPipeline());
      ctx.SetPushConstants(Temp::MeshArgs{
        .objects      = meshUniformz->GetDeviceBuffer().GetDeviceAddress(),
        .frame        = perFrameUniforms.GetDeviceBuffer().GetDeviceAddress(),
      });

      for (size_t i = 0; i < drawCalls.size(); i++)
      {
        const auto& mesh = drawCalls[i];
        ctx.BindIndexBuffer(mesh->indexBuffer.value(), 0, VK_INDEX_TYPE_UINT32);
        ctx.DrawIndexed((uint32_t)mesh->indices.size(), 1, 0, 0, (uint32_t)i);
      }

      if (!lines.empty())
      {
        ctx.BindGraphicsPipeline(debugLinesPipeline.GetPipeline());
        ctx.SetPushConstants(DebugLinesPushConstants{
          .vertexBufferIndex   = lineVertexBuffer->GetDeviceBuffer().GetResourceHandle().index,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .useGpuVertexBuffer  = 0,
        });
        ctx.Draw(uint32_t(lines.size() * 2), 1, 0, 0);
      }

      if (!billboards.empty())
      {
        ctx.BindGraphicsPipeline(billboardsPipeline.GetPipeline());
        ctx.SetPushConstants(BillboardPushConstants{
          .billboardsIndex     = billboardInstanceBuffer->GetDeviceBuffer().GetResourceHandle().index,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .cameraRight         = {view_from_world[0][0], view_from_world[1][0], view_from_world[2][0]},
          .cameraUp            = {view_from_world[0][1], view_from_world[1][1], view_from_world[2][1]},
          .texSampler          = voxelSampler,
        });
        ctx.Draw(uint32_t(billboards.size() * 6), 1, 0, 0);
      }

      if (!billboardSprites.empty())
      {
        ctx.BindGraphicsPipeline(billboardSpritesPipeline.GetPipeline());
        ctx.SetPushConstants(BillboardPushConstants{
          .billboardsIndex     = billboardSpriteInstanceBuffer->GetDeviceBuffer().GetResourceHandle().index,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .cameraRight         = {view_from_world[0][0], view_from_world[1][0], view_from_world[2][0]},
          .cameraUp            = {view_from_world[0][1], view_from_world[1][1], view_from_world[2][1]},
          .texSampler          = voxelSampler,
        });
        ctx.Draw(uint32_t(billboardSprites.size() * 6), 1, 0, 0);
      }

      // GPU-driven debug shapes
      {
        // GPU debug lines
        // ctx.DrawIndirect(debugRenderingInfo.value(), offsetof(DebugDrawData_t, aabbDrawCommand), 1, 0);
        // ctx.DrawIndirect(debugRenderingInfo.value(), offsetof(DebugDrawData_t, rectDrawCommand), 1, 0);
        ctx.BindGraphicsPipeline(debugLinesPipeline.GetPipeline());
        ctx.SetPushConstants(DebugLinesPushConstants{
          .vertexBufferIndex   = debugLineBuffer->GetResourceHandle().index,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .useGpuVertexBuffer  = 1,
        });
        ctx.DrawIndirect(debugRenderingInfo.value(), offsetof(DebugDrawData_t, lineDrawCommand), 1, 0);

        // GPU debug meshes
        // ctx.DrawIndirect();
      }
    }
    ctx.EndRendering();

    ctx.ImageBarrier(*frame.gAlbedo, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gNormal, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gRadiance, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gIlluminance, VK_IMAGE_LAYOUT_GENERAL);

    // Indirect illuminance
    if (giMethod_ == GIMethod::PerPixelPathTracing)
    {
      ctx.BindComputePipeline(perPixelPathtracerPipeline.GetPipeline());
      ctx.SetPushConstants(PerPixelPathtracerArguments{
        .voxels              = voxels,
        .gDepth              = frame.gDepth->ImageView().GetTexture2D(),
        .gNormal             = frame.gNormal->ImageView().GetTexture2D(),
        .gIndirectIrradiance = frame.gIlluminance->ImageView().GetImage2D(),
        .internalColorSpace  = COLOR_SPACE_sRGB_LINEAR,
        .uniformBufferIndex  = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .noiseTexture        = noiseTexture->ImageView().GetTexture2D(),
        .samples             = uint32_t(pathTracerSamples),
        .bounces             = uint32_t(pathTracerBounces),
      });
      ctx.DispatchInvocations(frame.gIlluminance->GetCreateInfo().extent);

      // Denoise. Issues barriers internally.
      bilateral_.DenoiseIlluminance(
        {
          .sceneAlbedo              = &frame.gAlbedo.value(),
          .sceneNormal              = &frame.gNormal.value(),
          .sceneDepth               = &frame.gDepth.value(),
          .sceneIlluminance         = &frame.gIlluminance.value(),
          .sceneIlluminancePingPong = &frame.gIlluminancePingPong.value(),
          .clip_from_view           = clip_from_view,
          .world_from_clip          = glm::inverse(clip_from_world),
          .cameraPos                = position,
        },
        commandBuffer);
    }
    
    ctx.ImageBarrier(*frame.gIlluminance, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

    const bool applySpelunkerEffect = Item::GetTotalEffectOnEntity(world, player, Item::EffectType::Spelunker, 0) > 0;
    if (applySpelunkerEffect)
    {
      ctx.ImageBarrierDiscard(*frame.gSpecial, VK_IMAGE_LAYOUT_GENERAL);
    }

    // Translucency pass
    {
      ctx.BindComputePipeline(translucentVoxelsPipeline.GetPipeline());
      ctx.SetPushConstants(ShadingPushConstants{
        .voxels             = voxels,
        .sceneColor         = frame.sceneColorInternalRes->ImageView().GetImage2D(),
        .internalColorSpace = COLOR_SPACE_sRGB_LINEAR,
        .uniformBufferIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .ddgi               = ddgi.argsBuffer.value().GetDeviceBuffer().GetDeviceAddress(),
        .samplerr           = linearClampSampler,
        .giMethod           = uint32_t(giMethod_),
      });
      ctx.DispatchInvocations(frame.sceneColorInternalRes->GetCreateInfo().extent);
    }

    // Spelunker effect, if active
    if (applySpelunkerEffect)
    {
      auto voxels2              = voxels;
      voxels2.materialBufferIdx = voxelMaterialBufferSpelunker->GetResourceHandle().index;

      ctx.BindComputePipeline(spelunkerEffectPipeline.GetPipeline());
      ctx.SetPushConstants(ShadingPushConstants{
        .voxels               = voxels2,
        .sceneColor           = frame.sceneColorInternalRes->ImageView().GetImage2D(),
        .internalColorSpace   = COLOR_SPACE_sRGB_LINEAR,
        .uniformBufferIndex   = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .ddgi                 = ddgi.argsBuffer.value().GetDeviceBuffer().GetDeviceAddress(),
        .samplerr             = linearClampSampler,
        .giMethod             = uint32_t(giMethod_),
      });
      ctx.DispatchInvocations(frame.sceneColorInternalRes->GetCreateInfo().extent);
    }

    Fvog::Texture* aoTexture = &whiteTexture_.value();
    if (giMethod_ == GIMethod::DDGI && enableAo_)
    {
      aoParams_.voxels          = voxels;
      aoParams_.inputDepth      = &frame.gDepth.value();
      aoParams_.inputNormal     = &frame.gNormal.value();
      aoParams_.outputSize      = {frame.gAlbedo->GetCreateInfo().extent.width, frame.gAlbedo->GetCreateInfo().extent.height};
      aoParams_.frameNumber     = uint32_t(Fvog::GetDevice().frameNumber);
      aoParams_.clip_from_view  = clip_from_view;
      aoParams_.world_from_clip = glm::inverse(clip_from_world);
      aoParams_.cameraPosWS     = position;

      aoTexture = &ao_.ComputeAO(commandBuffer, aoParams_);
    }

    // Shade image.
    {
      auto marker = ctx.MakeScopedDebugMarker("Shade deferred");
      ctx.BindComputePipeline(shadeDeferredPipeline.GetPipeline());
      ctx.SetPushConstants(ShadingPushConstants{
        .voxels               = voxels,
        .sceneColor           = frame.sceneColorInternalRes->ImageView().GetImage2D(),
        .internalColorSpace   = COLOR_SPACE_sRGB_LINEAR,
        .uniformBufferIndex   = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .ddgi                 = ddgi.argsBuffer.value().GetDeviceBuffer().GetDeviceAddress(),
        .samplerr             = linearClampSampler,
        .giMethod             = uint32_t(giMethod_),
        .applySpelunkerEffect = applySpelunkerEffect,
        .ambientOcclusion     = aoTexture->ImageView().GetTexture2D(),
      });
      ctx.DispatchInvocations(frame.sceneColorInternalRes->GetCreateInfo().extent);
    }

    ctx.Barrier();

    if (enableSsgi_)
    {
      ssgiParams_.inputAlbedo           = &frame.gAlbedo.value();
      ssgiParams_.inputDepth            = &frame.gDepth.value();
      ssgiParams_.inputNormal           = &frame.gNormal.value();
      ssgiParams_.inputDiffuseLuminance = &frame.sceneColorInternalRes.value();
      ssgiParams_.outputSize            = {frame.gAlbedo->GetCreateInfo().extent.width, frame.gAlbedo->GetCreateInfo().extent.height};
      ssgiParams_.frameNumber           = uint32_t(Fvog::GetDevice().frameNumber);
      ssgiParams_.view_from_world       = view_from_world;
      ssgiParams_.clip_from_view        = clip_from_view;
      ssgiParams_.debugDraw             = debugRenderingInfo->GetDeviceAddress();

      auto& sceneColorWithSSGI = ssgi_.Dispatch(commandBuffer, ssgiParams_);
      std::swap(sceneColorWithSSGI, frame.sceneColorInternalRes.value());
    }

    ctx.Barrier();
    ctx.ImageBarrier(frame.sceneColorInternalRes.value(), VK_IMAGE_LAYOUT_GENERAL);
    ctx.ImageBarrier(frame.gDepth.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

    {
      auto marker2 = ctx.MakeScopedDebugMarker("Clouds");
      rayMarchedClouds_->Render(commandBuffer,
        {
          .gDepth              = &frame.gDepth.value(),
          .renderWidth         = uint32_t(renderInternalWidth * cloudsUpscaleRatio.Get()),
          .renderHeight        = uint32_t(renderInternalHeight * cloudsUpscaleRatio.Get()),
          .upscaleWidth        = renderInternalWidth,
          .clip_from_view      = clip_from_view,
          .view_from_world     = view_from_world,
          .clip_from_view_old  = clip_from_view_old,
          .view_from_world_old = view_from_world_old,
          .distForMinRaySteps  = (float)cloudsDistForMinRayStepCount.Get(),
          .distForMaxRaySteps  = (float)cloudsDistForMaxRayStepCount.Get(),
          .numRayMarchStepsMin = uint32_t(cloudsNumRayMarchStepsMin.Get()),
          .numRayMarchStepsMax = uint32_t(cloudsNumRayMarchStepsMax.Get()),
          .sunDirection        = skyParameters.sunDir,
          .sunIntensity        = skyParameters.sunColor * skyParameters.sunBrightness,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .ddgi                = ddgi.argsBuffer->GetDeviceBuffer().GetDeviceAddress(),
          .frameNumber         = frameNumber,
          .zNear               = (float)cameraNearPlane.Get(),
        });
      weather_.cloudHorizontalOffset += weather_.windVelocity * (float)dt;
      ctx.Barrier();
      rayMarchedClouds_->Upscale(commandBuffer,
        {
          .gDepth        = &frame.gDepth.value(),
          .gDepthPrev    = &frame.gDepthPrev.value(),
          .upscaleWidth  = renderInternalWidth,
          .upscaleHeight = renderInternalHeight,
          .zNear         = (float)cameraNearPlane.Get(),
        });
      ctx.Barrier();
      rayMarchedClouds_->Composite(commandBuffer,
        {
          .gRadianceIn  = &frame.sceneColorInternalRes.value(),
          .gRadianceOut = &frame.sceneColorInternalRes.value(),
        });
      ctx.Barrier();
    }

    if (!debugDisableFog)
    {
      auto markerFog = ctx.MakeScopedDebugMarker("Froxel fog");

      ctx.ImageBarrierDiscard(inScatteringAndTransmittanceVolume.value(), VK_IMAGE_LAYOUT_GENERAL);
      ctx.ImageBarrierDiscard(fogColorAndDensityVolume.value(), VK_IMAGE_LAYOUT_GENERAL);
      const auto nearVolume            = 1.5f;
      const auto farVolume             = 1000.0f;
      const auto clip_from_view_volume = glm::perspectiveZO((float)cameraFovyRadians.Get(), aspectRatio, nearVolume, farVolume);
      fog_.UpdateUniforms(commandBuffer, world,
        {
          .viewPos           = position,
          .time              = 0,
          .invViewProjScene  = glm::inverse(clip_from_world),
          .viewProjVolume    = clip_from_view_volume * view_from_world,
          .invViewProjVolume = glm::inverse(clip_from_view_volume * view_from_world),
          //.sunViewProj                          =,
          //.sunDir                               =,
          .volumeNearPlane      = nearVolume,
          .volumeFarPlane       = farVolume,
          .useScatteringTexture = false,
          .anisotropyG          = 0,
          .noiseOffsetScale     = 1,
          .frog                 = false,
          .groundFogDensity     = 0.25f,
          //.sunColor                             =,
          .inSceneLuminance                     = frame.sceneColorInternalRes->ImageView().GetTexture2D(),
          .gDepth                               = frame.gDepth->ImageView().GetTexture2D(),
          .inScatteringAndTransmittanceVolume   = inScatteringAndTransmittanceVolume->ImageView().GetTexture3D(),
          .fogDensityVolume                     = fogColorAndDensityVolume->ImageView().GetTexture3D(),
          .blueNoise                            = noiseTexture->ImageView().GetTexture2D(),
          .inScatteringAndTransmittanceVolumeRW = inScatteringAndTransmittanceVolume->ImageView().GetImage3D(),
          .fogDensityVolumeRW                   = fogColorAndDensityVolume->ImageView().GetImage3D(),
          .outSceneLuminance                    = frame.sceneColorInternalRes->ImageView().GetImage2D(),
          .linearSampler                        = linearClampSampler,
          //.mieScattering                        = ,
          .globalSurfaceHeight = globalSurfaceHeightImage->ImageView().GetTexture2D(),
          .globalSurfaceFog = globalSurfaceFogImage->ImageView().GetTexture2D(),
          .globalFog = globalFogImage->ImageView().GetTexture3D(),
          .ddgi   = ddgi.argsBuffer.value().GetDeviceBuffer().GetDeviceAddress(),
          .voxels = voxels,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .sunSelfShadowSteps = sunSelfShadowSteps,
          .sunSelfShadowDist = sunSelfShadowDist,
        });
      fog_.InjectFog(commandBuffer, fogColorAndDensityVolume.value());
      fog_.MarchVolume(commandBuffer, fogColorAndDensityVolume.value(), inScatteringAndTransmittanceVolume.value());
      fog_.ApplyDeferred(commandBuffer, frame.sceneColorInternalRes.value(), frame.gDepth.value(), frame.sceneColorInternalRes.value(), inScatteringAndTransmittanceVolume.value());
    }
    ctx.Barrier();
  }

  // DDGI debug probes.
  if (ddgiDebugView_ != DDGIDebugView::None)
  {
    //auto marker = ctx.MakeScopedDebugMarker("DDGI Debug Probes");
    auto sceneColorAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneColorInternalRes.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD,
    };
    auto depthAttachment = Fvog::RenderDepthStencilAttachment{
      .texture    = frame.gDepth.value().ImageView(),
      .loadOp     = VK_ATTACHMENT_LOAD_OP_LOAD,
    };
    ctx.BeginRendering({
      .name             = "DDGI Debug Probes",
      .colorAttachments = {&sceneColorAttachment, 1},
      .depthAttachment  = depthAttachment,
    });

    ctx.BindGraphicsPipeline(ddgi.debugProbesPipeline.GetPipeline());
    const auto& icosphere_3 = g_meshes.at("icosphere_3");
    for (int cascade = 0; cascade < DDGI_NUM_CASCADES; cascade++)
    {
      if (ddgiDebugShowOnlyThisCascade_ < 0 || ddgiDebugShowOnlyThisCascade_ == cascade)
      {
        ctx.SetPushConstants(DebugProbesArguments{
          .vertexBuffer        = icosphere_3.vertexBuffer.value().GetDeviceAddress(),
          .ddgi                = ddgi.argsBuffer.value().GetDeviceBuffer().GetDeviceAddress(),
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .samplerr            = linearClampSampler,
          .debugMode           = uint32_t(ddgiDebugView_),
          .probeSize           = ddgiDebugProbeSize_,
          .cascade             = cascade,
        });
        ctx.BindIndexBuffer(icosphere_3.indexBuffer.value(), 0, VK_INDEX_TYPE_UINT32);
        const auto& res = ddgi.args.gridInfo[cascade].gridResolution;
        ctx.DrawIndexed(uint32_t(icosphere_3.indices.size()), res.x * res.y * res.z, 0, 0, 0);
      }
    }

    ctx.EndRendering();
  }

  ctx.Barrier();

  {
    ZoneScopedN("Auto Exposure");
    autoExposure_.Apply(commandBuffer,
      {
        .image           = frame.sceneColorInternalRes.value(),
        .exposureBuffer  = exposureBuffer,
        .deltaTime       = float(dt),
        .adjustmentSpeed = 1,
        .targetLuminance = 0.2140f,
        .logMinLuminance = -15.0f,
        .logMaxLuminance = 15.0f,
      });
  }

  ctx.Barrier();

  #ifdef FROGRENDER_FSR2_ENABLE
  if (fsr2Enable.Get() != 0)
  {
    auto marker = ctx.MakeScopedDebugMarker("FSR 2");

    ctx.ImageBarrier(*frame.sceneColorInternalRes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gMotion, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gReactiveMask, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.ImageBarrierDiscard(*frame.sceneColorOutputRes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    float jitterX{};
    float jitterY{};
    ffxFsr2GetJitterOffset(&jitterX, &jitterY, (int32_t)Fvog::GetDevice().frameNumber, ffxFsr2GetJitterPhaseCount(renderInternalWidth, renderOutputWidth));

    FfxFsr2DispatchDescription dispatchDesc{
      .commandList                = ffxGetCommandListVK(commandBuffer),
      .color                      = ffxGetTextureResourceVK(&fsr2Context,
        frame.sceneColorInternalRes->Image(),
        frame.sceneColorInternalRes->ImageView(),
        renderInternalWidth,
        renderInternalHeight,
        Fvog::detail::FormatToVk(frame.sceneColorInternalRes->GetCreateInfo().format)),
      .depth                      = ffxGetTextureResourceVK(&fsr2Context,
        frame.gDepth->Image(),
        frame.gDepth->ImageView(),
        renderInternalWidth,
        renderInternalHeight,
        Fvog::detail::FormatToVk(frame.gDepth->GetCreateInfo().format)),
      .motionVectors              = ffxGetTextureResourceVK(&fsr2Context,
        frame.gMotion->Image(),
        frame.gMotion->ImageView(),
        renderInternalWidth,
        renderInternalHeight,
        Fvog::detail::FormatToVk(frame.gMotion->GetCreateInfo().format)),
      .exposure                   = {},
      .reactive                   = ffxGetTextureResourceVK(&fsr2Context,
        frame.gReactiveMask->Image(),
        frame.gReactiveMask->ImageView(),
        renderInternalWidth,
        renderInternalHeight,
        Fvog::detail::FormatToVk(frame.gReactiveMask->GetCreateInfo().format)),
      .transparencyAndComposition = {},
      .output                     = ffxGetTextureResourceVK(&fsr2Context,
        frame.sceneColorOutputRes->Image(),
        frame.sceneColorOutputRes->ImageView(),
        renderOutputWidth,
        renderOutputHeight,
        Fvog::detail::FormatToVk(frame.sceneColorOutputRes->GetCreateInfo().format)),
      .jitterOffset               = {jitterX, jitterY},
      .motionVectorScale          = {float(renderInternalWidth), float(renderInternalHeight)},
      .renderSize                 = {renderInternalWidth, renderInternalHeight},
      .enableSharpening           = fsr2Sharpness != 0,
      .sharpness                  = fsr2Sharpness,
      .frameTimeDelta             = static_cast<float>(dt * 1000.0),
      .preExposure                = 1,
      .reset                      = false,
      .cameraNear                 = std::numeric_limits<float>::max(),
      .cameraFar                  = (float)cameraNearPlane.Get(),
      .cameraFovAngleVertical     = (float)cameraFovyRadians.Get(),
      .viewSpaceToMetersFactor    = 1,
    };

    if (auto err = ffxFsr2ContextDispatch(&fsr2Context, &dispatchDesc); err != FFX_OK)
    {
      spdlog::error("FSR 2 dispatch error: {}", err);
    }

    // Re-apply states that application assumes
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);
    *frame.sceneColorOutputRes->currentLayout = VK_IMAGE_LAYOUT_GENERAL;
  }
#endif

  ctx.Barrier();

  if (enableBloom)
  {
    ZoneScopedN("Bloom");
    bloom_.Apply(commandBuffer,
      {
        .target                      = fsr2Enable.Get() ? frame.sceneColorOutputRes.value() : frame.sceneColorInternalRes.value(),
        .scratchTexture              = frame.sceneColorBloomScratch.value(),
        .passes                      = 6,
        .strength                    = 1.0f / 16.0f,
        .width                       = 1,
        .useLowPassFilterOnFirstPass = true,
      });
  }

  ctx.ImageBarrier(frame.sceneColorInternalRes.value(), VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(frame.sceneColorTonemapped.value(), VK_IMAGE_LAYOUT_GENERAL);

  // Tonemap
  {
    ZoneScopedN("Tonemap");
    ctx.BindComputePipeline(tonemapPipeline.GetPipeline());
    ctx.SetPushConstants(shared::TonemapArguments{
      .sceneColor         = (fsr2Enable.Get() ? frame.sceneColorOutputRes : frame.sceneColorInternalRes)->ImageView().GetTexture2D(),
      .noise = noiseTexture->ImageView().GetTexture2D(),
      .nearestSampler = nearestSampler,
      .linearClampSampler = linearClampSampler,
      .exposure = exposureBuffer,
      .tonemapUniforms = tonemapUniformBuffer.GetDeviceBuffer(),
      .outputImage = frame.sceneColorTonemapped->ImageView().GetImage2D(),
      .tonyMcMapface = tonyMcMapfaceLut->ImageView().GetTexture3D(),
    });
    ctx.DispatchInvocations(frame.sceneColorTonemapped->GetCreateInfo().extent);
  }

  std::swap(frame.gDepth, frame.gDepthPrev);
  clip_from_world_old            = clip_from_world;
  clip_from_world_unjittered_old = clip_from_world_unjittered;
  clip_from_view_old             = clip_from_view;
  view_from_world_old            = view_from_world;
}

Fvog::Texture& VoxelRenderer::GetOrEmplaceCachedTexture(const std::string& name, bool srgb)
{
  if (auto it = stringToTexture.find(name); it != stringToTexture.end())
  {
    return it->second;
  }

  // Make a very sketchy and unscalable assumption about the path.
  auto texture = LoadImageFile(GetAssetDirectory() / "voxels" / "textures" / (name + ".png"), srgb);

  return stringToTexture.emplace(name, std::move(texture)).first->second;
}

void VoxelRenderer::InitDDGI(const DDGIProbeGridInfo& probeGridInfo)
{
  ZoneScoped;
  ASSERT(probeGridInfo.probeRadianceResolution.x > 0);
  ASSERT(probeGridInfo.probeRadianceResolution.x == probeGridInfo.probeRadianceResolution.y);
  ddgi.traceRaysPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
       .name = "DDGI Trace Luminance",
       .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
           .stage = Fvog::PipelineStage::COMPUTE_SHADER,
           .path  = GetShaderDirectory() / "ddgi/TraceProbes.comp.glsl",
      },
    .useMinSubgroupSize = true,
  });

  ddgi.convolveIrradiancePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "DDGI Convolve Illuminance",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "ddgi/ConvolveIrradiance.comp.glsl",
      },
  });

  ddgi.downsampleDepthPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "DDGI Downsample Probe Depth",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "ddgi/DownsampleProbeDepth.comp.glsl",
      },
  });

  ddgi.resetNewProbesPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Reset New Probes",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "ddgi/ResetNewProbes.comp.glsl",
      },
  });

  ddgi.debugProbesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug Probes",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "ddgi/DebugProbes.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "ddgi/DebugProbes.frag.glsl",
      },
    .state =
      {
        .rasterizationState = {.cullMode = VK_CULL_MODE_BACK_BIT},
        .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = FVOG_COMPARE_OP_NEARER_OR_EQUAL},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::sceneColorFormat}},
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  g_meshes.emplace("icosphere_3", LoadObjFile(GetAssetDirectory() / "models/icosphere_3.obj"));

  ddgi.args.gridInfo[0] = probeGridInfo;
  for (int i = 0; i < DDGI_NUM_CASCADES; i++)
  {
    ddgi.args.gridInfo[i] = ddgi.args.gridInfo[0];
  }
  ddgi.argsBuffer.emplace(1, "DDGI Arguments");
  const auto numProbes = probeGridInfo.gridResolution.x * probeGridInfo.gridResolution.y * probeGridInfo.gridResolution.z;

  ddgi.probeDataBuffers = std::make_unique<decltype(ddgi.probeDataBuffers)::element_type[]>(DDGI_NUM_CASCADES);
  for (int i = 0; i < DDGI_NUM_CASCADES; i++)
  {
    ddgi.probeDataBuffers[i].emplace(Fvog::TypedBufferCreateInfo{uint32_t(numProbes)}, std::format("Probe Data (cascade {})", i));
  }

  Fvog::GetDevice().ImmediateSubmit(
    [&](VkCommandBuffer cmd)
    {
      for (int i = 0; i < DDGI_NUM_CASCADES; i++)
      {
        ddgi.probeDataBuffers[i]->FillData(cmd);
      }
    });

  // Probe sizes are dilated to include a 1-texel border.
  const auto width1  = (2 + probeGridInfo.probeRadianceResolution.x) * std::ceil(std::sqrt(float(numProbes)));
  const auto height1 = (2 + probeGridInfo.probeRadianceResolution.x) * std::ceil(numProbes * (2 + probeGridInfo.probeRadianceResolution.x) / width1);
  ddgi.packedProbeRadiance =
    Fvog::CreateTexture2DArray({uint32_t(width1), uint32_t(height1)}, DDGI_NUM_CASCADES, DDGI::radianceFormat, Fvog::TextureUsage::GENERAL, "DDGI Probe Radiance");
  ddgi.packedProbeRawDepth =
    Fvog::CreateTexture2DArray({uint32_t(width1), uint32_t(height1)}, DDGI_NUM_CASCADES, Fvog::Format::R32_SFLOAT, Fvog::TextureUsage::GENERAL, "DDGI Probe Raw Depth");

  const auto width2  = (2 + probeGridInfo.probeIrradianceResolution.x) * std::ceil(std::sqrt(float(numProbes)));
  const auto height2 = (2 + probeGridInfo.probeIrradianceResolution.x) * std::ceil(numProbes * (2 + probeGridInfo.probeIrradianceResolution.x) / width2);
  ddgi.packedProbeIrradiance =
    Fvog::CreateTexture2DArray({uint32_t(width2), uint32_t(height2)}, DDGI_NUM_CASCADES, DDGI::radianceFormat, Fvog::TextureUsage::GENERAL, "DDGI Probe Irradiance");

  const auto width3  = (2 + probeGridInfo.probeDepthMomentsResolution.x) * std::ceil(std::sqrt(float(numProbes)));
  const auto height3 = (2 + probeGridInfo.probeDepthMomentsResolution.x) * std::ceil(numProbes * (2 + probeGridInfo.probeDepthMomentsResolution.x) / width2);
  ddgi.packedProbeDepthMoments = Fvog::CreateTexture2DArray({uint32_t(width3), uint32_t(height3)},
    DDGI_NUM_CASCADES,
    Fvog::Format::R32G32_SFLOAT,
    Fvog::TextureUsage::GENERAL,
    "DDGI Probe Depth Moments");

  Fvog::GetDevice().ImmediateSubmit(
    [&](VkCommandBuffer cmd)
    {
      auto ctx = Fvog::Context(cmd);
      ctx.ImageBarrierDiscard(ddgi.packedProbeRadiance.value(), VK_IMAGE_LAYOUT_GENERAL);
      ctx.ImageBarrierDiscard(ddgi.packedProbeIrradiance.value(), VK_IMAGE_LAYOUT_GENERAL);
      ctx.ImageBarrierDiscard(ddgi.packedProbeRawDepth.value(), VK_IMAGE_LAYOUT_GENERAL);
      ctx.ImageBarrierDiscard(ddgi.packedProbeDepthMoments.value(), VK_IMAGE_LAYOUT_GENERAL);
      ctx.ClearTexture(ddgi.packedProbeRadiance.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
      ctx.ClearTexture(ddgi.packedProbeIrradiance.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
      ctx.ClearTexture(ddgi.packedProbeRawDepth.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
      ctx.ClearTexture(ddgi.packedProbeDepthMoments.value(), {.color = {0.0f, 0.0f, 0.0f, 0.0f}});
    });
}
