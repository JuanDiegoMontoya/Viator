#include "Game/Game.h"
#include "Game/Assets.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "spdlog/spdlog.h"

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <span>
#include <string_view>
#include <vector>

int main(int argc, const char* const* argv)
{
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
        doctest::Context context;
        context.applyCommandLine(argc, argv);
        int res = context.run();
        if (context.shouldExit())
        {
          std::exit(res);
        }

        std::exit(res);
      }
      else if (!arg.starts_with("-"))
      {
        spdlog::info("Will load world: {}", arg);
        worldToLoad = GetDataDirectory() / "saves/worlds/" / arg;
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

  auto game = Game(30, worldToLoad, port);
  game.Run();

  return 0;
}
