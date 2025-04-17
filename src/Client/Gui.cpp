#include "VoxelRenderer.h"

#include "Game/Assets.h"
#include "MathUtilities.h"
#include "imgui.h"
#include "Core/Reflection.h"
#include "Core/Serialization.h"
#include "Game/Networking/Client.h"
#include "Game/Networking/Server.h"
#include "Core/Assert2.h"

#include "Game/Physics/Physics.h" // TODO: remove
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Game/Physics/PhysicsUtils.h"
#ifdef JPH_DEBUG_RENDERER
#include "Game/Physics/DebugRenderer.h"
#endif
#include "Game/Networking/RPC.h"

#include "vk_mem_alloc.h"
#include "tracy/Tracy.hpp"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "entt/meta/meta.hpp"
#include "entt/meta/factory.hpp"
#include "entt/meta/container.hpp"

#include <memory>
#include <numeric>
#include <type_traits>
#include <future>
#include <atomic>

namespace
{
  std::string FixupTypeString(std::string_view str)
  {
    if (auto pos = str.find("::"); pos != std::string_view::npos)
    {
      return std::string(str.substr(pos + 2));
    }

    if (auto pos = str.find_first_of(' '); pos != std::string_view::npos)
    {
      return std::string(str.substr(pos + 1));
    }

    return std::string(str);
  }

