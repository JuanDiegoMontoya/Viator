#include "Client/VoxelRenderer.h"
#include "Game/Game.h"
#include "Game/World.h"
#include "Game/Item.h"
#include "Core/StringUtilities.h"
#include "Core/Reflection.h"
#include "Game/Networking/RPC.h"
#include "Game/Globals.h"
#include "Game/Scripting.h"
#include "Client/GuiHelpers.h"

#include "imgui.h"
#include "Game/TraderNpcDialogue.h"
#include "rapidfuzz/fuzz.hpp"

namespace
{
  void DrawTooltipForItem(World& world, entt::entity parent, const ItemState& item)
  {
    if (item.id == entt::null)
    {
      return;
    }

    auto text = Item::GetName(world, item.id);

    if (Item::GetMaxStackSize(world, item.id) > 1)
    {
      text += "\n\n" + std::to_string(item.count) + " / " + std::to_string(Item::GetMaxStackSize(world, item.id));
    }

    using Item::EffectCondition;
    using Item::EffectQuantityType;
    using Item::EffectType;

    auto GetWornEffectTextMul = [&](Item::EffectType type, const char* name)
    {
      const auto effectAmount = Item::GetEffect(world, item.id, parent, EffectCondition::OnWorn, EffectQuantityType::Multiplicative, type);
      if (effectAmount != 1)
      {
        return std::format("{:+.0f}% {}\n", (effectAmount - 1) * 100, name);
      }

      return std::string();
    };
    auto GetWornEffectTextAdd = [&](Item::EffectType type, const char* name)
    {
      const auto effectAmount = Item::GetEffect(world, item.id, parent, EffectCondition::OnWorn, EffectQuantityType::Additive, type);
      if (effectAmount != 0)
      {
        return std::format("{} {}\n", Core::String::TrimTrailingZeros(std::format("{:+.2f}", effectAmount)), name);
      }

      return std::string();
    };

    auto GetHeldEffectTextMul = [&](Item::EffectType type, const char* name)
    {
      const auto effectAmount = Item::GetEffect(world, item.id, parent, EffectCondition::OnHeld, EffectQuantityType::Multiplicative, type);
      if (effectAmount != 1)
      {
        return std::format("{:+.0f}% {}\n", (effectAmount - 1) * 100, name);
      }

      return std::string();
    };
    auto GetHeldEffectTextAdd = [&](Item::EffectType type, const char* name)
    {
      const auto effectAmount = Item::GetEffect(world, item.id, parent, EffectCondition::OnHeld, EffectQuantityType::Additive, type);
      if (effectAmount != 0)
      {
        return std::format("{} {}\n", Core::String::TrimTrailingZeros(std::format("{:+.2f}", effectAmount)), name);
      }

      return std::string();
    };

    auto GetUseEffectText = [&](Item::EffectType type, const char* name)
    {
      const auto effectAmount = Item::GetEffect(world, item.id, parent, EffectCondition::OnUse, EffectQuantityType::Additive, type);
      if (effectAmount != 0)
      {
        return std::format("{} {}\n", Core::String::TrimTrailingZeros(std::format("{:+.2f}", effectAmount)), name);
      }

      return std::string();
    };

    bool written = false;

    // Use effects
    auto usedText = std::string();
    for (int i = 0; i < int(Item::EffectType::EFFECT_COUNT); i++)
    {
      const auto type = Item::EffectType(i);
      const auto name = Core::Reflection::EnumToString(type);
      usedText += GetUseEffectText(type, name);
    }

    if (!usedText.empty())
    {
      text += "\n\nOn use:\n" + usedText;
      written = true;
    }

    // Worn effects
    auto equippedText = std::string();
    for (int i = 0; i < int(Item::EffectType::EFFECT_COUNT); i++)
    {
      const auto type = Item::EffectType(i);
      const auto name = Core::Reflection::EnumToString(type);
      equippedText += GetWornEffectTextAdd(type, name);
      equippedText += GetWornEffectTextMul(type, name);
    }

    if (!equippedText.empty())
    {
      text += "\n\nWhen equipped:\n" + equippedText;
      written = true;
    }

    // Held effects
    auto heldText = std::string();
    for (int i = 0; i < int(Item::EffectType::EFFECT_COUNT); i++)
    {
      const auto type = Item::EffectType(i);
      const auto name = Core::Reflection::EnumToString(type);
      heldText += GetHeldEffectTextAdd(type, name);
      heldText += GetHeldEffectTextMul(type, name);
    }

    if (!heldText.empty())
    {
      text += std::string(!written ? "\n" : "") + "\nWhen held:\n" + heldText;
    }

    ImGui::SetTooltip("%s", text.c_str());
  }

