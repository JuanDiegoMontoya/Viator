#include "GuiHelpers.h"

#include "Fvog/Texture2.h"
#include "Fvog/detail/ApiToEnum2.h"
#include "Game/Assets.h"
#include "ImGui/imgui_impl_fvog.h"
#include "shaders/Color.h.glsl"

#include "imgui.h"
#include "imgui_internal.h"

namespace
{
  const char* StringifyRendererColorSpace(uint32_t colorSpace)
  {
    switch (colorSpace)
    {
    case COLOR_SPACE_sRGB_LINEAR: return "sRGB_LINEAR";
    case COLOR_SPACE_scRGB_LINEAR: return "scRGB_LINEAR";
    case COLOR_SPACE_sRGB_NONLINEAR: return "sRGB_NONLINEAR";
    case COLOR_SPACE_BT2020_LINEAR: return "BT2020_LINEAR";
    case COLOR_SPACE_HDR10_ST2084: return "HDR10_ST2084";
    default: return "Unknown color space";
    }
  }

  uint32_t VkToColorSpace(VkColorSpaceKHR colorSpace)
  {
    switch (colorSpace)
    {
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return COLOR_SPACE_sRGB_NONLINEAR;
    case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return COLOR_SPACE_scRGB_LINEAR;
    case VK_COLOR_SPACE_BT2020_LINEAR_EXT: return COLOR_SPACE_BT2020_LINEAR;
    case VK_COLOR_SPACE_HDR10_ST2084_EXT: return COLOR_SPACE_HDR10_ST2084;
    default: assert(0);
    }

    return static_cast<uint32_t>(-1);
  }

  std::string StringifySurfaceFormat(VkSurfaceFormatKHR surfaceFormat)
  {
    return std::string(Fvog::detail::FormatToString(Fvog::detail::VkToFormat(surfaceFormat.format))) + " | " +
           Fvog::detail::VkColorSpaceToString(surfaceFormat.colorSpace);
  }

  uint32_t SetBits(uint32_t v, uint32_t mask, bool apply)
  {
    v &= ~mask; // Unset bits in the mask
    if (apply)
    {
      v |= mask;
    }
    return v;
  }

  bool ImGui_FlagCheckbox(const char* label, uint32_t* v, uint32_t bit)
  {
    bool isSet     = *v & bit;
    const bool ret = ImGui::Checkbox(label, &isSet);
    *v             = SetBits(*v, bit, isSet);
    return ret;
  }

  void ImGui_HoverTooltip(const char* fmt, ...)
  {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
    {
      va_list args;
      va_start(args, fmt);
      ImGui::SetTooltipV(fmt, args);
      va_end(args);
    }
  }
}

