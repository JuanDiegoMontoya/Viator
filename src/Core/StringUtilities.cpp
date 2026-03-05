#include "StringUtilities.h"

#include <algorithm>
#include <ranges>

std::string Core::String::ToLower(std::string_view str)
{
  auto out = std::string(str);
  for (char& c : out)
  {
    c = static_cast<char>(std::tolower(c));
  }
  return out;
}

void Core::String::TrimEndWhitespace(std::string& str)
{
  str.erase(str.find_last_not_of(" \n\r\t") + 1);
}

void Core::String::TrimStartWhitespace(std::string& str)
{
  str.erase(0, str.find_first_not_of(" \n\r\t"));
}

bool Core::String::CompareCaseInsensitive(std::string_view a, std::string_view b)
{
  if (a.size() != b.size())
  {
    return false;
  }

  return std::ranges::all_of(std::views::zip(a, b),
    [](const auto& p)
    {
      auto [ca, cb] = p;
      return std::tolower(ca) == std::tolower(cb);
    });
}