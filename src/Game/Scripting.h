#pragma once
#include "Core/ClassImplMacros.h"

#include "entt/entity/fwd.hpp"
#include "entt/meta/meta.hpp"

#include <cstdint>
#include <unordered_map>
#include <filesystem>
#include <vector>
#include <string_view>

class World;
class asIScriptContext;
class asIScriptModule;

class Scripting
{
public:
  Scripting();
  ~Scripting();
  NO_COPY_NO_MOVE(Scripting);

  void RegisterCommands(World& world);

  bool AddScriptIfNotExist(const std::filesystem::path& path);
  void ExecuteScript(const std::filesystem::path& path, const char* decl, std::vector<entt::meta_any> args);
  void ExecuteScriptFromCode(std::string_view code, const char* decl, std::vector<entt::meta_any> args);
  void PollAndReloadModifiedScripts();

  [[nodiscard]] auto& GetEngine()
  {
    return *engine;
  }

#ifndef GAME_HEADLESS
  void DrawDebugUI(World& world);
#endif

private:
  struct ScriptInfo
  {
    std::filesystem::path path;
    uint64_t lastWriteTime{};
    asIScriptContext* context{};
  };

  class asIScriptEngine* engine = nullptr;
  std::unordered_map<std::filesystem::path, ScriptInfo> scripts;
};
