#include "Scripting.h"

#include "CommandParser.h"
#include "Commands.h"
#include "Globals.h"
#include "Core/Assert2.h"
#include "Game/Assets.h"
#include "Game/World.h"

#include "spdlog/spdlog.h"
#include "spdlog/fmt/std.h"

#include "angelscript.h"
#include "Client/Gui/Console.h"
#include "angelscript/sdk/add_on/scriptstdstring/scriptstdstring.h"
#include "angelscript/sdk/add_on/scriptbuilder/scriptbuilder.h"

#include "tracy/Tracy.hpp"

namespace
{
  void ExecuteScriptInternal(const asIScriptModule& mod, asIScriptContext& context, const char* decl, std::vector<entt::meta_any> args)
  {
    ZoneScoped;
    auto* func = mod.GetFunctionByName(decl);
    if (func == nullptr)
    {
      spdlog::warn("Tried to execute a function that doesn't exist.\n"
                   "Module: {}\n"
                   "Function: {}",
        mod.GetName(),
        decl);
      return;
    }

    if (context.Prepare(func) < 0)
    {
      spdlog::warn("Failed to prepare script.\n"
                   "Module: {}\n"
                   "Function: {}",
        mod.GetName(),
        func->GetDeclaration());
      return;
    }

    if (func->GetParamCount() != args.size())
    {
      spdlog::warn("Number of args provided ({}) does not match the number of function parameters ({}).\n"
                   "Module: {}\n"
                   "Function: {}",
        args.size(),
        func->GetParamCount(),
        mod.GetName(),
        func->GetDeclaration());
      return;
    }

    for (int i = 0; auto& arg : args)
    {
      ZoneScopedN("Argument");
      const auto argIdx = i++;
      using namespace entt::literals;
      auto fn = arg.type().func("ASSetArg"_hs);
      ASSERT(fn);
      ASSERT(fn.invoke({}, (void*)&context, argIdx, arg));
    }

    try
    {
      ZoneScopedN("script.context->Execute()");
      const auto ret = context.Execute();

      if (ret == asEXECUTION_EXCEPTION)
      {
        const auto* exceptFn = context.GetExceptionFunction();
        ASSERT(exceptFn);
        spdlog::warn("Exception thrown while executing script.\n"
                     "In module {}\n"
                     "Function {}\n"
                     "Line {}: {}",
          exceptFn->GetModuleName(),
          exceptFn->GetDeclaration(),
          context.GetExceptionLineNumber(),
          context.GetExceptionString());
      }

      if (ret == asEXECUTION_ERROR)
      {
        spdlog::warn("Unknown error while executing script.\n"
                     "In module {}\n"
                     "Top-level function {}",
          func->GetModuleName(),
          func->GetDeclaration());
      }
    }
    catch (std::exception& e)
    {
      spdlog::warn("C++ exception thrown while executing script.\n"
                   "In module {}\n"
                   "Top-level function {}\n"
                   "Message: {}",
        mod.GetName(),
        func->GetName(),
        e.what());
    }
    catch (...)
    {
      spdlog::warn("Unknown C++ exception thrown while executing script.\n"
                   "In module {}\n"
                   "Top-level function {}",
        mod.GetName(),
        func->GetName());
    }
  }
} // namespace

namespace
{
  void print(const std::string& msg)
  {
    printf("%s", msg.c_str());
  }
}

static void MessageCallback(asSMessageInfo* msg, void*)
{
  //if (msg->type == asMSGTYPE_ERROR || msg->type == asMSGTYPE_WARNING)
  {
    const char* type = msg->type == asMSGTYPE_ERROR ? "ERROR" : msg->type == asMSGTYPE_WARNING ? "WARNING" : "INFORMATION";
    spdlog::warn("{}({}, {}): {}: {}", msg->section, msg->row, msg->col, type, msg->message);
  }
}

Scripting::Scripting()
{
  ZoneScoped;
  spdlog::info("Initializing scripting engine.");
  engine = asCreateScriptEngine();
  ASSERT(engine->SetMessageCallback(asFUNCTION(MessageCallback), nullptr, asCALL_CDECL) >= 0);
  RegisterStdString(engine);
  ASSERT(engine->RegisterGlobalFunction("void print(const string& in)", asFUNCTION(print), asCALL_CDECL) >= 0);
}

Scripting::~Scripting()
{
  ZoneScoped;
  spdlog::info("Terminating scripting engine.");
  for (const auto& [path, script] : scripts)
  {
    script.context->Release();
  }

  engine->Release();
}

void Scripting::RegisterCommands(World& world)
{
  ZoneScoped;
  auto& commandRegistry = *world.globals->commandRegistry;
  commandRegistry.RegisterCommand({
    .name        = "exec",
    .description = "Executes some AngelScript code.",
    .function    = [this, &world](std::string_view args)
    {
      auto parser = Game2::CmdParser(args);
      const auto token  = parser.NextToken();

      const std::string* str = std::get_if<std::string>(&token);
      if (!str)
      {
        spdlog::warn("Usage: exec \"code\"");
        return;
      }

      auto stream = std::stringstream();
      stream << "void main(World& world){" << *str << "}\n";
      ExecuteScriptFromCode(stream.str(), "main", {&world});
    },
  });
}

