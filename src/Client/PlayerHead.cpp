#include "PlayerHead.h"

#define GLFW_INCLUDE_VULKAN

#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Assets.h"
#include "Game/Globals.h"

#include "Fvog/Device.h"
#include "Fvog/Buffer2.h"
#include "Fvog/Pipeline2.h"
#include "Fvog/Rendering2.h"
#include "Fvog/Shader2.h"
#include "Fvog/Texture2.h"
#include "Fvog/detail/ApiToEnum2.h"
#include "Fvog/detail/Common.h"
#include "PipelineManager.h"
#include "VoxelRenderer.h"
#include "PlayerAudio.h"
#include "Core/Timer.h"
#include "Core/Defer.h"

#include "ImGui/imgui_impl_fvog.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <implot.h>

#include <glm/gtc/constants.hpp>

#include <volk.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include "spdlog/spdlog.h"

#include "stb_image.h"
#include "Core/Platform/Sleep.h"

#include <GLFW/glfw3.h>
#include <VkBootstrap.h>
#include <glslang/Public/ShaderLang.h>

#include <bit>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace
{
  VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*)
  {
    if (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
    {
      return VK_FALSE;
    }

    //auto ms = vkb::to_string_message_severity(messageSeverity);
    //auto mt = vkb::to_string_message_type(messageType);
    //printf("[%s: %s]\n%s\n", ms, mt, pCallbackData->pMessage);

    auto level = spdlog::level::err;
    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
      level = spdlog::level::warn;
    }
    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
      level = spdlog::level::debug;
    }
    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
    {
      level = spdlog::level::trace;
    }
    spdlog::log(level, "{}", pCallbackData->pMessage);

    return VK_FALSE;
  }

  std::vector<VkImageView> MakeSwapchainImageViews(VkDevice device, std::span<const VkImage> swapchainImages, VkFormat format)
  {
    auto imageViews = std::vector<VkImageView>();
    for (int i = 0; auto image : swapchainImages)
    {
      VkImageView imageView{};
      Fvog::detail::CheckVkResult(vkCreateImageView(device,
        Fvog::detail::Address(VkImageViewCreateInfo{
          .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image    = image,
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format   = format,
          .subresourceRange =
            VkImageSubresourceRange{
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .levelCount = 1,
              .layerCount = 1,
            },
        }),
        nullptr,
        &imageView));
      imageViews.emplace_back(imageView);

      // TODO: gate behind compile-time switch
      vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
        Fvog::detail::Address(VkDebugUtilsObjectNameInfoEXT{
          .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
          .objectType   = VK_OBJECT_TYPE_IMAGE,
          .objectHandle = reinterpret_cast<uint64_t>(image),
          .pObjectName  = (std::string("Swapchain Image ") + std::to_string(i)).c_str(),
        }));
      vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
        Fvog::detail::Address(VkDebugUtilsObjectNameInfoEXT{
          .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
          .objectType   = VK_OBJECT_TYPE_IMAGE_VIEW,
          .objectHandle = reinterpret_cast<uint64_t>(imageView),
          .pObjectName  = (std::string("Swapchain Image View ") + std::to_string(i)).c_str(),
        }));

      i++;
    }
    return imageViews;
  }
} // namespace

// This class provides static callbacks for GLFW.
// It has access to the private members of PlayerHead and assumes a pointer to it is present in the window's user pointer.
class ApplicationAccess2
{
public:
  static void CursorPosCallback(GLFWwindow* window, double currentCursorX, double currentCursorY)
  {
    auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    app->inputSystem_->CursorPosCallback(currentCursorX, currentCursorY);
    // Prevent unwanted UI movement during gameplay.
    if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
      ImGui_ImplGlfw_CursorPosCallback(window, currentCursorX, currentCursorY);
    }
  }

  static void CursorEnterCallback(GLFWwindow* window, int entered)
  {
    auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    app->inputSystem_->CursorEnterCallback(entered);
    ImGui_ImplGlfw_CursorEnterCallback(window, entered);
  }

  static void FramebufferResizeCallback(GLFWwindow* window, int newWidth, int newHeight)
  {
    auto* app                    = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    app->windowFramebufferWidth  = newWidth;
    app->windowFramebufferHeight = newHeight;

    if (newWidth > 0 && newHeight > 0)
    {
      app->RemakeSwapchain(newWidth, newHeight);
      // app->shouldResizeNextFrame = true;
      // app->Draw(0.016);
    }
  }

  static void PathDropCallback(GLFWwindow*, int, const char**)
  {
    //auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    //app->OnPathDrop({paths, static_cast<size_t>(count)});
  }

  static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset)
  {
    auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    app->inputSystem_->ScrollCallback(xOffset, yOffset);
    // Prevent unwanted UI movement during gameplay.
    if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
      ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);
    }
  }

  static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
  {
    [[maybe_unused]] auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    // Prevent unwanted UI movement during gameplay.
    if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
    {
      ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    }
  }

  static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
  {
    [[maybe_unused]] auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
  }

  static void CharCallback(GLFWwindow* window, unsigned c)
  {
    [[maybe_unused]] auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    ImGui_ImplGlfw_CharCallback(window, c);
  }

  static void WindowFocusCallback(GLFWwindow* window, int focused)
  {
    [[maybe_unused]] auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    ImGui_ImplGlfw_WindowFocusCallback(window, focused);
  }

  static void MonitorCallback(GLFWmonitor* monitor, int event)
  {
    ImGui_ImplGlfw_MonitorCallback(monitor, event);
  }
};

