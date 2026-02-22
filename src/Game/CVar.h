#pragma once
#include "Client/Fvog/detail/Flags.h"
#include "Core/ClassImplMacros.h"

#include "glm/vec3.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

class Console;
class CommandRegistry;

namespace Game2
{
  using cvar_float  = double;
  using cvar_string = std::string;
  using cvar_vec3   = glm::vec3;

  enum class CVarFlagBits : uint32_t
  {
    NONE       = 0,
    ARCHIVE    = 1 << 0,
    READ_ONLY  = 1 << 1,
    CHEAT      = 1 << 2,
    REPLICATED = 1 << 3,
  };
  FVOG_DECLARE_FLAG_TYPE(CVarFlags, CVarFlagBits, uint32_t);

  std::string CVarFlagsToString(CVarFlags flags);

  enum class CVarType : uint8_t
  {
    INVALID,
    FLOAT,
    STRING,
    VEC3,
  };

  struct CVarParameters
  {
    std::string name;
    std::string description;
    CVarFlags flags;
    CVarType type = CVarType::INVALID;
    int index     = -1;
    // Only false when the cvar is loaded from a file, in which case
    // it's missing the description and flags, and the type may be wrong.
    bool isFullyInitialized = false;
  };

  template<typename T>
  using OnChangeCallback = std::function<void(std::string_view, T)>;

  template<typename T>
  struct CVarStorage
  {
    T initial{};
    T current{};
    std::optional<T> min{};
    std::optional<T> max{};
    CVarParameters* parameters{};
    OnChangeCallback<T> callback{};
  };

  namespace CVarInternal
  {
    struct CVarSystemStorage;
  }

  void LogCVarInfo(const CVarParameters& params, bool onlyLogValue = false);

  // One-per-program storage
  class CVarSystem
  {
  public:
    static CVarSystem* Get();
    static void InitInstance(); // Initializes the instance without loading anything.
    static void ResetInstance();
    NO_COPY_NO_MOVE(CVarSystem);
    ~CVarSystem();

    void LoadArchivableCVars(std::string_view fileName = "cvars.cfg");
    void SaveArchivableCVars(std::string_view fileName = "cvars.cfg") const;

    CVarParameters* RegisterCVar(std::string_view name,
      std::string_view description,
      cvar_string defaultValue,
      CVarFlags flags                        = CVarFlagBits::NONE,
      OnChangeCallback<cvar_string> callback = nullptr,
      bool isIncomplete                      = false);

    CVarParameters* RegisterCVar(std::string_view name,
      std::string_view description,
      cvar_float defaultValue,
      std::optional<cvar_float> minValue    = std::nullopt,
      std::optional<cvar_float> maxValue    = std::nullopt,
      CVarFlags flags                       = CVarFlagBits::NONE,
      OnChangeCallback<cvar_float> callback = nullptr,
      bool isIncomplete                     = false);

    CVarParameters* RegisterCVar(std::string_view name,
      std::string_view description,
      cvar_vec3 defaultValue,
      std::optional<cvar_vec3> minValue    = std::nullopt,
      std::optional<cvar_vec3> maxValue    = std::nullopt,
      CVarFlags flags                      = CVarFlagBits::NONE,
      OnChangeCallback<cvar_vec3> callback = nullptr,
      bool isIncomplete                    = false);

    template<typename T>
    [[nodiscard]] T GetCVarValue(std::string_view name);

    // Returns false if T is of the wrong type.
    template<typename T>
    bool SetCVarValue(std::string_view name, T value);

    bool SetCVarParse(std::string_view name, std::string_view args);

  private:
    template<typename U>
    friend class AutoCVarBase;

    friend Console;
    friend CommandRegistry;

    CVarSystem(bool loadCvars);
    CVarParameters* InitCVar(std::string_view name, std::string_view description, CVarFlags flags, bool isIncomplete, bool& wasIncomplete);
    const CVarParameters* GetCVarParams(std::string_view name) const;

    CVarInternal::CVarSystemStorage* storage = nullptr;

