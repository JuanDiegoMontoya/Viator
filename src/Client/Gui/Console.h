#pragma once
#include "Core/ClassImplMacros.h"

#include <string_view>

struct ConsoleStorage;
class World;

enum class ConsoleMessageType
{
  UNASSIGNED,
  LOG_TRACE,
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR,
  LOG_CRITICAL,
  COMMAND_INPUT,
  COMMAND_OUTPUT,

  NUM_MESSAGE_TYPES,
};

class Console
{
public:
  static Console* Get();
  ~Console();

  NO_COPY_NO_MOVE(Console);

  void ExecuteCommand(World& world, std::string_view name);

  void Log(ConsoleMessageType type, const char* format, ...);
  void LogColor(ConsoleMessageType type, float r, float g, float b, const char* format, ...);
  void ClearLogEntries();

  void Draw(World& world);

private:
  Console();
  void DrawWindow(World& world);
  void DrawPopup(World& world);
  ConsoleStorage* console;
};
