#pragma once
#include "Core/Assert2.h"

#include <array>
#include <mutex>
#include <shared_mutex>

namespace Game2::CVarInternal
{
  template<typename T, int Capacity>
  struct CVarArray
  {
    std::array<CVarStorage<T>, Capacity> cvars;
    std::atomic_int nextIndex{0};

    int AddCVar(T value, CVarParameters* params, OnChangeCallback<T> callback, std::optional<T> min = {}, std::optional<T> max = {})
    {
      auto index = params->index;
      const bool needsAllocIndex = index == -1;
      if (needsAllocIndex)
      {
        // Thread-safe
        index = nextIndex++;
        ASSERT(index < Capacity, "CVar count exceeds storage capacity");
      }

      // If loaded from archive and has the archive flag, don't update its current value.
      if (!(params->flags & CVarFlagBits::ARCHIVE) || needsAllocIndex)
      {
        cvars[index].current = value;
      }
      cvars[index].initial    = value;
      cvars[index].min        = std::move(min);
      cvars[index].max        = std::move(max);
      cvars[index].parameters = params;
      cvars[index].callback   = std::move(callback);

      params->index = index;
      return index;
    }
  };

  struct CVarSystemStorage
  {
    CVarInternal::CVarArray<cvar_float, 1000> floatCVars;
    CVarInternal::CVarArray<cvar_string, 1000> stringCVars;
    CVarInternal::CVarArray<cvar_vec3, 1000> vec3CVars;
    
  private:
    struct Hash : std::hash<std::string_view> { using is_transparent = void; };

  public:
    std::unordered_map<std::string, CVarParameters, Hash, std::equal_to<>> cvarParameters;
    std::shared_mutex cvarMutex;
    std::shared_mutex cvarParametersMutex;
  };
} // namespace Game2::CVarInternal