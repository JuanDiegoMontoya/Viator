#include "VoxelRenderer.h"

#include "Game/Assets.h"
#include "MathUtilities.h"
#include "imgui.h"
#include "Core/Reflection.h"
#include "Core/Serialization.h"
#include "Game/Networking/Client.h"
#include "Game/Networking/Server.h"
#include "Core/Assert2.h"
#include "GuiHelpers.h"
#include "Game/Item.h"
#include "Game/Scripting.h"
#include "Game/Game.h"
#include "Game/World.h"
#include "Game/Prefab.h"
#include "Game/Globals.h"
#include "Client/Gui/Console.h"
#include "Game/CVar.h"
#include "Game/Rendering/Particle.h"

#include "Game/Physics/Physics.h" // TODO: remove
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Game/Physics/PhysicsUtils.h"
#ifdef JPH_DEBUG_RENDERER
#include "Game/Physics/DebugRenderer.h"
#endif
#include "Game/Networking/RPC.h"

#include "vk_mem_alloc.h"
#include "GLFW/glfw3.h"
#include "ImGui/imgui_impl_fvog.h"
#include "tracy/Tracy.hpp"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "entt/meta/meta.hpp"
#include "entt/meta/factory.hpp"
#include "entt/meta/container.hpp"
#include "entt/core/hashed_string.hpp"
#include "entt/entity/registry.hpp"
#include "IconsFontAwesome6.h"
#include "IconsMaterialDesign.h"
#include "imgui_internal.h"
#include "miniaudio.h"
#include "Core/StringUtilities.h"
#include "Game/WeatherDirector.h"
#include "rapidfuzz/fuzz.hpp"
#include "spdlog/spdlog.h"

using namespace entt::literals;

// toml contains two instances of unreachable code in release mode.
#include "PlayerAudio.h"
#include "platform/choc_DisableAllWarnings.h"
#include "toml++/toml.hpp"
#include "platform/choc_ReenableAllWarnings.h"

#include <array>
#include <memory>
#include <numeric>
#include <type_traits>
#include <future>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <format>
#include <map>
#include <string>

namespace
{
  const auto sGameSettingsPath = GetConfigDirectory() / "rendererConfig.toml";
  const auto sAudioSettingsPath = GetConfigDirectory() / "audioConfig.toml";
  const auto sServerListPath = GetConfigDirectory() / "ServerList.toml";
  const auto sSavesDirectory = GetDataDirectory() / "saves";
  const auto sWorldSavesDirectory  = sSavesDirectory / "worlds";
  const auto sCharacterSavesDirectory = sSavesDirectory / "characters";
  std::string sNewWorldName           = std::string(256, '\0');

  auto sSelectedWorld = std::optional<std::filesystem::path>();

  // Rendering
  toml::parse_result sGameSettings;
  bool sGameSettingsModified = false;

  // Networking
  toml::parse_result sServerList;
  std::string sSelectedServerName;
  std::string sHostName    = std::string(256, '\0');
  std::string sHostAddress = std::string(256, '\0');
  uint16_t sHostPort       = 1234;

  // Fonts
  ImFont* sStandardFont{};
  ImFont* sMonospaceFont{};

