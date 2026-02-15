#include "Commands.h"

#include "Game/CommandParser.h"
#include "Client/Gui/Console.h"
#include "Core/Assert2.h"
#include "Core/StringUtilities.h"

Game2::CommandRegistry::CommandRegistry()
{
  RegisterCommand({
    .name        = "find",
    .description = "- Finds commands with substring",
    .function =
      [this, con = Console::Get()](std::string_view args)
    {
      CmdParser parser(args);
      CmdToken token = parser.NextToken();
      Identifier* id = std::get_if<Identifier>(&token);
      if (!id)
      {
        Console::Get()->Log(ConsoleMessageType::COMMAND_OUTPUT, "Usage: find <cvarname>");
        return;
      }

      std::vector<const Command*> commands;
      std::string idLower = Core::String::ToLower(id->name);
      for (const Command& cmd : GetAllCommands())
      {
        std::string cmdLower = Core::String::ToLower(cmd.name);
        if (cmdLower.find(idLower) != std::string::npos)
        {
          commands.push_back(&cmd);
        }
      }

      for (const Command* cmd : commands)
      {
        Console::Get()->Log(ConsoleMessageType::COMMAND_OUTPUT, "%-25s %s", cmd->name.c_str(), cmd->description.c_str());
      }
    },
  });
  RegisterCommand({
    .name        = "Lua",
    .description = "- Runs the following Lua code",
    .function    = [](std::string_view) { Console::Get()->Log(ConsoleMessageType::COMMAND_OUTPUT, "Lua code :)"); },
  });
  // RegisterCommand("set",
  //   "- Sets the value of a cvar",
  //   [](const char* args)
  //   {
  //     CmdParser parser(args);
  //     CmdToken token1 = parser.NextToken();
  //     auto* id      = std::get_if<Identifier>(&token1);
  //     if (!id || !CVarSystem::Get()->SetCVarParse(id->name.c_str(), parser.GetRemaining().c_str()))
  //     {
  //       Console::Get()->Log("Usage: set <convar> <value>");
  //     }
  //   });
  RegisterCommand({
    .name        = "findall",
    .description = "- Displays all cvars and commands",
    .function =
      [this](std::string_view)
    {
      for (const auto& cmd : GetAllCommands())
      {
        Console::Get()->Log(ConsoleMessageType::COMMAND_OUTPUT, "%-25s %s", cmd.name.c_str(), cmd.description.c_str());
      }
      // for (const auto& [key, params] : storage->cvarParameters)
      //{
      //   Console::Get()->Log(ConsoleMessageType::COMMAND_OUTPUT, "%-25s %s", params.name.c_str(), params.description.c_str());
      // }
    },
  });
  RegisterCommand({
    .name        = "clear",
    .description = "- Clears the contents of the console",
    .function    = [](std::string_view) { Console::Get()->ClearLogEntries(); },
  });
}

void Game2::CommandRegistry::RegisterCommand(Command command)
{
  commands_.push_back(std::move(command));
}

void Game2::CommandRegistry::ExecuteCommand([[maybe_unused]] std::string_view name)
{
  ASSERT(false);
}

const Game2::Command* Game2::CommandRegistry::GetCommand(std::string_view name) const
{
  auto nameLower = Core::String::ToLower(name);
  for (const auto& command : commands_)
  {
    if (Core::String::ToLower(command.name) == nameLower)
    {
      return &command;
    }
  }
  return nullptr;
}

const std::vector<Game2::Command>& Game2::CommandRegistry::GetAllCommands() const
{
  return commands_;
}