  bool DrawComponentHelper(World& world, entt::entity entity, entt::meta_any instance, entt::meta_custom custom, bool readonly, int& guiId)
  {
    using namespace Core::Reflection;
    auto meta = instance.type();
    readonly  = readonly || meta.traits<Traits>() & Traits::EDITOR_READ_ONLY;

    // If the type has a bespoke EditorWrite or EditorRead function, use that. Otherwise, recurse over data members.
    PropertiesMap properties = {};
    if (auto* mp = static_cast<const PropertiesMap*>(custom))
    {
      properties = *mp;
    }

    bool changed = false;
    if (auto writeFunc = meta.func("EditorWrite"_hs); writeFunc && !readonly)
    {
      changed |= writeFunc.invoke(instance, properties).cast<bool>();
    }
    else if (auto readFunc = meta.func("EditorRead"_hs))
    {
      readFunc.invoke(instance, properties);
    }
    else if (meta.is_sequence_container())
    {
      bool isOpen = false;
      if (auto it = properties.find("name"_hs); it != properties.end())
      {
        isOpen = ImGui::TreeNodeEx(instance.try_cast<void>(), 0, "%s: %d", it->second.cast<const char*>(), (int)instance.as_sequence_container().size());
      }
      else
      {
        auto name = FixupTypeString(meta.info().name());
        isOpen    = ImGui::TreeNodeEx(instance.try_cast<void>(), 0, "%s: %d", name.c_str(), (int)instance.as_sequence_container().size());
      }
      if (isOpen)
      {
        for (auto element : instance.as_sequence_container())
        {
          auto eType = element.type();
          ImGui::PushID(guiId++);
          changed |= DrawComponentHelper(world, entity, element.as_ref(), eType.custom(), readonly, guiId);
          ImGui::PopID();
        }
        ImGui::TreePop();
      }
    }
    else if (meta.is_associative_container())
    {
      ImGui::Text("TODO: associative containers");
      // TODO: Make two-column table.
      for (auto element : instance.as_associative_container())
      {
        // auto eType = element.second.type();
        // if (auto traits = eType.traits<Traits>(); traits & Traits::EDITOR || traits & Traits::EDITOR_READ)
        //{
        //   ImGui::PushID(guiId++);
        //   ImGui::Indent();
        //   DrawComponentHelper(element.second.get(eType.id()), eType.custom(), readonly || traits & Traits::EDITOR_READ, guiId);
        //   ImGui::Unindent();
        //   ImGui::PopID();
        // }
      }
    }
    else if (meta.is_enum())
    {
      bool isOpen = false;
      if (auto it = properties.find("name"_hs); it != properties.end())
      {
        isOpen = ImGui::TreeNodeEx(instance.try_cast<void>(), 0, "%s", it->second.cast<const char*>());
      }
      else
      {
        auto name = FixupTypeString(meta.info().name());
        isOpen    = ImGui::TreeNodeEx(instance.try_cast<void>(), 0, "%s", name.c_str());
      }
      if (isOpen)
      {
        for (auto [id, data] : meta.data())
        {
          PropertiesMap dataProps = {};
          if (auto* mp = static_cast<const PropertiesMap*>(data.custom()))
          {
            dataProps = *mp;
          }

          if (auto it = dataProps.find("name"_hs); it != dataProps.end())
          {
            ImGui::PushID(guiId++);
            auto name = it->second.cast<const char*>();
            if (ImGui::Selectable(name, instance == data.get({}), readonly ? ImGuiSelectableFlags_Disabled : 0))
            {
              instance.assign(data.get({}));
              changed = true;
            }
            ImGui::PopID();
          }
        }
        ImGui::TreePop();
      }
    }
    else
    {
      for (auto [id, data] : meta.data())
      {
        if (const auto traits = data.traits<Traits>(); !(traits & Traits::NO_EDITOR))
        {
          ImGui::PushID(guiId++);
          ImGui::Indent();
          changed |= DrawComponentHelper(world, entity, data.get(instance), data.custom(), readonly || traits & Traits::EDITOR_READ_ONLY, guiId);
          ImGui::Unindent();
          ImGui::PopID();
        }
      }
    }

    if (auto onUpdateFunc = meta.func("OnUpdate"_hs); onUpdateFunc && changed)
    {
      onUpdateFunc.invoke({}, entt::forward_as_meta(world), entity);
    }

    return changed;
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
} // namespace

void VoxelRenderer::OnGui([[maybe_unused]] DeltaTime dt, World& world, [[maybe_unused]] VkCommandBuffer commandBuffer)
{
  ZoneScoped;
  switch (auto& gameState = world.GetRegistry().ctx().get<GameState>())
  {
  case GameState::MENU:
    if (ImGui::Begin("Menu"))
    {
      if (ImGui::Button("Play"))
      {
        gameState = GameState::LOADING;
        world.InitializeGameState();
        // emplace_as doesn't overwrite context variables, so we have to first erase them (erase returns false if it failed, which is ok).
        world.GetRegistry().ctx().erase<std::atomic<const char*>>("progressText"_hs);
        world.GetRegistry().ctx().erase<std::atomic_int32_t>("progress"_hs);
        world.GetRegistry().ctx().erase<std::atomic_int32_t>("total"_hs);
        world.GetRegistry().ctx().erase<std::future<void>>("loading"_hs);
        world.GetRegistry().ctx().emplace_as<std::atomic<const char*>>("progressText"_hs, "");
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("progress"_hs, 0);
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("total"_hs, 1);
        world.GetRegistry().ctx().emplace_as<std::future<void>>("loading"_hs, std::async(std::launch::async, [&world] { world.GenerateMap(); }));
      }

      auto& networking          = world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>();
      static char hostName[256] = "localhost";
      ImGui::InputText("##Host", hostName, 256);
      ImGui::BeginDisabled(networking->get() != nullptr);
      if (ImGui::Button("Connect (WIP)"))
      {
        *networking = std::make_unique<Networking::Client>(world, hostName);
        gameState   = GameState::LOADING;
      }
      ImGui::EndDisabled();

      if (ImGui::Button("Exit to desktop"))
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
    if (ImGui::Begin("Paused"))
    {
      if (ImGui::Button("Unpause"))
      {
        gameState = GameState::GAME;
      }

      auto& networking = world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>();

      ImGui::BeginDisabled(networking->get());
      if (ImGui::Button("Open Server (WIP)"))
      {
        *networking = std::make_unique<Networking::Server>(world);
      }
      ImGui::EndDisabled();

      ImGui::BeginDisabled(!networking->get() || !world.IsServer());
      if (ImGui::Button("Close Server (WIP)"))
      {
        networking->reset();
      }
      ImGui::EndDisabled();

      if (ImGui::Button("Save (WIP)"))
      {
        Core::Serialization::SaveRegistryToFile(world, "TEST.bin");
      }

      if (ImGui::Button("Load (WIP)"))
      {
        Core::Serialization::LoadRegistryFromFile(world, "TEST.bin");
      }

      if (ImGui::Button("Exit to main menu"))
      {
        networking->reset();
        world.GetRegistryRaw().clear();
        world.GetRegistryRaw() = {};
        CreateContextVariablesAndObservers(world);
        gameState = GameState::MENU;
      }

      if (ImGui::Button("Exit to desktop"))
      {
        world.GetRegistry().ctx().emplace<CloseApplication>();
      }
    }
    ImGui::End();
    break;
  case GameState::LOADING:
  {
    if (auto& networking = world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>(); *networking)
    {
      auto* client = dynamic_cast<Networking::Client*>(networking->get());
      ASSERT(client);
      if (ImGui::Begin("Loading"))
      {
        ImGui::Text("%s", Core::Reflection::EnumToString(client->GetStatus()));

        if (ImGui::Button("Cancel"))
        {
          networking->reset();
          gameState = GameState::MENU;
        }
      }
      ImGui::End();
      break;
    }

    // There is an ongoing connection attempt or the world is loading.
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
        ImGui::Text("%s: %d / %d", progressText.load(), progress.load(), total.load());
      }
      ImGui::End();
    }
    break;
  }
  default: assert(0);
  }

