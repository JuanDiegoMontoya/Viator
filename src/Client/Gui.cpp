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
#include "IconsFontAwesome6.h"
#include "IconsMaterialDesign.h"
#include "imgui_internal.h"
#include "toml++/toml.hpp"

#include <array>
#include <memory>
#include <numeric>
#include <type_traits>
#include <future>
#include <atomic>
#include <fstream>
#include <filesystem>

namespace
{
  const auto g_defaultIniPath = (GetConfigDirectory() / "defaultLayout.ini").string();
  const auto sRendererSettingsPath = GetConfigDirectory() / "rendererConfig.toml";
  const auto sServerListPath = GetConfigDirectory() / "ServerList.toml";
  const auto sSavesDirectory = GetAssetDirectory() / "saves";
  const auto sWorldSavesDirectory  = sSavesDirectory / "worlds";
  const auto sCharacterSavesDirectory = sSavesDirectory / "characters";
  std::string sNewWorldName           = std::string(256, '\0');

  auto sSelectedWorld = std::optional<std::filesystem::path>();

  // Rendering
  toml::parse_result sRendererConfig;
  bool sRendererConfigModified = false;

  // Networking
  toml::parse_result sServerList;
  std::string sSelectedServerName;
  std::string sHostName    = std::string(256, '\0');
  std::string sHostAddress = std::string(256, '\0');
  uint16_t sHostPort       = 1234;

  void SaveRendererConfig()
  {
    ZoneScoped;
    spdlog::debug("Save renderer config.");
    auto tempPath = sRendererSettingsPath;
    tempPath += ".temp";
    {
      auto file = std::ofstream(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
      if (!file)
      {
        throw std::runtime_error("Could not open file for writing");
      }
      file << sRendererConfig;
    }
    std::filesystem::rename(tempPath, sRendererSettingsPath);
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

  // `minified`: Display just the first row of the inventory. Used to display the player's hotbar.
  // `userTransform`: Transform of the entity interacting with the container. Used to calculate throw position and direction.
  void DrawInventory(World& world, entt::entity parent, entt::entity user, Inventory& inventory, bool minified = false)
  {
    struct InventoryDragDropPayload
    {
      glm::ivec2 sourceRowCol;
      entt::entity sourceEntity;
    };

    auto title = "Inventory" + std::to_string(std::underlying_type_t<entt::entity>(parent));
    if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration))
    {
      ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0.5f, 0.5f});
      ImGui::BeginTable(title.c_str(), (int)inventory.width, ImGuiTableFlags_Borders);

      for (size_t row = 0; row < inventory.slots.size(); row++)
      {
        ImGui::PushID(int(row));
        for (size_t col = 0; col < inventory.slots[row].size(); col++)
        {
          const auto currentSlotCoord = glm::ivec2(row, col);
          ImGui::TableNextColumn();
          ImGui::PushID(int(col));
          auto& slot          = inventory.slots[row][col];
          std::string nameStr = "";
          if (slot.id != nullItem)
          {
            const auto& def = world.GetRegistry().ctx().get<ItemRegistry>().Get(slot.id);
            nameStr         = def.GetName();
            if (def.GetMaxStackSize() > 1)
            {
              nameStr += "\n" + std::to_string(slot.count) + "/" + std::to_string(def.GetMaxStackSize());
            }
          }
          const auto name      = nameStr.c_str();
          const auto cursorPos = ImGui::GetCursorPos();
          if (ImGui::Selectable(("##" + nameStr).c_str(), inventory.canHaveActiveItem && inventory.activeSlotCoord == currentSlotCoord, 0, {50, 50}))
          {
            Networking::CallRPC("SetActiveSlotRPC"_hs, world, parent, currentSlotCoord);
          }
          if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
          {
            const auto dragDropPayload = InventoryDragDropPayload{
              .sourceRowCol = currentSlotCoord,
              .sourceEntity = parent,
            };
            ImGui::SetDragDropPayload("INVENTORY_SLOT", &dragDropPayload, sizeof(dragDropPayload));
            ImGui::Text("%s", name);
            ImGui::EndDragDropSource();
          }
          if (ImGui::BeginDragDropTarget())
          {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT"))
            {
              assert(payload->DataSize == sizeof(InventoryDragDropPayload));
              const auto inventoryPayload = *static_cast<const InventoryDragDropPayload*>(payload->Data);
              Networking::CallRPC("SwapInventorySlotsRPC"_hs, world, inventoryPayload.sourceEntity, inventoryPayload.sourceRowCol, parent, currentSlotCoord);
            }
            ImGui::EndDragDropTarget();
          }
          ImGui::SetCursorPos(cursorPos);
          ImGui::TextWrapped("%s", name);
          ImGui::PopID();
        }
        ImGui::PopID();
        // Only show first row if inventory is not open
        if (minified)
        {
          break;
        }
      }
      ImGui::EndTable();

