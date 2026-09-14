#include "ReflectionUtilitiesInternal.h"

namespace Core::Reflection
{
  std::string RemoveNamespaces(std::string name)
  {
    if (auto pos = name.find_last_of(':'); pos != std::string::npos)
    {
      name.erase(name.begin(), name.begin() + pos + 1);
    }

    if (auto pos = name.find_last_of(' '); pos != std::string_view::npos)
    {
      name.erase(name.begin(), name.begin() + pos + 1);
    }

    return name;
  }
} // namespace Core::Reflection