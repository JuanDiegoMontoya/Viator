#pragma once
//#include "CVar.h"
#include <glm/vec3.hpp>
#include <string>
#include <string_view>
#include <variant>

namespace Game2
{
  struct ParseError
  {
    size_t where{};
    std::string what;
  };

  struct Identifier
  {
    std::string name;
  };

  using CmdToken = std::variant<ParseError, Identifier, double, std::string, glm::vec3>;

  class CmdParser
  {
  public:
    CmdParser(std::string_view command);

    CmdToken NextToken();

    [[nodiscard]] bool Valid() const noexcept
    {
      return current < cmd.size();
    }

    [[nodiscard]] std::string GetRemaining() const
    {
      return cmd.c_str() + current;
    }

  private:
    size_t current{0};
    std::string cmd;
  };
} // namespace Game2