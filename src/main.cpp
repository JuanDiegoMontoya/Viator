#include "Game/Game.h"
#include "Game/Assets.h"
#include "Core/Logging.h"
#include "Game/Physics/Physics.h"
#include "Core/Reflection.h"
#include "Game/Scripting.h"
#include "Core/Serialization.h"
#include "Core/Platform/PlatformInit.h"
#include "Game/CVar.h"
#ifdef GAME_HEADLESS
  #include "Game/Head.h"
#else
  #include "Client/PlayerHead.h"
#endif

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "spdlog/spdlog.h"

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <span>
#include <string_view>
#include <vector>

#ifdef TRACY_ENABLE
#include "tracy/Tracy.hpp"
#include <cstdlib>
void* operator new(std::size_t count)
{
  auto ptr = std::malloc(count);
  TracyAlloc(ptr, count);
  return ptr;
}

void operator delete(void* ptr) noexcept
{
  TracyFree(ptr);
  std::free(ptr);
}

void* operator new[](std::size_t count)
{
  auto ptr = std::malloc(count);
  TracyAlloc(ptr, count);
  return ptr;
}

void operator delete[](void* ptr) noexcept
{
  TracyFree(ptr);
  std::free(ptr);
}

void* operator new(std::size_t count, const std::nothrow_t&) noexcept
{
  auto ptr = std::malloc(count);
  TracyAlloc(ptr, count);
  return ptr;
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
  TracyFree(ptr);
  std::free(ptr);
}

void* operator new[](std::size_t count, const std::nothrow_t&) noexcept
{
  auto ptr = std::malloc(count);
  TracyAlloc(ptr, count);
  return ptr;
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
  TracyFree(ptr);
  std::free(ptr);
}
#endif

int main(int argc, const char* const* argv)
{
#ifdef TRACY_ENABLE
  TracySetProgramName("Viator");
#endif
  Core::Platform::Init();
  Core::Logging::Initialize();
  auto scripting = Scripting();
  Core::Reflection::Initialize(scripting);
  Core::Serialization::Initialize();
  Physics::Initialize();
  auto head = std::unique_ptr<Head>();

  auto worldToLoad = std::optional<std::filesystem::path>();
  auto port        = std::optional<uint16_t>();

#ifdef GAME_HEADLESS
  port = 1234;
#endif

  const auto ParseOptions = [&](std::span<std::string_view> args) -> void
  {
    using namespace std::literals;
    for (auto arg : args)
    {
      if (arg == "--test")
      {
        spdlog::info("Executing tests.");
        doctest::Context context;
        context.setOption("dt-exit", true);
        context.applyCommandLine(argc, argv);

        const auto lastLevel = spdlog::get_level();
        spdlog::set_level(spdlog::level::warn);
        int res = context.run();
        spdlog::set_level(lastLevel);

        spdlog::info("Tests complete.{}", context.shouldExit() ? " Exiting. (Continue with --dt-exit=false)" : "");
        if (context.shouldExit())
        {
          std::exit(res);
        }
      }
      else if (!arg.starts_with("-"))
      {
        spdlog::info("Will load world: {}", arg);
        worldToLoad = GetDataDirectory() / "saves" / "worlds/" / arg;
      }
      else if (arg.starts_with("--port="))
      {
        port = std::stoi(std::string(arg.substr("--port="sv.size())));
        spdlog::info("Using port: {}", *port);
      }
      else if (!arg.starts_with("--dt-"))
      {
        spdlog::warn("Unknown argument: {}", arg);
      }
    }
  };

  auto args = std::vector<std::string_view>();
  for (int i = 1; i < argc; i++)
  {
    args.emplace_back(argv[i]);
  }
  ParseOptions(args);

#ifdef GAME_HEADLESS
  head = std::make_unique<NullHead>();
#else
  head = std::make_unique<PlayerHead>(PlayerHead::CreateInfo{
    .name     = "Gabagool",
    .maximize = false,
    .decorate = true,
  });
#endif

  auto params = GameParams{
    .scripting    = &scripting,
    .worldToLoad  = worldToLoad,
    .port         = port,
    .head         = head.get(),
  };
  auto game = Game(params);
  game.Run();

  Physics::Terminate();

  Game2::CVarSystem::Get()->SaveArchivableCVars();
  return 0;
}