static auto MakePerSwapchainImageData(uint32_t count)
{
  auto datas = std::vector<PlayerHead::PerSwapchainImageData>();
  for (uint32_t i = 0; i < count; i++)
  {
    auto data = PlayerHead::PerSwapchainImageData{};
    Fvog::detail::CheckVkResult(vkCreateSemaphore(Fvog::GetDevice().device_,
      Fvog::detail::Address(VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      }),
      nullptr,
      &data.presentSemaphore));

    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
      Fvog::detail::Address(VkDebugUtilsObjectNameInfoEXT{
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType   = VK_OBJECT_TYPE_SEMAPHORE,
        .objectHandle = reinterpret_cast<uint64_t>(data.presentSemaphore),
        .pObjectName  = ("Render semaphore #" + std::to_string(i)).c_str(),
      }));

    datas.push_back(data);
  }
  return datas;
}

static void CleanupPerSwapchainImageData(std::span<PlayerHead::PerSwapchainImageData> datas)
{
  for (auto& data : datas)
  {
    vkDestroySemaphore(Fvog::GetDevice().device_, data.presentSemaphore, nullptr);
  }
}

static auto MakeVkbSwapchain(const vkb::Device& device,
  uint32_t width,
  uint32_t height,
  [[maybe_unused]] VkPresentModeKHR presentMode,
  uint32_t imageCount,
  VkSwapchainKHR oldSwapchain,
  VkSurfaceFormatKHR format)
{
  spdlog::info("Creating swapchain with size {}x{} and {} images", width, height, imageCount);
  return vkb::SwapchainBuilder{device}
    .set_desired_min_image_count(imageCount)
    .set_old_swapchain(oldSwapchain)
    .set_desired_present_mode(presentMode)
    .add_fallback_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
    .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
    .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_RELAXED_KHR)
    .add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
    .set_desired_extent(width, height)
    .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    .set_desired_format(format)
    .build()
    .value();
}

void PlayerHead::VariableUpdatePre(DeltaTime dt, World& world, bool willHaveGameTick)
{
  ZoneScoped;
  worldThisFrame_ = &world;

  if (audio_)
  {
    audio_->FreeUnusedResources();
  }

  if (enableFramePacing && (activePresentMode == VK_PRESENT_MODE_FIFO_KHR || activePresentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR))
  {
    ZoneScopedN("Anti lag at home (sleep)");

    // Based on https://codeberg.org/Games-by-Mason/mr_gpu/src/branch/main/src/ext/FramePacer.zig
    const float refreshPeriod   = 1.0f / videoMode->refreshRate;
    const float maxFrameTime    = refreshPeriod + static_cast<float>(framePacingOvershootTolerance.Get() / 1000.0);
    const float overshootAmount = dt.real - maxFrameTime;
    // Only back off if we are sure that our sleep caused the overshoot rather than natural variance.
    if (overshootAmount > 0 && lastFrameSleepDuration > 0.1f / 1000.0f && overshootAmount < lastFrameSleepDuration)
    {
      ZoneColor(tracy::Color::Goldenrod);
      ZoneTextF("Overshot previous frame due to sleep.\n"
                "Frame time: %.3fms\n"
                "Slept: %.3fms\n"
                "Max frame time: %.3fms",
        dt.real * 1000.0f,
        lastFrameSleepDuration * 1000.0f,
        maxFrameTime * 1000.0f);
      framePacingSleepDuration *= static_cast<float>(framePacingOvershootScale.Get());
    }

    const float headroom      = static_cast<float>(framePacingHeadroom.Get()) / 1000.0f;
    const auto sleepTime      = glm::min(lastFrameSlopTime + framePacingSleepDuration - headroom, refreshPeriod - headroom);
    framePacingSleepDuration  = glm::max(glm::mix(framePacingSleepDuration, sleepTime, static_cast<float>(framePacingSmoothing.Get())), 0.0f);
    // Reduce sleep if there will be a game tick this frame.
    // The amount of reduction is based on an estimate from previous tick durations.
    if (willHaveGameTick)
    {
      ZoneTextF("Compensating for tick(s) this frame with estimated duration %.3fms", smoothGameTickDuration * 1000);
    }
    const auto finalSleepTime = glm::max(framePacingSleepDuration - willHaveGameTick * smoothGameTickDuration, 0.0f);
    lastFrameSleepDuration    = finalSleepTime;

    TracyPlot("Slop (ms)", lastFrameSlopTime * 1000.0f);
    TracyPlot("Sleep (ms)", finalSleepTime * 1000.0f);
    Core::Platform::PreciseSleep(finalSleepTime);
  }

  {
    // To minimize input latency, wait for the frame to become available *before* polling for input.
    ZoneScopedN("vkWaitSemaphores (graphics queue)");
    vkWaitSemaphores(Fvog::GetDevice().device_,
      Fvog::detail::Address(VkSemaphoreWaitInfo{
        .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores    = &Fvog::GetDevice().graphicsQueueTimelineSemaphore_,
        .pValues        = &Fvog::GetDevice().GetCurrentFrameData().renderTimelineSemaphoreWaitValue,
      }),
      UINT64_MAX);
    ZoneTextF("Frame index %u", Fvog::GetDevice().frameNumber % Fvog::Device::frameOverlap);
  }

  inputSystem_->VariableUpdatePre(dt, world, swapchainOk);
}

