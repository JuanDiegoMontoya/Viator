#include "VoxelRenderer.h"
#include "Game/World.h"
#include "Core/Assert2.h"
#include "Game/Item.h"
#include "Core/Reflection.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "entt/meta/resolve.hpp"
#include "spdlog/spdlog.h"
#include "tracy/Tracy.hpp"
#include "rapidfuzz/fuzz.hpp"

#include <vector>
#include <algorithm>
#include <execution>

using namespace entt::literals;

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
} // namespace

namespace GuiHelper
{
  bool DrawComponent(World& world, entt::entity entity, entt::meta_any instance, entt::meta_custom custom, bool readonly, int& guiId)
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
          changed |= DrawComponent(world, entity, element.as_ref(), eType.custom(), readonly, guiId);
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
        if (auto fn = meta.func("to_underlying"_hs))
        {
          if (const auto* val = fn.invoke({}, instance).try_cast<uint32_t>())
          {
            ImGui::Text("Value: %u", *val);
          }
        }
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
    else if (meta.is_pointer_like())
    {
      if (meta.info() == entt::type_id<const char*>())
      {
        ImGui::Text("%s", instance.cast<const char*>());
      }
      else
      {
        ImGui::Text("TODO: pointer-likes");
      }
    }
    else if (meta.traits<Traits>() & Traits::VARIANT)
    {
      auto valueFn = meta.func("value"_hs);
      auto typeHashFn = meta.func("type_hash"_hs);
      ASSERT(valueFn);
      ASSERT(typeHashFn);

      auto name = std::string();
      if (auto it = properties.find("name"_hs); it != properties.end())
      {
        name = it->second.cast<const char*>();
      }
      else
      {
        name = FixupTypeString(meta.info().name());
      }

      auto typeHash = typeHashFn.invoke({}, instance).cast<entt::id_type>();

      PropertiesMap properties3 = {};
      if (auto* mp = static_cast<const PropertiesMap*>(meta.custom()))
      {
        properties3 = *mp;
      }
      if (auto it = properties3.find("alternatives"_hs); it != properties3.end())
      {
        if (ImGui::BeginCombo(name.c_str(), FixupTypeString(entt::resolve(typeHash).info().name()).c_str()))
        {
          ImGui::BeginDisabled(readonly);
          for (auto alternativeId : it->second.cast<std::vector<entt::id_type>>())
          {
            auto alternativeType = entt::resolve(alternativeId);
            if (ImGui::Selectable(FixupTypeString(alternativeType.info().name()).c_str(), alternativeId == typeHash))
            {
              instance.assign(meta.construct(alternativeType.construct()));
              changed = true;
            }
          }
          ImGui::EndCombo();
          ImGui::EndDisabled();
        }
      }
      
      auto value = valueFn.invoke({}, instance.as_ref()).as_ref();

      ImGui::PushID(guiId++);
      ImGui::Indent();
      changed |= GuiHelper::DrawComponent(world, entity, value.as_ref(), value.type().custom(), readonly, guiId);
      ImGui::Unindent();
      ImGui::PopID();

      if (changed)
      {
        // Hack because I can't seem to make value hold a real reference without invoking UB.
        instance.assign(meta.construct(value));
      }
    }
    else if (meta.traits<Traits>() & Traits::OPTIONAL)
    {
      auto hasValueFn = meta.func("has_value"_hs);
      auto emplaceFn = meta.func("emplace"_hs);
      auto resetFn = meta.func("reset"_hs);
      auto valueFn = meta.func("value"_hs);

      ASSERT(hasValueFn);

      bool hasValue = hasValueFn.invoke({}, instance.as_ref()).cast<bool>();

      ImGui::BeginDisabled(!emplaceFn || hasValue);
      if (ImGui::Button("Emplace"))
      {
        emplaceFn.invoke({}, instance.as_ref());
        hasValue = true;
        changed  = true;
      }
      ImGui::EndDisabled();

      ImGui::SameLine();

      ImGui::BeginDisabled(!resetFn || !hasValue || !hasValueFn);
      if (ImGui::Button("Erase"))
      {
        resetFn.invoke({}, instance.as_ref());
        hasValue = false;
        changed  = true;
      }
      ImGui::EndDisabled();

      ImGui::SameLine();

      if (auto it = properties.find("name"_hs); it != properties.end())
      {
        ImGui::Text("%s", it->second.cast<const char*>());
      }
      else
      {
        ImGui::Text("%s", FixupTypeString(meta.info().name()).c_str());
      }

      if (hasValue)
      {
        ASSERT(valueFn);
        auto value = valueFn.invoke({}, instance);
        ASSERT(value);
        ImGui::PushID(guiId++);
        changed |= GuiHelper::DrawComponent(world, entity, value.as_ref(), value.type().custom(), readonly, guiId);
        ImGui::PopID();
        if (changed)
        {
          // Hack because I can't seem to make value hold a real reference without invoking UB.
          instance.assign(meta.construct(value));
        }
      }
      else
      {
        ImGui::Text("nullopt (Emplace to add a value)");
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
          changed |= GuiHelper::DrawComponent(world, entity, data.get(instance), data.custom(), readonly || traits & Traits::EDITOR_READ_ONLY, guiId);
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
}

// Recursive
void VoxelRenderer::DrawEntityHelper(entt::registry& registry, entt::entity e, const Hierarchy* h)
{
  ImGui::PushID((int)e);
  bool opened = false;

  int flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (selectedHandle == entt::handle{registry, e})
  {
    flags |= ImGuiTreeNodeFlags_Selected;
  }

  if (!h || h->children.empty())
  {
    flags |= ImGuiTreeNodeFlags_Leaf;
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
    selectedHandle = {registry, e};
  }

  if (opened)
  {
    if (h)
    {
      for (auto child : h->children)
      {
        DrawEntityHelper(registry, child, registry.try_get<const Hierarchy>(child));
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void VoxelRenderer::ShowEditor([[maybe_unused]] DeltaTime dt, World& world, EditorMode mode)
{
  const char* title = nullptr;
  entt::registry* pRegistry{};
  switch (mode)
  {
  case EditorMode::Entities:
  {
    pRegistry = &world.GetRegistryRaw();
    title     = "Entities";
    break;
  }
  case EditorMode::Items:
  {
    pRegistry = &world.GetRegistry().ctx().get<Item::Registry>().GetRegistry();
    title     = "Items";
    break;
  }
  case EditorMode::Blocks:
  {
    pRegistry = &world.GetRegistry().ctx().get<Block::Registry>().GetRegistry();
    title     = "Blocks";
    break;
  }
  }

  auto& registry = *pRegistry;

  if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
  {
    ImGui::Text("Count: %d", registry.view<entt::entity>().size());
    ZoneScopedN("Editor");
    ZoneText(title, std::strlen(title));
    if (!ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() && ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left])
    {
      selectedHandle = {};
    }

    static char buffer[256]{};
    ImGui::Text("Filter");
    ImGui::SameLine();
    ImGui::InputText("##Filter", buffer, 256);
    const auto len = std::strlen(buffer);

    auto scores = std::vector<std::pair<double, entt::entity>>(registry.view<entt::entity>().size());

    {
      ZoneScopedN("Fuzzy string match");
      auto scorer = rapidfuzz::fuzz::CachedPartialRatio(buffer);
      
      auto view = registry.view<entt::entity>();
      {
        ZoneScopedN("transform");
        std::transform(std::execution::par,
          view.begin(),
          view.end(),
          scores.begin(),
          [&](entt::entity e) -> decltype(scores)::value_type
          {
            const auto* h = registry.try_get<const Hierarchy>(e);
            if (!h || h->parent == entt::null)
            {
              const auto* name = registry.try_get<const Name>(e);
              return {name ? scorer.similarity(name->name) : 0, e};
            }

            return {0, entt::null};
          });
      }

      {
        ZoneScopedN("erase_if");
        std::erase_if(scores, [](const auto& pair) { return pair.second == entt::null; });
      }
      {
        ZoneScopedN("sort");
        std::sort(std::execution::par, scores.begin(), scores.end(), [](const auto& p1, const auto& p2) { return p1.first > p2.first; });
      }
    }

    {
      ZoneScopedN("Draw entities");

      // Show entity hierarchy. If this is slow, ImGuiListClipper can improve perf, but it requires list elements to have a fixed height.
      for (const auto& [score, e] : scores)
      {
        if (len != 0 && score == 0)
        {
          continue;
        }
        // Draw only root nodes.
        if (const auto* h = registry.try_get<const Hierarchy>(e); !h || h->parent == entt::null)
        {
          DrawEntityHelper(registry, e, h);
        }
      }
    }
  }
  ImGui::End();

  if (selectedHandle.registry() != pRegistry)
  {
    ImGui::Begin("Components");
    ImGui::End();
    return;
  }

  if (ImGui::Begin("Components"))
  {
    ZoneScopedN("Components");
    if (selectedHandle.valid())
    {
      int openAction = 0;
      openAction += ImGui::Button("Expand all");
      ImGui::SameLine();
      openAction += -(int)ImGui::Button("Collapse all");

      auto e = selectedHandle.entity();
      ImGui::SameLine();
      if (ImGui::Button("Delete Entity"))
      {
        registry.emplace_or_replace<DeferredDelete>(e);
      }

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
          const auto traits = meta.traits<Core::Reflection::Traits>();
          bool displayInfo  = false;
          switch (mode)
          {
          case EditorMode::Entities:
          {
            displayInfo = traits & Core::Reflection::Traits::COMPONENT && !(traits & (Core::Reflection::Traits::ITEM_COMPONENT | Core::Reflection::Traits::BLOCK_COMPONENT));
            break;
          }
          case EditorMode::Items:
          {
            displayInfo = traits & Core::Reflection::Traits::ITEM_COMPONENT;
            break;
          }
          case EditorMode::Blocks:
          {
            displayInfo = traits & Core::Reflection::Traits::BLOCK_COMPONENT;
            break;
          }
          }
          if (displayInfo)
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
              if (addFunc && mode == EditorMode::Entities)
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
                DEBUG_ASSERT(false);
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
      using namespace entt::literals;
      auto* storages = registry.ctx().find<std::vector<TypeInfo>>();
      if (!storages)
      {
        ZoneScopedN("Sort editor storages");
        spdlog::debug("[Editor] Sort storages.");
        storages = &registry.ctx().insert_or_assign<std::vector<TypeInfo>>({});

        for (auto pair : registry.storage())
        {
          auto meta = entt::resolve(pair.first);
          storages->emplace_back(meta, &pair.second, meta ? FixupTypeString(meta.info().name()) : std::string());
        }
        std::sort(storages->begin(),
          storages->end(),
          [](const TypeInfo& p1, const TypeInfo& p2)
          {
            if (p1.meta && p2.meta)
            {
              return p1.fixupString < p2.fixupString;
            }
            return p1.meta.id() < p2.meta.id();
          });
      }

      ImGui::Separator();
      if (ImGui::BeginChild("Component Controls", {0, ImGui::GetContentRegionAvail().y}, 0, ImGuiWindowFlags_NoSavedSettings))
      {
        for (int i = 0; auto&& [meta, storage, _] : *storages)
        {
          if (!storage)
          {
            spdlog::debug("[Editor] Null component storage found. Re-sorting storages.");
            registry.ctx().erase<std::vector<TypeInfo>>();
            continue;
          }

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

          bool isEmptyType = false;
          auto flags       = 0;
          if (meta.traits<Core::Reflection::Traits>() & Core::Reflection::EMPTY)
          {
            isEmptyType = true;
            flags |= ImGuiTreeNodeFlags_Leaf;
          }
          if (openAction)
          {
            ImGui::SetNextItemOpen(openAction == 1);
          }
          ImGui::PushItemFlag(ImGuiItemFlags_Disabled, isEmptyType);
          if (ImGui::CollapsingHeader(FixupTypeString(storage->type().name()).c_str(), isEmptyType ? flags : 0))
          {
            if (storage->contains(e) && meta)
            {
              int i2 = 0;
              GuiHelper::DrawComponent(world,
                e,
                meta.from_void(storage->value(e)),
                meta.custom(),
                meta.traits<Core::Reflection::Traits>() & Core::Reflection::Traits::EDITOR_READ_ONLY,
                i2);
            }
            else
            {
              ImGui::Text("Reflection is unavailable for this type.");
            }
          }
          ImGui::PopItemFlag();
          if (isEmptyType && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          {
            ImGui::SetTooltip("Type contains no data.");
          }
          ImGui::PopID();
        }
      }
    }
    ImGui::EndChild();
  }
  ImGui::End();
}
