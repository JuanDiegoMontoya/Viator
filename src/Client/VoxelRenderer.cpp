#include "VoxelRenderer.h"

#include "Game/Assets.h"
#include "MathUtilities.h"
#include "PipelineManager.h"
#include "Fvog/Device.h"
#include "Fvog/Rendering2.h"
#include "Fvog/detail/Common.h"
#include "shaders/Config.shared.h"
#include "Core/Assert2.h"

#ifdef JPH_DEBUG_RENDERER
#include "Game/Physics/DebugRenderer.h"
#endif

#include "volk.h"
#include "Fvog/detail/ApiToEnum2.h"

#include "tiny_obj_loader.h"
#include "tracy/Tracy.hpp"
#include "tracy/TracyVulkan.hpp"
#include "stb_image.h"

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
  };
  FVOG_DECLARE_FLAG_TYPE(VoxelMaterialFlags, MaterialFlagBit, uint32_t);

  struct GpuVoxelMaterial
  {
    VoxelMaterialFlags materialFlags;
    shared::Texture2D baseColorTexture;
    glm::vec3 baseColorFactor;
    shared::Texture2D emissionTexture;
    glm::vec3 emissionFactor;
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

  [[nodiscard]] Fvog::Texture LoadImageFile(const std::filesystem::path& path)
  {
    stbi_set_flip_vertically_on_load(1);
    int x            = 0;
    int y            = 0;
    const auto pixels = stbi_load((GetTextureDirectory() / path).string().c_str(), &x, &y, nullptr, 4);
    assert(pixels);
    auto texture = Fvog::CreateTexture2D({static_cast<uint32_t>(x), static_cast<uint32_t>(y)}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::READ_ONLY, path.string());
    texture.UpdateImageSLOW({
      .extent = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
      .data   = pixels,
    });
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
  g_meshes.emplace("ar15", LoadObjFile(GetAssetDirectory() / "models/ar15.obj"));
  g_meshes.emplace("tracer", LoadObjFile(GetAssetDirectory() / "models/tracer.obj"));
  g_meshes.emplace("cube", LoadObjFile(GetAssetDirectory() / "models/cube.obj"));
  g_meshes.emplace("spear", LoadObjFile(GetAssetDirectory() / "models/spear.obj"));
  g_meshes.emplace("pickaxe", LoadObjFile(GetAssetDirectory() / "models/pickaxe.obj"));
  g_meshes.emplace("axe", LoadObjFile(GetAssetDirectory() / "models/axe.obj"));
  g_meshes.emplace("torch", LoadObjFile(GetAssetDirectory() / "models/torch.obj"));
  g_meshes.emplace("mushroom", LoadObjFile(GetAssetDirectory() / "models/mushroom.obj"));

  head_->renderCallback_ = [this](float dt, World& world, VkCommandBuffer cmd, uint32_t swapchainImageIndex) { OnRender(dt, world, cmd, swapchainImageIndex); };
  head_->framebufferResizeCallback_ = [this](uint32_t newWidth, uint32_t newHeight) { OnFramebufferResize(newWidth, newHeight); };
  head_->guiCallback_ = [this](DeltaTime dt, World& world, VkCommandBuffer cmd) { OnGui(dt, world, cmd); };

  testPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
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
            .depthWriteEnable = false,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
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
            .depthWriteEnable = false,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  noiseTexture = LoadImageFile("bluenoise256.png");

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
      gpuMat.baseColorTexture = GetOrEmplaceCachedTexture(*desc.baseColorTexture).ImageView().GetTexture2D();
    }
    if (desc.emissionTexture)
    {
      gpuMat.materialFlags |= MaterialFlagBit::HAS_EMISSION_TEXTURE;
      gpuMat.emissionTexture = GetOrEmplaceCachedTexture(*desc.emissionTexture).ImageView().GetTexture2D();
    }
    if (desc.isInvisible)
    {
      gpuMat.materialFlags |= MaterialFlagBit::IS_INVISIBLE;
    }

    voxelMaterials.emplace_back(gpuMat);
  }

  voxelMaterialBuffer = Fvog::Buffer({.size = voxelMaterials.size() * sizeof(GpuVoxelMaterial), .flag = Fvog::BufferFlagThingy::NONE}, "Voxel Material Buffer");
  Fvog::GetDevice().ImmediateSubmit([&](VkCommandBuffer cmd) { voxelMaterialBuffer->UpdateDataExpensive(cmd, std::span(voxelMaterials)); });
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

  if (auto gameState = world.GetRegistry().ctx().get<GameState>(); gameState == GameState::GAME || gameState == GameState::PAUSED)
  {
    ctx.Barrier();
    RenderGame(dt, world, commandBuffer);
    ctx.Barrier();
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
    .textureIndex = frame.sceneColor->ImageView().GetSampledResourceHandle().index,
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

  ctx.Barrier();

  auto viewMat  = glm::mat4(1);
  auto position = glm::vec3();
  for (auto&& [entity, inputLook, transform] : world.GetRegistry().view<const InputLookState, const GlobalTransform, const LocalPlayer>().each())
  {
    position = transform.position;
    // Flip z axis to correspond with Vulkan's NDC space.
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
  const auto clip_from_view  = Math::InfReverseZPerspectiveRH(glm::radians(65.0f), (float)head_->windowFramebufferWidth / head_->windowFramebufferHeight, 0.1f);
  const auto clip_from_world = clip_from_view * view_from_world;

  ctx.ImageBarrierDiscard(frame.sceneAlbedo.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneNormal.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneIlluminance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneRadiance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneDepth.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  perFrameUniforms.UpdateData(commandBuffer,
    Temp::Uniforms{
      .viewProj               = clip_from_world,
      .oldViewProjUnjittered  = glm::mat4{},
      .viewProjUnjittered     = glm::mat4{},
      .invViewProj            = glm::inverse(clip_from_world),
      .proj                   = glm::mat4{},
      .invProj                = glm::mat4{},
      .cameraPos              = glm::vec4(position, 0),
      .meshletCount           = 0,
      .maxIndices             = 0,
      .bindlessSamplerLodBias = 0,
      .flags                  = 0,
      .alphaHashScale         = 0,
    });

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
      GetOrEmplaceCachedTexture(billboardSprite.name).ImageView().GetTexture2D());
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

    auto& grid            = world.GetRegistry().ctx().get<TwoLevelGrid>();
    auto albedoAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneAlbedo.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto normalAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneNormal.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto illuminanceAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneIlluminance.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto radianceAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneRadiance.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    Fvog::RenderColorAttachment colorAttachments[] = {albedoAttachment, normalAttachment, illuminanceAttachment, radianceAttachment};
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
    const auto voxels = Temp::Voxels{
      .topLevelBricksDims         = grid.topLevelBricksDims_,
      .topLevelBrickPtrsBaseIndex = grid.topLevelBrickPtrsBaseIndex,
      .dimensions                 = grid.dimensions_,
      .bufferIdx                  = grid.buffer.GetGpuBuffer().GetResourceHandle().index,
      .materialBufferIdx          = voxelMaterialBuffer->GetResourceHandle().index,
      .voxelSampler               = voxelSampler,
      .numLights                  = (uint32_t)lights.size(),
      .lightBufferIdx             = lights.empty() ? 0 : lightBuffer->GetDeviceBuffer().GetResourceHandle().index,
    };
    {
      // Voxels
      TracyVkZone(head_->tracyVkContext_, commandBuffer, "Voxels");
      ctx.BindGraphicsPipeline(testPipeline.GetPipeline());
      ctx.SetPushConstants(Temp::PushConstants{
        .voxels = voxels,
        .uniformBufferIndex         = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .noiseTexture = noiseTexture->ImageView().GetTexture2D(),
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
        .voxels       = voxels,
        .noiseTexture = noiseTexture->ImageView().GetTexture2D(),
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
  }

  bilateral_.DenoiseIlluminance(
    {
      .sceneAlbedo              = &frame.sceneAlbedo.value(),
      .sceneNormal              = &frame.sceneNormal.value(),
      .sceneDepth               = &frame.sceneDepth.value(),
      .sceneRadiance            = &frame.sceneRadiance.value(),
      .sceneIlluminance         = &frame.sceneIlluminance.value(),
      .sceneIlluminancePingPong = &frame.sceneIlluminancePingPong.value(),
      .sceneColor               = &frame.sceneColor.value(),
      .clip_from_view           = clip_from_view,
      .world_from_clip          = glm::inverse(clip_from_world),
      .cameraPos                = position,
    },
    commandBuffer);
}

Fvog::Texture& VoxelRenderer::GetOrEmplaceCachedTexture(const std::string& name)
{
  if (auto it = stringToTexture.find(name); it != stringToTexture.end())
  {
    return it->second;
  }

  // Make a very sketchy and unscalable assumption about the path.
  auto texture = LoadImageFile(std::filesystem::path("voxels") / (name + ".png"));

  return stringToTexture.emplace(name, std::move(texture)).first->second;
}