void PlayerHead::VariableUpdatePost(DeltaTime dt, World& world)
{
  ZoneScoped;
  if (world.globals->game->gameState == GameState::GAME || world.IsClient())
  {
    ZoneScopedN("Interpolate transforms");
    for (auto&& [entity, transform, rtransform] : world.GetRegistry().view<const GlobalTransform, const RenderTransform>().each())
    {
      if (!world.GetRegistry().all_of<RenderTransform>(entity))
      {
        continue;
      }

      if (const auto* previousGlobalTransform = world.GetRegistry().try_get<const PreviousGlobalTransform>(entity))
      {
        const auto alpha = dt.fraction;
        // Improve numerical stability when motionless.
        if (previousGlobalTransform->teleported)
        {
          world.GetRegistry().get<RenderTransform>(entity).transform = transform;

          auto& prevGlobalTransformMut = world.GetRegistry().get<PreviousGlobalTransform>(entity);
          prevGlobalTransformMut.teleported = false;
          prevGlobalTransformMut.position = transform.position;
          prevGlobalTransformMut.rotation = transform.rotation;
          prevGlobalTransformMut.scale = transform.scale;
        }
        else
        {
          auto& renderTransformMut = world.GetRegistry().get<RenderTransform>(entity);
          renderTransformMut.prevTransform = renderTransformMut.transform;
          auto& newRenderTransformMut = renderTransformMut.transform;
          
          if (previousGlobalTransform->position != transform.position || previousGlobalTransform->rotation != transform.rotation ||
              previousGlobalTransform->scale != transform.scale || rtransform.transform.position != transform.position)
          {
            if (previousGlobalTransform->position == transform.position)
            {
              newRenderTransformMut.position = transform.position;
            }
            else
            {
              newRenderTransformMut.position = glm::mix(previousGlobalTransform->position, transform.position, alpha);
            }
            newRenderTransformMut.rotation = glm::slerp(previousGlobalTransform->rotation, transform.rotation, alpha);
            newRenderTransformMut.scale = glm::mix(previousGlobalTransform->scale, transform.scale, alpha);
          }
        }
      }
      else
      {
        world.GetRegistry().get<RenderTransform>(entity).transform = transform;
      }

      if (const auto* emitter = world.GetRegistry().try_get<const SoundEmitter>(entity))
      {
        if (auto ptr = emitter->handle.lock())
        {
          ptr->SetPosition(rtransform.transform.position);
          if (const auto* velocity = world.GetRegistry().try_get<const LinearVelocity>(entity))
          {
            ptr->SetVelocity(velocity->v);
          }
        }
      }
    }

    if (auto entity = world.TryGetLocalPlayer(); entity != entt::null)
    {
      const auto& [transform, velocity] = world.GetRegistry().get<const RenderTransform, const LinearVelocity>(entity);
      GetAudio()->UpdateListener(transform.transform.position, GetForward(transform.transform.rotation), velocity.v);
    }
  }

  if (!swapchainOk)
  {
    return;
  }

  if (shouldRemakeSwapchainNextFrame)
  {
    swapchainFormat_ = nextSwapchainFormat_; // "Flush" nextSwapchainFormat_
    RemakeSwapchain(windowFramebufferWidth, windowFramebufferHeight);
    shouldRemakeSwapchainNextFrame = false;
  }

  if (windowFramebufferWidth > 0 && windowFramebufferHeight > 0)
  {
    Draw(dt);
  }

  if (glfwWindowShouldClose(window))
  {
    world.GetRegistry().ctx().emplace<CloseApplication>();
  }
}