    static CVarSystem* sInstance;
  };

  template<typename T>
  class AutoCVarBase
  {
  public:
    NO_COPY_NO_MOVE(AutoCVarBase);

    const CVarStorage<T>& Info() const;

    // returning string_view is not thread-safe
    //std::conditional_t<std::is_same_v<T, cvar_string>, std::string_view, T> Get() const;
    [[nodiscard]] T Get() const;
    void Set(T value);

  protected:
    AutoCVarBase() = default;

    int index{};
  };

  template<typename T>
  class AutoCVar final : public AutoCVarBase<T>
  {
  public:
    AutoCVar(std::string_view name, std::string_view description, T defaultValue, CVarFlags flags = CVarFlagBits::NONE, OnChangeCallback<T> callback = nullptr);
  };

  template<>
  class AutoCVar<cvar_float> final : public AutoCVarBase<cvar_float>
  {
  public:
    AutoCVar(std::string_view name,
      std::string_view description,
      cvar_float defaultValue,
      std::optional<cvar_float> minValue    = std::nullopt,
      std::optional<cvar_float> maxValue    = std::nullopt,
      CVarFlags flags                       = CVarFlagBits::NONE,
      OnChangeCallback<cvar_float> callback = nullptr);
  };

  template<>
  class AutoCVar<cvar_vec3> final : public AutoCVarBase<cvar_vec3>
  {
  public:
    AutoCVar(std::string_view name,
      std::string_view description,
      cvar_vec3 defaultValue,
      std::optional<cvar_vec3> minValue    = std::nullopt,
      std::optional<cvar_vec3> maxValue    = std::nullopt,
      CVarFlags flags                      = CVarFlagBits::NONE,
      OnChangeCallback<cvar_vec3> callback = nullptr);
  };

  template<>
  cvar_float CVarSystem::GetCVarValue(std::string_view);
  template<>
  cvar_string CVarSystem::GetCVarValue(std::string_view);
  template<>
  cvar_vec3 CVarSystem::GetCVarValue(std::string_view);

  template<>
  bool CVarSystem::SetCVarValue(std::string_view, cvar_float);
  template<>
  bool CVarSystem::SetCVarValue(std::string_view, cvar_string);
  template<>
  bool CVarSystem::SetCVarValue(std::string_view, cvar_vec3);

  // template<>
  // AutoCVar<cvar_float>::AutoCVar(std::string_view,
  //   std::string_view,
  //   cvar_float,
  //   std::optional<cvar_float>,
  //   std::optional<cvar_float>,
  //   CVarFlags,
  //   std::function<void(cvar_float)>);
  template<>
  AutoCVar<cvar_string>::AutoCVar(std::string_view, std::string_view, cvar_string, CVarFlags, OnChangeCallback<cvar_string>);
  // template<>
  // AutoCVar<cvar_vec3>::AutoCVar(std::string_view,
  //   std::string_view,
  //   cvar_vec3,
  //   std::optional<cvar_vec3>,
  //   std::optional<cvar_vec3>,
  //   CVarFlags,
  //   OnChangeCallback<cvar_vec3>);

  template<>
  const CVarStorage<cvar_float>& AutoCVarBase<cvar_float>::Info() const;
  template<>
  const CVarStorage<cvar_string>& AutoCVarBase<cvar_string>::Info() const;
  template<>
  const CVarStorage<cvar_vec3>& AutoCVarBase<cvar_vec3>::Info() const;

  template<>
  cvar_float AutoCVarBase<cvar_float>::Get() const;
  template<>
  cvar_string AutoCVarBase<cvar_string>::Get() const;
  template<>
  cvar_vec3 AutoCVarBase<cvar_vec3>::Get() const;

  template<>
  void AutoCVarBase<cvar_float>::Set(cvar_float);
  template<>
  void AutoCVarBase<cvar_string>::Set(cvar_string);
  template<>
  void AutoCVarBase<cvar_vec3>::Set(cvar_vec3);
} // namespace Game2