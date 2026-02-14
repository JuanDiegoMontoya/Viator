//#include "PCH.h"
//#include "CVarInternal.h"
#include "Core/Assert2.h"
#include "CommandParser.h"
#include <charconv>
#include <vector>

namespace
{
  std::vector<std::string> split(const char* str)
  {
    std::vector<std::string> result;

    do
    {
      const char* begin = str;

      while (!std::isblank(*str) && *str)
      {
        str++;
      }

      std::string tok(begin, str);
      if (std::ranges::find_if(tok, [](char c) { return !std::isblank(c); }) != tok.end())
      {
        result.push_back(tok);
      }
    } while (0 != *str++);

    return result;
  }

  float stof_nothrow(const std::string& str, size_t* idx) noexcept
  {
    try
    {
      return std::stof(str, idx);
    }
    catch (std::invalid_argument&)
    {
      return 0.0f;
    }
  }

  double stod_nothrow(const std::string& str, size_t* idx) noexcept
  {
    try
    {
      return std::stod(str, idx);
    }
    catch (std::invalid_argument&)
    {
      return 0.0;
    }
  }
} // namespace

enum class CmdType
{
  INVALID,
  FLOAT,
  STRING,
  IDENTIFIER, // command or cvar identifier
  VEC3,
};

CmdParser::CmdParser(std::string_view command) : cmd(command)
{
  // trim whitespace from beginning and end of string
  cmd.erase(0, cmd.find_first_not_of(" \n\r\t"));
  cmd.erase(cmd.find_last_not_of(" \n\r\t") + 1);
}

CmdToken CmdParser::NextToken()
{
  if (!Valid())
  {
    return ParseError{.where = current, .what = "Empty command"};
  }

  auto type = CmdType::INVALID;

  // determine type by first character
  if (cmd[current] == '"')
  {
    type = CmdType::STRING;
    current++;
  }
  else if (cmd[current] == '{' || cmd[current] == '[' || cmd[current] == '(')
  {
    type = CmdType::VEC3;
    current++;
  }
  else if (std::isalpha(cmd[current]) || cmd[current] == '_')
  {
    type = CmdType::IDENTIFIER;
  }
  else if (std::isdigit(cmd[current]) || cmd[current] == '-' || cmd[current] == '.')
  {
    type = CmdType::FLOAT;
  }
  else
  {
    current = cmd.size();
    return ParseError{.where = current, .what = "Token begins with invalid character"};
  }

  auto token          = std::string();
  auto escapeNextChar = false;
  while (Valid())
  {
    char c = cmd[current];
    current++;

    if (std::isblank(c) && !(type == CmdType::STRING || type == CmdType::VEC3))
    {
      break;
    }

    if (type == CmdType::STRING)
    {
      // end string with un-escaped quotation mark
      if (c == '"' && !escapeNextChar)
      {
        break;
      }

      // unescaped backslash will escape next character, how confusing!
      if (c == '\\' && !escapeNextChar)
      {
        escapeNextChar = true;
        continue;
      }

      escapeNextChar = false;
    }

    if (type == CmdType::IDENTIFIER)
    {
      if (!(std::isalnum(c) || c == '.' || c == '_'))
      {
        current = cmd.size();
        return ParseError{.where = current, .what = "Invalid character in identifier"};
      }
    }

    if (type == CmdType::VEC3)
    {
      if (c == '}' || c == ']' || c == ')')
      {
        break;
      }
    }

    token.push_back(c);
  }

  // Skip whitespace between tokens.
  size_t pos = cmd.find_first_not_of(" \n\r\t", current);
  if (pos != std::string::npos)
  {
    current = pos;
  }

  switch (type)
  {
  case CmdType::FLOAT:
  {
    size_t read{};
    double f = stod_nothrow(token, &read);
    if (read != token.size())
    {
      return ParseError{.where = current, .what = "Failed to read float value"};
    }
    return static_cast<float>(f);
  }
  case CmdType::STRING: return token;
  case CmdType::IDENTIFIER: return Identifier{.name = token};
  case CmdType::VEC3:
  {
    std::vector<std::string> tokens = split(token.c_str());
    if (tokens.size() != 3)
    {
      return ParseError{.where = current, .what = "Vector does not contain three tokens"};
    }

    glm::vec3 vec{};
    for (int i = 0; i < 3; i++)
    {
      auto result = std::from_chars(tokens[i].c_str(), tokens[i].c_str() + tokens[i].size(), vec[i]);
      if (result.ec == std::errc::invalid_argument)
      {
        return ParseError{.where = static_cast<size_t>(result.ptr - tokens[i].c_str()), .what = "Failed to read float value"};
      }
    }
    return vec;
  }
  default: break;
  }

  UNREACHABLE;
}