void PlayerHead::UpdateGameTickTiming(float tickDuration_s)
{
  smoothGameTickDuration = glm::mix(smoothGameTickDuration, tickDuration_s, 0.1f);
}

void PlayerHead::CreateRenderingMaterials(const World& world)
{
  voxelRenderer_->CreateRenderingMaterials(world);
}

void PlayerHead::RegisterParticleArchetype(std::string name, const Game2::Render::ParticleArchetype& archetype)
{
  voxelRenderer_->RegisterParticleArchetype(std::move(name), archetype);
}

void PlayerHead::SpawnParticles(std::span<const Game2::Render::Particle> particles)
{
  voxelRenderer_->SpawnParticles(particles);
}

void PlayerHead::SpawnParticleArchetypes(std::span<const Game2::Render::ParticleArchetypeSpawnInfo> archetypeSpawnInfos)
{
  voxelRenderer_->SpawnParticleArchetypes(archetypeSpawnInfos);
}

void PlayerHead::SetWeather(const Weather::State& state)
{
  voxelRenderer_->SetWeather(state);
}

Audio* PlayerHead::GetAudio()
{
  static auto nullAudio = std::make_unique<NullAudio>();
  if (!audio_)
  {
    return nullAudio.get();
  }
  return audio_.get();
}

PlayerHead::PlayerHead(const CreateInfo& createInfo)
{
  ZoneScoped;

  {
    ZoneScopedN("Initialize GLFW");
    if (!glfwInit())
    {
      throw std::runtime_error("Failed to initialize GLFW");
    }
  }

  destroyList_.Push(
    []
    {
      ZoneScopedN("glfwTerminate");
      glfwTerminate();
    });

  glfwSetErrorCallback([](int, const char* desc) { std::cout << "GLFW error: " << desc << '\n'; });

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_MAXIMIZED, createInfo.maximize);
  glfwWindowHint(GLFW_DECORATED, createInfo.decorate);
  glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);

  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  if (monitor == nullptr)
  {
    throw std::runtime_error("No monitor detected");
  }
  videoMode = glfwGetVideoMode(monitor);
  {
    ZoneScopedN("Create Window");
    spdlog::info("Creating window");
    window = glfwCreateWindow(static_cast<int>(videoMode->width * .75), static_cast<int>(videoMode->height * .75), createInfo.name.c_str(), nullptr, nullptr);
    if (!window)
    {
      throw std::runtime_error("Failed to create window");
    }
  }

  int xSize{};
  int ySize{};
  glfwGetFramebufferSize(window, &xSize, &ySize);
  windowFramebufferWidth  = static_cast<uint32_t>(xSize);
  windowFramebufferHeight = static_cast<uint32_t>(ySize);

  int monitorLeft{};
  int monitorTop{};
  glfwGetMonitorPos(monitor, &monitorLeft, &monitorTop);

  glfwSetWindowPos(window, videoMode->width / 2 - windowFramebufferWidth / 2 + monitorLeft, videoMode->height / 2 - windowFramebufferHeight / 2 + monitorTop);

  glfwSetWindowUserPointer(window, this);

  glfwSetWindowFocusCallback(window, ApplicationAccess2::WindowFocusCallback);
  glfwSetCursorEnterCallback(window, ApplicationAccess2::CursorEnterCallback);
  glfwSetCursorPosCallback(window, ApplicationAccess2::CursorPosCallback);
  glfwSetMouseButtonCallback(window, ApplicationAccess2::MouseButtonCallback);
  glfwSetScrollCallback(window, ApplicationAccess2::ScrollCallback);
  glfwSetKeyCallback(window, ApplicationAccess2::KeyCallback);
  glfwSetCharCallback(window, ApplicationAccess2::CharCallback);
  glfwSetMonitorCallback(ApplicationAccess2::MonitorCallback);
  glfwSetDropCallback(window, ApplicationAccess2::PathDropCallback);
  glfwSetFramebufferSizeCallback(window, ApplicationAccess2::FramebufferResizeCallback);
  
  // Load app icon
  {
    int x             = 0;
    int y             = 0;
    const auto pixels = stbi_load((GetTextureDirectory() / "froge.png").string().c_str(), &x, &y, nullptr, 4);
    if (pixels)
    {
      const auto image = GLFWimage{
        .width  = x,
        .height = y,
        .pixels = pixels,
      };
      glfwSetWindowIcon(window, 1, &image);
      stbi_image_free(pixels);
    }
  }

  // Initialize Vulkan
  // instance
  {
    ZoneScopedN("Create Vulkan Instance");
    spdlog::info("Creating Vulkan instance");
    instance_ = vkb::InstanceBuilder()
                  .set_app_name("Frogrenderer")
                  .require_api_version(1, 3, 0)
                  .set_debug_callback(vulkan_debug_callback)
                  .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                  .enable_extension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME)
                  .build()
                  .value();

    destroyList_.Push(
      [this]
      {
        ZoneScopedN("vkDestroyInstance");
        vkb::destroy_instance(instance_);
      });
  }

  {
    ZoneScopedN("Initialize Volk");
    if (volkInitialize() != VK_SUCCESS)
    {
      throw std::runtime_error("rip");
    }

    destroyList_.Push(
      []
      {
        ZoneScopedN("volkFinalize()");
        volkFinalize();
      });

    volkLoadInstance(instance_);
  }

  // surface
  {
    ZoneScopedN("Create Window Surface");
    spdlog::info("Creating window surface");
    if (auto err = glfwCreateWindowSurface(instance_, window, nullptr, &surface_); err != VK_SUCCESS)
    {
      const char* error_msg;
      if (int ret = glfwGetError(&error_msg))
      {
        std::cout << ret << " ";
        if (error_msg != nullptr)
          std::cout << error_msg;
        std::cout << "\n";
      }
      throw std::runtime_error("rip");
    }
  }

  destroyList_.Push(
    [this]
    {
      ZoneScopedN("vkDestroySurfaceKHR()");
      vkDestroySurfaceKHR(instance_, surface_, nullptr);
    });

  // device
  {
    ZoneScopedN("Create Device");
    spdlog::info("Creating device");
    Fvog::CreateDevice(instance_, surface_);
  }

  {
    ZoneScopedN("Create Pipeline Manager");
    CreateGlobalPipelineManager();
  }

  // swapchain
  {
    ZoneScopedN("Create Swapchain");

    // Get available present modes for this surface
    uint32_t presentModeCount{};
    vkGetPhysicalDeviceSurfacePresentModesKHR(Fvog::GetDevice().physicalDevice_, surface_, &presentModeCount, nullptr);
    availablePresentModes_.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(Fvog::GetDevice().physicalDevice_, surface_, &presentModeCount, availablePresentModes_.data());

    // Get surface capabilities (namely minImageCount and maxImageCount) for each present mode.
    // Vulkan offers a way to query this information irrespective of the present mode (simply don't use 
    // pNext in VkSurfaceCapabilities2), but it provides less accurate limits.
    for (auto presentMode : availablePresentModes_)
    {
      auto surfaceInfo = VkPhysicalDeviceSurfaceInfo2KHR{
        .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        .surface = surface_,
      };
      auto surfacePresentMode = VkSurfacePresentModeKHR{
        .sType       = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR,
        .presentMode = presentMode,
      };
      auto surfaceCapabilities = VkSurfaceCapabilities2KHR{
        .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
        .pNext = &surfacePresentMode,
      };
      Fvog::detail::CheckVkResult(vkGetPhysicalDeviceSurfaceCapabilities2KHR(Fvog::GetDevice().physicalDevice_, &surfaceInfo, &surfaceCapabilities));
      presentModeSurfaceCapabilities_.emplace(presentMode, surfaceCapabilities.surfaceCapabilities);
    }

    ASSERT(presentModeSurfaceCapabilities_.contains(activePresentMode));
    const auto surfaceCapabilities = presentModeSurfaceCapabilities_[activePresentMode];
    numSwapchainImages = glm::max(surfaceCapabilities.minImageCount + numExtraSwapchainImages, surfaceCapabilities.minImageCount);
    // The spec allows maxImageCount to be 0 (unlimited), and some implementations (notably llvmpipe) actually report this, so we ought to check.
    if (surfaceCapabilities.maxImageCount > 0)
    {
      numSwapchainImages = glm::min(numSwapchainImages, surfaceCapabilities.maxImageCount);
    }
    swapchain_ = MakeVkbSwapchain(Fvog::GetDevice().device_,
      windowFramebufferWidth,
      windowFramebufferHeight,
      activePresentMode,
      numSwapchainImages,
      VK_NULL_HANDLE,
      swapchainFormat_);
    CleanupPerSwapchainImageData(perSwapchainImageData);
    perSwapchainImageData = MakePerSwapchainImageData(numSwapchainImages);

    swapchainImages_     = swapchain_.get_images().value();
    swapchainImageViews_ = MakeSwapchainImageViews(Fvog::GetDevice().device_, swapchainImages_, swapchainFormat_.format);

    // Get available formats for this surface
    uint32_t surfaceFormatCount{};
    vkGetPhysicalDeviceSurfaceFormatsKHR(Fvog::GetDevice().physicalDevice_, surface_, &surfaceFormatCount, nullptr);
    availableSurfaceFormats_.resize(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(Fvog::GetDevice().physicalDevice_, surface_, &surfaceFormatCount, availableSurfaceFormats_.data());
  }

  glslang::InitializeProcess();
  destroyList_.Push(
    []
    {
      ZoneScopedN("glslang::FinalizeProcess()");
      glslang::FinalizeProcess();
    });

  // Initialize Tracy
  tracyVkContext_ = TracyVkContextHostCalibrated(Fvog::GetDevice().physicalDevice_,
    Fvog::GetDevice().device_,
    vkResetQueryPool,
    vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
    vkGetCalibratedTimestampsEXT);

  // Initialize ImGui and a backend for it.
  // Because we allow the GLFW backend to install callbacks, it will automatically call our own that we provided.
  ImGui::CreateContext();
  destroyList_.Push(
    []
    {
      ZoneScopedN("ImGui::DestroyContext()");
      ImGui::DestroyContext();
    });
  ImPlot::CreateContext();
  destroyList_.Push(
    []
    {
      ZoneScopedN("ImPlot::DestroyContext()");
      ImPlot::DestroyContext();
    });
  ImGui_ImplGlfw_InitForVulkan(window, false);
  destroyList_.Push(
    []
    {
      ZoneScopedN("ImGui_ImplGlfw_Shutdown()");
      ImGui_ImplGlfw_Shutdown();
    });

  // ImGui may create many sets, but each will only have one combined image sampler
  vkCreateDescriptorPool(Fvog::GetDevice().device_,
    Fvog::detail::Address(VkDescriptorPoolCreateInfo{
      .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets       = 1234, // TODO: make this constant a variable
      .poolSizeCount = 1,
      .pPoolSizes    = Fvog::detail::Address(VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2}),
    }),
    nullptr,
    &imguiDescriptorPool_);

  auto imguiVulkanInitInfo = ImGui_ImplFvog_InitInfo{
    .Instance        = instance_,
    .PhysicalDevice  = Fvog::GetDevice().physicalDevice_,
    .QueueFamily     = Fvog::GetDevice().graphicsQueueFamilyIndex_,
    .Queue           = Fvog::GetDevice().graphicsQueue_,
    .DescriptorPool  = imguiDescriptorPool_,
    .MinImageCount   = swapchain_.image_count,
    .ImageCount      = swapchain_.image_count,
    .CheckVkResultFn = Fvog::detail::CheckVkResult,
  };

  ImGui_ImplFvog_LoadFunctions([](const char* functionName, void* vulkanInstance)
    { return vkGetInstanceProcAddr(*static_cast<VkInstance*>(vulkanInstance), functionName); },
    &instance_.instance);
  ImGui_ImplFvog_Init(&imguiVulkanInitInfo);
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  //audio_         = std::make_unique<PlayerAudio>();
  voxelRenderer_ = std::make_unique<VoxelRenderer>(this);
  inputSystem_   = std::make_unique<InputSystem>(window);

  // Inform the user that the renderer is done loading
  glfwRequestWindowAttention(window);
}

