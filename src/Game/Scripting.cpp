#include "Scripting.h"
#include "Core/Assert2.h"
#include "Game/Assets.h"

#include "spdlog/spdlog.h"
#include "spdlog/fmt/std.h"

#include "angelscript.h"
#include "angelscript/sdk/add_on/scriptstdstring/scriptstdstring.h"
#include "angelscript/sdk/add_on/scriptbuilder/scriptbuilder.h"

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
  engine = asCreateScriptEngine();
  ASSERT(engine->SetMessageCallback(asFUNCTION(MessageCallback), nullptr, asCALL_CDECL) >= 0);
  RegisterStdString(engine);
  ASSERT(engine->RegisterGlobalFunction("void print(const string& in)", asFUNCTION(print), asCALL_CDECL) >= 0);
}

Scripting::~Scripting()
{
  for (const auto& [path, script] : scripts)
  {
    script.context->Release();
  }

  engine->Release();
}

bool Scripting::AddScriptIfNotExist(const std::filesystem::path& path)
{
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

  scripts.emplace(path,
    ScriptInfo{
      .path          = path,
      .lastWriteTime = static_cast<uint64_t>(std::filesystem::last_write_time(path).time_since_epoch().count()),
      .context       = engine->CreateContext(),
    });
  return true;
}

void Scripting::ExecuteScriptW(const std::filesystem::path& path, World& world, entt::entity entity)
{
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

  const char* const decl = "void main(World& inout, entity)";
  auto* func = mod->GetFunctionByDecl(decl);
  if (func == nullptr)
  {
    spdlog::warn("Tried to execute a function that doesn't exist.\n"
                 "Module: {}\n"
                 "Function: {}",
      mod->GetName(),
      decl);
    return;
  }

  if (script.context->Prepare(func) < 0)
  {
    spdlog::warn("Failed to prepare script.\n"
                 "Module: {}\n"
                 "Function: {}",
      mod->GetName(),
      func->GetName());
    return;
  }
  
  script.context->SetArgObject(0, &world);
  script.context->SetArgObject(1, &entity);

  try
  {
    const auto ret = script.context->Execute();

    if (ret == asEXECUTION_EXCEPTION)
    {
      const auto* exceptFn = script.context->GetExceptionFunction();
      ASSERT(exceptFn);
      spdlog::warn("Exception thrown while executing script.\n"
                   "In module {}\n"
                   "Function {}\n"
                   "Line {}: {}",
        exceptFn->GetModuleName(),
        exceptFn->GetDeclaration(),
        script.context->GetExceptionLineNumber(),
        script.context->GetExceptionString());
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
      mod->GetName(),
      func->GetName(),
      e.what());
  }
  catch (...)
  {
    spdlog::warn("Unknown C++ exception thrown while executing script.\n"
                 "In module {}\n"
                 "Top-level function {}",
      mod->GetName(),
      func->GetName());
  }
}

void Scripting::PollAndReloadModifiedScripts()
{
  for (auto& [path, script] : scripts)
  {
    const auto lastWriteTime = static_cast<uint64_t>(std::filesystem::last_write_time(path).time_since_epoch().count());
    if (lastWriteTime > script.lastWriteTime)
    {
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
        ExecuteScriptW(path, world, entt::entity(ent));
      }

      ImGui::SameLine();

      ImGui::Text("%s", path.string().c_str());
      ImGui::PopID();
    }
  }
  ImGui::End();
}

#endif