      if (!minified)
      {
        // Moving entity from inventory to world
        ImGui::Selectable("Ground", false, 0, {ImGui::GetContentRegionAvail().x, 50});
        if (ImGui::BeginDragDropTarget())
        {
          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT"))
          {
            assert(payload->DataSize == sizeof(InventoryDragDropPayload));
            const auto inventoryPayload = *static_cast<const InventoryDragDropPayload*>(payload->Data);

            Networking::CallRPC("ThrowItemRPC"_hs, world, inventoryPayload.sourceEntity, user, inventoryPayload.sourceRowCol);
          }
          ImGui::EndDragDropTarget();
        }
      }
      ImGui::PopStyleVar();
    }
    ImGui::End();
  }

  bool LoadingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col)
  {
    using namespace ImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
      return false;

    ImGuiContext& g         = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id        = window->GetID(label);

    ImVec2 pos  = window->DC.CursorPos;
    ImVec2 size = size_arg;
    size.x -= style.FramePadding.x * 2;

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
      return false;

    // Render
    window->DrawList->AddRectFilled(bb.Min, ImVec2(pos.x + size.x, bb.Max.y), bg_col);
    window->DrawList->AddRectFilled(bb.Min, ImVec2(pos.x + size.x * value, bb.Max.y), fg_col);

    return true;
  }
} // namespace

// Also used to discard unsaved changes to the config when exiting the options menu.
void VoxelRenderer::LoadRendererConfig()
{
  ZoneScoped;
  spdlog::debug("Load renderer config.");

  sRendererConfigModified = false;
  auto file               = std::fstream(sRendererSettingsPath, std::fstream::in | std::fstream::out | std::fstream::app);
  sRendererConfig         = toml::parse(file);
  giMethod_               = sRendererConfig["gi"]["method"].value_or(giMethod_);
  pathTracerSamples       = sRendererConfig["pathtracer"]["samples"].value_or(pathTracerSamples);
  pathTracerBounces       = sRendererConfig["pathtracer"]["bounces"].value_or(pathTracerBounces);
  enableBloom             = sRendererConfig["bloom"]["enable"].value_or(enableBloom);
}

void VoxelRenderer::InitGui()
{
  ZoneScoped;
  spdlog::info("Initializing GUI.");
  LoadRendererConfig();
  // Attempt to load default layout, if it exists
  if (std::filesystem::exists(g_defaultIniPath) && !std::filesystem::is_directory(g_defaultIniPath))
  {
    ImGui::LoadIniSettingsFromDisk(g_defaultIniPath.c_str());
  }

  // Settings are only saved when the user explicitly requests it
  ImGui::GetIO().IniFilename = nullptr;

  float xscale, yscale;
  glfwGetWindowContentScale(head_->window, &xscale, &yscale);
  const auto contentScale = std::max(xscale, yscale); // I don't know how to properly handle the case where xdpi != ydpi. Hopefully that never happens
  const float fontSize    = std::floor(18 * contentScale);
  ImGui::GetIO().Fonts->AddFontFromFileTTF((GetTextureDirectory() / "RobotoCondensed-Regular.ttf").string().c_str(), fontSize);
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
    ImGui::Text("Global Illumination Method");
    ImGui::Separator();
    if (ImGui::RadioButton("None", giMethod_ == GIMethod::None))
    {
      sRendererConfigModified = true;
      giMethod_               = GIMethod::None;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Path Tracing", giMethod_ == GIMethod::PerPixelPathTracing))
    {
      sRendererConfigModified = true;
      giMethod_               = GIMethod::PerPixelPathTracing;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("DDGI", giMethod_ == GIMethod::DDGI))
    {
      sRendererConfigModified = true;
      giMethod_               = GIMethod::DDGI;
    }
    sRendererConfigModified |= ImGui::SliderInt("PT Samples", &pathTracerSamples, 1, 32);
    sRendererConfigModified |= ImGui::SliderInt("PT Bounces", &pathTracerBounces, 0, 8);
    sRendererConfigModified |= ImGui::Checkbox("Bloom", &enableBloom);

    ImGui::Separator();
    ImGui::BeginDisabled(!sRendererConfigModified);
    if (ImGui::Button("Apply###apply"))
    {
      auto pathtracer = toml::table();
      pathtracer.insert_or_assign("samples", pathTracerSamples);
      pathtracer.insert_or_assign("bounces", pathTracerBounces);
      sRendererConfig.insert_or_assign("pathtracer", pathtracer);
      auto bloom = toml::table();
      bloom.insert_or_assign("enable", enableBloom);
      sRendererConfig.insert_or_assign("bloom", bloom);
      auto gi = toml::table();
      gi.insert_or_assign("method", giMethod_);
      sRendererConfig.insert_or_assign("gi", gi);
      SaveRendererConfig();
      sRendererConfigModified = false;
    }
    ImGui::EndDisabled();
    return true;
  }
  return false;
}

