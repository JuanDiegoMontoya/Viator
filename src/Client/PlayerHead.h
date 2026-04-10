#pragma once
#include "Game/Head.h"
#include "Core/ClassImplMacros.h"
#include "Client/Input.h"

#include "vulkan/vulkan_core.h"
#include "VkBootstrap.h"
#include "glm/vec2.hpp"

#include <span>
#include <functional>

struct GLFWwindow;

class VoxelRenderer;
class PlayerAudio;

namespace tracy
{
  class VkCtx;
}

// List of functions to execute in reverse order in its destructor
class DestroyList2
{
public:
  DestroyList2() = default;
  void Push(std::function<void()> fn);
  void Terminate();
  ~DestroyList2();

  NO_COPY_NO_MOVE(DestroyList2);

private:
  std::vector<std::function<void()>> destructorList;
};

class PlayerHead final : public Head
{
public:
  NO_COPY_NO_MOVE(PlayerHead);
  void VariableUpdatePre(DeltaTime dt, World& world) override;
  void VariableUpdatePost(DeltaTime dt, World& world) override;
  void CreateRenderingMaterials(const World& world) override;
  void RegisterParticleArchetype(std::string name, const Game2::Render::ParticleArchetype& archetype) override;
  void SpawnParticles(std::span<const Game2::Render::Particle> particles) override;
  void SpawnParticleArchetypes(std::span<const Game2::Render::ParticleArchetypeSpawnInfo> archetypeSpawnInfos) override;
  Audio* GetAudio() override;

  struct CreateInfo
  {
    std::string_view name        = "";
    bool maximize                = false;
    bool decorate                = true;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  };

  PlayerHead(const CreateInfo& createInfo);

  ~PlayerHead() override;

  struct PerSwapchainImageData
  {
    VkSemaphore renderSemaphore;
  };

private:
  friend class VoxelRenderer; // TODO: HACK
  // Create swapchain size-dependent resources
  std::function<void(uint32_t newWidth, uint32_t newHeight)> framebufferResizeCallback_;
  std::function<void(DeltaTime dt, World& world, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)> renderCallback_;
  std::function<void(DeltaTime dt, World& world, VkCommandBuffer commandBuffer)> guiCallback_;

  // destroyList will be the last object to be automatically destroyed after the destructor returns
  DestroyList2 destroyList_;
  vkb::Instance instance_{};
  VkSurfaceKHR surface_{};
  VkDescriptorPool imguiDescriptorPool_{};
  vkb::Swapchain swapchain_{};
  std::vector<VkImage> swapchainImages_;
  std::vector<VkImageView> swapchainImageViews_;
  std::vector<VkSurfaceFormatKHR> availableSurfaceFormats_;
  std::vector<VkPresentModeKHR> availablePresentModes_;
  static constexpr VkSurfaceFormatKHR defaultSwapchainFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  VkSurfaceFormatKHR swapchainFormat_                        = defaultSwapchainFormat; // Only Application should modify this
  VkSurfaceFormatKHR nextSwapchainFormat_ =
    swapchainFormat_; // Workaround to prevent ImGui backend from using incorrect pipeline layout after changing swapchainFormat in GUI
  float maxDisplayNits = 200.0f;

  tracy::VkCtx* tracyVkContext_{};
  GLFWwindow* window;

  uint32_t windowFramebufferWidth{};
  uint32_t windowFramebufferHeight{};

  // Resizing from UI is deferred until next frame so texture handles remain valid when ImGui is rendered
  bool shouldResizeNextFrame          = false;
  bool shouldRemakeSwapchainNextFrame = false;
  VkPresentModeKHR presentMode;
  static constexpr uint32_t numSwapchainImages = 3;

  std::vector<PerSwapchainImageData> perSwapchainImageData;

  friend class ApplicationAccess2;

  void RemakeSwapchain(uint32_t newWidth, uint32_t newHeight);
  void Draw(DeltaTime dt);
  World* worldThisFrame_{};
  
  bool swapchainOk = true;
  std::unique_ptr<VoxelRenderer> voxelRenderer_;
  std::unique_ptr<InputSystem> inputSystem_;
  std::unique_ptr<PlayerAudio> audio_;
};