PlayerHead::~PlayerHead()
{
  ZoneScoped;

  {
    ZoneScopedN("voxelRenderer_.reset()");
    voxelRenderer_.reset();
  }

  // Must happen before device is destroyed, thus cannot go in the destroy list
  {
    ZoneScopedN("ImGui_ImplFvog_Shutdown()");
    ImGui_ImplFvog_Shutdown();
  }

  vkDestroyDescriptorPool(Fvog::GetDevice().device_, imguiDescriptorPool_, nullptr);

#ifdef TRACY_ENABLE
  DestroyVkContext(tracyVkContext_);
#endif

  {
    ZoneScopedN("vkb::destroy_swapchain()");
    vkb::destroy_swapchain(swapchain_);
  }

  for (auto view : swapchainImageViews_)
  {
    vkDestroyImageView(Fvog::GetDevice().device_, view, nullptr);
  }

  {
    ZoneScopedN("CleanupPerSwapchainImageData()");
    CleanupPerSwapchainImageData(perSwapchainImageData);
  }

  {
    ZoneScopedN("DestroyGlobalPipelineManager()");
    DestroyGlobalPipelineManager();
  }

  {
    ZoneScopedN("Fvog::DestroyDevice()");
    Fvog::DestroyDevice();
  }

  //audio_.reset();
  //destroyList_.Terminate();
}

