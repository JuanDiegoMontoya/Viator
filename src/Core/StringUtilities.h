#pragma once
#include <string>
#include <string_view>

namespace Core::String
{
  std::string ToLower(std::string_view str);

  void TrimEndWhitespace(std::string& str);

  void TrimStartWhitespace(std::string& str);
}