namespace Gui
{
  void ApplySteamImGuiStyle()
  {
    auto& style = ImGui::GetStyle();
    // style.WindowPadding    = ImVec2(20, 6);
    // style.WindowTitleAlign = ImVec2(0.30f, 0.50f);
    // style.ScrollbarSize    = 17;
    // style.FramePadding     = ImVec2(5, 6);

    // style.FrameRounding     = 0;
    // style.WindowRounding    = 0;
    // style.ScrollbarRounding = 0;
    // style.ChildRounding     = 0;
    // style.PopupRounding     = 0;
    // style.GrabRounding      = 0;
    // style.TabRounding       = 0;

    // style.WindowBorderSize = 1;
    // style.FrameBorderSize  = 1;
    // style.ChildBorderSize  = 1;
    // style.PopupBorderSize  = 1;
    // style.TabBorderSize    = 1;

    style.Colors[ImGuiCol_Text]                  = ImVec4(0.85f, 0.87f, 0.83f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.63f, 0.67f, 0.58f, 1.00f);
    style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_Border]                = ImVec4(0.53f, 0.57f, 0.50f, 1.00f);
    style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.16f, 0.18f, 0.13f, 1.00f);
    style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.24f, 0.27f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.24f, 0.27f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.24f, 0.27f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.35f, 0.42f, 0.31f, 0.52f);
    style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_CheckMark]             = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_Button]                = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_Header]                = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive]          = ImVec4(1.00f, 0.49f, 0.11f, 1.00f);
    style.Colors[ImGuiCol_Separator]             = ImVec4(0.16f, 0.18f, 0.13f, 1.00f);
    style.Colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.16f, 0.18f, 0.13f, 1.00f);
    style.Colors[ImGuiCol_SeparatorActive]       = ImVec4(0.16f, 0.18f, 0.13f, 1.00f);
    style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_Tab]                   = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TabHovered]            = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TabActive]             = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocused]          = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.30f, 0.35f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_DockingPreview]        = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    style.Colors[ImGuiCol_DockingEmptyBg]        = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_PlotLines]             = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(0.26f, 0.59f, 0.98f, 0.34f);
    style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 1.00f, 0.00f, 0.78f);
    style.Colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_TableBorderStrong]     = ImVec4(1.00f, 1.00f, 1.00f, 0.67f);
    style.Colors[ImGuiCol_TableBorderLight]      = ImVec4(0.80f, 0.80f, 0.80f, 0.31f);
    style.Colors[ImGuiCol_TableRowBg]            = ImVec4(0.80f, 0.80f, 0.80f, 0.38f);
    style.Colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.58f, 0.53f, 0.19f, 1.00f);
    style.Colors[ImGuiCol_DragDropTarget]        = ImVec4(0.58f, 0.53f, 0.19f, 1.00f);
    style.Colors[ImGuiCol_NavHighlight]          = ImVec4(1.00f, 0.00f, 0.94f, 1.00f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.69f, 1.00f);
    style.Colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.12f, 0.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
  }

  void ApplyFrogImGuiStyle()
  {
    ImGui::StyleColorsDark();
    auto& style                                  = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.19f, 0.19f, 0.19f, 0.95f);
    style.Colors[ImGuiCol_Border]                = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.47f, 0.86f, 0.33f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    style.Colors[ImGuiCol_Button]                = ImVec4(0.00f, 0.00f, 0.00f, 0.54f);
    style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    style.Colors[ImGuiCol_Header]                = ImVec4(0.40f, 0.40f, 0.40f, 0.33f);
    style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.40f, 0.40f, 0.40f, 0.50f);
    style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.40f, 0.40f, 0.40f, 0.80f);
    style.Colors[ImGuiCol_Separator]             = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    style.Colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    style.Colors[ImGuiCol_SeparatorActive]       = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    style.Colors[ImGuiCol_Tab]                   = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    style.Colors[ImGuiCol_TabHovered]            = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    style.Colors[ImGuiCol_TabActive]             = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    style.Colors[ImGuiCol_TabUnfocused]          = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    style.Colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    style.Colors[ImGuiCol_DockingPreview]        = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    style.Colors[ImGuiCol_DockingEmptyBg]        = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_PlotLines]             = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    style.Colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    style.Colors[ImGuiCol_TableBorderLight]      = ImVec4(0.28f, 0.28f, 0.28f, 0.43f);
    style.Colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    style.Colors[ImGuiCol_DragDropTarget]        = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    style.Colors[ImGuiCol_NavHighlight]          = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    style.Colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    style.Colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  }

  // Some of these helpers have been shamelessly "borrowed" from Oxylus.
  // https://github.com/Hatrickek/OxylusEngine/blob/dev/Oxylus/src/UI/OxUI.cpp
  bool BeginProperties(ImGuiTableFlags flags, bool fixedWidth, float widthIfFixed)
  {
    if (ImGui::BeginTable("table", 2, flags))
    {
      ImGui::TableSetupColumn("name");
      if (!fixedWidth)
      {
        ImGui::TableSetupColumn("property");
      }
      else
      {
        ImGui::TableSetupColumn("property", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() * widthIfFixed);
      }
      return true;
    }

    return false;
  }

  void EndProperties()
  {
    ImGui::EndTable();
  }

  int propertyId = 0;

  void PushPropertyId()
  {
    propertyId++;
    ImGui::PushID(propertyId);
  }

  void PopPropertyId()
  {
    ImGui::PopID();
    propertyId--;
  }

  bool BeginProperty(const char* label, const char* tooltip, bool alignTextRight)
  {
    PushPropertyId();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    if (alignTextRight)
    {
      const auto posX = ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(label).x - ImGui::GetScrollX();
      if (posX > ImGui::GetCursorPosX())
      {
        ImGui::SetCursorPosX(posX);
      }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{});
    // ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{});
    // ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{});
    // ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{});
    // ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4{});
    // const auto pressed = ImGui::Button(label);
    // ImGui::PopStyleColor(5);

    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
      ImGui::SetTooltip("%s", tooltip);
    }

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.0f);
    return false;
  }

  bool BeginSelectableProperty(const char* label, const char* tooltip, bool alignTextRight, bool selected, ImGuiSelectableFlags flags)
  {
    PushPropertyId();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    if (alignTextRight)
    {
      const auto posX = ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(label).x - ImGui::GetScrollX();
      if (posX > ImGui::GetCursorPosX())
      {
        ImGui::SetCursorPosX(posX);
      }
    }

    ImGui::AlignTextToFramePadding();

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{});
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4{});
    const auto pressed = ImGui::Selectable(label, selected, flags);
    ImGui::PopStyleColor(2);

    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
      ImGui::SetTooltip("%s", tooltip);
    }

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.0f);
    return pressed;
  }

  void EndProperty()
  {
    ImGui::PopStyleVar();
    PopPropertyId();
  }

  void Text(const char* label, const char* fmt, const char* tooltip, ...)
  {
    BeginProperty(label, tooltip, false);
    va_list args;
    va_start(args, tooltip);
    ImGui::TextV(fmt, args);
    va_end(args);
    EndProperty();
  }

  bool Checkbox(const char* label, bool* b, const char* tooltip)
  {
    bool pressed0 = BeginSelectableProperty(label, tooltip, true, false, ImGuiSelectableFlags_SpanAllColumns);
    ImGui::PushID(label);
    bool pressed = ImGui::Checkbox("", b);
    ImGui::PopID();
    EndProperty();
    if (pressed0 == true)
    {
      *b = !*b;
    }
    return pressed || pressed0;
  }

  bool SliderFloat(const char* label, float* f, float min, float max, const char* tooltip, const char* format, ImGuiSliderFlags flags)
  {
    BeginProperty(label, tooltip);
    bool pressed = ImGui::SliderFloat(label, f, min, max, format, flags);
    EndProperty();
    return pressed;
  }

  bool DragFloat(const char* label, float* f, float speed, float min, float max, const char* tooltip, const char* format, ImGuiSliderFlags flags)
  {
    BeginProperty(label, tooltip);
    bool pressed = ImGui::DragFloat(label, f, speed, min, max, format, flags);
    EndProperty();
    return pressed;
  }

  bool SliderScalar(const char* label, ImGuiDataType type, void* s, const void* min, const void* max, const char* tooltip, const char* format, ImGuiSliderFlags flags)
  {
    BeginProperty(label, tooltip);
    bool pressed = ImGui::SliderScalar(label, type, s, min, max, format, flags);
    EndProperty();
    return pressed;
  }

  bool ColorEdit3(const char* label, float* f3, const char* tooltip, ImGuiColorEditFlags flags)
  {
    BeginProperty(label, tooltip);
    bool pressed = ImGui::ColorEdit3(label, f3, flags);
    EndProperty();
    return pressed;
  }

  bool ColorEdit4(const char* label, float* f4, const char* tooltip, ImGuiColorEditFlags flags)
  {
    BeginProperty(label, tooltip);
    bool pressed = ImGui::ColorEdit4(label, f4, flags);
    EndProperty();
    return pressed;
  }

  bool RadioButton(const char* label, bool active, const char* tooltip)
  {
    BeginProperty(label, tooltip);
    ImGui::PushID(label);
    bool pressed = ImGui::RadioButton("", active);
    ImGui::PopID();
    EndProperty();
    return pressed;
  }

  // Thingy for putting 16x16 icons after the arrow in a tree node
  bool TreeNodeWithImage16(const char* label, Fvog::Texture& texture, std::optional<Fvog::Sampler> sampler, ImGuiTreeNodeFlags flags)
  {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{});
    const auto open = ImGui::TreeNodeEx((std::string("###") + label).c_str(), flags);
    ImGui::SameLine();
    ImGui::Image(ImTextureSampler(texture.ImageView().GetSampledResourceHandle().index, sampler ? sampler->GetResourceHandle().index : 0), {16, 16});
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text(" %s", label);
    ImGui::PopStyleVar(2);
    return open;
  }

  bool DragFloat3(const char* label, float* f3, float speed, float min, float max, const char* format, ImGuiSliderFlags flags, const char* tooltip)
  {
    const float frameHeight = ImGui::GetFrameHeight();
    const ImVec2 buttonSize = {2.f, frameHeight};

    const auto buttonFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoPicker |
                             ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoTooltip;

    BeginProperty(label, tooltip);

    // The cursor falls a few pixels off the edge on the last element, causing it to be clipped and appear smaller than the other items.
    // Presumably this is caused by the tiny color buttons and item spacing shenanigans we do.
    // This magic somewhat mitigates the issue by allocating a bit less than a third of the available area to each big widget,
    // giving some room for items to safely spill into (without being clipped).
    // The downside to this approach is that the first time opening such a table causes it to slowly "expand" to fill its space.
    constexpr auto magic = 3.25f;
    const auto avail     = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushID(label);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});
    ImGui::ColorButton("##0", {1, 0, 0, 1}, buttonFlags, buttonSize);
    ImGui::SameLine();
    ImGui::PushItemWidth(avail / magic);
    bool a = ImGui::DragFloat("##x", &f3[0], speed, min, max, format, flags);
    ImGui::PopItemWidth();
    ImGui::PopStyleVar();

    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});
    ImGui::ColorButton("##1", {0, 1, 0, 1}, buttonFlags, buttonSize);
    ImGui::SameLine();
    ImGui::PushItemWidth(avail / magic);
    bool b = ImGui::DragFloat("##y", &f3[1], speed, min, max, format, flags);
    ImGui::PopItemWidth();
    ImGui::PopStyleVar();

    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});
    ImGui::ColorButton("##2", {0, 0, 1, 1}, buttonFlags, buttonSize);
    ImGui::SameLine();
    ImGui::PushItemWidth(avail / magic);
    bool c = ImGui::DragFloat("##z", &f3[2], speed, min, max, format, flags);
    ImGui::PopItemWidth();
    ImGui::PopStyleVar();

    ImGui::PopID();
    ImGui::PopStyleVar();

    EndProperty();

    return a || b || c;
  }

  bool FlagCheckbox(const char* label, uint32_t* bitfield, uint32_t bits, const char* tooltip)
  {
    bool pressed0 = BeginSelectableProperty(label, tooltip, true, false, ImGuiSelectableFlags_SpanAllColumns);
    ImGui::PushID(label);
    bool pressed = ImGui_FlagCheckbox("", bitfield, bits);
    ImGui::PopID();
    EndProperty();
    if (pressed0 == true)
    {
      *bitfield = SetBits(*bitfield, bits, !(*bitfield & bits));
    }
    return pressed || pressed0;
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
} // namespace Gui