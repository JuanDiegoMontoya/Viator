#include "Assert2.h"
#include "Exception.h"
#include "spdlog/spdlog.h"

#include <cstdlib>
#ifdef __cpp_lib_stacktrace
  #include <stacktrace>
#endif

namespace Assert::detail
{
  void LogAssert(const char* condition, const char* fileName, int lineNumber, const char* message)
  {
#ifdef __cpp_lib_stacktrace
    spdlog::error("Assertion: {} failed at {}:{}: {}\n{}", condition, fileName, lineNumber, message, std::to_string(std::stacktrace::current()));
#else
    spdlog::error("Assertion: {} failed at {}:{}: {}", condition, fileName, lineNumber, message);
#endif
  }

  void LogAssert(const char* condition, const char* fileName, int lineNumber)
  {
#ifdef __cpp_lib_stacktrace
    spdlog::error("Assertion: {} failed at {}:{}\n{}", condition, fileName, lineNumber, std::to_string(std::stacktrace::current()));
#else
    spdlog::error("Assertion: {} failed at {}:{}", condition, fileName, lineNumber);
#endif
  }

  void LogPanic(const char* fileName, int lineNumber)
  {
#ifdef __cpp_lib_stacktrace
    spdlog::critical("Panic at {}:{}\n{}", fileName, lineNumber, std::to_string(std::stacktrace::current()));
#else
    spdlog::critical("Panic at {}:{}", fileName, lineNumber);
#endif
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
#ifdef __cpp_lib_stacktrace
      throw Core::PanicException(fmt::format("Panic at {}:{}\n{}", fileName, lineNumber, std::to_string(std::stacktrace::current())));
#else
      throw Core::PanicException(fmt::format("Panic at {}:{}", fileName, lineNumber));
#endif
    }
  }

  void Abort()
  {
    spdlog::default_logger()->flush();
    std::abort();
  }
} // namespace Assert::detail