void PlayerHead::Draw(DeltaTime dt)
{
  ZoneScoped;
  auto lock = std::scoped_lock(Fvog::GetDevice().copiumMutex_);

  auto _ = Defer([] { Fvog::GetDevice().frameNumber++; });
  auto& currentFrameData = Fvog::GetDevice().GetCurrentFrameData();

  {
    ZoneScopedN("vkWaitSemaphores (graphics queue timeline)");
    vkWaitSemaphores(Fvog::GetDevice().device_,
      Fvog::detail::Address(VkSemaphoreWaitInfo{
        .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores    = &Fvog::GetDevice().graphicsQueueTimelineSemaphore_,
        .pValues        = &currentFrameData.renderTimelineSemaphoreWaitValue,
      }),
      UINT64_MAX);
    ZoneTextF("Frame index %u", Fvog::GetDevice().frameNumber % Fvog::Device::frameOverlap);
  }
  
  // Garbage collection
  Fvog::GetDevice().FreeUnusedResources();

  uint32_t swapchainImageIndex{};

  lastFrameSlopTime = 0;

  {
    ZoneScopedN("vkAcquireNextImage2KHR");
    // https://gist.github.com/nanokatze/bb03a486571e13a7b6a8709368bd87cf#file-handling-window-resize-md
    auto acquireTime = Timer::Create();
    if (auto acquireResult = vkAcquireNextImage2KHR(Fvog::GetDevice().device_,
          Fvog::detail::Address(VkAcquireNextImageInfoKHR{
            .sType      = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
            .swapchain  = swapchain_,
            .timeout    = static_cast<uint64_t>(-1),
            .semaphore  = currentFrameData.acquireSemaphore,
            .deviceMask = 1,
          }),
          &swapchainImageIndex);
        acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
      swapchainOk = false;
    }
    else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
      throw std::runtime_error("vkAcquireNextImage failed with code " + std::to_string(acquireResult));
    }

    if (!swapchainOk)
    {
      return;
    }

    lastFrameSlopTime += static_cast<float>(acquireTime->Elapsed_s());
    ZoneTextF("Image index %u", swapchainImageIndex);
  }

  auto commandBuffer = currentFrameData.commandBuffer;

  {
    ZoneScopedN("vkResetCommandPool");
    Fvog::detail::CheckVkResult(vkResetCommandPool(Fvog::GetDevice().device_, currentFrameData.commandPool, 0));
  }

  {
    ZoneScopedN("vkBeginCommandBuffer");
    Fvog::detail::CheckVkResult(vkBeginCommandBuffer(commandBuffer,
      Fvog::detail::Address(VkCommandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      })));
  }

  auto ctx = Fvog::Context(commandBuffer);

  {
    ZoneScopedN("Begin ImGui frame");
    ImGui_ImplFvog_NewFrame();
    {
      ZoneScopedN("ImGui_ImplGlfw_NewFrame");
      ImGui_ImplGlfw_NewFrame();
    }
    {
      ZoneScopedN("ImGui::NewFrame");
      ImGui::NewFrame();
    }
  }

  {
    {
      if (renderCallback_)
      {
        TracyVkZone(tracyVkContext_, commandBuffer, "OnRender");
        renderCallback_(dt, *worldThisFrame_, commandBuffer, swapchainImageIndex);
      }
    }
    {
      if (guiCallback_)
      {
        TracyVkZone(tracyVkContext_, commandBuffer, "OnGui");
        guiCallback_(dt, *worldThisFrame_, commandBuffer);
      }
    }
  }

  // Render ImGui
  // A frame marker is inserted to distinguish ImGui rendering from the application's in a debugger.
  {
    ZoneScopedN("Draw UI");
    auto marker = ctx.MakeScopedDebugMarker("ImGui");
    ImGui::Render();
    auto* drawData = ImGui::GetDrawData();
    if (drawData->CmdListsCount > 0)
    {
      ctx.Barrier();
      vkCmdBeginRendering(commandBuffer,
        Fvog::detail::Address(VkRenderingInfo{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
          .renderArea                                = {{}, {windowFramebufferWidth, windowFramebufferHeight}},
          .layerCount                                = 1,
          .colorAttachmentCount                      = 1,
          .pColorAttachments                         = Fvog::detail::Address(VkRenderingAttachmentInfo{
                                    .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                    .imageView   = swapchainImageViews_[swapchainImageIndex],
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
                                    .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
          })}));
      // auto marker = Fwog::ScopedDebugMarker("Draw GUI");
      const bool isSurfaceHDR =
        swapchainFormat_.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT || swapchainFormat_.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
      ImGui_ImplFvog_RenderDrawData(drawData, commandBuffer, swapchainFormat_, isSurfaceHDR ? maxDisplayNits : 1);
      vkCmdEndRendering(commandBuffer);
    }

    ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  }

  ctx.Barrier();

  {
    TracyVkCollect(tracyVkContext_, commandBuffer);

    {
      ZoneScopedN("End Recording");
      Fvog::detail::CheckVkResult(vkEndCommandBuffer(commandBuffer));
    }

    {
      ZoneScopedN("Submit");
      const auto queueSubmitSignalSemaphores = std::array{
        VkSemaphoreSubmitInfo{
          .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = Fvog::GetDevice().graphicsQueueTimelineSemaphore_,
          .value     = Fvog::GetDevice().frameNumber,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        },
        VkSemaphoreSubmitInfo{
          .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = perSwapchainImageData[swapchainImageIndex].presentSemaphore,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        },
      };

      Fvog::detail::CheckVkResult(vkQueueSubmit2(Fvog::GetDevice().graphicsQueue_,
        1,
        Fvog::detail::Address(VkSubmitInfo2{
          .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
          .waitSemaphoreInfoCount   = 1,
          .pWaitSemaphoreInfos      = Fvog::detail::Address(VkSemaphoreSubmitInfo{
                 .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                 .semaphore = currentFrameData.acquireSemaphore,
                 .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
          }),
          .commandBufferInfoCount   = 1,
          .pCommandBufferInfos      = Fvog::detail::Address(VkCommandBufferSubmitInfo{
                 .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                 .commandBuffer = commandBuffer,
          }),
          .signalSemaphoreInfoCount = static_cast<uint32_t>(queueSubmitSignalSemaphores.size()),
          .pSignalSemaphoreInfos    = queueSubmitSignalSemaphores.data(),
        }),
        VK_NULL_HANDLE));

      currentFrameData.renderTimelineSemaphoreWaitValue = Fvog::GetDevice().frameNumber;
    }

    {
      ZoneScopedN("Present");
      auto presentTimer = Timer::Create();
      if (auto presentResult = vkQueuePresentKHR(Fvog::GetDevice().graphicsQueue_,
            Fvog::detail::Address(VkPresentInfoKHR{
              .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
              .waitSemaphoreCount = 1,
              .pWaitSemaphores    = &perSwapchainImageData[swapchainImageIndex].presentSemaphore,
              .swapchainCount     = 1,
              .pSwapchains        = &swapchain_.swapchain,
              .pImageIndices      = &swapchainImageIndex,
            }));
          presentResult == VK_ERROR_OUT_OF_DATE_KHR)
      {
        swapchainOk = false;
      }
      else if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
      {
        throw std::runtime_error("vkQueuePresent failed");
      }

      lastFrameSlopTime += static_cast<float>(presentTimer->Elapsed_s());
    }
  }

  FrameMark;
}

