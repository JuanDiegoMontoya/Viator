#include "Console.h"
#include "Game/CommandParser.h"
#include "Client/VoxelRenderer.h"
#include "Game/Game.h"
#include "Game/Globals.h"
#include "Game/World.h"
#include "Game/CVarInternal.h"
#include "Core/StringUtilities.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "Game/Commands.h"
#include "rapidfuzz/rapidfuzz_all.hpp"

#include <array>
#include <execution>
#include <functional>
#include <vector>

//AutoCVar<cvar_vec3> defaultInputColor("c.inputColor", "Default color of console input", cvar_vec3(0.6f));
//AutoCVar<cvar_vec3> defaultTextColor("c.textColor", "Default color of console text", cvar_vec3(1));

namespace
{
  struct CColor
  {
    float r{}, g{}, b{};
  };

  // https://github.com/ocornut/imgui/issues/5280#issuecomment-1117155573
  void TextWithHoverColor(ImVec4 col, bool force, const char* fmt, ...)
  {
    using namespace ImGui;
    ImGuiContext& g     = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
      return;

    // Format text
    va_list args;
    va_start(args, fmt);
    const char* text_begin = g.TempBuffer.Data;
    const char* text_end   = g.TempBuffer.Data + ImFormatStringV(g.TempBuffer.Data, g.TempBuffer.Size, fmt, args);
    va_end(args);

    // Layout
    const ImVec2 text_pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    const ImVec2 text_size = CalcTextSize(text_begin, text_end);
    ImRect bb(text_pos.x, text_pos.y, text_pos.x + text_size.x, text_pos.y + text_size.y);
    ItemSize(text_size, 0.0f);
    if (!ItemAdd(bb, 0))
      return;

    // Render
    bool hovered = IsItemHovered() || force;
    if (hovered)
      PushStyleColor(ImGuiCol_Text, col);
    RenderText(bb.Min, text_begin, text_end, false);
    if (hovered)
      PopStyleColor();
  }

  // From ImGui.h, but modified to accept an ImGuiID instead of getting the ID of the previous widget.
  bool BeginPopupContextItem(ImGuiID id, ImGuiPopupFlags popup_flags)
  {
    using namespace ImGui;
    ImGuiContext& g     = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if (window->SkipItems)
      return false;
    int mouse_button = (popup_flags & ImGuiPopupFlags_MouseButtonMask_);
    if (IsMouseReleased(mouse_button) && IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
      OpenPopupEx(id, popup_flags);
    return BeginPopupEx(id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
  }
}

struct ConsoleLogEntry
{
  ConsoleMessageType type;
  std::string message;
  CColor color;
};

struct ConsoleStorage
{
  bool isOpen = false;
  bool wasOpenLastFrame = false;
  std::vector<ConsoleLogEntry> logEntries;
  std::vector<std::string> inputHistory;
  int historyPos{-1};
  std::array<char, 256> inputBuffer{0};
  std::vector<std::string> autocompleteCandidates;
  bool autoScroll     = true;
  bool scrollToBottom = false;

  bool filterMessageTypes[static_cast<int>(ConsoleMessageType::NUM_MESSAGE_TYPES)]{};

  struct PopupState
  {
    bool isPopupOpen      = false;
    int activeIdx         = -1;    // Index of currently 'active' item by use of up/down keys
    int clickedIdx        = -1;    // Index of popup item clicked with the mouse
    bool selectionChanged = false; // Flag to help focus the correct item when selecting active item
    ImVec2 popupPos{};
    ImVec2 popupSize{};
    bool isWindowFocused = false;
    bool isPopupFocused  = false;
    bool userTypedKey    = false;
  } state;
};

namespace
{
  int TextEditCallback(ImGuiInputTextCallbackData* data, ConsoleStorage* console)
  {
    switch (data->EventFlag)
    {
    case ImGuiInputTextFlags_CallbackCompletion:
    {
      if (console->state.isPopupOpen && console->state.activeIdx != -1 && console->autocompleteCandidates.size() > 0)
      {
        const auto& str = console->autocompleteCandidates[console->state.activeIdx];
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, str.c_str());
      }

      console->state.isPopupOpen = false;
      console->state.activeIdx   = -1;
      console->state.clickedIdx  = -1;
      break;
    }
    case ImGuiInputTextFlags_CallbackHistory:
    {
      // move autocomplete cursor if there are candidates
      if (console->state.isPopupOpen)
      {
        if (data->EventKey == ImGuiKey_DownArrow)
        {
          if (++console->state.activeIdx > (int)console->autocompleteCandidates.size() - 1)
            console->state.activeIdx = 0;
        }
        else if (data->EventKey == ImGuiKey_UpArrow)
        {
          if (--console->state.activeIdx < 0)
            console->state.activeIdx = (int)console->autocompleteCandidates.size() - 1;
        }
      }
      else // move history cursor if there is no autocomplete candidate
      {
        const int prev_history_pos = console->historyPos;
        if (data->EventKey == ImGuiKey_UpArrow)
        {
          if (console->historyPos == -1)
            console->historyPos = (int)console->inputHistory.size() - 1;
          else if (console->historyPos > 0)
            console->historyPos--;
        }
        else if (data->EventKey == ImGuiKey_DownArrow)
        {
          if (console->historyPos != -1)
            if (++console->historyPos >= console->inputHistory.size())
              console->historyPos = -1;
        }

        // a better implementation would preserve the data on the current input line along with cursor position
        if (prev_history_pos != console->historyPos)
        {
          const char* history_str = (console->historyPos >= 0) ? console->inputHistory[console->historyPos].c_str() : "";
          data->DeleteChars(0, data->BufTextLen);
          data->InsertChars(0, history_str);
        }
      }
      break;
    }
    case ImGuiInputTextFlags_CallbackAlways:
    {
      if (console->state.clickedIdx != -1)
      {
        const auto& str = console->autocompleteCandidates[console->state.clickedIdx];
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, str.c_str());

        console->state.isPopupOpen = false;
        console->state.activeIdx   = -1;
        console->state.clickedIdx  = -1;
      }

      if (console->inputBuffer[0] == '\0')
        console->state.userTypedKey = false;

      if (console->autocompleteCandidates.empty())
        console->state.isPopupOpen = false;
      else if (console->state.userTypedKey)
        console->state.isPopupOpen = true;
      break;
    }
    case ImGuiInputTextFlags_CallbackEdit:
    {
      console->state.userTypedKey = true;
      break;
    }
    }

    return 0;
  }
} // namespace

