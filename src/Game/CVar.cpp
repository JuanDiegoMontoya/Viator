#include "CVar.h"

#include "Assets.h"
#include "CVarInternal.h"
#include "CommandParser.h"
#include "Client/Gui/Console.h" // TODO: replace with console storage
#include "Core/Assert2.h"

#include "spdlog/spdlog.h"

#include <array>
#include <shared_mutex>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <format>
#include <set>

namespace Game2
{
  std::string CVarFlagsToString(CVarFlags flags)
  {
    auto stream = std::stringstream();
    if (flags.flags == 0)
    {
      stream << "NONE";
    }
    if (flags & CVarFlagBits::ARCHIVE)
    {
      stream << "ARCHIVE | ";
    }
    if (flags & CVarFlagBits::READ_ONLY)
    {
      stream << "READ_ONLY | ";
    }
    if (flags & CVarFlagBits::CHEAT)
    {
      stream << "CHEAT | ";
    }
    if (flags & CVarFlagBits::REPLICATED)
    {
      stream << "REPLICATED | ";
    }

    auto str = stream.str();
    if (str.size() >= 3 && str.back() == ' ')
    {
      str.pop_back();
      str.pop_back();
      str.pop_back();
    }
    return str;
  }

  void LogCVarInfo(const CVarParameters& params, bool onlyLogValue)
  {
    switch (params.type)
    {
    case Game2::CVarType::FLOAT:
    {
      auto value = Game2::CVarSystem::Get()->GetCVarValue<Game2::cvar_float>(params.name);
      Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.9f, 0.4f, 0.4f, "%s = %f", params.name.c_str(), value);
      break;
    }
    case Game2::CVarType::STRING:
    {
      auto value = Game2::CVarSystem::Get()->GetCVarValue<Game2::cvar_string>(params.name);
      Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.9f, 0.4f, 0.4f, "%s = \"%s\"", params.name.c_str(), value.c_str());
      break;
    }
    case Game2::CVarType::VEC3:
    {
      auto value = Game2::CVarSystem::Get()->GetCVarValue<Game2::cvar_vec3>(params.name);
      Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.9f, 0.4f, 0.4f, "%s = {%f, %f, %f}", params.name.c_str(), value.x, value.y, value.z);
      break;
    }
    }

    if (!onlyLogValue)
    {
      Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.67f, 0.67f, 0.67f, "%s", CVarFlagsToString(params.flags).c_str());
      Console::Get()->Log(ConsoleMessageType::COMMAND_OUTPUT, "%s", params.description.c_str());
    }
  }

  CVarSystem* CVarSystem::Get()
  {
    static CVarSystem system{};
    return &system;
  }

  bool CVarSystem::SetCVarParse(std::string_view name, std::string_view args)
  {
    const auto* params = GetCVarParams(name);
    if (params == nullptr)
    {
      return false;
    }

    auto parser = CmdParser(args);
    auto token  = parser.NextToken();

    switch (params->type)
    {
    case CVarType::FLOAT:
    {
      if (auto* f = std::get_if<cvar_float>(&token))
      {
        return SetCVarValue<cvar_float>(name, *f);
      }
      break;
    }
    case CVarType::STRING:
    {
      if (auto* s = std::get_if<std::string>(&token))
      {
        return SetCVarValue<cvar_string>(name, std::move(*s));
      }
      break;
    }
    case CVarType::VEC3:
    {
      if (auto* v = std::get_if<cvar_vec3>(&token))
      {
        return SetCVarValue<cvar_vec3>(name, *v);
      }
      break;
    }
    }

    return false;
  }

  CVarSystem::CVarSystem()
  {
    storage = new CVarInternal::CVarSystemStorage();

    auto file = std::ifstream(GetConfigDirectory() / "cvars.cfg");

    // Parse each line
    for (int i = 0; file.peek() != EOF; i++)
    {
      auto line = std::string();
      std::getline(file, line);
      auto parser = CmdParser(line);

      // Skip blank lines.
      if (!parser.Valid())
      {
        continue;
      }

      const auto firstToken = parser.NextToken();
      if (const auto* ip = std::get_if<Identifier>(&firstToken))
      {
        const auto secondToken = parser.NextToken();
        try
        {
          if (const auto* dp = std::get_if<cvar_float>(&secondToken))
          {
            RegisterCVar(ip->name, "", *dp, {}, {}, CVarFlagBits::NONE, nullptr, true);
          }
          else if (const auto* sp = std::get_if<cvar_string>(&secondToken))
          {
            RegisterCVar(ip->name, "", *sp, CVarFlagBits::NONE, nullptr, true);
          }
          else if (const auto* vp = std::get_if<cvar_vec3>(&secondToken))
          {
            RegisterCVar(ip->name, "", *vp, {}, {}, CVarFlagBits::NONE, nullptr, true);
          }
          else
          {
            spdlog::error("Invalid argument in cvars.cfg, line {}", i);
          }
        }
        catch (const std::runtime_error& e)
        {
          spdlog::error("{}", e.what());
        }
      }
      else
      {
        spdlog::error("Invalid identifier in cvars.cfg, line {}", i);
      }
    }
  }

  CVarSystem::~CVarSystem()
  {
    // Save archived cvars
    auto file = std::ofstream(GetConfigDirectory() / "cvars.cfg", std::ios::out | std::ios::trunc);

    auto orderedCvarParams = std::set<const CVarParameters*, decltype([](const auto& a, const auto& b) { return a->name < b->name; })>();
    for (const auto& [_, params] : storage->cvarParameters)
    {
      orderedCvarParams.insert(&params);
    }

    for (const auto* params : orderedCvarParams)
    {
      switch (params->type)
      {
      case CVarType::FLOAT:
      {
        file << params->name << ' ' << storage->floatCVars.cvars.at(params->index).current << '\n';
        break;
      }
      case CVarType::STRING:
      {
        file << params->name << " \"" << storage->stringCVars.cvars.at(params->index).current << "\"\n";
        break;
      }
      case CVarType::VEC3:
      {
        auto v = storage->vec3CVars.cvars.at(params->index).current;
        file << params->name << " {" << v.x << ", " << v.y << ", " << v.z << "}\n";
        break;
      }
      default:
      {
        // TODO: Make the saving behavior more manual and don't put it in the destructor.
        // Sketchy log since this is the destructor of a global, and spdlog may have been deinitialized already.
        spdlog::error("Failed to save cvar {} because it has invalid type {}", params->name, int(params->type));
      }
      }
    }

    delete storage;
  }

  CVarParameters* CVarSystem::InitCVar(std::string_view name, std::string_view description, CVarFlags flags, bool isIncomplete, bool& wasIncomplete)
  {
    auto lock = std::scoped_lock(storage->mutex);
    wasIncomplete = false;

    // TODO: transparent lookup
    auto it = storage->cvarParameters.find(std::string(name));
    if (it != storage->cvarParameters.end())
    {
      if (it->second.isFullyInitialized)
      {
        throw std::runtime_error(std::format("Tried to create a new CVar with name {}, but it already exists!", name));
      }
      wasIncomplete = true;
      spdlog::trace("Finishing initialization of partially initialized cvar {}", name);
    }
    else
    {
      spdlog::trace("Initializing cvar {}", name);
      it               = storage->cvarParameters.emplace(std::string(name), CVarParameters{}).first;
      it->second.index = -1;
    }
    CVarParameters& params    = it->second;
    params.name               = name;
    params.description        = description;
    params.flags              = flags;
    params.isFullyInitialized = !isIncomplete;
    return &params;
  }

  const CVarParameters* CVarSystem::GetCVarParams(std::string_view name) const
  {
    if (auto it = storage->cvarParameters.find(std::string(name)); it != storage->cvarParameters.end())
    {
      return &it->second;
    }
    return nullptr;
  }

  CVarParameters* CVarSystem::RegisterCVar(std::string_view name,
    std::string_view description,
    cvar_float defaultValue,
    std::optional<cvar_float> minValue,
    std::optional<cvar_float> maxValue,
    CVarFlags flags,
    OnChangeCallback<cvar_float> callback,
    bool isIncomplete)
  {
    bool wasIncomplete{};
    auto* params = InitCVar(name, description, flags, isIncomplete, wasIncomplete);
    if (wasIncomplete && params->type != CVarType::FLOAT)
    {
      throw std::runtime_error(
        std::format("Tried to register partially initialized cvar {} as type {}, but it was already type {}", name, int(CVarType::FLOAT), int(params->type)));
    }
    params->type = CVarType::FLOAT;
    storage->floatCVars.AddCVar(defaultValue, params, std::move(callback), minValue, maxValue);
    return params;
  }

  CVarParameters* CVarSystem::RegisterCVar(std::string_view name,
    std::string_view description,
    cvar_string defaultValue,
    CVarFlags flags,
    OnChangeCallback<cvar_string> callback,
    bool isIncomplete)
  {
    bool wasIncomplete{};
    auto* params = InitCVar(name, description, flags, isIncomplete, wasIncomplete);
    if (wasIncomplete && params->type != CVarType::STRING)
    {
      throw std::runtime_error(
        std::format("Tried to register partially initialized cvar {} as type {}, but it was already type {}", name, int(CVarType::STRING), int(params->type)));
    }
    params->type = CVarType::STRING;
    storage->stringCVars.AddCVar(std::move(defaultValue), params, std::move(callback));
    return params;
  }

  CVarParameters* CVarSystem::RegisterCVar(std::string_view name,
    std::string_view description,
    cvar_vec3 defaultValue,
    std::optional<cvar_vec3> minValue,
    std::optional<cvar_vec3> maxValue,
    CVarFlags flags,
    OnChangeCallback<cvar_vec3> callback,
    bool isIncomplete)
  {
    bool wasIncomplete{};
    auto* params = InitCVar(name, description, flags, isIncomplete, wasIncomplete);
    if (wasIncomplete && params->type != CVarType::VEC3)
    {
      throw std::runtime_error(
        std::format("Tried to register partially initialized cvar {} as type {}, but it was already type {}", name, int(CVarType::VEC3), int(params->type)));
    }
    params->type = CVarType::VEC3;
    storage->vec3CVars.AddCVar(defaultValue, params, std::move(callback), minValue, maxValue);
    return params;
  }

  template<>
  cvar_float CVarSystem::GetCVarValue(std::string_view name)
  {
    std::shared_lock lck(storage->mutex);
    if (const auto* params = GetCVarParams(name))
    {
      return storage->floatCVars.cvars[params->index].current;
    }
    return 0;
  }

  template<>
  cvar_string CVarSystem::GetCVarValue(std::string_view name)
  {
    std::shared_lock lck(storage->mutex);
    if (const auto* params = GetCVarParams(name))
    {
      return storage->stringCVars.cvars[params->index].current;
    }
    return "";
  }

  template<>
  cvar_vec3 CVarSystem::GetCVarValue(std::string_view name)
  {
    std::shared_lock lck(storage->mutex);
    if (const auto* params = GetCVarParams(name))
    {
      return storage->vec3CVars.cvars[params->index].current;
    }
    return cvar_vec3{};
  }

  template<>
  bool CVarSystem::SetCVarValue(std::string_view name, cvar_float value)
  {
    auto lock = std::scoped_lock(storage->mutex);
    if (const auto* params = GetCVarParams(name))
    {
      auto& cvar = storage->floatCVars.cvars[params->index];
      value      = glm::clamp(value, cvar.min.value_or(value), cvar.max.value_or(value));
      if (cvar.callback)
      {
        cvar.callback(name, value);
      }
      cvar.current = value;
      return true;
    }
    return false;
  }

  template<>
  bool CVarSystem::SetCVarValue(std::string_view name, cvar_string value)
  {
    auto lock = std::scoped_lock(storage->mutex);
    if (const auto* params = GetCVarParams(name))
    {
      auto& cvar = storage->stringCVars.cvars[params->index];
      if (cvar.callback)
      {
        cvar.callback(name, value);
      }
      cvar.current = value;
      return true;
    }
    return false;
  }

  template<>
  bool CVarSystem::SetCVarValue(std::string_view name, cvar_vec3 value)
  {
    auto lock = std::scoped_lock(storage->mutex);
    if (const auto* params = GetCVarParams(name))
    {
      auto& cvar = storage->vec3CVars.cvars[params->index];
      value      = glm::clamp(value, cvar.min.value_or(value), cvar.max.value_or(value));
      if (cvar.callback)
      {
        cvar.callback(name, value);
      }
      cvar.current = value;
      return true;
    }
    return false;
  }

  AutoCVar<cvar_float>::AutoCVar(std::string_view name,
    std::string_view description,
    cvar_float defaultValue,
    std::optional<cvar_float> minValue,
    std::optional<cvar_float> maxValue,
    CVarFlags flags,
    OnChangeCallback<cvar_float> callback)
  {
    auto* params        = CVarSystem::Get()->RegisterCVar(name, description, defaultValue, minValue, maxValue, flags, std::move(callback));
    AutoCVarBase::index = params->index;
  }

  template<>
  AutoCVar<std::string_view>::AutoCVar(std::string_view name, std::string_view description, std::string_view defaultValue, CVarFlags flags, OnChangeCallback<std::string_view> callback)
  {
    auto* params = CVarSystem::Get()->RegisterCVar(std::string(name), description, std::string(defaultValue), flags, callback);
    index        = params->index;
  }

  AutoCVar<cvar_vec3>::AutoCVar(std::string_view name,
    std::string_view description,
    cvar_vec3 defaultValue,
    std::optional<cvar_vec3> minValue,
    std::optional<cvar_vec3> maxValue,
    CVarFlags flags,
    OnChangeCallback<cvar_vec3> callback)
  {
    auto* params = CVarSystem::Get()->RegisterCVar(name, description, defaultValue, minValue, maxValue, flags, std::move(callback));
    index        = params->index;
  }

  template<>
  const CVarStorage<cvar_float>& AutoCVarBase<cvar_float>::Info() const
  {
    return CVarSystem::Get()->storage->floatCVars.cvars[index];
  }

  template<>
  const CVarStorage<cvar_string>& AutoCVarBase<cvar_string>::Info() const
  {
    return CVarSystem::Get()->storage->stringCVars.cvars[index];
  }

  template<>
  const CVarStorage<cvar_vec3>& AutoCVarBase<cvar_vec3>::Info() const
  {
    return CVarSystem::Get()->storage->vec3CVars.cvars[index];
  }

  template<>
  cvar_float AutoCVarBase<cvar_float>::Get() const
  {
    return CVarSystem::Get()->storage->floatCVars.cvars[index].current;
  }

  template<>
  cvar_string AutoCVarBase<cvar_string>::Get() const
  {
    return CVarSystem::Get()->storage->stringCVars.cvars[index].current;
  }

  template<>
  cvar_vec3 AutoCVarBase<cvar_vec3>::Get() const
  {
    return CVarSystem::Get()->storage->vec3CVars.cvars[index].current;
  }

  template<>
  void AutoCVarBase<cvar_float>::Set(cvar_float value)
  {
    auto& cvar = CVarSystem::Get()->storage->floatCVars.cvars[index];
    CVarSystem::Get()->SetCVarValue(cvar.parameters->name, value);
  }

  template<>
  void AutoCVarBase<cvar_string>::Set(std::string value)
  {
    auto& cvar = CVarSystem::Get()->storage->floatCVars.cvars[index];
    CVarSystem::Get()->SetCVarValue(cvar.parameters->name, std::move(value));
  }

  template<>
  void AutoCVarBase<cvar_vec3>::Set(cvar_vec3 value)
  {
    auto& cvar = CVarSystem::Get()->storage->floatCVars.cvars[index];
    CVarSystem::Get()->SetCVarValue(cvar.parameters->name, value);
  }
}