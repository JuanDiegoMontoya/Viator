#include "Assert2.h"
#include "Exception.h"
#include "spdlog/spdlog.h"

#include <cstdlib>

namespace Assert::detail
{
  void LogAssert(const char* condition, const char* fileName, int lineNumber, const char* message)
  {
    spdlog::error("Assertion: {} failed at {}:{}: {}", condition, fileName, lineNumber, message);
  }

  void LogAssert(const char* condition, const char* fileName, int lineNumber)
  {
    spdlog::error("Assertion: {} failed at {}:{}", condition, fileName, lineNumber);
  }

  void LogPanic(const char* fileName, int lineNumber)
  {
    spdlog::critical("Panic at {}:{}", fileName, lineNumber);
  }

  void Panic(const char* fileName, int lineNumber)
  {
    spdlog::default_logger()->flush();
    if constexpr (debug)
    {
      std::abort();
    }
    else
    {
      throw Core::PanicException(fmt::format("Panic at {}:{}", fileName, lineNumber));
    }
  }

  void Abort()
  {
    spdlog::default_logger()->flush();
    std::abort();
  }
} // namespace Assert::detail