Console* Console::Get()
{
  static Console console{};
  return &console;
}

Console::Console()
{
  console = new ConsoleStorage;

  std::ranges::fill(console->filterMessageTypes, true);
  console->filterMessageTypes[static_cast<int>(ConsoleMessageType::LOG_TRACE)] = false;
}

Console::~Console()
{
  delete console;
}

void Console::Log(ConsoleMessageType type, const char* format, ...)
{
  std::array<char, 1024> buf;
  va_list args;
  va_start(args, format);
  vsnprintf(buf.data(), buf.size(), format, args);
  buf.back() = 0;
  va_end(args);
  //cvar_vec3 color = defaultTextColor.Get();
  //console->logEntries.push_back({buf.data(), {color.r, color.g, color.b}});
  console->logEntries.push_back({type, buf.data(), {1, 1, 1}});
}

void Console::LogColor(ConsoleMessageType type, float r, float g, float b, const char* format, ...)
{
  std::array<char, 1024> buf;
  va_list args;
  va_start(args, format);
  vsnprintf(buf.data(), buf.size(), format, args);
  buf.back() = 0;
  va_end(args);
  console->logEntries.push_back({type, buf.data(), {r, g, b}});
}

void Console::ClearLogEntries()
{
  console->logEntries.clear();
}

void Console::Draw(World& world)
{
  ZoneScoped;
  DrawWindow(world);
  DrawPopup(world);

  if (!console->state.isWindowFocused && !console->state.isPopupFocused)
  {
    console->state.isPopupOpen = false;
  }
}

