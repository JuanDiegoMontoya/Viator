#include "Logging.h"

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/fmt/chrono.h"

#include <shared_mutex>

#ifndef GAME_HEADLESS
  #include "Client/GUI/Console.h"
  #include <mutex>
#endif

namespace
{
#ifndef GAME_HEADLESS
  template<typename Mutex>
  class console_sink : public spdlog::sinks::base_sink<Mutex>
  {
  protected:
    void sink_it_(const spdlog::details::log_msg& msg) final
    {
      spdlog::memory_buf_t formatted;

      struct
      {
        float r, g, b;
      } color = {1, 1, 1};

      auto messageType = ConsoleMessageType{};

      switch (msg.level)
      {
      case spdlog::level::trace: messageType = ConsoleMessageType::LOG_TRACE; break;
      case spdlog::level::debug:
        color       = {0.2f, 0.6f, 0.9f};
        messageType = ConsoleMessageType::LOG_DEBUG;
        break;
      case spdlog::level::info:
        color       = {0.05f, 0.7f, 0.05f};
        messageType = ConsoleMessageType::LOG_INFO;
        break;
      case spdlog::level::warn:
        color       = {1.0f, 0.5f, 0};
        messageType = ConsoleMessageType::LOG_WARNING;
        break;
      case spdlog::level::err:
        color       = {1, 0, 0};
        messageType = ConsoleMessageType::LOG_ERROR;
        break;
      case spdlog::level::critical: messageType = ConsoleMessageType::LOG_CRITICAL; break;
      }


      spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
      Console::Get()->LogColor(messageType, color.r, color.g, color.b, fmt::to_string(formatted).c_str());
    }

    void flush_() final {}
  };

  using console_sink_mt = console_sink<std::mutex>;
  using console_sink_st = console_sink<spdlog::details::null_mutex>;

  std::shared_ptr<console_sink_mt> gameConsoleSink;
#endif
  std::shared_ptr<spdlog::sinks::basic_file_sink_mt> fileSink;
  std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> stdoutSink;
  std::shared_ptr<spdlog::logger> defaultLogger;
} // namespace

namespace Core::Logging
{
  void Initialize()
  {
    auto time = std::chrono::system_clock::time_point(std::chrono::system_clock::now());
    auto filename = fmt::format("logs/Log {:%Y-%m-%d %H-%M-%S}.txt", time);

    fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true);
    fileSink->set_level(spdlog::level::trace);
    stdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdoutSink->set_level(spdlog::level::info);
#ifndef GAME_HEADLESS
    gameConsoleSink = std::make_shared<console_sink_mt>();
    gameConsoleSink->set_level(spdlog::level::trace);
#endif

    spdlog::init_thread_pool(8192, 1);
#ifndef GAME_HEADLESS
    auto sinks = spdlog::sinks_init_list{fileSink, stdoutSink, gameConsoleSink};
#else
    auto sinks = spdlog::sinks_init_list{fileSink, stdoutSink};
#endif
    //defaultLogger = std::make_shared<spdlog::async_logger>("default", sinks, spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    defaultLogger = std::make_shared<spdlog::logger>("default", sinks);
    defaultLogger->flush_on(spdlog::level::err);
    defaultLogger->set_pattern("[%H:%M:%S:%e] [%^%l%$] %v");
    defaultLogger->set_level(spdlog::level::trace);
    
    spdlog::set_default_logger(defaultLogger);
  }
}
