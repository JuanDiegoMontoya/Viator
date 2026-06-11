#include "CVarWidgets.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace Gui
{
  void CVarFloat(Game2::AutoCVar<Game2::cvar_float>& cvar) {
    auto value       = cvar.Get();
    const auto& info = cvar.Info();
    bool modified    = false;
    if (info.min && info.max)
    {
      modified = ImGui::SliderScalar(info.parameters->name.c_str(),
        ImGuiDataType_Double,
        &value,
        &info.min.value(),
        &info.max.value(),
        "%.3f",
        ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp);
    }
    else
    {
      modified = ImGui::DragScalar(info.parameters->name.c_str(),
        ImGuiDataType_Double,
        &value,
        1,
        nullptr,
        nullptr,
        "%.3f",
        ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp);
    }

    if (modified)
    {
      cvar.Set(value);
    }
  }

  void CVarFloatCheckbox(Game2::AutoCVar<Game2::cvar_float>& cvar) {
    auto value       = static_cast<bool>(cvar.Get());
    const auto& info = cvar.Info();

    if (ImGui::Checkbox(info.parameters->name.c_str(), &value))
    {
      cvar.Set(static_cast<Game2::cvar_float>(value));
    }
  }

  void CVarVec3(Game2::AutoCVar<Game2::cvar_vec3>& cvar) {
    auto value       = cvar.Get();
    const auto& info = cvar.Info();
    bool modified    = false;
    if (info.min && info.max)
    {
      modified = ImGui::SliderScalarN(info.parameters->name.c_str(),
        ImGuiDataType_Double,
        &value,
        3, // Components
        &info.min.value(),
        &info.max.value(),
        "%.3f",
        ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp);
    }
    else
    {
      modified = ImGui::DragScalarN(info.parameters->name.c_str(),
        ImGuiDataType_Double,
        &value,
        3, // Components
        1,
        nullptr,
        nullptr,
        "%.3f",
        ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp);
    }

    if (modified)
    {
      cvar.Set(value);
    }
  }

  void CVarString(Game2::AutoCVar<Game2::cvar_string>& cvar) {
    auto value       = cvar.Get();
    const auto& info = cvar.Info();

    if (ImGui::InputText(info.parameters->name.c_str(), &value, ImGuiInputTextFlags_EnterReturnsTrue))
    {
      cvar.Set(std::move(value));
    }
  }
}