void Console::DrawWindow(World& world)
{
  ZoneScoped;

  if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent))
  {
    console->isOpen = !console->isOpen;
  }

  if (!console->isOpen)
  {
    if (console->wasOpenLastFrame)
    {
      world.globals->game->debugging.forceShowCursor = false;
      console->wasOpenLastFrame                      = false;
    }
    return;
  }

  console->wasOpenLastFrame = true;

  ImGui::SetNextWindowSize(ImVec2(720, 600), ImGuiCond_FirstUseEver);
  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar;
  if (console->state.isPopupOpen)
  {
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
  }
  
  if (!ImGui::Begin("Console", &console->isOpen, windowFlags))
  {
    ImGui::End();
    return;
  }

  if (ImGui::IsWindowAppearing())
  {
    world.globals->game->debugging.forceShowCursor = true;
  }

  if (ImGui::BeginPopupContextItem())
  {
    if (ImGui::MenuItem("Close Console"))
    {
      console->isOpen = false;
    }
    ImGui::EndPopup();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {2, 2});
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});

  static char filterBuffer[256]{};
  static float scoreThreshold = 85;
  if (ImGui::BeginMenuBar())
  {
    // Use normal padding for this menu.
    ImGui::PopStyleVar(2);
    if (ImGui::BeginMenu("Edit"))
    {
      if (ImGui::MenuItem("Clear", nullptr, nullptr))
      {
        ClearLogEntries();
      }
      ImGui::MenuItem("Auto Scroll", nullptr, &console->autoScroll);
      ImGui::EndMenu();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {2, 2});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});

    constexpr int inputTextLength = 180;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("Filter").x - inputTextLength - 22);
    ImGui::Text("Filter");
    ImGui::SetNextItemWidth(inputTextLength);
    ImGui::InputText("##Filter", filterBuffer, std::size(filterBuffer));

    ImGui::EndMenuBar();
  }

  const auto filterLen = std::strlen(filterBuffer);

  ImGui::BeginChild("child window", {}, ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings);
  if (ImGui::BeginMenuBar())
  {
    ImGui::Checkbox("Trace", &console->filterMessageTypes[static_cast<int>(ConsoleMessageType::LOG_TRACE)]);
    ImGui::Checkbox("Debug", &console->filterMessageTypes[static_cast<int>(ConsoleMessageType::LOG_DEBUG)]);
    ImGui::Checkbox("Info", &console->filterMessageTypes[static_cast<int>(ConsoleMessageType::LOG_INFO)]);
    ImGui::Checkbox("Warning", &console->filterMessageTypes[static_cast<int>(ConsoleMessageType::LOG_WARNING)]);
    ImGui::Checkbox("Error", &console->filterMessageTypes[static_cast<int>(ConsoleMessageType::LOG_ERROR)]);
    ImGui::Checkbox("Critical", &console->filterMessageTypes[static_cast<int>(ConsoleMessageType::LOG_CRITICAL)]);
    ImGui::Checkbox("Cmd In", &console->filterMessageTypes[static_cast<int>(ConsoleMessageType::COMMAND_INPUT)]);
    ImGui::Checkbox("Cmd Out", &console->filterMessageTypes[static_cast<int>(ConsoleMessageType::COMMAND_OUTPUT)]);
    ImGui::EndMenuBar();
  }
  ImGui::EndChild();

  ImGui::PopStyleVar(2);

  // Reserve enough left-over height for 1 separator + 1 input text
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
  const float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footerHeightToReserve), false, ImGuiWindowFlags_HorizontalScrollbar);
  if (ImGui::BeginPopupContextWindow())
  {
    if (ImGui::Selectable("Clear"))
    {
      ClearLogEntries();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();


  auto scores = std::vector<std::pair<double, const ConsoleLogEntry*>>(console->logEntries.size());

  {
    ZoneScopedN("Fuzzy string match");
    auto scorer = rapidfuzz::fuzz::CachedPartialRatio(filterBuffer);

    {
      ZoneScopedN("transform");
      std::transform(std::execution::par_unseq,
        console->logEntries.begin(),
        console->logEntries.end(),
        scores.begin(),
        [&](const ConsoleLogEntry& entry) -> decltype(scores)::value_type
        {
          if (!console->filterMessageTypes[static_cast<int>(entry.type)])
          {
            return {0, nullptr};
          }
          ZoneScopedN("Score similarity");
          return {scorer.similarity(entry.message), &entry};
        });
    }

    if (filterLen > 0)
    {
      ZoneScopedN("erase_if");
      std::erase_if(scores, [](const auto& pair) { return pair.first == 0; });
    }
  }

  // Display all the entries in the console.
  ImGui::PushFont(GuiHelper::GetMonospaceFont());
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
  for (const auto& [score, entry] : scores)
  {
    if ((filterLen > 0 && score < scoreThreshold) || entry == nullptr)
    {
      continue;
    }

    const auto& [type, message, color] = *entry;

    // Hashing the entry pointer is more efficient than hashing the message.
    const auto popupId  = ImGui::GetID(static_cast<const void*>(entry));

    ImGui::PushStyleColor(ImGuiCol_Text, {color.r, color.g, color.b, 1});
    auto hoverColor = ImVec4(color.r, color.g, color.b, 0.5f);
    if (type != ConsoleMessageType::COMMAND_INPUT && type != ConsoleMessageType::COMMAND_OUTPUT) // For log colors, brighten the color since they're already fairly dark.
    {
      hoverColor = {pow(color.r, 1 / 3.0f), pow(color.g, 1 / 3.0f), pow(color.b, 1 / 3.0f), 1.0f};
    }
    //ImGui::TextUnformatted(message.c_str());
    TextWithHoverColor(hoverColor, ImGui::IsPopupOpen(popupId, 0), "%s", message.c_str());
    ImGui::PopStyleColor();
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
    if (BeginPopupContextItem(popupId, 1))
    {
      ImGui::PushFont(GuiHelper::GetStandardFont());
      if (ImGui::Selectable("Copy"))
      {
        ImGui::SetClipboardText(message.c_str());
      }
      ImGui::PopFont();
      ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
  }
  ImGui::Dummy({1, 10}); // Add some breathing room to the end of the log.
  ImGui::PopFont();

  if (console->scrollToBottom || (console->autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
  {
    ImGui::SetScrollHereY(1.0f);
  }
  console->scrollToBottom = false;

  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::Separator();

  // Build list of autocomplete candidates.
  console->autocompleteCandidates.clear();
  if (console->inputBuffer[0] != 0)
  {
    auto inputLower = Core::String::ToLower(console->inputBuffer.data());
    Core::String::TrimStartWhitespace(inputLower);
    Core::String::TrimEndWhitespace(inputLower);
    for (const auto& command : world.globals->commandRegistry->GetAllCommands())
    {
      std::string cmdLower = Core::String::ToLower(command.name);
      if (cmdLower.find(inputLower) != std::string::npos)
      {
        console->autocompleteCandidates.push_back(command.name);
      }
    }

    for (const auto& [key, val] : Game2::CVarSystem::Get()->storage->cvarParameters)
    {
      std::string cvarLower = Core::String::ToLower(val.name);
      if (cvarLower.find(inputLower) != std::string::npos)
      {
        console->autocompleteCandidates.push_back(val.name);
      }
    }
  }

  if (console->state.activeIdx == -1 && !console->autocompleteCandidates.empty())
  {
    console->state.activeIdx        = 0;
    console->state.selectionChanged = true;
  }

  auto SubmitCommand = [this, &world]
  {
    auto s = std::string(console->inputBuffer.data());
    Core::String::TrimEndWhitespace(s);
    if (!s.empty())
    {
      ExecuteCommand(world, s);
    }
    console->inputBuffer[0]    = NULL;
    console->state.isPopupOpen = false;
    console->state.activeIdx   = -1;
    console->scrollToBottom    = true;
  };

  bool shouldReclaimFocus = false;
  if (ImGui::IsWindowAppearing())
  {
    ImGui::SetKeyboardFocusHere();
  }
  if (ImGui::InputText(
        "##Input",
        console->inputBuffer.data(),
        console->inputBuffer.size(),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory |
          ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackEdit,
        [](ImGuiInputTextCallbackData* data) -> int
        {
          auto* cc = static_cast<ConsoleStorage*>(data->UserData);
          return TextEditCallback(data, cc);
        },
        console))
  {
    ImGui::SetKeyboardFocusHere(-1);

    SubmitCommand();

    shouldReclaimFocus = true;
  }

  if (console->state.clickedIdx != -1)
  {
    ImGui::SetKeyboardFocusHere(-1);
    console->state.isPopupOpen  = false;
    console->state.userTypedKey = false;
  }

  if (shouldReclaimFocus)
  {
    ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
  }

  //if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
  //{
  //  ImGui::SetKeyboardFocusHere(-1);
  //}

  console->state.popupPos    = ImGui::GetItemRectMin();
  console->state.popupSize.x = ImGui::GetItemRectSize().x - 60;
  console->state.popupSize.y = ImGui::GetTextLineHeightWithSpacing() * 6;
  console->state.popupPos.y += ImGui::GetItemRectSize().y + 10;

  console->state.isWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);

  ImGui::SameLine();

  if (ImGui::Button("Submit"))
  {
    SubmitCommand();
  }

  ImGui::End();
}

void Console::DrawPopup(World&)
{
  ZoneScoped;
  if (!console->state.isPopupOpen)
  {
    return;
  }

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;

  // allow the popup to auto resize to any height, but constrain the X axis
  ImGui::SetNextWindowPos(console->state.popupPos);
  ImGui::SetNextWindowSizeConstraints({console->state.popupSize.x, 0}, {console->state.popupSize.x, FLT_MAX});
  ImGui::Begin("console popup", nullptr, flags);
  ImGui::PushAllowKeyboardFocus(false);

  for (int i = 0; const auto& candidate : console->autocompleteCandidates)
  {
    bool isIndexActive = console->state.activeIdx == i;
    if (isIndexActive)
    {
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 0, 0, 1));
    }

    ImGui::PushID(i);
    if (ImGui::Selectable(candidate.c_str(), isIndexActive))
    {
      console->state.clickedIdx = i;
    }
    ImGui::PopID();

    if (isIndexActive)
    {
      if (console->state.selectionChanged)
      {
        ImGui::SetScrollHereY();
        console->state.selectionChanged = false;
      }

      ImGui::PopStyleColor();
    }
    i++;
  }

  console->state.isPopupFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
  ImGui::PopAllowKeyboardFocus();
  ImGui::End();
}

void Console::ExecuteCommand(World& world, std::string_view name)
{
  //const auto& color = defaultInputColor.Get();
  const auto color = glm::vec3(1, 1, 1);
  LogColor(ConsoleMessageType::COMMAND_INPUT, color.r, color.g, color.b, ">>> %.*s <<<\n", static_cast<int>(name.size()), name.data());

  // Insert into history. First find match and delete it so it can be pushed to the back.
  // This isn't trying to be smart or optimal.
  console->historyPos = -1;
  for (int i = (int)console->inputHistory.size() - 1; i >= 0; i--)
  {
    if (console->inputHistory[i] == name)
    {
      console->inputHistory.erase(console->inputHistory.begin() + i);
      break;
    }
  }
  console->inputHistory.push_back(std::string(name));
  
  auto parser    = Game2::CmdParser(name);
  const auto var = parser.NextToken();
  const auto* id = std::get_if<Game2::Identifier>(&var);
  if (!id)
  {
    Log(ConsoleMessageType::COMMAND_OUTPUT, "Commands must begin with an identifier\n"
                                            "%.*s\n"
                                            "^ not an identifier", static_cast<int>(name.size()), name.data());
    return;
  }

  const auto cmdLower = Core::String::ToLower(id->name);
  const auto& commands = world.globals->commandRegistry->GetAllCommands();
  const auto it       = std::ranges::find(commands, cmdLower, [](const auto& c) { return Core::String::ToLower(c.name); });

  if (it != commands.end())
  {
    it->function(parser.GetRemaining());
    return;
  }

  if (const auto* params = Game2::CVarSystem::Get()->GetCVarParams(id->name))
  {
    std::string remaining = parser.GetRemaining();
    if (remaining.empty())
    {
      Game2::LogCVarInfo(*params);
    }
    else
    {
      if (Game2::CVarSystem::Get()->SetCVarParse(id->name, remaining))
      {
        Game2::LogCVarInfo(*params, true);
      }
      else
      {
        switch (params->type)
        {
        case Game2::CVarType::FLOAT: Log(ConsoleMessageType::COMMAND_OUTPUT, "Usage: %s <float>\n", params->name.c_str()); break;
        case Game2::CVarType::STRING: Log(ConsoleMessageType::COMMAND_OUTPUT, "Usage: %s <string>\n", params->name.c_str()); break;
        case Game2::CVarType::VEC3: Log(ConsoleMessageType::COMMAND_OUTPUT, "Usage: %s <vec3>\n", params->name.c_str()); break;
        }
      }
    }
  }
  else
  {
    Log(ConsoleMessageType::COMMAND_OUTPUT, "No cvar or command with identifier <%s> exists\n", id->name.c_str());
  }
}