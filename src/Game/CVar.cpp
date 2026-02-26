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
      const auto value = Game2::CVarSystem::Get()->GetCVarValue<Game2::cvar_float>(params.name);
      Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.9f, 0.4f, 0.4f, "%s = %f", params.name.c_str(), value);
      break;
    }
    case Game2::CVarType::STRING:
    {
      const auto value = Game2::CVarSystem::Get()->GetCVarValue<Game2::cvar_string>(params.name);
      Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.9f, 0.4f, 0.4f, "%s = \"%s\"", params.name.c_str(), value.c_str());
      break;
    }
    case Game2::CVarType::VEC3:
    {
      const auto value = Game2::CVarSystem::Get()->GetCVarValue<Game2::cvar_vec3>(params.name);
      Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.9f, 0.4f, 0.4f, "%s = {%f, %f, %f}", params.name.c_str(), value.x, value.y, value.z);
      break;
    }
    }

    if (!onlyLogValue)
    {
      switch (params.type)
      {
      case CVarType::FLOAT:
      {
        const auto value = Game2::CVarSystem::Get()->GetDefaultCVarValue<Game2::cvar_float>(params.name);
        Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.67f, 0.67f, 0.67f, "Default: %f", value);
        break;
      }
      case CVarType::STRING:
      {
        const auto value = Game2::CVarSystem::Get()->GetDefaultCVarValue<Game2::cvar_string>(params.name);
        Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.67f, 0.67f, 0.67f, "Default: \"%s\"", value.c_str());
        break;
      }
      case CVarType::VEC3:
      {
        const auto value = Game2::CVarSystem::Get()->GetDefaultCVarValue<cvar_vec3>(params.name);
        Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.67f, 0.67f, 0.67f, "Default: {%f, %f, %f}", value.x, value.y, value.z);
        break;
      }
      }
      Console::Get()->LogColor(ConsoleMessageType::COMMAND_OUTPUT, 0.67f, 0.67f, 0.67f, "%s", CVarFlagsToString(params.flags).c_str());
      Console::Get()->Log(ConsoleMessageType::COMMAND_OUTPUT, "%s", params.description.c_str());
    }
  }

  CVarSystem* CVarSystem::sInstance = nullptr;
  static std::mutex sInitMutex;

  CVarSystem* CVarSystem::Get()
  {
    if (!sInstance)
    {
      auto lock = std::scoped_lock(sInitMutex);
      if (!sInstance)
      {
        sInstance = new CVarSystem(true);
      }
    }
    return sInstance;
  }

  void CVarSystem::InitInstance()
  {
    ASSERT(!sInstance);
    sInstance = new CVarSystem(false);
  }

  void CVarSystem::ResetInstance()
  {
    if (sInstance)
    {
      auto lock = std::scoped_lock(sInitMutex);
      delete sInstance;
      sInstance = nullptr;
    }
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

  CVarSystem::CVarSystem(bool loadCvars)
  {
    spdlog::info("Initializing CVar system");
    storage = new CVarInternal::CVarSystemStorage();

    if (loadCvars)
    {
      LoadArchivableCVars();
    }
  }

  CVarSystem::~CVarSystem()
  {
    delete storage;
  }

  void CVarSystem::LoadArchivableCVars(std::string_view fileName)
  {
    spdlog::debug("Loading archivable cvars");
    auto file = std::ifstream(GetConfigDirectory() / fileName);

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

  void CVarSystem::SaveArchivableCVars(std::string_view fileName) const
  {
    // This function is not called in the destructor because
    // 1. We don't want to force CVars to be archived in all situations (e.g. certain hypothetical tests); and
    // 2. The logging done in this function would be unsafe as both CVarSystem and spdlog are globals with no well-defined lifetime ordering.
    auto file = std::ofstream(GetConfigDirectory() / fileName, std::ios::out | std::ios::trunc);

    auto orderedCvarParams = std::set<const CVarParameters*, decltype([](const auto& a, const auto& b) { return a->name < b->name; })>();
    for (const auto& [_, params] : storage->cvarParameters)
    {
      orderedCvarParams.insert(&params);
    }

    for (const auto* params : orderedCvarParams)
    {
      if (!(params->flags & CVarFlagBits::ARCHIVE))
      {
        continue;
      }

#ifdef FROG_DEBUG
      // Don't save non-fully initialized cvars. This prevents cvars.cfg from slowly expanding
      // over time as cvars are modified (removed, renamed, type changed, losing the ARCHIVE flag).
      // Although "real" cvars are expected to be initialized very early in the program lifecycle,
      // graceful program termination before then may cause some of their archived values to be lost.
      // Only performing this check in debug builds should prevent users from losing data.
      if (!params->isFullyInitialized)
      {
        spdlog::debug("cvar {} will not be archived because it is not fully initialized", params->name);
        continue;
      }
#endif

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
        spdlog::error("Failed to save cvar {} because it has invalid type {}", params->name, int(params->type));
      }
      }
    }
  }

  CVarParameters* CVarSystem::InitCVar(std::string_view name, std::string_view description, CVarFlags flags, bool isIncomplete, bool& wasIncomplete)
  {
    auto lock = std::scoped_lock(storage->cvarParametersMutex);
    wasIncomplete = false;

    auto it = storage->cvarParameters.find(name);
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
    auto lock = std::shared_lock(storage->cvarParametersMutex);
    if (auto it = storage->cvarParameters.find(name); it != storage->cvarParameters.end())
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
      spdlog::warn("Tried to register partially initialized cvar {} as type {}, but it was already type {}", name, int(CVarType::FLOAT), int(params->type));
    }
    params->type = CVarType::FLOAT;
    storage->floatCVars.AddCVar(defaultValue, params, std::move(callback), minValue, maxValue);
    if (wasIncomplete) // The old value may need to be clamped if its constraints changed.
    {
      auto& cvar   = storage->floatCVars.cvars[params->index];
      cvar.current = glm::clamp(cvar.current, cvar.min.value_or(cvar.current), cvar.max.value_or(cvar.current));
    }
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
      spdlog::warn("Tried to register partially initialized cvar {} as type {}, but it was already type {}", name, int(CVarType::STRING), int(params->type));
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
      spdlog::warn("Tried to register partially initialized cvar {} as type {}, but it was already type {}", name, int(CVarType::VEC3), int(params->type));
    }
    params->type = CVarType::VEC3;
    storage->vec3CVars.AddCVar(defaultValue, params, std::move(callback), minValue, maxValue);
    if (wasIncomplete) // The old value may need to be clamped if its constraints changed.
    {
      auto& cvar = storage->vec3CVars.cvars[params->index];
      cvar.current = glm::clamp(cvar.current, cvar.min.value_or(cvar.current), cvar.max.value_or(cvar.current));
    }
    return params;
  }

  template<>
  cvar_float CVarSystem::GetCVarValue(std::string_view name)
  {
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::FLOAT)
    {
      auto lock = std::shared_lock(storage->cvarMutex);
      return storage->floatCVars.cvars[params->index].current;
    }
    return {};
  }

  template<>
  cvar_string CVarSystem::GetCVarValue(std::string_view name)
  {
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::STRING)
    {
      auto lock = std::shared_lock(storage->cvarMutex);
      return storage->stringCVars.cvars[params->index].current;
    }
    return {};
  }

  template<>
  cvar_vec3 CVarSystem::GetCVarValue(std::string_view name)
  {
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::VEC3)
    {
      auto lock = std::shared_lock(storage->cvarMutex);
      return storage->vec3CVars.cvars[params->index].current;
    }
    return {};
  }

  template<>
  cvar_float CVarSystem::GetDefaultCVarValue(std::string_view name)
  {
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::FLOAT)
    {
      return storage->floatCVars.cvars[params->index].initial;
    }
    return {};
  }

  template<>
  cvar_string CVarSystem::GetDefaultCVarValue(std::string_view name)
  {
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::STRING)
    {
      return storage->stringCVars.cvars[params->index].initial;
    }
    return {};
  }

  template<>
  cvar_vec3 CVarSystem::GetDefaultCVarValue(std::string_view name)
  {
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::VEC3)
    {
      return storage->vec3CVars.cvars[params->index].initial;
    }
    return {};
  }

  template<>
  bool CVarSystem::SetCVarValue(std::string_view name, cvar_float value)
  {
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::FLOAT && !(params->flags & CVarFlagBits::READ_ONLY))
    {
      auto lock = std::scoped_lock(storage->cvarMutex);
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
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::STRING && !(params->flags& CVarFlagBits::READ_ONLY))
    {
      auto lock = std::scoped_lock(storage->cvarMutex);
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
    if (const auto* params = GetCVarParams(name); params && params->type == CVarType::VEC3 && !(params->flags & CVarFlagBits::READ_ONLY))
    {
      auto lock = std::scoped_lock(storage->cvarMutex);
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


#include "doctest.h"

TEST_CASE("CVarSystem")
{
  Game2::CVarSystem::ResetInstance();
  Game2::CVarSystem::InitInstance();
  auto* system = Game2::CVarSystem::Get();

  SUBCASE("Simple")
  {
    constexpr auto name        = std::string_view("default.name");
    constexpr auto description = std::string_view("test description");
    SUBCASE("Test string cvar")
    {
      const auto defaultValue = Game2::cvar_string("default string value");
      auto* params = system->RegisterCVar(name, description, defaultValue);
      CHECK(params->name == name);
      CHECK(params->description == description);
      CHECK(params->type == Game2::CVarType::STRING);
      CHECK(params->flags.flags == 0);
      CHECK(params->isFullyInitialized);

      CHECK_EQ(system->GetCVarValue<Game2::cvar_string>(name), defaultValue);
      CHECK(system->SetCVarValue(name, Game2::cvar_string("new string")));
      CHECK_EQ(system->GetCVarValue<Game2::cvar_string>(name), Game2::cvar_string("new string"));
      CHECK_FALSE(system->SetCVarValue(name, Game2::cvar_float(0)));
      CHECK_FALSE(system->SetCVarValue(name, Game2::cvar_vec3(0)));
    }

    SUBCASE("Test float cvar")
    {
      constexpr auto defaultValue = Game2::cvar_float(2.5f);
      constexpr auto minValue     = Game2::cvar_float(0.0f);
      constexpr auto maxValue     = Game2::cvar_float(10.0f);
      auto* params                = system->RegisterCVar(name, description, defaultValue, minValue, maxValue);
      CHECK(params->name == name);
      CHECK(params->description == description);
      CHECK(params->type == Game2::CVarType::FLOAT);
      CHECK(params->flags.flags == 0);
      CHECK(params->isFullyInitialized);

      CHECK_EQ(system->GetCVarValue<Game2::cvar_float>(name), defaultValue);
      CHECK(system->SetCVarValue(name, Game2::cvar_float(3.75f)));
      CHECK_EQ(system->GetCVarValue<Game2::cvar_float>(name), Game2::cvar_float(3.75f));
      CHECK_FALSE(system->SetCVarValue(name, Game2::cvar_string("a string")));
      CHECK_FALSE(system->SetCVarValue(name, Game2::cvar_vec3(0)));
    }

    SUBCASE("Test vec3 cvar")
    {
      constexpr auto defaultValue = Game2::cvar_vec3(1.0f, 2.5f, 3.0f);
      constexpr auto minValue     = Game2::cvar_vec3(0.0f);
      constexpr auto maxValue     = Game2::cvar_vec3(10.0f);
      auto* params                = system->RegisterCVar(name, description, defaultValue, minValue, maxValue);
      CHECK(params->name == name);
      CHECK(params->description == description);
      CHECK(params->type == Game2::CVarType::VEC3);
      CHECK(params->flags.flags == 0);
      CHECK(params->isFullyInitialized);

      CHECK_EQ(system->GetCVarValue<Game2::cvar_vec3>(name), defaultValue);
      CHECK(system->SetCVarValue(name, Game2::cvar_vec3(0, 5, 10)));
      CHECK_EQ(system->GetCVarValue<Game2::cvar_vec3>(name), Game2::cvar_vec3(0, 5, 10));
      CHECK_FALSE(system->SetCVarValue(name, Game2::cvar_string("a string")));
      CHECK_FALSE(system->SetCVarValue(name, Game2::cvar_float(0)));
    }
  }

  SUBCASE("CVar serialization round-trip")
  {
    system->RegisterCVar("test.string", "", Game2::cvar_string("test value"), Game2::CVarFlagBits::ARCHIVE);
    system->RegisterCVar("test.float", "", Game2::cvar_float(4.5f), {}, {}, Game2::CVarFlagBits::ARCHIVE);
    system->RegisterCVar("test.vec3", "", Game2::cvar_vec3(1, 2, 3), {}, {}, Game2::CVarFlagBits::ARCHIVE);
    system->RegisterCVar("not.archived", "", Game2::cvar_vec3(1, 2, 3));
    system->SaveArchivableCVars("test.cfg");
    Game2::CVarSystem::ResetInstance();
    Game2::CVarSystem::InitInstance();
    system = Game2::CVarSystem::Get();
    system->LoadArchivableCVars("test.cfg");
    CHECK_EQ(system->GetCVarValue<Game2::cvar_string>("test.string"), Game2::cvar_string("test value"));
    CHECK_EQ(system->GetCVarValue<Game2::cvar_float>("test.float"), Game2::cvar_float(4.5f));
    CHECK_EQ(system->GetCVarValue<Game2::cvar_vec3>("test.vec3"), Game2::cvar_vec3(1, 2, 3));
    CHECK_EQ(system->GetCVarParams("not.archived"), nullptr);
    CHECK_EQ(system->GetCVarValue<Game2::cvar_vec3>("not.archived"), Game2::cvar_vec3{});
    std::filesystem::remove(GetConfigDirectory() / "test.cfg");
  }
}