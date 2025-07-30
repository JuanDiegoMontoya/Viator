#include "VoxelRenderer.h"

#include "Game/World.h"
#include "Game/Assets.h"
#include "Game/Item.h"
#include "MathUtilities.h"
#include "PipelineManager.h"
#include "Fvog/Device.h"
#include "Fvog/Rendering2.h"
#include "Fvog/detail/Common.h"
#include "Core/Assert2.h"
#include "Game/VoxLoader.h"

#include "shaders/Config.shared.h"
#include "shaders/voxels/PerPixelPathtracer.shared.h"
#include "shaders/voxels/ShadeDeferred.shared.h"
#include "shaders/ddgi/DebugProbesCommon.h.glsl"

#ifdef JPH_DEBUG_RENDERER
#include "Game/Physics/DebugRenderer.h"
#endif

#include "volk.h"
#include "Fvog/detail/ApiToEnum2.h"

#include "tiny_obj_loader.h"
#include "tracy/Tracy.hpp"
#include "tracy/TracyVulkan.hpp"
#include "stb_image.h"

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

  enum class MaterialFlagBit
  {
    HAS_BASE_COLOR_TEXTURE       = 1 << 0,
    HAS_EMISSION_TEXTURE         = 1 << 1,
    RANDOMIZE_TEXCOORDS_ROTATION = 1 << 2,
    IS_INVISIBLE                 = 1 << 3,
    IS_SUBGRID                   = 1 << 4,
  };
  FVOG_DECLARE_FLAG_TYPE(VoxelMaterialFlags, MaterialFlagBit, uint32_t);

  struct GpuVoxelMaterial
  {
    VoxelMaterialFlags materialFlags;
    shared::Texture2D baseColorTexture;
    glm::vec3 baseColorFactor;
    shared::Texture2D emissionTexture;
    glm::vec3 emissionFactor;
    FVOG_UINT32 subGridIndex;
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
} // namespace

VoxelRenderer::VoxelRenderer(PlayerHead* head, World&) : head_(head)
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

  head_->renderCallback_ = [this](float dt, World& world, VkCommandBuffer cmd, uint32_t swapchainImageIndex) { OnRender(dt, world, cmd, swapchainImageIndex); };
  head_->framebufferResizeCallback_ = [this](uint32_t newWidth, uint32_t newHeight) { OnFramebufferResize(newWidth, newHeight); };
  head_->guiCallback_ = [this](DeltaTime dt, World& world, VkCommandBuffer cmd) { OnGui(dt, world, cmd); };

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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
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

  skyTransmittancePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Sky Transmittance LUT",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "sky/TransmittanceLUT.comp.glsl",
      },
  });

  skyMultiscatteringPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Sky Multiscattering LUT",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "sky/MultiscatteringLUT.comp.glsl",
      },
  });

  skyViewPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Sky View LUT",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "sky/SkyViewLUT.comp.glsl",
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

  transmittanceLut = Fvog::Texture(
    {
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format   = Fvog::Format::R16G16B16A16_SFLOAT,
      .extent   = {256, 64, 1},
      .usage    = Fvog::TextureUsage::GENERAL,
    },
    "Transmittance LUT");

  multiscatteringLut = Fvog::Texture(
    {
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format   = Fvog::Format::R16G16B16A16_SFLOAT,
      .extent   = {32, 32, 1},
      .usage    = Fvog::TextureUsage::GENERAL,
    },
    "Multiscattering LUT");

  skyViewLut = Fvog::Texture(
    {
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format   = Fvog::Format::R16G16B16A16_SFLOAT,
      .extent   = {256, 192, 1},
      .usage    = Fvog::TextureUsage::GENERAL,
    },
    "Sky View LUT");

  transmittanceLutView   = transmittanceLut->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  multiscatteringLutView = multiscatteringLut->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  skyViewLutView         = skyViewLut->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});

  Fvog::GetDevice().ImmediateSubmit([this](VkCommandBuffer cmd) { exposureBuffer.UpdateDataExpensive(cmd, 0.0f); });

  OnFramebufferResize(head_->windowFramebufferWidth, head_->windowFramebufferHeight);
}

