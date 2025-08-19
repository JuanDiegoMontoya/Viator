#pragma once
#include "ClassImplMacros.h"

#include "entt/entity/fwd.hpp"

#include <cstdint>
#include <unordered_map>
#include <filesystem>

class Scripting
{
public:
  Scripting();
  ~Scripting();
  NO_COPY_NO_MOVE(Scripting);

  bool AddScriptIfNotExist(const std::filesystem::path& path);
  void ExecuteScriptW(const std::filesystem::path& path, class World& world, entt::entity entity);
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
    class asIScriptContext* context{};
  };

  class asIScriptEngine* engine = nullptr;
  std::unordered_map<std::filesystem::path, ScriptInfo> scripts;
};
