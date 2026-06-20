#include "Client/VoxelRenderer.h"
#include "Game/WeatherDirector.h"
#include "Game/Globals.h"
#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Item.h"
#include "Core/Reflection.h"
#include "Game/NpcDirector.h"

#include "imgui.h"
#include "rapidfuzz/fuzz.hpp"
#include "entt/meta/meta.hpp"

#include <print>

namespace
{
  void ShowEntityPrefabTab(World& world)
  {
    ZoneScoped;
    const auto localPlayer      = world.TryGetLocalPlayer();
    const auto* playerTransform = world.GetRegistry().try_get<const GlobalTransform>(localPlayer);
    ImGui::BeginDisabled(localPlayer == entt::null);
    const auto& prefabs = world.globals->entityPrefabRegistry;
    for (const auto& prefab : prefabs->GetAllPrefabs())
    {
      ImGui::PushID(prefab->GetCreateInfo().name.c_str());
      if (ImGui::Button(" x1 "))
      {
        prefab->Spawn(world, playerTransform->position + 5.0f * GetForward(playerTransform->rotation));
      }
      ImGui::SameLine();
      if (ImGui::Button(" x5 "))
      {
        for (int i = 0; i < 5; i++)
        {
          auto& rng         = world.globals->game->rng;
          const auto jitter = 1.5f * glm::vec3(rng.RandFloat(-1, 1), rng.RandFloat(-1, 1), rng.RandFloat(-1, 1));
          prefab->Spawn(world, jitter + playerTransform->position + 5.0f * GetForward(playerTransform->rotation));
        }
      }
      ImGui::SameLine();
      ImGui::Text("%s", prefab->GetCreateInfo().name.c_str());
      ImGui::PopID();
    }
    ImGui::EndDisabled();
  }

  void ShowItemTab(World& world)
  {
    ZoneScoped;
    auto range = world.GetRegistry().view<const Player, const LocalPlayer, Inventory, const GlobalTransform>().each();

    if (range.begin() == range.end())
    {
      return;
    }

    auto&& [playerEntity, p, inventory, gt] = *range.begin();
    static char buffer[256]{};
    ImGui::Text("Filter");
    ImGui::SameLine();
    ImGui::InputText("##Filter", buffer, 256);
    const auto len = std::strlen(buffer);

    const auto& itemRegistry = world.globals->itemRegistry;
    auto scores              = std::multimap<double, decltype(*itemRegistry->GetNameToIdMap().begin()), std::greater<double>>();
    for (const auto& pair : itemRegistry->GetNameToIdMap())
    {
      scores.emplace(rapidfuzz::fuzz::partial_ratio(buffer, pair.first), pair);
    }

    for (int i = 0; const auto& [score, pair] : scores)
    {
      if (len != 0 && score == 0)
      {
        continue;
      }
      const auto& [tag, id] = pair;
      ImGui::PushID(i);
      if (ImGui::Button(tag.c_str(), {-1, 0}))
      {
        auto item = ItemState{static_cast<ItemId>(id), 1};
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

  void ShowNpcDirectorTab(World& world)
  {
    ImGui::SeparatorText("House interior constraints");
    static auto params = Game2::NpcDirector::HousingParams{};
    ImGui::SliderInt("minWidth", &params.minWidth, 1, 10);
    ImGui::SliderInt("maxWidth", &params.maxWidth, 1, 10);
    ImGui::SliderInt("minHeight", &params.minHeight, 1, 10);
    ImGui::SliderInt("maxHeight", &params.maxHeight, 1, 10);
    if (ImGui::Button("Check housing validity"))
    {
      if (const auto* xform = world.TryGetLocalPlayerTransform())
      {
        auto hit = Voxel::Grid::HitSurfaceParameters{};
        if (world.globals->grid->TraceRaySimple(xform->position, GetForward(xform->rotation), 10, hit))
        {
          const bool res = Game2::NpcDirector::CheckIsValidHousing(world, glm::ivec3(hit.voxelPosition + hit.flatNormalWorld), params);
          std::println("is valid housing? {}", res ? "YES" : "NO");
        }
      }
    }
  }
}

void VoxelRenderer::ShowGameDebugWindow(World& world)
{
  if (ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
  {
    if (ImGui::BeginTabBar("Options"))
    {
      if (ImGui::BeginTabItem("Core"))
      {
        ImGui::Text("World: %s", world.globals->worldName.c_str());
        if (ImGui::Button("Enable noclip"))
        {
          if (auto player = world.TryGetLocalPlayer(); player != entt::null)
          {
            world.GetRegistry().emplace_or_replace<NoclipCharacterController>(player);
          }
        }

        auto& debug = world.globals->game->debugging;
        ImGui::Text("Game state: %s", Core::Reflection::EnumToString(world.globals->game->gameState));
        ImGui::Text("Time: %f", world.globals->game->time);
        ImGui::Checkbox("Spawn NPCs", &world.globals->game->updateNpcSpawnDirector);
        ImGui::Checkbox("Infinite items", &debug.infiniteItems);

        ImGui::SliderFloat("Time Scale", &world.globals->game->timeScale.scale, 0, 4, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
        auto min = uint32_t(5);
        auto max = uint32_t(120);
        ImGui::SliderScalar("Tick Rate", ImGuiDataType_U32, &world.globals->game->tickRate.hz, &min, &max, "%u", ImGuiSliderFlags_AlwaysClamp);

        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Sun"))
      {
        auto& sunInfo = world.globals->game->sunInfo;
        ImGui::Checkbox("Freeze time", &sunInfo.pauseDayNightCycle);
        ImGui::DragFloat("Time of day", &sunInfo.timeOfDay, 0.01f, 0, 2, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
        ImGui::SliderFloat("Azimuth", &sunInfo.azimuth, -glm::two_pi<float>(), glm::two_pi<float>());
        ImGui::SliderFloat("Day length", &sunInfo.dayLength, 5, 3600, "%.0f s");

        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Weather"))
      {
        auto& weather = *world.globals->game->weatherDirector;
        if (ImGui::Button("Pick new weather"))
        {
          weather.PickNewWeather(world.globals->game->rng);
        }

        for (int i = 0; i < (int)Weather::Preset::COUNT; i++)
        {
          if (ImGui::Button(std::format("Preset {}", i).c_str()))
          {
            weather.SetWeatherToPreset((Weather::Preset)i, world.globals->game->rng);
          }
        }

        ImGui::SliderFloat("Transition speed", &weather.transitionSpeed, 0, 10, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Items"))
      {
        ShowItemTab(world);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Entity Prefabs"))
      {
        ShowEntityPrefabTab(world);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("NPCs"))
      {
        ImGui::SliderFloat("Spawn period", &world.globals->game->npcSpawnDirector.timeBetweenSpawns, 0.01f, 100.0f, "%.2fs", ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
          ImGui::SetTooltip("How often the NPC director will attempt to spawn mobs.");
        }
        ImGui::Checkbox("Disable pathfinding", &world.globals->game->disableNpcPathfinding);
        ImGui::Checkbox("Ignore players", &world.globals->game->npcsIgnorePlayers);

        ShowNpcDirectorTab(world);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}