bool Scripting::AddScriptIfNotExist(const std::filesystem::path& path)
{
  ZoneScoped;

  if (scripts.contains(path))
  {
    spdlog::warn("Failed to add script {}.\n"
                 "File already exists.", path);
    return false;
  }

  if (!std::filesystem::is_regular_file(path))
  {
    spdlog::warn("Failed to add script {}.\n"
                 "File doesn't exist.", path);
    return false;
  }

  const auto pathStr = path.string();
  auto builder = CScriptBuilder();
  if (builder.StartNewModule(engine, pathStr.c_str()) < 0)
  {
    return false;
  }
  if (builder.AddSectionFromFile(pathStr.c_str()) < 0)
  {
    return false;
  }
  if (builder.BuildModule() < 0)
  {
    return false;
  }

  spdlog::info("Loaded script {}", path);
  scripts.emplace(path,
    ScriptInfo{
      .path          = path,
      .lastWriteTime = static_cast<uint64_t>(std::filesystem::last_write_time(path).time_since_epoch().count()),
      .context       = engine->CreateContext(),
    });
  return true;
}

void Scripting::ExecuteScript(const std::filesystem::path& path, const char* decl, std::vector<entt::meta_any> args)
{
  ZoneScoped;

  auto it = scripts.find(path);
  if (it == scripts.end())
  {
    spdlog::warn("Tried to load a script that doesn't exist.\n"
                 "Script: {}",
      path);
    return;
  }

  auto& script = it->second;

  auto* mod = engine->GetModule(path.string().c_str());
  if (mod == nullptr)
  {
    spdlog::warn("Tried to load a module that doesn't exist.\n"
                 "Module: {}",
      path);
    return;
  }

  ExecuteScriptInternal(*mod, *script.context, decl, std::move(args));
}

void Scripting::ExecuteScriptFromCode(std::string_view code, const char* decl, std::vector<entt::meta_any> args)
{
  ZoneScoped;

  constexpr const char* tempModuleName = "TemporaryModuleDoNotReuseThisName";

  auto builder = CScriptBuilder();
  if (builder.StartNewModule(engine, tempModuleName) < 0)
  {
    spdlog::warn("Failed to start new module");
    return;
  }
  if (builder.AddSectionFromMemory("Section", code.data(), static_cast<unsigned>(code.size())) < 0)
  {
    spdlog::warn("Failed to add section to script");
    return;
  }
  if (builder.BuildModule() < 0)
  {
    spdlog::warn("Failed to build module");
    return;
  }

  auto* context = engine->CreateContext();

  ExecuteScriptInternal(*builder.GetModule(), *context, decl, std::move(args));

  engine->ReturnContext(context);

  if (engine->DiscardModule(tempModuleName))
  {
    spdlog::warn("Failed to discard temporary module");
  }
}

void Scripting::PollAndReloadModifiedScripts()
{
  ZoneScoped;

  for (auto& [path, script] : scripts)
  {
    const auto lastWriteTime = static_cast<uint64_t>(std::filesystem::last_write_time(path).time_since_epoch().count());
    if (lastWriteTime > script.lastWriteTime)
    {
      spdlog::info("Recompiling script {}", path);
      script.lastWriteTime = lastWriteTime;

      const auto pathStr = path.string();
      auto builder       = CScriptBuilder();
      if (builder.StartNewModule(engine, (pathStr + ".TEMP").c_str()) < 0)
      {
        continue;
      }
      if (builder.AddSectionFromFile(pathStr.c_str()) < 0)
      {
        continue;
      }
      if (builder.BuildModule() < 0)
      {
        continue;
      }

      engine->DiscardModule(pathStr.c_str());
      builder.GetModule()->SetName(pathStr.c_str());
    }
  }
}

#ifndef GAME_HEADLESS
#include "imgui.h"

void Scripting::DrawDebugUI(World& world)
{
  ZoneScoped;

  // TODO: temp
  PollAndReloadModifiedScripts();

  if (ImGui::Begin("Scripts"))
  {
    static char buffer[256]{};
    if (ImGui::Button("Load"))
    {
      AddScriptIfNotExist(GetScriptDirectory() / buffer);
    }
    ImGui::SameLine();
    ImGui::InputText("##add", buffer, 256);

    ImGui::SeparatorText("Loaded scripts");
    static int32_t ent = 0;
    ImGui::InputInt("entity", &ent);
    for (int i = 0; auto& [path, script] : scripts)
    {
      ImGui::PushID(i++);
      if (ImGui::Button("Execute"))
      {
        ExecuteScript(path, "main", {&world, entt::entity(ent), 42});
      }

      ImGui::SameLine();

      ImGui::Text("%s", path.string().c_str());
      ImGui::PopID();
    }
  }
  ImGui::End();
}

#endif