VoxelRenderer::~VoxelRenderer()
{
  ZoneScoped;
  g_meshes.clear();
  vkDeviceWaitIdle(Fvog::GetDevice().device_);

  Fvog::GetDevice().FreeUnusedResources();

//#if FROGRENDER_FSR2_ENABLE
//  if (!fsr2FirstInit)
//  {
//    ffxFsr2ContextDestroy(&fsr2Context);
//  }
//#endif
}

void VoxelRenderer::CreateRenderingMaterials(std::span<const std::unique_ptr<BlockDefinition>> blockDefinitions)
{
  auto voxelMaterials = std::vector<GpuVoxelMaterial>();
  auto voxelMaterialsSpelunker = std::vector<GpuVoxelMaterial>();

  // Translate block definitions to GPU materials, then upload.
  for (const auto& def : blockDefinitions)
  {
    const auto& desc = def->GetMaterialDesc();

    auto gpuMat = GpuVoxelMaterial{};
    gpuMat.baseColorFactor = desc.baseColorFactor;
    gpuMat.emissionFactor  = desc.emissionFactor;
    if (desc.randomizeTexcoordRotation)
    {
      gpuMat.materialFlags |= MaterialFlagBit::RANDOMIZE_TEXCOORDS_ROTATION;
    }
    if (desc.baseColorTexture)
    {
      gpuMat.materialFlags |= MaterialFlagBit::HAS_BASE_COLOR_TEXTURE;
      gpuMat.baseColorTexture = GetOrEmplaceCachedTexture(*desc.baseColorTexture, true).ImageView().GetTexture2D();
    }
    if (desc.emissionTexture)
    {
      gpuMat.materialFlags |= MaterialFlagBit::HAS_EMISSION_TEXTURE;
      gpuMat.emissionTexture = GetOrEmplaceCachedTexture(*desc.emissionTexture, true).ImageView().GetTexture2D();
    }
    if (desc.isInvisible)
    {
      gpuMat.materialFlags |= MaterialFlagBit::IS_INVISIBLE;
    }
    if (def->GetSubGrid())
    {
      gpuMat.materialFlags |= MaterialFlagBit::IS_SUBGRID;
      gpuMat.subGridIndex = def->GetSubGrid()->myIndexINTERNAL;
    }

    voxelMaterials.emplace_back(gpuMat);
    if (!desc.isValuable)
    {
      gpuMat.materialFlags |= MaterialFlagBit::IS_INVISIBLE;
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
}

void VoxelRenderer::OnFramebufferResize(uint32_t newWidth, uint32_t newHeight)
{
  ZoneScoped;

  const auto extent      = VkExtent2D{newWidth, newHeight};
  frame.sceneAlbedo      = Fvog::CreateTexture2D(extent, Frame::sceneAlbedoFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene albedo");
  frame.sceneNormal      = Fvog::CreateTexture2D(extent, Frame::sceneNormalFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene normal");
  frame.sceneRadiance    = Fvog::CreateTexture2D(extent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene radiance");
  frame.sceneIlluminance = Fvog::CreateTexture2D(extent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene illuminance");
  frame.sceneIlluminancePingPong = Fvog::CreateTexture2D(extent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene illuminance 2");
  frame.sceneDepth       = Fvog::CreateTexture2D(extent, Frame::sceneDepthFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene depth");
  frame.sceneColor       = Fvog::CreateTexture2D(extent, Frame::sceneColorFormat, Fvog::TextureUsage::GENERAL, "Scene color");
  frame.sceneSpecial     = Fvog::CreateTexture2D(extent, Frame::sceneSpecialFormat, Fvog::TextureUsage::GENERAL, "Scene special");

  frame.sceneColorBloomScratch = Fvog::CreateTexture2DMip({extent.width / 2, extent.height / 2}, Frame::sceneColorFormat, 8, Fvog::TextureUsage::GENERAL, "Scene color (bloom scratch buffer)");

  frame.sceneColorTonemapped = Fvog::CreateTexture2D(extent, Frame::sceneColorTonemappedFormat, Fvog::TextureUsage::GENERAL, "Scene color tonemapped");
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
  if (auto gameState = world.GetRegistry().ctx().get<GameState>();
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

  if (world.GetRegistry().ctx().contains<TwoLevelGrid>())
  {
    auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
    grid.buffer.FlushWritesToGPU(commandBuffer);
  }

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
  const auto fovy            = glm::radians(65.0f);
  const auto aspectRatio     = (float)head_->windowFramebufferWidth / head_->windowFramebufferHeight;
  const auto clip_from_view  = Math::InfReverseZPerspectiveRH(fovy, aspectRatio, 0.1f);
  const auto clip_from_world = clip_from_view * view_from_world;

  ctx.ImageBarrierDiscard(frame.sceneAlbedo.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneNormal.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneIlluminance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneRadiance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneDepth.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  const auto& sunInfo = world.GetRegistry().ctx().get<SunInfo>();
  sunElevation        = sunInfo.timeOfDay * glm::pi<float>() - glm::pi<float>();
  sunAzimuth          = sunInfo.azimuth;

  skyParameters.sunDir = Math::SphericalToCartesian(sunElevation, sunAzimuth);
  skyParameters.sunColor = sunColor;
  skyParameters.sunBrightness = sunBrightness; // Intended to be used with solid_angle_mapping_PDF(radians(0.5))

  perFrameUniforms.UpdateData(commandBuffer,
    GlobalUniforms{
      .viewProj               = clip_from_world,
      .oldViewProjUnjittered  = glm::mat4{},
      .viewProjUnjittered     = glm::mat4{},
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
      .skyViewLut             = skyViewLut.value().ImageView().GetTexture2D(),
      .transmittanceLut       = transmittanceLut.value().ImageView().GetTexture2D(),
      .linearSampler          = linearClampSampler,
    });

  ctx.ImageBarrierDiscard(transmittanceLut.value(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(multiscatteringLut.value(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(skyViewLut.value(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL);

  ctx.BindComputePipeline(skyTransmittancePipeline.GetPipeline());
  TransmittancePush transmittancePush;
  transmittancePush.globalUniformsIndexTransmittance = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index;
  transmittancePush.transmittanceImage = transmittanceLut.value().ImageView().GetImage2D();
  ctx.SetPushConstants(transmittancePush);
  ctx.DispatchInvocations(transmittanceLut.value().GetCreateInfo().extent);
  
  ctx.BindComputePipeline(skyMultiscatteringPipeline.GetPipeline());
  MultiscatteringPush multiscatteringPush;
  multiscatteringPush.globalUniformsIndexMultiscattering = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index;
  multiscatteringPush.transmittanceTexture = transmittanceLut.value().ImageView().GetTexture2D();
  multiscatteringPush.transmittanceSampler = linearClampSampler;
  multiscatteringPush.multiscatteringImage = multiscatteringLut.value().ImageView().GetImage2D();
  ctx.SetPushConstants(multiscatteringPush);
  ctx.ImageBarrier(transmittanceLut.value(), VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  ctx.DispatchInvocations(multiscatteringLut.value().GetCreateInfo().extent);

  ctx.BindComputePipeline(skyViewPipeline.GetPipeline());
  SkyViewPush skyViewPush;
  skyViewPush.globalUniformsIndexSkyView = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index;
  skyViewPush.transmittanceTexture = transmittanceLut.value().ImageView().GetTexture2D();
  skyViewPush.multiscatteringTexture = multiscatteringLut.value().ImageView().GetTexture2D();
  skyViewPush.multiscatteringTransmittanceSampler = linearClampSampler;
  skyViewPush.skyViewImage = skyViewLut.value().ImageView().GetImage2D();
  ctx.SetPushConstants(skyViewPush);
  ctx.ImageBarrier(multiscatteringLut.value(), VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  ctx.DispatchInvocations(skyViewLut.value().GetCreateInfo().extent);

  auto drawCalls       = std::vector<GpuMesh*>();
  auto meshUniformzVec = std::vector<Temp::ObjectUniforms>();
  for (auto&& [entity, transform, mesh] : world.GetRegistry().view<const GlobalTransform, const Mesh>().each())
  {
    GlobalTransform actualTransform = transform;
    if (auto* renderTransform = world.GetRegistry().try_get<const RenderTransform>(entity))
    {
      actualTransform = renderTransform->transform;
    }
    auto worldFromObject = glm::translate(glm::mat4(1), actualTransform.position) * glm::mat4_cast(actualTransform.rotation) *
                           glm::scale(glm::mat4(1), glm::vec3(actualTransform.scale));
    auto& gpuMesh = g_meshes[mesh.name];
    auto tint     = glm::vec3(1);
    if (auto* tp = world.GetRegistry().try_get<const Tint>(entity))
    {
      tint = tp->color;
    }
    meshUniformzVec.emplace_back(worldFromObject, gpuMesh.vertexBuffer->GetDeviceAddress(), tint);
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

  if (world.GetRegistry().ctx().contains<TwoLevelGrid>())
  {
    auto lines           = std::vector<Debug::Line>();
    const auto& ecsLines = world.GetRegistry().ctx().get<std::vector<Debug::Line>>();
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

    auto& grid        = world.GetRegistry().ctx().get<TwoLevelGrid>();
    const auto voxels = Voxels{
      .topLevelBricksDims         = grid.topLevelBricksDims_,
      .topLevelBrickPtrsBaseIndex = grid.topLevelBrickPtrsBaseIndex,
      .dimensions                 = grid.dimensions_,
      .bufferIdx                  = grid.buffer.GetGpuBuffer().GetResourceHandle().index,
      .materialBufferIdx          = voxelMaterialBuffer->GetResourceHandle().index,
      .voxelSampler               = voxelSampler,
      .numLights                  = (uint32_t)lights.size(),
      .lightBufferIdx             = lights.empty() ? 0 : lightBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .globalUniformsIndex        = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
    };

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
          ddgi.args.gridInfo[i].probes        = ddgi.probeDataBuffers[i].value().GetDeviceAddress();
          ddgi.args.gridInfo[i].oldGridOffset = ddgi.args.gridInfo[i].gridOffset;
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
      .texture = frame.sceneAlbedo.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto normalAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneNormal.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto radianceAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneRadiance.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    Fvog::RenderColorAttachment colorAttachments[] = {albedoAttachment, normalAttachment, radianceAttachment};
    auto depthAttachment = Fvog::RenderDepthStencilAttachment{
      .texture = frame.sceneDepth.value().ImageView(),
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
    }
    ctx.EndRendering();

    ctx.ImageBarrier(*frame.sceneAlbedo, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.sceneNormal, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.sceneRadiance, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.sceneDepth, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.sceneIlluminance, VK_IMAGE_LAYOUT_GENERAL);

    // Indirect illuminance
    if (giMethod_ == GIMethod::PerPixelPathTracing)
    {
      ctx.BindComputePipeline(perPixelPathtracerPipeline.GetPipeline());
      ctx.SetPushConstants(PerPixelPathtracerArguments{
        .voxels              = voxels,
        .gDepth              = frame.sceneDepth->ImageView().GetTexture2D(),
        .gNormal             = frame.sceneNormal->ImageView().GetTexture2D(),
        .gIndirectIrradiance = frame.sceneIlluminance->ImageView().GetImage2D(),
        .internalColorSpace  = COLOR_SPACE_sRGB_LINEAR,
        .uniformBufferIndex  = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .noiseTexture        = noiseTexture->ImageView().GetTexture2D(),
        .samples             = uint32_t(pathTracerSamples),
        .bounces             = uint32_t(pathTracerBounces),
      });
      ctx.DispatchInvocations(frame.sceneIlluminance->GetCreateInfo().extent);

      // Denoise. Issues barriers internally.
      bilateral_.DenoiseIlluminance(
        {
          .sceneAlbedo              = &frame.sceneAlbedo.value(),
          .sceneNormal              = &frame.sceneNormal.value(),
          .sceneDepth               = &frame.sceneDepth.value(),
          .sceneIlluminance         = &frame.sceneIlluminance.value(),
          .sceneIlluminancePingPong = &frame.sceneIlluminancePingPong.value(),
          .clip_from_view           = clip_from_view,
          .world_from_clip          = glm::inverse(clip_from_world),
          .cameraPos                = position,
        },
        commandBuffer);
    }
    
    ctx.ImageBarrier(*frame.sceneIlluminance, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

    // Spelunker effect, if active
    const bool applySpelunkerEffect = GetTotalEffectOnEntity(world, player, ItemDefinition::EffectType::Spelunker, 0) > 0;
    if (applySpelunkerEffect)
    {
      auto voxels2              = voxels;
      voxels2.materialBufferIdx = voxelMaterialBufferSpelunker->GetResourceHandle().index;

      ctx.BindComputePipeline(spelunkerEffectPipeline.GetPipeline());
      ctx.SetPushConstants(ShadingPushConstants{
        .voxels               = voxels2,
        .gAlbedo              = frame.sceneAlbedo->ImageView().GetTexture2D(),
        .gDepth               = frame.sceneDepth->ImageView().GetTexture2D(),
        .gNormal              = frame.sceneNormal->ImageView().GetTexture2D(),
        .gRadiance            = frame.sceneRadiance->ImageView().GetTexture2D(),
        .gIndirectIlluminance = frame.sceneIlluminance->ImageView().GetTexture2D(),
        .gSpecial             = frame.sceneSpecial->ImageView().GetUImage2D(),
        .sceneColor           = frame.sceneColor->ImageView().GetImage2D(),
        .internalColorSpace   = COLOR_SPACE_sRGB_LINEAR,
        .uniformBufferIndex   = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .ddgi                 = ddgi.argsBuffer.value().GetDeviceBuffer().GetDeviceAddress(),
        .samplerr             = linearClampSampler,
        .giMethod             = uint32_t(giMethod_),
      });
      ctx.ImageBarrierDiscard(*frame.sceneSpecial, VK_IMAGE_LAYOUT_GENERAL);
      ctx.DispatchInvocations(frame.sceneColor->GetCreateInfo().extent);
      ctx.Barrier();
    }

    Fvog::Texture* aoTexture = &whiteTexture_.value();
    if (giMethod_ == GIMethod::DDGI && enableAo_)
    {
      aoParams_.voxels          = voxels;
      aoParams_.inputDepth      = &frame.sceneDepth.value();
      aoParams_.inputNormal     = &frame.sceneNormal.value();
      aoParams_.outputSize      = {frame.sceneAlbedo->GetCreateInfo().extent.width, frame.sceneAlbedo->GetCreateInfo().extent.height};
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
        .gAlbedo              = frame.sceneAlbedo->ImageView().GetTexture2D(),
        .gDepth               = frame.sceneDepth->ImageView().GetTexture2D(),
        .gNormal              = frame.sceneNormal->ImageView().GetTexture2D(),
        .gRadiance            = frame.sceneRadiance->ImageView().GetTexture2D(),
        .gIndirectIlluminance = frame.sceneIlluminance->ImageView().GetTexture2D(),
        .gSpecial             = frame.sceneSpecial->ImageView().GetUImage2D(),
        .sceneColor           = frame.sceneColor->ImageView().GetImage2D(),
        .internalColorSpace   = COLOR_SPACE_sRGB_LINEAR,
        .uniformBufferIndex   = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .ddgi                 = ddgi.argsBuffer.value().GetDeviceBuffer().GetDeviceAddress(),
        .samplerr             = linearClampSampler,
        .giMethod             = uint32_t(giMethod_),
        .applySpelunkerEffect = applySpelunkerEffect,
        .ambientOcclusion     = aoTexture->ImageView().GetTexture2D(),
      });
      ctx.DispatchInvocations(frame.sceneColor->GetCreateInfo().extent);
    }

    ctx.Barrier();
    ctx.ImageBarrier(frame.sceneColor.value(), VK_IMAGE_LAYOUT_GENERAL);
    ctx.ImageBarrier(frame.sceneDepth.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

    if (!debugDisableFog)
    {
      auto markerFog = ctx.MakeScopedDebugMarker("Froxel fog");

      ctx.ImageBarrierDiscard(inScatteringAndTransmittanceVolume.value(), VK_IMAGE_LAYOUT_GENERAL);
      ctx.ImageBarrierDiscard(fogColorAndDensityVolume.value(), VK_IMAGE_LAYOUT_GENERAL);
      const auto nearVolume            = 1.5f;
      const auto farVolume             = 1000.0f;
      const auto clip_from_view_volume = glm::perspectiveZO(fovy, aspectRatio, nearVolume, farVolume);
      fog_.UpdateUniforms(commandBuffer,
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
          .inSceneLuminance                     = frame.sceneColor->ImageView().GetTexture2D(),
          .gDepth                               = frame.sceneDepth->ImageView().GetTexture2D(),
          .inScatteringAndTransmittanceVolume   = inScatteringAndTransmittanceVolume->ImageView().GetTexture3D(),
          .fogDensityVolume                     = fogColorAndDensityVolume->ImageView().GetTexture3D(),
          .blueNoise                            = noiseTexture->ImageView().GetTexture2D(),
          .inScatteringAndTransmittanceVolumeRW = inScatteringAndTransmittanceVolume->ImageView().GetImage3D(),
          .fogDensityVolumeRW                   = fogColorAndDensityVolume->ImageView().GetImage3D(),
          .outSceneLuminance                    = frame.sceneColor->ImageView().GetImage2D(),
          .linearSampler                        = linearClampSampler,
          //.mieScattering                        = ,
          .ddgi   = ddgi.argsBuffer.value().GetDeviceBuffer().GetDeviceAddress(),
          .voxels = voxels,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .sunSelfShadowSteps = sunSelfShadowSteps,
          .sunSelfShadowDist = sunSelfShadowDist,
        });
      fog_.InjectFog(commandBuffer, fogColorAndDensityVolume.value());
      fog_.MarchVolume(commandBuffer, fogColorAndDensityVolume.value(), inScatteringAndTransmittanceVolume.value());
      fog_.ApplyDeferred(commandBuffer, frame.sceneColor.value(), frame.sceneDepth.value(), frame.sceneColor.value(), inScatteringAndTransmittanceVolume.value());
    }
    ctx.Barrier();
  }

  // DDGI debug probes.
  if (ddgiDebugView_ != DDGIDebugView::None)
  {
    //auto marker = ctx.MakeScopedDebugMarker("DDGI Debug Probes");
    auto sceneColorAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneColor.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD,
    };
    auto depthAttachment = Fvog::RenderDepthStencilAttachment{
      .texture    = frame.sceneDepth.value().ImageView(),
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
        .image           = frame.sceneColor.value(),
        .exposureBuffer  = exposureBuffer,
        .deltaTime       = float(dt),
        .adjustmentSpeed = 1,
        .targetLuminance = 0.2140f,
        .logMinLuminance = -15.0f,
        .logMaxLuminance = 15.0f,
      });
  }

  ctx.Barrier();

  if (enableBloom)
  {
    ZoneScopedN("Bloom");
    bloom_.Apply(commandBuffer,
      {
        .target                      = frame.sceneColor.value(),
        .scratchTexture              = frame.sceneColorBloomScratch.value(),
        .passes                      = 6,
        .strength                    = 1.0f / 16.0f,
        .width                       = 1,
        .useLowPassFilterOnFirstPass = true,
      });
  }

  ctx.ImageBarrier(frame.sceneColor.value(), VK_IMAGE_LAYOUT_GENERAL);
  ctx.ImageBarrierDiscard(frame.sceneColorTonemapped.value(), VK_IMAGE_LAYOUT_GENERAL);

  // Tonemap
  {
    ZoneScopedN("Tonemap");
    ctx.BindComputePipeline(tonemapPipeline.GetPipeline());
    ctx.SetPushConstants(shared::TonemapArguments{
      .sceneColor = frame.sceneColor->ImageView().GetTexture2D(),
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