  if (world.GetRegistry().ctx().get<Debugging>().showDebugGui)
  {
    auto& registry = world.GetRegistryRaw();
    if (ImGui::Begin("Entities"))
    {
      ZoneScopedN("Entities");
      if (!ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() && ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left])
      {
        selectedEntity = entt::null;
      }

      // Show entity hierarchy.
      for (auto e : registry.view<entt::entity>())
      {
        ImGui::PushID((int)e);
        bool opened = false;

        int flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selectedEntity == e)
        {
          flags |= ImGuiTreeNodeFlags_Selected;
        }

        if (auto* s = registry.try_get<const Name>(e))
        {
          opened = ImGui::TreeNodeEx("entity", flags, "%u (%s) (v%u)", entt::to_entity(e), s->name.c_str(), entt::to_version(e));
        }
        else
        {
          opened = ImGui::TreeNodeEx("entity", flags, "%u (v%u)", entt::to_entity(e), entt::to_version(e));
        }

        // Single-clicking anywhere should select the node
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
          selectedEntity = e;
        }

        if (opened)
        {
          ImGui::TextUnformatted("TODO lol");
          ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
      }
    }
    ImGui::End();

    if (ImGui::Begin("Components"))
    {
      ZoneScopedN("Components");
      if (registry.valid(selectedEntity))
      {
        auto e = selectedEntity;

        if (ImGui::Button("Delete Entity"))
        {
          registry.emplace_or_replace<DeferredDelete>(e);
        }
        ImGui::SameLine();
        if (ImGui::BeginCombo("##add", "Add Component"))
        {
          using MetaPair = decltype(*entt::resolve().begin());
          auto metas     = std::vector<MetaPair>();
          for (auto pair : entt::resolve())
          {
            if ((registry.storage(pair.first) && !registry.storage(pair.first)->contains(e)))
            {
              metas.emplace_back(pair);
            }
          }
          std::sort(metas.begin(),
            metas.end(),
            [](const MetaPair& p1, const MetaPair& p2) { return FixupTypeString(p1.second.info().name()) < FixupTypeString(p2.second.info().name()); });

          for (auto [id, meta] : metas)
          {
            if (meta.traits<Core::Reflection::Traits>() & Core::Reflection::Traits::COMPONENT)
            {
              const auto label        = FixupTypeString(meta.info().name());
              auto addFunc            = meta.func("add"_hs);
              auto emplaceDefaultFunc = meta.func("EmplaceDefault"_hs);
              int flags               = 0;
              if ((!addFunc && !emplaceDefaultFunc) || meta.traits<Core::Reflection::Traits>() & Core::Reflection::EDITOR_READ_ONLY)
              {
                flags |= ImGuiSelectableFlags_Disabled;
              }
              if (ImGui::Selectable(label.c_str(), false, flags))
              {
                if (addFunc)
                {
                  addFunc.invoke({}, &world, e); // Can't figure out how to invoke with a reference (std::ref doesn't work), so pointers it is.
                }
                else if (emplaceDefaultFunc)
                {
                  emplaceDefaultFunc.invoke({}, &registry, e);
                }
                else
                {
                  // Sad face :(
                  assert(false);
                }
              }
            }
          }
          ImGui::EndCombo();
        }

        // Sort component types by name.
        struct TypeInfo
        {
          entt::meta_type meta;
          entt::sparse_set* set;
          std::string fixupString;
        };
        static bool isInitialized = false; // Naughty hack to make this sorting only happen once.
        static auto storages      = std::vector<TypeInfo>();
        if (!isInitialized)
        {
          isInitialized = true;
          for (auto pair : world.GetRegistryRaw().storage())
          {
            auto meta = entt::resolve(pair.first);
            storages.emplace_back(meta, &pair.second, meta ? FixupTypeString(meta.info().name()) : std::string());
          }
          std::sort(storages.begin(),
            storages.end(),
            [](const TypeInfo& p1, const TypeInfo& p2)
            {
              if (p1.meta && p2.meta)
              {
                return p1.fixupString < p2.fixupString;
              }
              return p1.meta.id() < p2.meta.id();
            });
        }

        for (int i = 0; auto&& [meta, storage, _] : storages)
        {
          if (!storage->contains(e))
          {
            continue;
          }

          ImGui::PushID(i++);
          if (ImGui::Button("X"))
          {
            storage->remove(e);
          }
          ImGui::SameLine();
          ImGui::SeparatorText(FixupTypeString(storage->type().name()).c_str());

          if (storage->contains(e) && meta)
          {
            DrawComponentHelper(world,
              e,
              meta.from_void(storage->value(e)),
              meta.custom(),
              meta.traits<Core::Reflection::Traits>() & Core::Reflection::Traits::EDITOR_READ_ONLY,
              i);
          }
          else
          {
            ImGui::Text("Reflection is unavailable for this type.");
          }
          ImGui::PopID();
        }
      }
    }
    ImGui::End();

    if (ImGui::Begin("Context"))
    {
      auto& ctx   = world.GetRegistry().ctx();
      auto& debug = ctx.get<Debugging>();
      ImGui::Checkbox("Show Debug GUI", &debug.showDebugGui);
      ImGui::Checkbox("Force Show Cursor", &debug.forceShowCursor);
      ImGui::Checkbox("Draw Debug Probe", &debug.drawDebugProbe);
      ImGui::Checkbox("Draw Physics Shapes", &debug.drawPhysicsShapes);
      ImGui::Checkbox("Draw Physics Velocity", &debug.drawPhysicsVelocity);

      ImGui::Text("Game state: %s", GameStateToStr(ctx.get<GameState>()));
      ImGui::Text("Time: %f", ctx.get<float>("time"_hs));

      ImGui::SliderFloat("Time Scale", &ctx.get<TimeScale>().scale, 0, 4, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
      auto min = uint32_t(5);
      auto max = uint32_t(120);
      ImGui::SliderScalar("Tick Rate", ImGuiDataType_U32, &world.GetRegistry().ctx().get<TickRate>().hz, &min, &max, "%u", ImGuiSliderFlags_AlwaysClamp);
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