  struct InventoryDragDropPayload
  {
    glm::ivec2 sourceRowCol;
    entt::entity sourceEntity;
  };

  struct ArmorDragDropPayload
  {
    ArmorAndAccessories::Slot slot;
    entt::entity sourceEntity;
  };

  struct Rect
  {
    ImVec2 pos;
    ImVec2 size;
  };

  // `minified`: Display just the first row of the inventory. Used to display the player's hotbar.
  // `userTransform`: Transform of the entity interacting with the container. Used to calculate throw position and direction.
  Rect DrawInventory(World& world, entt::entity parent, entt::entity user, Inventory& inventory, bool minified = false)
  {
    Rect rect{};

    const auto title = "Inventory" + std::string(parent == user ? "##self" : "##other");
    if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration))
    {
      if (parent != user)
      {
        ImGui::Text("%s", world.GetRegistry().get<Name>(parent).name.c_str());
      }
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
          auto& slot           = inventory.slots[row][col];
          std::string nameStr  = "";
          const auto cursorPos = ImGui::GetCursorPos();
          if (slot.id != entt::null)
          {
            nameStr = Item::GetName(world, slot.id);
            if (Item::GetMaxStackSize(world, slot.id) > 1)
            {
              ImGui::BeginDisabled();
              ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0, 1});
              ImGui::Selectable(std::to_string(slot.count).c_str(), false, ImGuiSelectableFlags_AllowOverlap, {50, 50});
              ImGui::PopStyleVar();
              ImGui::EndDisabled();
              // nameStr += "\n" + std::to_string(slot.count);// + "/" + std::to_string(def.GetMaxStackSize());
            }
          }
          const auto name = nameStr.c_str();

          ImGui::SetCursorPos(cursorPos);
          if (ImGui::Selectable(("##" + nameStr).c_str(), inventory.canHaveActiveItem && inventory.activeSlotCoord == currentSlotCoord, 0, {50, 50}))
          {
            Networking::CallRPC("SetActiveSlotRPC"_hs, world, parent, currentSlotCoord);
          }
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
          {
            DrawTooltipForItem(world, parent, slot);
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
              DEBUG_ASSERT(payload->DataSize == sizeof(InventoryDragDropPayload));
              const auto inventoryPayload = *static_cast<const InventoryDragDropPayload*>(payload->Data);
              Networking::CallRPC("SwapInventorySlotsRPC"_hs, world, inventoryPayload.sourceEntity, inventoryPayload.sourceRowCol, parent, currentSlotCoord);
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ARMOR_SLOT"))
            {
              DEBUG_ASSERT(payload->DataSize == sizeof(ArmorDragDropPayload));
              const auto armorPayload = *static_cast<const ArmorDragDropPayload*>(payload->Data);
              Networking::CallRPC("SwapInventorySlotAndArmorSlotRPC"_hs, world, parent, currentSlotCoord, armorPayload.sourceEntity, armorPayload.slot);
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
            DEBUG_ASSERT(payload->DataSize == sizeof(InventoryDragDropPayload));
            const auto inventoryPayload = *static_cast<const InventoryDragDropPayload*>(payload->Data);
            Networking::CallRPC("ThrowItemRPC"_hs, world, inventoryPayload.sourceEntity, user, inventoryPayload.sourceRowCol);
          }
          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ARMOR_SLOT"))
          {
            DEBUG_ASSERT(payload->DataSize == sizeof(ArmorDragDropPayload));
            const auto armorPayload = *static_cast<const ArmorDragDropPayload*>(payload->Data);
            Networking::CallRPC("ThrowItemFromArmorRPC"_hs, world, armorPayload.sourceEntity, user, armorPayload.slot);
          }
          ImGui::EndDragDropTarget();
        }
      }
      ImGui::PopStyleVar();
      rect.pos  = ImGui::GetWindowPos();
      rect.size = ImGui::GetWindowSize();
    }
    ImGui::End();

    return rect;
  }

  void DrawArmorAndAccessories(World& world, entt::entity parent, [[maybe_unused]] entt::entity user, ArmorAndAccessories& armorAndAccessories)
  {
    const auto title = "ArmorAndAccessories" + std::string(parent == user ? "##self" : "##other");
    if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration))
    {
      ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0.5f, 0.5f});
      ImGui::BeginTable(title.c_str(), 1, ImGuiTableFlags_Borders);
      for (size_t i = 0; i < ArmorAndAccessories::SLOT_COUNT; i++)
      {
        ImGui::PushID(int(i));
        ImGui::TableNextColumn();
        auto& slot          = armorAndAccessories.slots[i];
        std::string nameStr = "";
        if (slot.id != entt::null)
        {
          nameStr = Item::GetName(world, slot.id);
        }
        const auto cursorPos = ImGui::GetCursorPos();
        ImGui::Selectable(("##" + nameStr).c_str(), false, 0, {50, 50});
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
        {
          if (slot.id == entt::null)
          {
            ImGui::SetTooltip("%s", Core::Reflection::EnumToString(ArmorAndAccessories::Slot(i)));
          }
          else
          {
            DrawTooltipForItem(world, parent, slot);
          }
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
          const auto dragDropPayload = ArmorDragDropPayload{
            .slot         = ArmorAndAccessories::Slot(i),
            .sourceEntity = parent,
          };
          ImGui::SetDragDropPayload("ARMOR_SLOT", &dragDropPayload, sizeof(dragDropPayload));
          ImGui::Text("%s", nameStr.c_str());
          ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT"))
          {
            DEBUG_ASSERT(payload->DataSize == sizeof(InventoryDragDropPayload));
            const auto inventoryPayload = *static_cast<const InventoryDragDropPayload*>(payload->Data);
            Networking::CallRPC("SwapInventorySlotAndArmorSlotRPC"_hs,
              world,
              inventoryPayload.sourceEntity,
              inventoryPayload.sourceRowCol,
              parent,
              ArmorAndAccessories::Slot(i));
          }

          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ARMOR_SLOT"))
          {
            DEBUG_ASSERT(payload->DataSize == sizeof(ArmorDragDropPayload));
            const auto armorPayload = *static_cast<const ArmorDragDropPayload*>(payload->Data);
            Networking::CallRPC("SwapArmorSlotsRPC"_hs, world, armorPayload.sourceEntity, armorPayload.slot, parent, ArmorAndAccessories::Slot(i));
          }
        }
        ImGui::SetCursorPos(cursorPos);
        if (nameStr.empty())
        {
          ImGui::TextColored({1, 1, 1, 0.25f}, "%s", Core::Reflection::EnumToIcon(ArmorAndAccessories::Slot(i)));
        }
        else
        {
          ImGui::TextWrapped("%s", nameStr.c_str());
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
      ImGui::PopStyleVar();
    }
    ImGui::End();
  }

  void DrawSimpleScriptableWindow(World& world, [[maybe_unused]] entt::entity parent, SimpleScriptable& script)
  {
    ImGui::SetNextWindowSize({700, 600}, ImGuiCond_Once);
    if (ImGui::Begin("SimpleScriptable", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
      ImGui::BeginDisabled(!script.playersCanExecute);
      if (ImGui::Button("Execute"))
      {
        world.globals->scripting->ExecuteScriptFromCode(script.code, "main", {&world});
      }
      ImGui::EndDisabled();

      ImGui::PushFont(GuiHelper::GetMonospaceFont());
      if (ImGui::InputTextMultiline(
            "##text",
            script.code.data(),
            script.code.capacity() + 1,
            {-1, -1}, // Fill the remaining window space.
            ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput | (script.playersCanWrite ? 0 : ImGuiInputTextFlags_ReadOnly),
            [](ImGuiInputTextCallbackData* data) -> int
            {
              if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
              {
                auto* code = static_cast<std::string*>(data->UserData);
                code->resize(data->BufTextLen);
                data->Buf = code->data();
              }
              return 0;
            },
            &script.code))
      {
        Networking::CallRPC("UpdateSimpleScriptableCodeRPC"_hs, world, parent, script.code);
      }
      ImGui::PopFont();
    }
    ImGui::End();
  }

  void DrawCraftingWindow(World& world, Game2::Crafting& crafting, entt::entity playerEntity)
  {
    const auto& gt = world.GetRegistry().get<GlobalTransform>(playerEntity);
    auto& inventory = world.GetRegistry().get<Inventory>(playerEntity);

    // Get set of blocks around player. This is used to find the "crafting stations" that are near the player, which some recipes call for.
    auto nearVoxels  = std::unordered_set<BlockId>();
    const auto& grid = *world.globals->grid;
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

    static char buffer[256]{};
    ImGui::Text("Filter");
    ImGui::SameLine();
    ImGui::InputText("##Filter", buffer, 256);
    const auto len = std::strlen(buffer);

    auto scores = std::multimap<double, int, std::greater<double>>();
    for (int index = 0; const auto& recipe : crafting.recipes)
    {
      auto name = std::string();
      if (recipe.name.empty())
      {
        for (auto output : recipe.output)
        {
          name += Item::GetName(world, output.item) + '\n';
        }
      }
      else
      {
        name = recipe.name;
      }
      scores.emplace(rapidfuzz::fuzz::partial_ratio(buffer, name), index);
      index++;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Show all recipes", &crafting.showUncraftableRecipes);
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Includes recipes that cannot be crafted due to insufficient materials.");
    }

    ImGui::BeginChild("Left (recipe list)", {ImGui::GetContentRegionAvail().x / 3, ImGui::GetContentRegionAvail().y});
    for (auto [score, index] : scores)
    {
      if (len != 0 && score == 0)
      {
        continue;
      }
      const auto& recipe        = crafting.recipes[index];
      const bool canCraftRecipe = inventory.CanCraftRecipe(recipe) && nearVoxels.contains(recipe.craftingStation);
      if (!crafting.showUncraftableRecipes && !canCraftRecipe)
      {
        continue;
      }

      ImGui::Separator();
      ImGui::PushID(index);
      ImGui::BeginDisabled(!canCraftRecipe);

      const auto cursorPosStart = ImGui::GetCursorPos();
      if (recipe.name.empty())
      {
        for (const auto& output : recipe.output)
        {
          ImGui::TextWrapped("%s", Item::GetName(world, output.item).c_str());
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
          {
            DrawTooltipForItem(world, playerEntity, {.id = output.item, .count = output.count});
          }
        }
      }
      else
      {
        ImGui::TextWrapped("%s", recipe.name.c_str());
      }

      const auto cursorPosEnd = ImGui::GetCursorPos();
      ImGui::SetCursorPos(cursorPosStart);
      ImGui::EndDisabled();
      if (ImGui::Selectable("",
            crafting.selectedRecipeIndex ? *crafting.selectedRecipeIndex == index : false,
            0,
            {ImGui::GetContentRegionAvail().x, cursorPosEnd.y - cursorPosStart.y}))
      {
        crafting.selectedRecipeIndex = index;
      }
      ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Right (recipe details)", {ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y});
    if (crafting.selectedRecipeIndex.has_value())
    {
      const auto& recipe        = crafting.recipes[*crafting.selectedRecipeIndex];
      const bool canCraftRecipe = inventory.CanCraftRecipe(recipe) && nearVoxels.contains(recipe.craftingStation);
      ImGui::BeginDisabled(!canCraftRecipe);
      if (ImGui::Button("Craft", {60, 40}))
      {
        Networking::CallRPC("TryCraftRecipeRPC"_hs, world, playerEntity, recipe);
      }
      ImGui::EndDisabled();
      ImGui::Separator();

      ImGui::Text("Produces");
      ImGui::Indent();
      for (const auto& output : recipe.output)
      {
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.x *= .85f;
        color.y *= .85f;
        ImGui::TextColored(color, "%s x%d", Item::GetName(world, output.item).c_str(), output.count);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
        {
          DrawTooltipForItem(world, playerEntity, {.id = output.item, .count = output.count});
        }
      }
      ImGui::Unindent();
      ImGui::Separator();

      ImGui::Text("Ingredients");
      ImGui::Indent();
      for (const auto& ingredient : recipe.ingredients)
      {
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.z *= .5f;
        if (inventory.CountItem(ingredient.item) < ingredient.count)
        {
          color.y *= .5f; // Red
        }
        else
        {
          color.x *= .5f; // Green
        }
        ImGui::TextColored(color, "%s x%d", Item::GetName(world, ingredient.item).c_str(), ingredient.count);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
        {
          DrawTooltipForItem(world, playerEntity, {.id = ingredient.item, .count = ingredient.count});
        }
      }
      ImGui::Unindent();

      if (recipe.craftingStation != voxel_t::Air)
      {
        ImGui::Separator();
        ImGui::Text("Crafting station");
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.z *= .5f;
        if (!nearVoxels.contains(recipe.craftingStation))
        {
          color.y *= .5f; // Red
        }
        else
        {
          color.x *= .5f; // Green
        }
        ImGui::Indent();
        ImGui::TextColored(color, "%s", Block::GetName(world, recipe.craftingStation).c_str());
        ImGui::Unindent();
      }

      if (!recipe.description.empty())
      {
        ImGui::Separator();
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.w *= .5f;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", recipe.description.c_str());
        ImGui::PopStyleColor();
      }
    }
    ImGui::EndChild();
  }
} // namespace

void VoxelRenderer::ShowGameGui(World& world)
{
  auto localPlayer = world.TryGetLocalPlayer();
  if (localPlayer != entt::null)
  {
    if (auto* gp = world.GetRegistry().try_get<const GhostPlayer>(localPlayer))
    {
      ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      constexpr auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
      if (ImGui::Begin("###death_window", nullptr, flags))
      {
        ImGui::Text("You died");
        ImGui::Text("%.0f s", gp->remainingSeconds);
      }
      ImGui::End();
    }
  }

  // Get information about the local player
  auto range = world.GetRegistry().view<Player, const LocalPlayer, Inventory, const GlobalTransform>(entt::exclude<GhostPlayer>).each();

  if (range.begin() == range.end())
  {
    return;
  }

  auto&& [playerEntity, p, inventory, gt] = *range.begin();

  // TODO: replace with bitmap font rendered above each creature
  // auto collector = Physics::NearestRayCollector();
  // auto dir       = GetForward(gt.rotation);
  // auto start     = gt.position;
  // Physics::GetNarrowPhaseQuery().CastRay(JPH::RRayCast(Physics::ToJolt(start), Physics::ToJolt(dir * 20.0f)),
  //  JPH::RayCastSettings(),
  //  collector,
  //  Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_CHARACTER),
  //  Physics::GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_CHARACTER));
  const auto displaySize = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos({displaySize.x, 0}, 0, {1, 0});

  if (ImGui::Begin("Target",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
  {
    if (auto* h = world.GetRegistry().try_get<const Health>(playerEntity))
    {
      ImGui::Text("Health: %.0f/%.0f", h->hp, h->maxHp);
      constexpr auto bgColor = ImColor(207.f / 255, 69.f / 255, 27.f / 255, 1.0f);
      constexpr auto fgColor = ImColor(27.f / 255, 207.f / 255, 75.f / 255, 1.0f);
      Gui::LoadingBar("##health", h->hp / h->maxHp, ImVec2(400, 50), bgColor, fgColor);
    }
    // if (collector.nearest)
    //{
    //   auto entity = static_cast<entt::entity>(Physics::GetBodyInterface().GetUserData(collector.nearest->mBodyID));
    //   if (auto* n = world.GetRegistry().try_get<const Name>(entity))
    //   {
    //     ImGui::Text("%s", n->name.c_str());
    //   }

    //  if (auto* h = world.GetRegistry().try_get<const Health>(entity))
    //  {
    //    ImGui::Text("Health: %.0f", h->hp);
    //  }
    //}
  }
  ImGui::End();

  const auto rect = DrawInventory(world, playerEntity, playerEntity, inventory, !p.inventoryIsOpen);

  // Draw effects
  {
    const auto& effects = world.GetRegistry().get<const TemporaryEffects>(playerEntity).effects;
    ImGui::SetNextWindowPos({rect.pos.x, rect.pos.y + rect.size.y});
    if (ImGui::Begin("##effects",
          nullptr,
          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
      for (int i = 0; i < effects.size(); i++)
      {
        const auto& effect = effects[i];
        char buffer[256]{};
        std::snprintf(buffer, 256, "%s: %.0f s", Item::GetName(world, effect.id).c_str(), effect.useAccum);
        if (ImGui::Selectable(buffer))
        {
          world.GetRegistry().get<TemporaryEffects>(playerEntity).effects.erase(effects.begin() + i);
          i--;
        }
      }
    }
    ImGui::End();
  }

  if (p.inventoryIsOpen)
  {
    DrawArmorAndAccessories(world, playerEntity, playerEntity, world.GetRegistry().get<ArmorAndAccessories>(playerEntity));
  }

  if (world.GetRegistry().valid(p.openContainerId))
  {
    if (auto* ip = world.GetRegistry().try_get<Inventory>(p.openContainerId))
    {
      p.inventoryIsOpen = true;
      DrawInventory(world, p.openContainerId, playerEntity, *ip);
    }
    if (auto* ap = world.GetRegistry().try_get<ArmorAndAccessories>(p.openContainerId))
    {
      DrawArmorAndAccessories(world, p.openContainerId, playerEntity, *ap);
    }
    if (auto* sp = world.GetRegistry().try_get<SimpleScriptable>(p.openContainerId))
    {
      p.inventoryIsOpen = true;
      DrawSimpleScriptableWindow(world, p.openContainerId, *sp);
    }
    if (auto* wp = world.GetRegistry().try_get<Game2::TraderNpcDialogueState>(p.openContainerId))
    {
      p.inventoryIsOpen = true;
      if (ImGui::Begin("Trading"))
      {
        auto* wares = world.GetRegistry().try_get<Game2::Comp::TraderNpcWares>(p.openContainerId);
        switch (wp->state)
        {
        case Game2::TraderNpcDialogueState::State::None:
        {
          wp->state = Game2::TraderNpcDialogueState::State::Greet;
          break;
        }
        case Game2::TraderNpcDialogueState::State::Greet:
        {
          if (ImGui::Button("Goodbye"))
          {
            p.openContainerId = entt::null;
            p.inventoryIsOpen = false;
            wp->state         = Game2::TraderNpcDialogueState::State::None;
          }
          ImGui::SameLine();
          ImGui::BeginDisabled(wares == nullptr);
          if (ImGui::Button("Trade"))
          {
            wp->state = Game2::TraderNpcDialogueState::State::Trade;
          }
          ImGui::EndDisabled();
          ImGui::Separator();
          ImGui::Text("Hello there!");
          break;
        }
        case Game2::TraderNpcDialogueState::State::Trade:
        {
          if (wares)
          {
            if (ImGui::Button("Goodbye"))
            {
              p.openContainerId = entt::null;
              p.inventoryIsOpen = false;
              wp->state         = Game2::TraderNpcDialogueState::State::None;
            }
            ImGui::Separator();
            DrawCraftingWindow(world, wares->crafting, playerEntity);
          }
          break;
        }
        }
      }
      ImGui::End();
    }
  }

  if (p.showInteractPrompt)
  {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.55f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    constexpr auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground |
                           ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Interact", nullptr, flags))
    {
      ImGui::Text("Press F to use");
    }
    ImGui::End();
  }

  if (p.inventoryIsOpen)
  {
    if (ImGui::Begin("Crafting"))
    {
      DrawCraftingWindow(world, world.globals->game->crafting, playerEntity);
    }
    ImGui::End();
  }
}