#include "Logging.h"

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/fmt/chrono.h"

#include <shared_mutex>

namespace Core::Logging
{
  static std::shared_ptr<spdlog::sinks::basic_file_sink_mt> fileSink;
  static std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> stdoutSink;
  static std::shared_ptr<spdlog::logger> defaultLogger;

  void Initialize()
  {
    auto time = std::chrono::system_clock::time_point(std::chrono::system_clock::now());
    auto filename = fmt::format("logs/Log {:%Y-%m-%d %H-%M-%S}.txt", time);

    fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true);
    fileSink->set_level(spdlog::level::debug);
    stdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdoutSink->set_level(spdlog::level::info);

    spdlog::init_thread_pool(8192, 1);
    auto sinks = spdlog::sinks_init_list{fileSink, stdoutSink};
    //defaultLogger = std::make_shared<spdlog::async_logger>("default", sinks, spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    defaultLogger = std::make_shared<spdlog::logger>("default", sinks);
    defaultLogger->flush_on(spdlog::level::err);
    defaultLogger->set_pattern("[%H:%M:%S:%e] [%^%l%$] %v");
    defaultLogger->set_level(spdlog::level::debug);
    
    spdlog::set_default_logger(defaultLogger);
  }
}
