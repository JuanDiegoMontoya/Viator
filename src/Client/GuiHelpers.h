#pragma once
#include "Client/Fvog/Texture2.h"

#include "imgui.h"

namespace Gui
{
  void ApplySteamImGuiStyle();
  void ApplyFrogImGuiStyle();

  static constexpr ImGuiTableFlags defaultPropertiesFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame;
  static constexpr ImGuiTreeNodeFlags defaultTreeNodeImageFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
  bool BeginProperties(ImGuiTableFlags flags = defaultPropertiesFlags, bool fixedWidth = false, float widthIfFixed = 0.5f);
  void EndProperties();
  void PushPropertyId();
  void PopPropertyId();
  bool BeginProperty(const char* label, const char* tooltip = nullptr, bool alignTextRight = true);
  bool BeginSelectableProperty(const char* label, const char* tooltip = nullptr, bool alignTextRight = true, bool selected = false, ImGuiSelectableFlags flags = {});
  void EndProperty();
  void Text(const char* label, const char* fmt, const char* tooltip = nullptr, ...);
  bool Checkbox(const char* label, bool* b, const char* tooltip = nullptr);
  bool SliderFloat(const char* label, float* f, float min, float max, const char* tooltip = nullptr, const char* format = nullptr, ImGuiSliderFlags flags = 0);
  bool DragFloat(const char* label,
    float* f,
    float speed            = 1,
    float min              = 0,
    float max              = 0,
    const char* tooltip    = nullptr,
    const char* format     = nullptr,
    ImGuiSliderFlags flags = 0);
  bool SliderScalar(const char* label,
    ImGuiDataType type,
    void* s,
    const void* min,
    const void* max,
    const char* tooltip    = nullptr,
    const char* format     = nullptr,
    ImGuiSliderFlags flags = 0);
  bool ColorEdit3(const char* label, float* f3, const char* tooltip = nullptr, ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float);
  bool ColorEdit4(const char* label, float* f4, const char* tooltip = nullptr, ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float);
  bool RadioButton(const char* label, bool active, const char* tooltip = nullptr);
  bool TreeNodeWithImage16(const char* label, Fvog::Texture& texture, std::optional<Fvog::Sampler> sampler = {}, ImGuiTreeNodeFlags flags = defaultTreeNodeImageFlags);
  bool DragFloat3(const char* label,
    float* f3,
    float speed,
    float min              = 0,
    float max              = 0,
    const char* format     = "%.3f",
    ImGuiSliderFlags flags = 0,
    const char* tooltip    = nullptr);
  bool FlagCheckbox(const char* label, uint32_t* bitfield, uint32_t bits, const char* tooltip = nullptr);

  bool LoadingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col);
}