  void SaveGameSettings()
  {
    ZoneScoped;
    spdlog::debug("Save game settings.");
    auto tempPath = sGameSettingsPath;
    tempPath += ".temp";
    {
      auto file = std::ofstream(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
      if (!file)
      {
        throw std::runtime_error("Could not open file for writing");
      }
      file << sGameSettings;
    }
    std::filesystem::rename(tempPath, sGameSettingsPath);
  }

  void LoadServerList()
  {
    ZoneScoped;
    spdlog::debug("Load server list.");
    // Ensure file exists before attempting to parse it.
    auto file = std::fstream(sServerListPath, std::fstream::in | std::fstream::out | std::fstream::app);
    sServerList = toml::parse(file);
  }

  void SaveServerList()
  {
    ZoneScoped;
    spdlog::debug("Save server list.");
    auto tempPath = sServerListPath;
    tempPath += ".temp";
    {
      auto file = std::ofstream(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
      if (!file)
      {
        throw std::runtime_error("Could not open file for writing");
      }
      file << sServerList;
    }
    std::filesystem::rename(tempPath, sServerListPath);
  }

  const char* NameFromProperties(const entt::meta_custom& custom)
  {
    const auto* props = static_cast<const Core::Reflection::PropertiesMap*>(custom);
    if (!props)
    {
      return nullptr;
    }

    if (auto it = props->find("name"_hs); it != props->end())
    {
      if (auto* pName = it->second.try_cast<const char*>())
      {
        return *pName;
      }
    }

    return nullptr;
  }

  void DisplayTraits(Core::Reflection::Traits traits)
  {
    using namespace Core::Reflection;
#define SHOW_TRAIT(trait)        \
  do                             \
  {                              \
    if (traits & trait)          \
    {                            \
      ImGui::Text("%s", #trait); \
    }                            \
  } while (false)

    SHOW_TRAIT(TRANSIENT);
    SHOW_TRAIT(NO_EDITOR);
    SHOW_TRAIT(EDITOR_READ_ONLY);
    SHOW_TRAIT(VARIANT);
    SHOW_TRAIT(COMPONENT);
    SHOW_TRAIT(REPLICATED);
    SHOW_TRAIT(TRIVIAL);
    SHOW_TRAIT(POLYMORPHIC);
    SHOW_TRAIT(EMPTY);
    SHOW_TRAIT(ITEM_COMPONENT);
    SHOW_TRAIT(BLOCK_COMPONENT);
    SHOW_TRAIT(OPTIONAL);

#undef SHOW_TRAIT
  }

  void DisplayRpcTraits(Core::Reflection::RpcTraits traits)
  {
    using Core::Reflection::RpcTraits;
#define SHOW_TRAIT(trait)        \
  do                             \
  {                              \
    if (uint32_t(traits & trait))\
    {                            \
      ImGui::Text("%s", #trait); \
    }                            \
  } while (false)

    SHOW_TRAIT(RpcTraits::Client);
    SHOW_TRAIT(RpcTraits::Server);
    SHOW_TRAIT(RpcTraits::Remote);
    SHOW_TRAIT(RpcTraits::Broadcast);
    SHOW_TRAIT(RpcTraits::Unreliable);
    SHOW_TRAIT(RpcTraits::UseVoxelChannel);

#undef SHOW_TRAIT
  }

  void DrawMetaData(World& world, const entt::meta_data& data)
  {
    ImGui::Indent();

    if (const char* name = NameFromProperties(data.custom()))
    {
      ImGui::Text("%s: %s", name, std::string(data.type().info().name()).c_str());
    }
    else
    {
      ImGui::Text("%s", std::string(data.type().info().name()).c_str());
    }

    const auto traits = data.traits<Core::Reflection::Traits>();

    if (traits != 0)
    {
      if (ImGui::TreeNode("Member traits"))
      {
        DisplayTraits(traits);
        ImGui::TreePop();
      }
      ImGui::Separator();
    }

    if (ImGui::TreeNode("Member properties"))
    {
      if (const auto* props = static_cast<const Core::Reflection::PropertiesMap*>(data.custom()))
      {
        for (int i = 0; const auto& [propId, prop] : *props)
        {
          i++;
          ImGui::Text("%s: %s =", propId.data(), std::string(prop.type().info().name()).c_str());
          ImGui::SameLine();
          GuiHelper::DrawComponent(world, entt::null, prop, prop.type().custom(), true, i);
        }
      }
      ImGui::TreePop();
    }

    ImGui::Unindent();
  }

  void DrawReflectionWindow(World& world)
  {
    if (ImGui::Begin("Reflection", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
    {
      static char buffer[256]{};
      ImGui::Text("Filter");
      ImGui::SameLine();
      ImGui::InputText("##Filter", buffer, 256);
      const auto len = std::strlen(buffer);

      static bool showEnums             = true;
      static bool showRegularComponents = true;
      static bool showItemComponents    = true;
      static bool showBlockComponents   = true;
      static bool showNonComponentTypes = true;
      static bool showPrimitiveTypes    = true;
      static bool showClassTypes        = true;
      static bool showArrayTypes        = true;

      ImGui::Columns(2);
      ImGui::Checkbox("Components", &showRegularComponents);
      ImGui::NextColumn();
      ImGui::Checkbox("Classes", &showClassTypes);
      ImGui::NextColumn();

      ImGui::Checkbox("Item Components", &showItemComponents);
      ImGui::NextColumn();
      ImGui::Checkbox("Primitives", &showPrimitiveTypes);
      ImGui::NextColumn();

      ImGui::Checkbox("Block Components", &showBlockComponents);
      ImGui::NextColumn();
      ImGui::Checkbox("Enums", &showEnums);
      ImGui::NextColumn();

      ImGui::Checkbox("Non-Component Types", &showNonComponentTypes);
      ImGui::NextColumn();
      ImGui::Checkbox("Arrays", &showArrayTypes);

      ImGui::EndColumns();

      ImGui::Separator();

      auto types = std::ranges::to<std::vector>(entt::resolve());

      auto scores = std::multimap<double, int, std::greater<double>>();
      for (int index = 0; const auto& [id, type] : types)
      {
        auto name = std::string(type.info().name());
        scores.emplace(rapidfuzz::fuzz::partial_ratio(Core::String::ToLower(buffer), Core::String::ToLower(name)), index);
        index++;
      }

      for (int i = 0; const auto& [score, index] : scores)
      {
        i++;
        if (len > 0 && score < 100)
        {
          continue;
        }

        const auto& [typeId, type] = types[index];
        const auto traits = type.traits<Core::Reflection::Traits>();

        if (!showEnums && type.is_enum())
        {
          continue;
        }

        if (!showRegularComponents && traits & Core::Reflection::COMPONENT && !(traits & Core::Reflection::BLOCK_COMPONENT) &&
            !(traits & Core::Reflection::ITEM_COMPONENT))
        {
          continue;
        }

        if (!showItemComponents && traits & Core::Reflection::ITEM_COMPONENT)
        {
          continue;
        }

        if (!showBlockComponents && traits & Core::Reflection::BLOCK_COMPONENT)
        {
          continue;
        }

        if (!showNonComponentTypes && !(traits & Core::Reflection::COMPONENT))
        {
          continue;
        }

        if (!showPrimitiveTypes && (type.is_arithmetic() || type.is_pointer()))
        {
          continue;
        }

        if (!showClassTypes && type.is_class())
        {
          continue;
        }

        if (!showArrayTypes && type.is_array())
        {
          continue;
        }

        if (ImGui::CollapsingHeader(std::string(type.info().name()).c_str()))
        {
          ImGui::PushID(i);

          if (traits != 0)
          {
            if (ImGui::TreeNode("Type traits"))
            {
              DisplayTraits(traits);
              ImGui::TreePop();
            }
            ImGui::Separator();
          }

          if (const auto* props = static_cast<const Core::Reflection::PropertiesMap*>(type.custom()))
          {
            if (!props->empty())
            {
              if (ImGui::TreeNode("Type properties"))
              {
                for (int j = 0; const auto& [propId, prop] : *props)
                {
                  j++;
                  ImGui::Text("%s: %s =", propId.data(), std::string(prop.type().info().name()).c_str());
                  ImGui::SameLine();
                  GuiHelper::DrawComponent(world, entt::null, prop, prop.type().custom(), true, j);
                }
                ImGui::TreePop();
              }
              ImGui::Separator();
            }
          }

          for (int j = 0; const auto&& [id, func] : type.func())
          {
            j++;
            ImGui::PushID(j);

            if (auto* name = NameFromProperties(func.custom()))
            {
              ImGui::Text("%s", name);
              const auto rpcTraits = func.traits<Core::Reflection::RpcTraits>();
              DisplayRpcTraits(rpcTraits);

              if (func.arity() > 0)
              {
                if (ImGui::TreeNode("Args"))
                {
                  for (size_t argIdx = 0; argIdx < func.arity(); argIdx++)
                  {
                    const auto arg = func.arg(argIdx);

                    if (auto* argName = NameFromProperties(arg.custom()))
                    {
                      ImGui::Text("%s", argName);
                    }
                    else
                    {
                      ImGui::Text("%s", std::string(arg.info().name()).c_str());
                    }
                  }
                  ImGui::TreePop();
                }

                ImGui::Separator();
              }
            }

            ImGui::PopID();
            ImGui::Separator();
          }

          for (int j = 0; const auto&& [dataId, data] : type.data())
          {
            j++;
            ImGui::PushID(j);

            if (type.is_enum())
            {
              if (auto* name = NameFromProperties(data.custom()))
              {
                auto value = data.get({});
                if (value.allow_cast<int>())
                {
                  ImGui::Text("%s: %d", name, value.cast<int>());
                } 
              }
            }
            else
            {
              DrawMetaData(world, data);
              ImGui::Separator();
            }

            ImGui::PopID();
          }

          ImGui::PopID();
        }
      }
    }
    ImGui::End();
  }
} // namespace

// Also used to discard unsaved changes to the config when exiting the options menu.
void VoxelRenderer::LoadGameSettings()
{
  ZoneScoped;
  spdlog::debug("Load renderer config.");

  sGameSettingsModified = false;

  auto file               = std::fstream(sGameSettingsPath, std::fstream::in | std::fstream::out | std::fstream::app);

  sGameSettings      = toml::parse(file);
  giMethod_          = sGameSettings["graphics"]["gi"]["method"].value_or(giMethod_);
  pathTracerSamples  = sGameSettings["graphics"]["pathtracer"]["samples"].value_or(pathTracerSamples);
  pathTracerBounces  = sGameSettings["graphics"]["pathtracer"]["bounces"].value_or(pathTracerBounces);
  enableBloom        = sGameSettings["graphics"]["bloom"]["enable"].value_or(enableBloom);
  enableAo_          = sGameSettings["graphics"]["ambient_occlusion"].value_or(enableAo_);
  head_->activePresentMode = (VkPresentModeKHR)glm::clamp(sGameSettings["graphics"]["present_mode"].value_or((int)head_->activePresentMode), 0, 3);
  head_->enableFramePacing = sGameSettings["graphics"]["frame_pacing"].value_or(false);
  head_->numExtraSwapchainImages = (int)sGameSettings["graphics"]["triple_buffering"].value_or(true);
  head_->shouldRemakeSwapchainNextFrame = true;

  if (head_->audio_)
  {
    ma_engine_set_volume(head_->audio_->engine_, sGameSettings["audio"]["volume"].value_or(0.5f));
  }
}

void VoxelRenderer::InitGui()
{
  ZoneScoped;
  spdlog::info("Initializing GUI.");
  LoadGameSettings();
  // Attempt to load default layout, if it exists
  if (std::filesystem::exists(uiLayoutPath) && !std::filesystem::is_directory(uiLayoutPath))
  {
    ImGui::LoadIniSettingsFromDisk(uiLayoutPath.c_str());
  }

  // Settings are only saved when the user explicitly requests it
  ImGui::GetIO().IniFilename = nullptr;

  float xscale, yscale;
  glfwGetWindowContentScale(head_->window, &xscale, &yscale);
  const auto contentScale = std::max(xscale, yscale); // I don't know how to properly handle the case where xdpi != ydpi. Hopefully that never happens
  const float fontSize    = std::floor(18 * contentScale);
  sStandardFont           = ImGui::GetIO().Fonts->AddFontFromFileTTF((GetTextureDirectory() / "RobotoCondensed-Regular.ttf").string().c_str(), fontSize);
  sMonospaceFont          = ImGui::GetIO().Fonts->AddFontDefault();
  // constexpr float iconFontSize = fontSize * 2.0f / 3.0f; // if GlyphOffset.y is not biased, uncomment this
  
  // These fonts appear to interfere, possibly due to having overlapping ranges.
  // Loading FA first appears to cause less breakage
  {
    const float iconFontSize            = fontSize * 4.0f / 5.0f * contentScale;
    static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode        = true;
    icons_config.PixelSnapH       = true;
    icons_config.GlyphMinAdvanceX = iconFontSize;
    icons_config.GlyphOffset.y    = 0; // Hack to realign the icons
    ImGui::GetIO().Fonts->AddFontFromFileTTF((GetTextureDirectory() / FONT_ICON_FILE_NAME_FAS).string().c_str(), iconFontSize, &icons_config, icons_ranges);
  }

  {
    const float iconFontSize            = fontSize;
    static const ImWchar icons_ranges[] = {ICON_MIN_MD, ICON_MAX_16_MD, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode        = true;
    icons_config.PixelSnapH       = true;
    icons_config.GlyphMinAdvanceX = iconFontSize;
    icons_config.GlyphOffset.y    = 4; // Hack to realign the icons
    ImGui::GetIO().Fonts->AddFontFromFileTTF((GetTextureDirectory() / FONT_ICON_FILE_NAME_MD).string().c_str(), iconFontSize, &icons_config, icons_ranges);
  }

  ImGui_ImplFvog_CreateFontsTexture();

  //Gui::ApplySteamImGuiStyle();
  Gui::ApplyFrogImGuiStyle();

  auto& style = ImGui::GetStyle();
  style.ScaleAllSizes(contentScale);
  style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
  style.WindowMenuButtonPosition        = ImGuiDir_None;
  style.IndentSpacing                   = 15;
}

bool VoxelRenderer::ShowSettingsWindow([[maybe_unused]] World& world)
{
  if (ImGui::Begin("Settings"))
  {
    if (ImGui::BeginTabBar("SettingsTabBar"))
    {
      if (ImGui::BeginTabItem("Graphics"))
      {
        constexpr auto items    = std::array{"Immediate", "Mailbox", "FIFO", "FIFO Relaxed"};
        constexpr auto tooltips = std::array{
          "Classic \"vsync off\" mode. Uncapped framerate, with tearing.",
          "Uncapped framerate, without tearing, but with more latency than Immediate.",
          "Classic \"vsync on\" mode. Framerate is capped to the display refresh rate, without tearing.",
          "Same as FIFO, except late frames will be presented immediately (with tearing), keeping latency relatively consistent."
        };
        const auto presentMode = static_cast<int>(head_->activePresentMode);
        if (ImGui::BeginCombo("Present Mode", items[presentMode]))
        {
          for (int i = 0; i < 4; i++)
          {
            ImGui::BeginDisabled(!std::ranges::contains(head_->availablePresentModes_, static_cast<VkPresentModeKHR>(i)));
            if (ImGui::Selectable(items[i], i == presentMode))
            {
              head_->shouldRemakeSwapchainNextFrame = true;
              head_->activePresentMode                    = static_cast<VkPresentModeKHR>(i);
              sGameSettingsModified                 = true;
            }
            ImGui::EndDisabled();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
            {
              ImGui::SetTooltip("%s", tooltips[i]);
            }
          }

          ImGui::EndCombo();
        }

        bool tripleBuffering = head_->numExtraSwapchainImages != 0;
        if (ImGui::Checkbox("Triple buffering", &tripleBuffering))
        {
          sGameSettingsModified = true;
          head_->shouldRemakeSwapchainNextFrame = true;
        }
        head_->numExtraSwapchainImages = tripleBuffering ? 1 : 0;

        if (ImGui::IsItemHovered())
        {
          // It should be noted that this isn't strictly describing triple buffering. However, desktop implementations
          // have a surface minImageCount of 2, so this is true in practice. The assumption will be wrong on non-desktop GPUs.
          // There are reports of minImageCounts of 1, 2, 3, and 4 for mobile devices on gpuinfo.org. Specifying it as an extra image
          // and clamping to the device limits means that the behavior will be slightly misleading at worst.
          ImGui::SetTooltip("%s", "Adds an extra swapchain image, which may increase the overall \n"
                                  "frame rate, but increase input latency.");
        }

        ImGui::BeginDisabled(head_->activePresentMode != VK_PRESENT_MODE_FIFO_KHR && head_->activePresentMode != VK_PRESENT_MODE_FIFO_RELAXED_KHR);
        sGameSettingsModified |= ImGui::Checkbox("Reduce latency", &head_->enableFramePacing);
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
        {
          ImGui::SetTooltip("%s", "If enabled, delays input polling so that it occurs as close\n"
                                  "to presentation as possible. This reduces input latency, but\n"
                                  "adds a risk of skipping frames. Only available for FIFO and \n"
                                  "FIFO Relaxed present modes.");
        }

        ImGui::Spacing();

        ImGui::Text("Global Illumination Method");
        ImGui::Separator();
        if (ImGui::RadioButton("None", giMethod_ == GIMethod::None))
        {
          sGameSettingsModified = true;
          giMethod_             = GIMethod::None;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Path Tracing", giMethod_ == GIMethod::PerPixelPathTracing))
        {
          sGameSettingsModified = true;
          giMethod_             = GIMethod::PerPixelPathTracing;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("DDGI", giMethod_ == GIMethod::DDGI))
        {
          sGameSettingsModified = true;
          giMethod_             = GIMethod::DDGI;
        }
        ImGui::BeginDisabled(giMethod_ != GIMethod::PerPixelPathTracing);
        sGameSettingsModified |= ImGui::SliderInt("PT Samples", &pathTracerSamples, 1, 32);
        sGameSettingsModified |= ImGui::SliderInt("PT Bounces", &pathTracerBounces, 0, 8);
        ImGui::EndDisabled();

        sGameSettingsModified |= ImGui::Checkbox("Bloom", &enableBloom);

        ImGui::BeginDisabled(giMethod_ != GIMethod::DDGI);
        sGameSettingsModified |= ImGui::Checkbox("Ambient Occlusion", &enableAo_);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
          ImGui::SetTooltip("For DDGI only");
        }

        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Audio"))
      {
        if (head_->audio_)
        {
          auto volume = ma_engine_get_volume(head_->audio_->engine_);
          if (ImGui::SliderFloat("Volume", &volume, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp))
          {
            sGameSettingsModified = true;
            ma_engine_set_volume(head_->audio_->engine_, volume);
          }
        }
        else
        {
          ImGui::TextUnformatted("Missing audio interface.");
        }

        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!sGameSettingsModified);
    if (ImGui::Button("Apply###apply"))
    {
      auto graphics   = toml::table();
      auto pathtracer = toml::table();
      pathtracer.insert_or_assign("samples", pathTracerSamples);
      pathtracer.insert_or_assign("bounces", pathTracerBounces);
      graphics.insert_or_assign("pathtracer", pathtracer);
      auto bloom = toml::table();
      bloom.insert_or_assign("enable", enableBloom);
      graphics.insert_or_assign("bloom", bloom);
      graphics.insert_or_assign("ambient_occlusion", enableAo_);
      graphics.insert_or_assign("present_mode", head_->activePresentMode);
      graphics.insert_or_assign("frame_pacing", head_->enableFramePacing);
      graphics.insert_or_assign("triple_buffering", static_cast<bool>(head_->numExtraSwapchainImages));
      auto gi = toml::table();
      gi.insert_or_assign("method", giMethod_);
      graphics.insert_or_assign("gi", gi);
      
      auto audio = toml::table();

      if (head_->audio_)
      {
        audio.insert_or_assign("volume", ma_engine_get_volume(head_->audio_->engine_));
      }
      
      sGameSettings.insert_or_assign("graphics", graphics);
      sGameSettings.insert_or_assign("audio", audio);
      SaveGameSettings();
      sGameSettingsModified = false;
    }
    ImGui::EndDisabled();
    return true;
  }
  ImGui::End();
  return false;
}

void VoxelRenderer::OnGui([[maybe_unused]] DeltaTime dt, World& world, [[maybe_unused]] VkCommandBuffer commandBuffer)
{
  ZoneScoped;

  if (world.globals->game->debugging.disableAllUi)
  {
    return;
  }

  // Toggle pause state if the user presses Escape.
  if (ImGui::GetKeyPressedAmount(ImGuiKey_Escape, 10000, 1))
  {
    auto& state = world.globals->game->gameState;
    if (state == GameState::PAUSED)
    {
      state = GameState::GAME;
    }
    else if (state == GameState::PAUSED_SETTINGS)
    {
      LoadGameSettings();
      state = GameState::GAME;
    }
    else if (state == GameState::GAME)
    {
      if (auto p = world.TryGetLocalPlayer(); p != entt::null)
      {
        auto& player = world.GetRegistry().get<Player>(p);
        if (player.inventoryIsOpen)
        {
          player.inventoryIsOpen = false;
          player.openContainerId = entt::null;
        }
      }

      state = GameState::PAUSED;
    }
  }

  auto& gameState = world.globals->game->gameState;
  switch (gameState)
  {
  case GameState::MENU:
    if (ImGui::Begin("Menu"))
    {
      if (ImGui::Selectable("Singleplayer"))
      {
        sSelectedWorld = std::nullopt;
        gameState = GameState::WORLD_SELECT;
      }

      if (ImGui::Selectable("Multiplayer"))
      {
        LoadServerList();
        sSelectedServerName = std::string();
        gameState = GameState::SERVER_SELECT;
      }

      if (ImGui::Selectable("Settings"))
      {
        gameState = GameState::MENU_SETTINGS;
      }

      if (ImGui::Selectable("Exit to desktop"))
      {
        world.GetRegistry().ctx().emplace<CloseApplication>();
      }
    }
    ImGui::End();
    break;
  case GameState::GAME:
  {
    {
      ZoneScopedN("Poll Modified Shaders");
      GetPipelineManager().EnqueueModifiedShaders();
    }

    ShowGameGui(world);
    break;
  }
  case GameState::PAUSED:
  {
    if (ImGui::Begin("Paused"))
    {
      if (ImGui::Selectable("Resume"))
      {
        gameState = GameState::GAME;
      }

      if (ImGui::Selectable("Settings"))
      {
        gameState = GameState::PAUSED_SETTINGS;
      }

      auto& networking = world.globals->networking;

      ImGui::BeginDisabled(networking->get());
      if (ImGui::Selectable("Open Server"))
      {
        *networking = Networking::Server::Create(world);
      }
      ImGui::EndDisabled();

      ImGui::BeginDisabled(!networking->get() || !world.IsServer());
      if (ImGui::Selectable("Close Server"))
      {
        networking->reset();
      }
      ImGui::EndDisabled();

      ImGui::BeginDisabled(world.IsClient());
      if (ImGui::Selectable("Save"))
      {
        if (!std::filesystem::is_directory(sWorldSavesDirectory))
        {
          std::filesystem::create_directories(sWorldSavesDirectory);
        }
        Core::Serialization::SaveRegistryToFile(world, sWorldSavesDirectory / (world.globals->worldName.c_str() + std::string(".rizz")));
      }
      ImGui::EndDisabled();

      if (ImGui::Selectable("Exit to main menu"))
      {
        networking->reset();
        world.GetRegistryRaw().clear();
        world.GetRegistryRaw() = {};
        CreateContextVariablesAndObservers(world);
        world.globals->game->gameState = GameState::MENU; // CreateContextVariablesAndObservers() invalidates the reference `gameState`.
      }

      if (ImGui::Selectable("Exit to desktop"))
      {
        world.GetRegistry().ctx().emplace<CloseApplication>();
      }
    }
    ImGui::End();
    break;
  }
  case GameState::WORLD_SELECT:
  {
    ZoneScopedN("World Select");
    auto saveNames = std::unordered_set<std::string>();
    if (ImGui::Begin("World Select"))
    {
      if (std::filesystem::is_directory(sWorldSavesDirectory))
      {
        for (int i = 0; const auto& entry : std::filesystem::directory_iterator(sWorldSavesDirectory))
        {
          ImGui::PushID(i++);
          saveNames.emplace(entry.path().stem().string().c_str());
          if (entry.is_regular_file() && ImGui::Selectable(entry.path().stem().string().c_str(), entry.path() == sSelectedWorld))
          {
            sSelectedWorld = entry.path();
          }
          ImGui::PopID();
        }
      }

      ImGui::Separator();

      ImGui::BeginDisabled(!sSelectedWorld.has_value());
      if (ImGui::Button("Load"))
      {
        world.GetRegistryRaw().clear();
        world.GetRegistryRaw() = {};
        *world.globals->progressText = "Loading world";
        *world.globals->progress     = 0;
        *world.globals->total        = 1;
        CreateContextVariablesAndObservers(world);
        world.globals->game->gameState = GameState::LOADING_SP;
        world.GetRegistry().ctx().emplace_as<std::future<void>>("loading"_hs,
          std::async(std::launch::async,
            [&world, path = sSelectedWorld.value()]
            {
              Core::Serialization::LoadRegistryFromFile(world, path);
              world.InitializeGameDefinitions();
              world.globals->worldName = path.stem().string();
              world.globals->game->gameState = GameState::LOADING_SP;
            }));
        sSelectedWorld = std::nullopt;
      }

      ImGui::SameLine();
      if (ImGui::Button("Delete"))
      {
        ImGui::OpenPopup("Delete World");
      }

      const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

      if (ImGui::BeginPopupModal("Delete World", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
      {
        ImGui::Text("Are you sure?");
        ImGui::Separator();
        if (ImGui::Button("Yes"))
        {
          std::filesystem::remove(sSelectedWorld.value());
          sSelectedWorld = std::nullopt;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::Button("New World"))
      {
        constexpr char initialWorldName[] = "World";
        std::memcpy(sNewWorldName.data(), initialWorldName, sizeof(initialWorldName));
        ImGui::OpenPopup("New World##1");
      }
      
      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

      static auto mapGenInfo       = MapGenInfo{};
      static int selectedWorldSize = 0;
      auto MakeWorld = [&]
      {
        constexpr auto worldSizes = std::array{glm::ivec3{2, 10, 2}, glm::ivec3{4, 10, 4}, glm::ivec3{8, 10, 8}, glm::ivec3{10, 10, 10}, glm::ivec3{20, 10, 20}};
        world.InitializeGameState();
        world.CreateGrid(worldSizes.at(selectedWorldSize));
        world.CreateInitialEntities();
        *world.globals->progress  = 0;
        *world.globals->total     = 1;
        *world.globals->progressText = "";
        world.globals->worldName = sNewWorldName;
        // emplace_as doesn't overwrite context variables, so we have to first erase them (erase returns false if it failed, which is ok).
        world.GetRegistry().ctx().erase<std::future<void>>("loading"_hs);
        world.GetRegistry().ctx().emplace_as<std::future<void>>("loading"_hs,
          std::async(std::launch::async,
            [&world]
            {
              world.GenerateMap(mapGenInfo);
              *world.globals->progressText = "Saving";
              // Save world right after creating it.
              if (!std::filesystem::is_directory(sWorldSavesDirectory))
              {
                std::filesystem::create_directories(sWorldSavesDirectory);
              }
              // Call to c_str() implicitly discards the trailing NULs that are present in worldName.
              Core::Serialization::SaveRegistryToFile(world, sWorldSavesDirectory / (world.globals->worldName.c_str() + std::string(".rizz")));
            }));
        gameState = GameState::LOADING_SP;
        ImGui::CloseCurrentPopup();
      };
      if (ImGui::BeginPopupModal("New World##1", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
      {
        ImGui::PushItemWidth(200);
        ImGui::InputText("Name", sNewWorldName.data(), sNewWorldName.size());
        if (ImGui::IsWindowAppearing())
        {
          selectedWorldSize = 0;
          mapGenInfo        = {};
        }

        ImGui::Combo("Size", &selectedWorldSize, "Tiny\0Small\0Medium\0");

        if (ImGui::TreeNodeEx("Advanced"))
        {
          ImGui::InputInt("Seed", &mapGenInfo.seed);
          ImGui::Checkbox("Settle liquids", &mapGenInfo.settleLiquids);
          ImGui::Checkbox("Generate caves", &mapGenInfo.generateCaves);
          ImGui::Checkbox("Spawn Yggdrasil", &mapGenInfo.spawnYggdrasil);
          ImGui::TreePop();
        }

        ImGui::Separator();

        if (ImGui::Button("Create"))
        {
          if (saveNames.contains(sNewWorldName.c_str()))
          {
            ImGui::OpenPopup("Overwrite World");
          }
          else
          {
            MakeWorld();
          }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
          ImGui::CloseCurrentPopup();
        }

        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Overwrite World", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
          ImGui::Text("A world with this name already exists.\nDo you want to overwrite it?");

          ImGui::Separator();

          if (ImGui::Button("Yes"))
          {
            MakeWorld();
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel"))
          {
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }

        ImGui::EndPopup();
      }

      if (ImGui::Button("Back"))
      {
        gameState = GameState::MENU;
      }
    }
    ImGui::End();
    break;
  }
  case GameState::LOADING_SP:
  {
    auto& future = world.GetRegistry().ctx().get<std::future<void>>("loading"_hs);
    using namespace std::chrono_literals;
    if (future.wait_for(0s) == std::future_status::ready)
    {
      future.get();
      gameState = GameState::GAME;
      world.CreateRenderingMaterials();
    }
    else
    {
      // Show loading bar.
      const auto& progressText = world.globals->progressText;
      const auto& progress     = world.globals->progress;
      const auto& total        = world.globals->total;
      if (ImGui::Begin("Loading"))
      {
        ImGui::Text("%s", progressText->load());
        constexpr auto bgColor = ImColor(0.4f, 0.4f, 0.4f, 1.0f);
        constexpr auto fgColor = ImColor(1.0f, 1.0f, 1.0f, 1.0f);
        Gui::LoadingBar("##loading", float(progress->load()) / total->load(), ImVec2(ImGui::GetContentRegionAvail().x, 15), bgColor, fgColor);
      }
      ImGui::End();
    }
    break;
  }
  case GameState::LOADING_MP:
  {
    if (auto& networking = world.globals->networking)
    {
      auto* client = dynamic_cast<Networking::Client*>(networking->get());
      ASSERT(client);
      if (ImGui::Begin("Loading"))
      {
        ImGui::Text("%s", Core::Reflection::EnumToString(client->GetStatus()));

        if (ImGui::Selectable("Cancel"))
        {
          networking->reset();
          gameState = GameState::SERVER_SELECT;
        }
      }
      ImGui::End();
      break;
    }
    UNREACHABLE;
  }
  case GameState::MENU_SETTINGS:
  {
    if (ShowSettingsWindow(world))
    {
      ImGui::SameLine();
      if (ImGui::Button("Back"))
      {
        LoadGameSettings();
        gameState = GameState::MENU;
      }
    }
    ImGui::End();
    break;
  }
  case GameState::PAUSED_SETTINGS:
  {
    if (ShowSettingsWindow(world))
    {
      ImGui::SameLine();
      if (ImGui::Button("Back"))
      {
        LoadGameSettings();
        gameState = GameState::PAUSED;
      }
    }
    ImGui::End();
    break;
  }
  case GameState::SERVER_SELECT:
  {
    if (ImGui::Begin("Server Select"))
    {
      for (auto&& [k, v] : sServerList)
      {
        if (v.is_table())
        {
          auto serverName = k.str();
          
          if (ImGui::Selectable(std::string(serverName).c_str(), sSelectedServerName == serverName))
          {
            sSelectedServerName = serverName;
          }
        }
      }

      ImGui::Separator();

      {
        auto& networking = world.globals->networking;
        const auto table  = sServerList.get_as<toml::table>(sSelectedServerName);
        ImGui::BeginDisabled(!table);
        if (ImGui::Button("Join Server"))
        {
          auto address               = (*table)["address"].value_or(std::string_view("localhost"));
          [[maybe_unused]] auto port = (*table)["port"].value_or(1234);
          *networking = Networking::Client::Create(world, std::string(address).c_str());
          gameState   = GameState::LOADING_MP;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
          ImGui::OpenPopup("Delete Server");
        }
        ImGui::EndDisabled();

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Delete Server", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
          ImGui::Text("Are you sure?");
          ImGui::Separator();
          if (ImGui::Button("Yes"))
          {
            sServerList.erase(sSelectedServerName);
            SaveServerList();
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel"))
          {
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Add Server"))
      {
        sHostName = std::string(256, '\0');
        const auto init = "Server";
        std::memcpy(sHostName.data(), init, std::strlen(init));
        sHostAddress  = std::string(256, '\0');
        sHostPort      = 1234;
        gameState = GameState::SERVER_SELECT_ADD_SERVER;
      }

      if (ImGui::Button("Back"))
      {
        gameState = GameState::MENU;
      }
    }
    ImGui::End();
    break;
  }
  case GameState::SERVER_SELECT_ADD_SERVER:
  {
    if (ImGui::Begin("Add Server"))
    {
      ImGui::Text("Name");

      ImGui::PushItemWidth(158);
      ImGui::InputText("###hostname", sHostName.data(), sHostName.size());
      ImGui::PopItemWidth();

      ImGui::Text("Address");

      ImGui::PushItemWidth(100);
      ImGui::InputText("##Host", sHostAddress.data(), sHostAddress.size());
      ImGui::PopItemWidth();

      ImGui::SameLine();
      ImGui::PushItemWidth(50);
      ImGui::InputScalar("##Port", ImGuiDataType_U16, &sHostPort, nullptr, nullptr, "%hu");
      ImGui::PopItemWidth();

      ImGui::BeginDisabled(sServerList.contains(sHostName) || std::strlen(sHostName.c_str()) == 0);
      if (ImGui::Button("Done"))
      {
        auto table = toml::table();
        // Use C strings when inserting, otherwise all the NULs at the end of the string appear in the file as \u0000.
        table.insert("address", sHostAddress.c_str());
        table.insert("port", sHostPort);
        sServerList.insert(sHostName.c_str(), table);
        SaveServerList();
        gameState = GameState::SERVER_SELECT;
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::Button("Back"))
      {
        gameState = GameState::SERVER_SELECT;
      }
    }
    ImGui::End();
    break;
  }
  default: DEBUG_ASSERT(0);
  }

  if (world.globals->game->debugging.showFps)
  {
    ImGui::SetNextWindowPos({0, ImGui::GetIO().DisplaySize.y}, ImGuiCond_Always, {0, 1});
    if (ImGui::Begin("Test",
          nullptr,
          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
      static float smoothDt = dt.real;
      smoothDt              = glm::mix(smoothDt, dt.real, 0.04f);
      ImGui::Text("FPS: %.0f (%.2fms)", 1 / dt.real, dt.real * 1000);
      ImGui::Text("Avg. FPS: %.0f (%.2fms)", 1 / smoothDt, smoothDt * 1000);
    }
    ImGui::End();
  }

  Console::Get()->Draw(world);

  if (world.globals->game->debugging.showDebugGui)
  {
    if (head_->GetAudio())
    {
      if (auto* pAudio = dynamic_cast<PlayerAudio*>(head_->GetAudio()))
      {
        pAudio->DrawDebugUI();
      }
    }
    world.globals->scripting->DrawDebugUI(world);
    Physics::DrawDebugUI(world);

    ShowEditor(dt, world, EditorMode::Entities);
    ShowEditor(dt, world, EditorMode::Items);
    ShowEditor(dt, world, EditorMode::Blocks);

    DrawReflectionWindow(world);

    if (false)
    {
      if (ImGui::Begin("TEST PROBULUS", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
      {
        static glm::vec3 probePos  = {0, 60, 0};
        static glm::vec3 probePos2 = {0, 61, 0};
        static float probeRadius   = 2;

#ifdef JPH_DEBUG_RENDERER
        // JPH::DebugRenderer::sInstance->DrawWireSphere(Physics::ToJolt(probePos), probeRadius, JPH::Color::sGreen);
        const auto jup    = Physics::ToJolt(glm::normalize(probePos2 - probePos));
        const auto jfor   = jup.GetNormalizedPerpendicular();
        const auto jright = jfor.Cross(jup);

        auto mat = JPH::Mat44::sIdentity();
        mat.SetAxisX(jright);
        mat.SetAxisY(jup);
        mat.SetAxisZ(jfor);
        mat.SetTranslation(Physics::ToJolt((probePos + probePos2) / 2.0f));
        JPH::DebugRenderer::sInstance->DrawCapsule(mat, glm::distance(probePos, probePos2) / 2.0f, probeRadius, JPH::Color::sGreen);
#endif

        ImGui::DragFloat3("Probe pos1", &probePos[0], 0.25f);
        ImGui::DragFloat3("Probe pos2", &probePos2[0], 0.25f, 0, 0, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
        ImGui::DragFloat("Probe radius", &probeRadius, 0.125f, 0, 1000);
        ImGui::Separator();
        auto entities = world.GetEntitiesInCapsule(probePos, probePos2, probeRadius);
        ImGui::Text("Covered: %llu", entities.size());
        for (auto entity : entities)
        {
          auto name = std::string();
          if (const auto* n = world.GetRegistry().try_get<const Name>(entity))
          {
            name = n->name;
          }
          ImGui::Text("%u (%s)", entt::to_entity(entity), name.c_str());
        }
      }
      ImGui::End();
    }

    ShowGraphicsWindow(world);

    ShowGameDebugWindow(world);
    
    if (ImGui::Begin("Networking", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
    {
      if (auto& networking = *world.globals->networking)
      {
        if (world.IsServer())
        {
          ImGui::Text("Status: hosting server");
        }
        else
        {
          ImGui::Text("Status: client");
        }

        if (ImGui::BeginTable("Clients", 4))
        {
          ImGui::TableSetupColumn("Client ID");
          ImGui::TableSetupColumn("Status");
          ImGui::TableSetupColumn("Ping (variance)");
          ImGui::TableSetupColumn("Packet loss");
          ImGui::TableHeadersRow();
          for (const auto& info : networking.get()->GetClientNetworkInfos())
          {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", info.entity);
            ImGui::TableNextColumn();
            ImGui::Text("%s", Core::Reflection::EnumToString(info.status));
            ImGui::TableNextColumn();
            ImGui::Text("%u (%u)", info.roundTripTime, info.roundTripTimeVariance);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", 100 * info.packetLoss);
          }
          ImGui::EndTable();
        }
      }
      else
      {
        ImGui::TextWrapped("%s", "Host or connect to a server to see networking info.");
      }
    }
    ImGui::End();

    if (ImGui::Begin("Prefab Editor", nullptr, ImGuiWindowFlags_NoFocusOnAppearing) && gameState == GameState::GAME)
    {
      static float pickLength        = 5;
      static int selectedPositions   = 0;
      static glm::vec3 wpositions[3] = {};
      static glm::vec3 hposition     = {};
      static char sName[256]         = {};
      static bool skipAir            = false;
      static auto selectedPrefab     = std::filesystem::path();

      auto prefabSaveNames = std::unordered_set<std::string>();

      auto& grid = world.globals->grid;

      const auto pTransform = world.TryGetLocalPlayerTransform();
      ASSERT(pTransform);

      auto hit = Voxel::Grid::HitSurfaceParameters{};
      if (grid->TraceRaySimple(pTransform->position, GetForward(pTransform->rotation), pickLength, hit))
      {
        const auto pos = glm::round(hit.positionWorld);

        if (selectedPositions == 0)
        {
          hposition = pos;
        }
        else if (selectedPositions == 1)
        {
          // find axis that has smallest difference, and lock that one
          glm::vec3 diff = pos - wpositions[0];
          diff           = glm::abs(diff);
          float smol     = std::min(diff.x, std::min(diff.y, diff.z));
          if (smol == diff.x)
            hposition = glm::vec3(wpositions[0].x, pos.y, pos.z);
          else if (smol == diff.y)
            hposition = glm::vec3(pos.x, wpositions[0].y, pos.z);
          else if (smol == diff.z)
            hposition = glm::vec3(pos.x, pos.y, wpositions[0].z);
        }
        else if (selectedPositions == 2)
        {
          // only move the axis that is shared between first two positions
          glm::vec3 diff = wpositions[1] - wpositions[0];
          hposition      = wpositions[0];
          if (!diff.x)
            hposition.x = pos.x;
          if (!diff.y)
            hposition.y = pos.y;
          if (!diff.z)
            hposition.z = pos.z;
        }
      }


      ImGui::BeginDisabled(selectedPositions == 3);
      if (ImGui::Button("Select position"))
      {
        wpositions[selectedPositions++] = hposition;
      }
      ImGui::EndDisabled();

      ImGui::BeginDisabled(selectedPositions == 0);
      if (ImGui::Button("Cancel selection"))
      {
        selectedPositions = 0;
      }
      ImGui::EndDisabled();

      ImGui::Checkbox("Skip air when saving", &skipAir);

      if (std::filesystem::is_directory(GetAssetDirectory() / "prefabs"))
      {
        for (int i = 0; const auto& entry : std::filesystem::directory_iterator(GetAssetDirectory() / "prefabs"))
        {
          ImGui::PushID(i++);
          prefabSaveNames.emplace(entry.path().stem().string().c_str());
          if (entry.is_regular_file() && ImGui::Selectable(entry.path().stem().string().c_str(), entry.path() == selectedPrefab))
          {
            selectedPrefab = entry.path();
          }
          ImGui::PopID();
        }
      }

      ImGui::BeginDisabled(selectedPrefab == std::filesystem::path());
      if (ImGui::Button("Spawn selected at cursor"))
      {
        auto prefab = SimplePrefab(world, {"placeholder"}, selectedPrefab.string());

        const auto offsetWS = pTransform->position + GetForward(pTransform->rotation) * pickLength;
        prefab.Instantiate(world, offsetWS);
      }
      ImGui::EndDisabled();

      // Draw
      // actually draw the bounding box
      auto min = glm::vec3();
      auto max = glm::vec3();

      if (selectedPositions == 0)
      {
        min = hposition - 0.125f;
        max = hposition + 0.125f;
      }
      else if (selectedPositions == 1)
      {
        min = {glm::min(wpositions[0].x, hposition.x), glm::min(wpositions[0].y, hposition.y), glm::min(wpositions[0].z, hposition.z)};
        max = {glm::max(wpositions[0].x, hposition.x), glm::max(wpositions[0].y, hposition.y), glm::max(wpositions[0].z, hposition.z)};
      }
      else if (selectedPositions == 2)
      {
        min = {glm::min(wpositions[0].x, glm::min(wpositions[1].x, hposition.x)),
          glm::min(wpositions[0].y, glm::min(wpositions[1].y, hposition.y)),
          glm::min(wpositions[0].z, glm::min(wpositions[1].z, hposition.z))};
        max = {glm::max(wpositions[0].x, glm::max(wpositions[1].x, hposition.x)),
          glm::max(wpositions[0].y, glm::max(wpositions[1].y, hposition.y)),
          glm::max(wpositions[0].z, glm::max(wpositions[1].z, hposition.z))};
      }
      else // if (selectedPositions == 3)
      {
        min = {glm::min(wpositions[0].x, glm::min(wpositions[1].x, wpositions[2].x)),
          glm::min(wpositions[0].y, glm::min(wpositions[1].y, wpositions[2].y)),
          glm::min(wpositions[0].z, glm::min(wpositions[1].z, wpositions[2].z))};
        max = {glm::max(wpositions[0].x, glm::max(wpositions[1].x, wpositions[2].x)),
          glm::max(wpositions[0].y, glm::max(wpositions[1].y, wpositions[2].y)),
          glm::max(wpositions[0].z, glm::max(wpositions[1].z, wpositions[2].z))};
      }

#ifdef JPH_DEBUG_RENDERER
      JPH::DebugRenderer::sInstance->DrawWireBox(JPH::AABox(Physics::ToJolt(min), Physics::ToJolt(max)), JPH::Color::sCyan);
#endif

      const auto savePrefab = [&]
      {
        auto prefab = std::vector<std::pair<glm::ivec3, voxel_t>>();

        for (int z = (int)min.z; z < max.z; z++)
        for (int y = (int)min.y; y < max.y; y++)
        for (int x = (int)min.x; x < max.x; x++)
        {
          const auto posWS = glm::ivec3(x, y, z);
          const auto voxel = grid->GetVoxelAt(posWS);
          if (skipAir && voxel == voxel_t::Air)
          {
            continue;
          }
          prefab.emplace_back(posWS - glm::ivec3(min), voxel);
        }

        const auto serialized = Core::Serialization::SerializeObject(SerializableSimplePrefab(world, prefab));
        auto file             = std::ofstream(GetAssetDirectory() / "prefabs" / (sName + std::string(".pfb")), std::fstream::out | std::fstream::trunc | std::fstream::binary);
        ASSERT(file);
        file.write(serialized.data(), serialized.size());
      };

      ImGui::BeginDisabled(selectedPositions != 3 || std::strlen(sName) == 0);
      if (ImGui::Button("Save"))
      {
        if (prefabSaveNames.contains(sName))
        {
          ImGui::OpenPopup("Overwrite Prefab");
        }
        else
        {
          savePrefab();
        }
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      ImGui::InputText("##save", sName, 256);

      const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal("Overwrite Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
      {
        ImGui::Text("A prefab with the name %s already exists.\nDo you want to overwrite it?", sName);

        ImGui::Separator();

        if (ImGui::Button("Yes"))
        {
          savePrefab();
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }
    ImGui::End();
  }
}

ImFont* GuiHelper::GetStandardFont()
{
  return sStandardFont;
}

ImFont* GuiHelper::GetMonospaceFont()
{
  return sMonospaceFont;
}