void PlayerHead::RemakeSwapchain([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight)
{
  ZoneScoped;

  ASSERT(newWidth > 0 && newHeight > 0);

  {
    ZoneScopedN("Device Wait Idle");
    vkDeviceWaitIdle(Fvog::GetDevice().device_);
  }

  const auto oldSwapchain = swapchain_;

  {
    ZoneScopedN("Create New Swapchain");
    ASSERT(presentModeSurfaceCapabilities_.contains(activePresentMode));
    const auto surfaceCapabilities = presentModeSurfaceCapabilities_[activePresentMode];
    numSwapchainImages = glm::max(surfaceCapabilities.minImageCount + numExtraSwapchainImages, surfaceCapabilities.minImageCount);
    if (surfaceCapabilities.maxImageCount > 0)
    {
      numSwapchainImages = glm::min(numSwapchainImages, surfaceCapabilities.maxImageCount);
    }
    swapchain_ = MakeVkbSwapchain(Fvog::GetDevice().device_,
      windowFramebufferWidth,
      windowFramebufferHeight,
      activePresentMode,
      numSwapchainImages,
      oldSwapchain,
      swapchainFormat_);
    CleanupPerSwapchainImageData(perSwapchainImageData);
    perSwapchainImageData = MakePerSwapchainImageData(numSwapchainImages);
  }

  {
    ZoneScopedN("Destroy Old Swapchain");

    // Technically UB, but in practice the WFI makes it work
    vkb::destroy_swapchain(oldSwapchain);

    for (auto view : swapchainImageViews_)
    {
      vkDestroyImageView(Fvog::GetDevice().device_, view, nullptr);
    }
  }

  swapchainImages_     = swapchain_.get_images().value();
  swapchainImageViews_ = MakeSwapchainImageViews(Fvog::GetDevice().device_, swapchainImages_, swapchainFormat_.format);

  swapchainOk = true;

  shouldResizeNextFrame = true;
  
  // This line triggers the recreation of window-size-dependent resources.
  // Commenting it out results in a faster, but lower quality resizing experience.
  // OnUpdate(0);
  Draw({0, 0, 0});
}

void DestroyList2::Push(std::function<void()> fn)
{
  destructorList.emplace_back(std::move(fn));
}

void DestroyList2::Terminate()
{
  ZoneScoped;
  for (auto it = destructorList.rbegin(); it != destructorList.rend(); ++it)
  {
    (*it)();
  }

  destructorList.clear();
}

DestroyList2::~DestroyList2()
{
  Terminate();
}
