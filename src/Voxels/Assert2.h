#pragma once

namespace Assert::detail
{
#ifdef FROG_DEBUG
  constexpr bool debug = true;
#else
  constexpr bool debug = false;
#endif
  // TODO: Support function signatures.
  void AssertImpl(const char* condition, const char* fileName, int lineNumber, const char* message);
  void AssertImpl(const char* condition, const char* fileName, int lineNumber);
  void LogPanic(const char* fileName, int lineNumber);
  [[noreturn]] void PanicImpl(const char* fileName, int lineNumber);
  [[noreturn]] void Abort();
} // namespace Assert::detail

#ifdef FROG_DEBUG
  #define UNREACHABLE ASSERT(0)
  #define ASSUME(x)   ASSERT(x)
#else
  #ifdef _MSC_VER
    #define UNREACHABLE __assume(0)
    #define ASSUME(x)   __assume(x)
  #else // GCC, Clang
    #define UNREACHABLE __builtin_unreachable()
  #endif
#endif

#define PANIC                                      \
  do                                               \
  {                                                \
    Assert::detail::LogPanic(__FILE__, __LINE__);  \
    Assert::detail::PanicImpl(__FILE__, __LINE__); \
  } while (0)

// Aborts in debug, does nothing in release.
#ifdef FROG_DEBUG
  #define DEBUG_ASSERT(x, ...)                                                        \
    do                                                                                \
    {                                                                                 \
      if (!(x))                                                                       \
      {                                                                               \
        Assert::detail::AssertImpl(#x, __FILE__, __LINE__ __VA_OPT__(, __VA_ARGS__)); \
        Assert::detail::Abort();                                                      \
      }                                                                               \
    } while (0)
#else
  #define DEBUG_ASSERT(x, ...)               \
    do                                       \
    {                                        \
      (void)sizeof(x);                       \
      __VA_OPT__((void)sizeof(__VA_ARGS__)); \
    } while (0)
#endif

// Aborts in debug, throws PanicException in release.
#define ASSERT(x, ...)                                                              \
  do                                                                                \
  {                                                                                 \
    if (!(x))                                                                       \
    {                                                                               \
      Assert::detail::AssertImpl(#x, __FILE__, __LINE__ __VA_OPT__(, __VA_ARGS__)); \
      if constexpr (Assert::detail::debug)                                          \
        Assert::detail::Abort();                                                    \
      else                                                                          \
        Assert::detail::PanicImpl(__FILE__, __LINE__);                              \
    }                                                                               \
  } while (0)
