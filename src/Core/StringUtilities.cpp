#include "StringUtilities.h"

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