#pragma once
#include "Core/ClassImplMacros.h"
#include <functional>

struct ConsoleStorage;
class World;

using ConsoleFunc = std::function<void(const char*)>;

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

  void RegisterCommand(const char* name, const char* description, ConsoleFunc fn);
  void ExecuteCommand(const char* cmd);
  const char* GetCommandDesc(const char* name) const;

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
