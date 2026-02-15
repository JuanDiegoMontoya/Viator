#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Game2
{
  struct Command
  {
    std::string name;
    std::string description;
    std::function<void(std::string_view)> function;
  };

  class CommandRegistry
  {
  public:
    CommandRegistry();

    void RegisterCommand(Command command);
    void ExecuteCommand(std::string_view name);
    const Command* GetCommand(std::string_view name) const;
    const std::vector<Command>& GetAllCommands() const;

  private:
    std::vector<Command> commands_;
  };
}