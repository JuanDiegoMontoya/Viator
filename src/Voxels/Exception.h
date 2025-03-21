#pragma once
#include <exception>
#include <string>

namespace Core
{
  class Exception : public std::exception
  {
  public:
    Exception() = default;
    explicit Exception(std::string message) : message_(static_cast<std::string&&>(message)) {}

    [[nodiscard]] const char* what() const noexcept override
    {
      return message_.c_str();
    }

  protected:
    std::string message_;
  };

  class PanicException : public Exception
  {
    using Exception::Exception;
  };
} // namespace Core
