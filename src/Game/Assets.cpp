#include "Assets.h"
#include <optional>

std::filesystem::path GetDataDirectory()
{
  static std::optional<std::filesystem::path> dataPath;
  if (!dataPath)
  {
    auto dir = std::filesystem::current_path();
    while (!dir.empty())
    {
      auto maybeData = dir / "data";
      if (exists(maybeData) && is_directory(maybeData))
      {
        dataPath = maybeData;
        break;
      }

      if (!dir.has_parent_path())
      {
        break;
      }

      dir = dir.parent_path();
    }
  }
  return dataPath.value(); // Will throw if data directory wasn't found.
}

std::filesystem::path GetAssetDirectory()
{
  // Assets directory is expected to be a submodule.
  return GetDataDirectory() / "assets";
}

std::filesystem::path GetShaderDirectory()
{
  return GetDataDirectory() / "shaders";
}

std::filesystem::path GetTextureDirectory()
{
  return GetAssetDirectory() / "textures";
}

std::filesystem::path GetConfigDirectory()
{
  return GetDataDirectory() / "config";
}