void VoxelRenderer::OnGui([[maybe_unused]] DeltaTime dt, World& world, [[maybe_unused]] VkCommandBuffer commandBuffer)
{
  ZoneScoped;

  // Toggle pause state if the user presses Escape.
  if (ImGui::GetKeyPressedAmount(ImGuiKey_Escape, 10000, 1))
  {
    auto& state = world.GetRegistry().ctx().get<GameState>();
    if (state == GameState::PAUSED)
    {
      state = GameState::GAME;
    }
    else if (state == GameState::PAUSED_SETTINGS)
    {
      LoadRendererConfig();
      state = GameState::GAME;
    }
    else if (state == GameState::GAME)
    {
      state = GameState::PAUSED;
    }
  }

  switch (auto& gameState = world.GetRegistry().ctx().get<GameState>())
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

    auto localPlayer = world.TryGetLocalPlayer();
    if (localPlayer != entt::null)
    {
      if (auto* gp = world.GetRegistry().try_get<const GhostPlayer>(localPlayer))
      {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        constexpr auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("###death_window", nullptr, flags))
        {
          ImGui::Text("You died");
          ImGui::Text("%.0fs", gp->remainingSeconds);
        }
        ImGui::End();
      }
    }

    if (ImGui::Begin("Test"))
    {
      ImGui::Text("Framerate: %.0f (%.2fms)", 1 / dt.real, dt.real * 1000);
      auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
      VmaStatistics stats{};
      vmaGetVirtualBlockStatistics(grid.buffer.GetAllocator(), &stats);
      auto [usedSuffix, usedDivisor]   = Math::BytesToSuffixAndDivisor(stats.allocationBytes);
      auto [blockSuffix, blockDivisor] = Math::BytesToSuffixAndDivisor(stats.blockBytes);
      ImGui::Text("Voxel memory: %.2f %s / %.2f %s", stats.allocationBytes / usedDivisor, usedSuffix, stats.blockBytes / blockDivisor, blockSuffix);

      if (ImGui::Button("Collapse da grid"))
      {
        grid.CoalesceDirtyBricks();
      }
    }
    ImGui::End();

    // Get information about the local player
    auto range = world.GetRegistry().view<Player, const LocalPlayer, Inventory, const GlobalTransform>(entt::exclude<GhostPlayer>).each();

    if (range.begin() == range.end())
    {
      return;
    }

    auto&& [playerEntity, p, inventory, gt] = *range.begin();

    // TODO: replace with bitmap font rendered above each creature
    auto collector = Physics::NearestRayCollector();
    auto dir       = GetForward(gt.rotation);
    auto start     = gt.position;
    Physics::GetNarrowPhaseQuery().CastRay(JPH::RRayCast(Physics::ToJolt(start), Physics::ToJolt(dir * 20.0f)),
      JPH::RayCastSettings(),
      collector,
      Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_CHARACTER),
      Physics::GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_CHARACTER));
    if (ImGui::Begin("Target"))
    {
      if (auto* h = world.GetRegistry().try_get<const Health>(playerEntity))
      {
        ImGui::Text("Player HP: %.2f", h->hp);
      }
      if (collector.nearest)
      {
        auto entity = static_cast<entt::entity>(Physics::GetBodyInterface().GetUserData(collector.nearest->mBodyID));
        if (auto* n = world.GetRegistry().try_get<const Name>(entity))
        {
          ImGui::Text("%s", n->name.c_str());
        }

        if (auto* h = world.GetRegistry().try_get<const Health>(entity))
        {
          ImGui::Text("Health: %.2f", h->hp);
        }
      }
    }
    ImGui::End();

    DrawInventory(world, playerEntity, playerEntity, inventory, !p.inventoryIsOpen);

    if (world.GetRegistry().valid(p.openContainerId))
    {
      if (auto* ip = world.GetRegistry().try_get<Inventory>(p.openContainerId))
      {
        p.inventoryIsOpen = true;
        DrawInventory(world, p.openContainerId, playerEntity, *ip);
      }
    }

    if (p.showInteractPrompt)
    {
      ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.55f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      constexpr auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground;
      if (ImGui::Begin("Interact", nullptr, flags))
      {
        ImGui::Text("Press F to pay respects");
      }
      ImGui::End();
    }

    if (p.inventoryIsOpen)
    {
      if (ImGui::Begin("Crafting"))
      {
        // Get set of blocks around player. This is used to find the "crafting stations" that are near the player, which some recipes call for.
        auto nearVoxels  = std::unordered_set<BlockId>();
        const auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
        for (int z = -5; z <= 5; z++)
          for (int y = -5; y <= 5; y++)
            for (int x = -5; x <= 5; x++)
            {
              const auto vp = glm::vec3(x, y, z);
              const auto fp = glm::ivec3(glm::floor(gt.position));
              if (Math::Distance2(glm::vec3(fp) + vp + 0.5f, gt.position) <= 5 * 5)
              {
                nearVoxels.emplace(grid.GetVoxelAt(glm::ivec3(vp) + fp));
              }
            }

        const auto& crafting      = world.GetRegistry().ctx().get<Crafting>();
        const auto& itemRegistry  = world.GetRegistry().ctx().get<ItemRegistry>();
        const auto& blockRegistry = world.GetRegistry().ctx().get<BlockRegistry>();
        for (int index = 0; const auto& recipe : crafting.recipes)
        {
          if (index != 0)
          {
            ImGui::Separator();
          }
          ImGui::PushID(index);
          ImGui::BeginDisabled(!inventory.CanCraftRecipe(recipe) || !nearVoxels.contains(recipe.craftingStation));
          if (ImGui::Button("Craft"))
          {
            Networking::CallRPC("TryCraftRecipeRPC"_hs, world, playerEntity, recipe);
          }
          ImGui::Text("Output");
          ImGui::Indent();
          for (const auto& output : recipe.output)
          {
            const auto& def = itemRegistry.Get(output.item);
            ImGui::TextWrapped("%s: %d", def.GetName().c_str(), output.count);
          }
          ImGui::Unindent();

          ImGui::Text("Ingredients");
          ImGui::Indent();
          for (const auto& ingredient : recipe.ingredients)
          {
            const auto& def = itemRegistry.Get(ingredient.item);
            ImGui::TextWrapped("%s: %d", def.GetName().c_str(), ingredient.count);
          }
          ImGui::Unindent();
          if (recipe.craftingStation != voxel_t::Air)
          {
            ImGui::Text("Required: %s", blockRegistry.Get(recipe.craftingStation).GetName().c_str());
          }
          ImGui::EndDisabled();
          ImGui::PopID();
          index++;
        }
      }
      ImGui::End();
    }
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

      auto& networking = world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>();

      ImGui::BeginDisabled(networking->get());
      if (ImGui::Selectable("Open Server"))
      {
        *networking = std::make_unique<Networking::Server>(world);
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
        Core::Serialization::SaveRegistryToFile(world, sWorldSavesDirectory / (world.GetRegistry().ctx().get<std::string>("WorldName"_hs) + ".rizz"));
      }
      ImGui::EndDisabled();

      /*
      if (ImGui::Selectable("Load (WIP)"))
      {
        if (!std::filesystem::is_directory(sWorldSavesDirectory))
        {
          std::filesystem::create_directories(sWorldSavesDirectory);
        }
        const auto worldName = world.GetRegistry().ctx().get<std::string>("WorldName"_hs);

        world.GetRegistryRaw().clear();
        world.GetRegistryRaw() = {};
        world.GetRegistry().ctx().emplace_as<std::string>("WorldName"_hs, worldName);
        world.GetRegistry().ctx().emplace_as<std::atomic<const char*>>("progressText"_hs, "");
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("progress"_hs, 0);
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("total"_hs, 1);
        CreateContextVariablesAndObservers(world);
        world.GetRegistry().ctx().get<GameState>() = GameState::PAUSED;
        Core::Serialization::LoadRegistryFromFile(world, sWorldSavesDirectory / (worldName + ".rizz"));
      }
      */

      if (ImGui::Selectable("Exit to main menu"))
      {
        networking->reset();
        world.GetRegistryRaw().clear();
        world.GetRegistryRaw() = {};
        CreateContextVariablesAndObservers(world);
        gameState = GameState::MENU;
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
        world.GetRegistry().ctx().emplace_as<std::atomic<const char*>>("progressText"_hs, "");
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("progress"_hs, 0);
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("total"_hs, 1);
        CreateContextVariablesAndObservers(world);
        world.GetRegistry().ctx().get<GameState>() = GameState::LOADING_SP;
        world.GetRegistry().ctx().emplace_as<std::future<void>>("loading"_hs,
          std::async(std::launch::async,
            [&world, path = sSelectedWorld.value()]
            {
              Core::Serialization::LoadRegistryFromFile(world, path);
              world.GetRegistry().ctx().emplace_as<std::string>("WorldName"_hs, path.stem().string());
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

      if (ImGui::BeginPopupModal("Delete World", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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

      static int selectedWorldSize = 0;
      auto MakeWorld = [&]
      {
        constexpr auto worldSizes = std::array{glm::ivec3{2, 5, 2}, glm::ivec3{4, 5, 4}, glm::ivec3{8, 5, 8}, glm::ivec3{10, 5, 10}};
        world.InitializeGameState();
        world.CreateGrid(worldSizes.at(selectedWorldSize));
        world.CreateInitialEntities();
        // emplace_as doesn't overwrite context variables, so we have to first erase them (erase returns false if it failed, which is ok).
        world.GetRegistry().ctx().erase<std::string>("WorldName"_hs);
        world.GetRegistry().ctx().erase<std::atomic<const char*>>("progressText"_hs);
        world.GetRegistry().ctx().erase<std::atomic_int32_t>("progress"_hs);
        world.GetRegistry().ctx().erase<std::atomic_int32_t>("total"_hs);
        world.GetRegistry().ctx().erase<std::future<void>>("loading"_hs);
        world.GetRegistry().ctx().emplace_as<std::string>("WorldName"_hs, sNewWorldName.c_str()); // Construct from C string so extra NULs aren't included.
        world.GetRegistry().ctx().emplace_as<std::atomic<const char*>>("progressText"_hs, "");
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("progress"_hs, 0);
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("total"_hs, 1);
        world.GetRegistry().ctx().emplace_as<std::future<void>>("loading"_hs,
          std::async(std::launch::async,
            [&world]
            {
              world.GenerateMap({});
              world.GetRegistry().ctx().get<std::atomic<const char*>>("progressText"_hs) = "Saving";
              // Save world right after creating it.
              if (!std::filesystem::is_directory(sWorldSavesDirectory))
              {
                std::filesystem::create_directories(sWorldSavesDirectory);
              }
              Core::Serialization::SaveRegistryToFile(world, sWorldSavesDirectory / (world.GetRegistry().ctx().get<std::string>("WorldName"_hs) + ".rizz"));
            }));
        gameState = GameState::LOADING_SP;
        ImGui::CloseCurrentPopup();
      };
      if (ImGui::BeginPopupModal("New World##1", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      {
        ImGui::PushItemWidth(200);
        ImGui::InputText("Name", sNewWorldName.data(), sNewWorldName.size());
        if (ImGui::IsWindowAppearing())
        {
          selectedWorldSize = 0;
        }
        // TODO: Larger sizes crash the game because they overflow voxel memory during world generation. The fix is to compress chunks after generating them.
        //ImGui::Combo("Size", &selectedWorldSize, "Tiny\0Small\0Medium\0Big");
        ImGui::Combo("Size", &selectedWorldSize, "Tiny\0Small");

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
        if (ImGui::BeginPopupModal("Overwrite World", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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
      gameState = GameState::GAME;
    }
    else
    {
      // Show loading bar.
      const auto& progressText = world.GetRegistry().ctx().get<std::atomic<const char*>>("progressText"_hs);
      const auto& progress     = world.GetRegistry().ctx().get<std::atomic_int32_t>("progress"_hs);
      const auto& total        = world.GetRegistry().ctx().get<std::atomic_int32_t>("total"_hs);
      if (ImGui::Begin("Loading"))
      {
        ImGui::Text("%s", progressText.load());
        constexpr auto bgColor = ImColor(0.4f, 0.4f, 0.4f, 1.0f);
        constexpr auto fgColor = ImColor(1.0f, 1.0f, 1.0f, 1.0f);
        LoadingBar("##loading", float(progress.load()) / total.load(), ImVec2(ImGui::GetContentRegionAvail().x, 15), bgColor, fgColor);
      }
      ImGui::End();
    }
    break;
  }
  case GameState::LOADING_MP:
  {
    if (auto& networking = world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>(); *networking)
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
        LoadRendererConfig();
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
        LoadRendererConfig();
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
        auto& networking = world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>();
        const auto table  = sServerList.get_as<toml::table>(sSelectedServerName);
        ImGui::BeginDisabled(!table);
        if (ImGui::Button("Join Server"))
        {
          auto address               = (*table)["address"].value_or(std::string_view("localhost"));
          [[maybe_unused]] auto port = (*table)["port"].value_or(1234);
          *networking = std::make_unique<Networking::Client>(world, std::string(address).c_str());
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

        if (ImGui::BeginPopupModal("Delete Server", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
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

  if (world.GetRegistry().ctx().get<Debugging>().showDebugGui)
  {
    ShowEditor(dt, world);

    if (ImGui::Begin("Context"))
    {
      auto& ctx   = world.GetRegistry().ctx();
      auto& debug = ctx.get<Debugging>();
      ImGui::Checkbox("Show Debug GUI", &debug.showDebugGui);
      ImGui::Checkbox("Force Show Cursor", &debug.forceShowCursor);
      ImGui::Text("DDGI");
      ImGui::Separator();
      if (ImGui::RadioButton("None", ddgiDebugView_ == DDGIDebugView::None))
      {
        ddgiDebugView_ = DDGIDebugView::None;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Luminance", ddgiDebugView_ == DDGIDebugView::Luminance))
      {
        ddgiDebugView_ = DDGIDebugView::Luminance;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Illuminance", ddgiDebugView_ == DDGIDebugView::Illuminance))
      {
        ddgiDebugView_ = DDGIDebugView::Illuminance;
      }
      ImGui::Checkbox("Draw Debug Probe", &debug.drawDebugProbe);
      ImGui::Checkbox("Draw Physics Shapes", &debug.drawPhysicsShapes);
      ImGui::Checkbox("Draw Physics Velocity", &debug.drawPhysicsVelocity);

      ImGui::Text("Game state: %s", Core::Reflection::EnumToString(ctx.get<GameState>()));
      ImGui::Text("Time: %f", ctx.get<float>("time"_hs));

      ImGui::SliderFloat("Time Scale", &ctx.get<TimeScale>().scale, 0, 4, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
      auto min = uint32_t(5);
      auto max = uint32_t(120);
      ImGui::SliderScalar("Tick Rate", ImGuiDataType_U32, &world.GetRegistry().ctx().get<TickRate>().hz, &min, &max, "%u", ImGuiSliderFlags_AlwaysClamp);

      if (ImGui::Selectable("Save UI layout"))
      {
        ImGui::SaveIniSettingsToDisk(g_defaultIniPath.c_str());
      }
    }
    ImGui::End();

    if (ImGui::Begin("TEST PROBULUS"))
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

    auto range = world.GetRegistry().view<const Player, const LocalPlayer, Inventory, const GlobalTransform>().each();

    if (range.begin() == range.end())
    {
      return;
    }

    auto&& [playerEntity, p, inventory, gt] = *range.begin();

    if (ImGui::Begin("It's free real estate"))
    {
      const auto& itemRegistry = world.GetRegistry().ctx().get<ItemRegistry>();
      for (int i = 0; const auto& itemDefinition : itemRegistry.GetAllItemDefinitions())
      {
        ImGui::PushID(i);
        if (ImGui::Button(itemDefinition->GetName().c_str(), {-1, 0}))
        {
          auto item = ItemState{static_cast<ItemId>(i), 1};
          inventory.TryStackItem(world, item);
          if (item.count > 0)
          {
            if (auto slot = inventory.GetFirstEmptySlot())
            {
              inventory.OverwriteSlot(world, *slot, item, playerEntity);
            }
            else
            {
              world.CreateDroppedItem(item, gt.position, gt.rotation, gt.scale);
            }
          }
        }
        ImGui::PopID();
        i++;
      }
    }
    ImGui::End();
  }

  if (auto& networking = world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>(); *networking)
  {
    if (ImGui::Begin("Networking"))
    {
      if (ImGui::BeginTable("Clients", 4))
      {
        ImGui::TableSetupColumn("Client ID");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Ping (variance)");
        ImGui::TableSetupColumn("Packet loss");
        ImGui::TableHeadersRow();
        for (const auto& info : networking->get()->GetClientNetworkInfos())
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
    ImGui::End();
  }
}