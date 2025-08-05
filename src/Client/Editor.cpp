#include "VoxelRenderer.h"
#include "Game/World.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "Core/Assert2.h"
#include "entt/meta/resolve.hpp"
#include "spdlog/spdlog.h"
#include "tracy/Tracy.hpp"
#include "rapidfuzz/fuzz.hpp"

#include <vector>
#include <algorithm>
#include <execution>

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
} // namespace

// Recursive
void VoxelRenderer::DrawEntityHelper(World& world, entt::entity e, [[maybe_unused]] const Hierarchy* h)
{
  auto& registry = world.GetRegistryRaw();
  ImGui::PushID((int)e);
  bool opened = false;

  int flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (selectedEntity == e)
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
    selectedEntity = e;
  }

  if (opened)
  {
    if (h)
    {
      for (auto child : h->children)
      {
        DrawEntityHelper(world, child, registry.try_get<const Hierarchy>(child));
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}
void VoxelRenderer::ShowEditor([[maybe_unused]] DeltaTime dt, World& world)
{
  auto& registry = world.GetRegistryRaw();
  if (ImGui::Begin("Entities"))
  {
    ImGui::Text("Count: %d", registry.view<entt::entity>().size());
    ZoneScopedN("Entities");
    if (!ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() && ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left])
    {
      selectedEntity = entt::null;
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
          DrawEntityHelper(world, e, h);
        }
      }
    }
  }
  ImGui::End();

  if (ImGui::Begin("Components"))
  {
    ZoneScopedN("Components");
    if (registry.valid(selectedEntity))
    {
      int openAction = 0;
      openAction += ImGui::Button("Expand all");
      ImGui::SameLine();
      openAction += -(int)ImGui::Button("Collapse all");

      auto e = selectedEntity;
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
      static auto storages = std::vector<TypeInfo>();
      if (!world.GetRegistry().ctx().contains<bool>("isEditorStorageInitialized"_hs)) // Naughty hack to make this sorting only happen once.
      {
        ZoneScopedN("Sort editor storages");
        spdlog::debug("[Editor] Sort storages.");
        storages.clear();
        world.GetRegistry().ctx().emplace_as<bool>("isEditorStorageInitialized"_hs, true);

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

      ImGui::Separator();
      if (ImGui::BeginChild("Component Controls", {0, ImGui::GetContentRegionAvail().y}, 0, ImGuiWindowFlags_NoSavedSettings))
      {
        for (int i = 0; auto&& [meta, storage, _] : storages)
        {
          if (!storage)
          {
            spdlog::debug("[Editor] Null component storage found. Re-sorting storages.");
            world.GetRegistry().ctx().erase<bool>("isEditorStorageInitialized"_hs);
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
              DrawComponentHelper(world,
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
