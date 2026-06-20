#include "NpcDirector.h"
#include "Game/World.h"
#include "Game/Globals.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "Block.h"
#include "Voxel/Grid.h"
#include "glm/vec3.hpp"
#include "glm/gtx/component_wise.hpp"
#include "spdlog/spdlog.h"

#include <mdspan>
#include <optional>
#include <queue>
#include <ranges>
#include <vector>

#include "tracy/Tracy.hpp"

namespace
{
  enum class TraversalState
  {
    NotTraversed,
    NotSolid,
    Solid,
  };

  struct Prism
  {
    glm::ivec3 min;
    glm::ivec3 extent{};

    bool operator==(const Prism&) const = default;
  };

  struct Rect
  {
    glm::ivec2 min;
    glm::ivec2 extent{};

    bool operator==(const Rect&) const = default;
  };

  struct Line
  {
    int min;
    int extent{};

    bool operator==(const Line&) const = default;
  };

  [[nodiscard]] std::vector<Line> GetLinesInRow(std::mdspan<const TraversalState, std::dextents<std::size_t, 3>> region, int y, int z)
  {
    ZoneScoped;
    auto lines = std::vector<Line>();

    auto currentLine = std::optional<Line>();
    for (size_t x : std::ranges::views::iota(0ull, region.extent(0)))
    {
      if (region[x, y, z] != TraversalState::NotSolid)
      {
        if (currentLine.has_value())
        {
          lines.push_back(*currentLine);
          currentLine = std::nullopt;
        }
        continue;
      }

      if (!currentLine.has_value())
      {
        currentLine = Line{.min = (int)x};
      }

      currentLine->extent++;
    }

    if (currentLine.has_value())
    {
      lines.push_back(*currentLine);
    }

    return lines;
  }

  // Related to https://stackoverflow.com/questions/5931735/finding-largest-rectangle-in-2d-array
  // Given a slice of a 3D volume with obstructions, find all the maximal (not unnecessarily small) rects that fit in the unobstructed areas.
  // Each cell is visited once.
  // Note: returned vector may contain redundant, non-maximal rects which are subsets of other rects.
  [[nodiscard]] std::vector<Rect> GetRectsInSlab(std::mdspan<const TraversalState, std::dextents<std::size_t, 3>> region, int z)
  {
    ZoneScoped;
    auto rects = std::vector<Rect>();

    for (size_t y : std::ranges::views::iota(0ull, region.extent(1)))
    {
      const auto lines = GetLinesInRow(region, (int)y, z);
      auto rectsToAdd  = std::vector<Rect>();

      for (const auto& line : lines)
      {
        bool perfectlySpannedRect  = false;
        bool touchedRect           = false;
        bool spawnedLineExtentRect = false;
        for (auto& rect : rects)
        {
          // Perform an action for rects touched by lines:
          if (rect.min.y + rect.extent.y == (int)y && rect.min.x < line.min + line.extent && rect.min.x + rect.extent.x > line.min)
          {
            touchedRect = true;

            // If only part of a rect is touched by a line, spawn a new rect which is as thin as the part spanned by the line, and as tall as the rect plus one.
            if (rect.min.x < line.min || rect.min.x + rect.extent.x > line.min + line.extent)
            {
              const auto min = glm::max(rect.min.x, line.min);
              const auto max = glm::min(rect.min.x + rect.extent.x, line.min + line.extent);
              rectsToAdd.push_back({.min = {min, rect.min.y}, .extent = {max - min, rect.extent.y + 1}});
              if (max - min == line.extent)
              {
                spawnedLineExtentRect = true;
              }
              continue;
            }

            // Otherwise the line covers at least the entire side of the rect. Increase the rect's height by one.
            rect.extent.y++;

            // If any rect is perfectly spanned by the line, we don't need to make a new rect from the line.
            if (rect.min.x == line.min && rect.extent.x == line.extent)
            {
              perfectlySpannedRect = true;
            }
          }
        }

        if ((!touchedRect || !perfectlySpannedRect) && !spawnedLineExtentRect)
        {
          rectsToAdd.push_back({.min = {line.min, y}, .extent = {line.extent, 1}});
        }
      }

      rects.append_range(rectsToAdd);
    }
    
    return rects;
  }

  [[nodiscard]] std::vector<Prism> GetPrismsInVolume(std::mdspan<const TraversalState, std::dextents<std::size_t, 3>> region)
  {
    ZoneScoped;
    auto prisms = std::vector<Prism>();

    for (size_t z : std::ranges::views::iota(0ull, region.extent(2)))
    {
      const auto rects = GetRectsInSlab(region, (int)z);
      auto prismsToAdd  = std::vector<Prism>();

      for (const auto& rect : rects)
      {
        bool perfectlySpannedPrism  = false;
        bool touchedPrism           = false;
        bool spawnedRectExtentPrism = false;
        for (auto& prism : prisms)
        {
          // Perform an action for prisms touched by rects:
          if (prism.min.z + prism.extent.z == (int)z && 
            all(lessThan(glm::ivec2(prism.min), rect.min + rect.extent)) && 
            all(greaterThan(glm::ivec2(prism.min + prism.extent), rect.min)))
          {
            touchedPrism = true;

            // If only part of a prism is touched by a rect, spawn a new prism which is as thin as the part spanned by the rect, and as deep as the prism plus one.
            // TODO: This check is a bit too permissive and will sometimes spawn unnecessary prisms (see three orthogonal slabs test case). I can't think of an efficient way to avoid it though.
            if (any(lessThan(glm::ivec2(prism.min), rect.min)) || 
              any(greaterThan(glm::ivec2(prism.min + prism.extent), rect.min + rect.extent)))
            {
              const auto min = glm::max(glm::ivec2(prism.min), rect.min);
              const auto max = glm::min(glm::ivec2(prism.min + prism.extent), rect.min + rect.extent);
              prismsToAdd.push_back({.min = {min, prism.min.z}, .extent = {max - min, prism.extent.z + 1}});
              if (max - min == rect.extent)
              {
                spawnedRectExtentPrism = true;
              }
              continue;
            }

            // Otherwise the rect covers at least the entire side of the prism. Increase the prism's depth by one.
            prism.extent.z++;

            // If any prism is perfectly spanned by the line, we don't need to make a new prism from the rect.
            if (glm::ivec2(prism.min) == rect.min && glm::ivec2(prism.extent) == rect.extent)
            {
              perfectlySpannedPrism = true;
            }
          }
        }

        if ((!touchedPrism || !perfectlySpannedPrism) && !spawnedRectExtentPrism)
        {
          prismsToAdd.push_back({.min = {rect.min, z}, .extent = {rect.extent, 1}});
        }
      }

      prisms.append_range(prismsToAdd);
    }

    return prisms;
  }
}

void Game2::NpcDirector::Update([[maybe_unused]] float dt) {}

bool Game2::NpcDirector::CheckIsValidHousing(World& world, glm::ivec3 originalPos, const HousingParams& params)
{
  ZoneScoped;
  const auto& grid   = *world.globals->grid;
  [[maybe_unused]] const auto& blocks = world.globals->blockRegistry->GetRegistry();

  const int maxWidth  = params.maxWidth;
  const int maxHeight = params.maxHeight;

  const int minWidth  = params.minWidth;
  const int minHeight = params.minHeight;

  auto nodesTraversed1D = std::vector<TraversalState>(maxWidth * 2 * maxWidth * 2 * maxHeight * 2, TraversalState::NotTraversed);
  auto nodesTraversed   = std::mdspan(nodesTraversed1D.data(), maxWidth * 2, maxHeight * 2, maxWidth * 2);

  auto minPos = glm::ivec3(INT32_MAX);
  auto maxPos = glm::ivec3(INT32_MIN);

  // TODO: preallocate queue space (use std::pmr)
  auto nodesToTraverse = std::queue<glm::ivec3>();

  // Step 1: flood fill to discover our surroundings and early out if we go OOB (of either the map or the max house size).
  nodesToTraverse.push(originalPos);
  while (!nodesToTraverse.empty())
  {
    const auto nextPos = nodesToTraverse.front();
    nodesToTraverse.pop();

    if (!grid.IsPositionInGrid(nextPos))
    {
      spdlog::info("Flooded area reaches world edge.");
      return false;
    }

    const auto block   = grid.GetVoxelAtUnchecked(nextPos);
    const auto isSolid = Block::IsSolid(world, block);
    
    if (!isSolid && (
      glm::abs(originalPos.x - nextPos.x) >= maxWidth || 
      glm::abs(originalPos.z - nextPos.z) >= maxWidth ||
      glm::abs(originalPos.y - nextPos.y) >= maxHeight))
    {
      spdlog::info("Flooded area is too large.");
      return false;
    }

    const auto relPos = nextPos - originalPos + glm::ivec3(maxWidth, maxHeight, maxWidth);

    nodesTraversed[relPos.x, relPos.y, relPos.z] = isSolid ? TraversalState::Solid : TraversalState::NotSolid;

    minPos = glm::min(relPos, minPos);
    maxPos = glm::max(relPos, maxPos);

    if (!isSolid)
    {
      for (int i = 0; i < 6; i++)
      {
        const auto neighbor  = Block::DirectionToNeighbor(static_cast<Block::Direction>(i));
        const auto newRelPos = relPos + neighbor;
        // TODO: how are there no negative indices here?
        ASSERT(all(greaterThanEqual(newRelPos, glm::ivec3(0))));
        if (nodesTraversed[newRelPos.x, newRelPos.y, newRelPos.z] == TraversalState::NotTraversed)
        {
          nodesToTraverse.push(nextPos + neighbor);
        }
      }
    }
  }

  // Step 2: early out if flooded area is smaller than the minimum room size.
  if (const auto diff = maxPos - minPos; diff.x < minWidth || diff.y < minHeight || diff.z < minWidth)
  {
    spdlog::info("Flooded area too small. Extents: ({}, {}, {})", diff.x, diff.y, diff.z);
    return false;
  }

  // Step 3: find a sufficiently large prism that encompasses the point.
  const auto prisms = GetPrismsInVolume(nodesTraversed);
  for (const auto& prism : prisms)
  {
    if (prism.extent.x >= minWidth && prism.extent.x <= maxWidth &&
        prism.extent.y >= minHeight && prism.extent.y <= maxHeight &&
        prism.extent.z >= minWidth && prism.extent.z <= maxWidth)
    {
      const auto p = glm::ivec3(maxWidth, maxHeight, maxWidth);
      if (all(greaterThanEqual(p, prism.min)) && all(lessThan(p, prism.min + prism.extent)))
      {
        return true;
      }
    }
  }

  spdlog::info("Failed to find prism of sufficient size.");
  return false;
}


#include "doctest.h"

TEST_CASE("GetLinesInRow")
{
  constexpr int width = 10;

  auto region1D = std::vector<TraversalState>(width, TraversalState::NotTraversed);
  auto region   = std::mdspan(region1D.data(), width, 1, 1);

  SUBCASE("Maximum line")
  {
    std::ranges::fill(region1D, TraversalState::NotSolid);
    const auto lines = GetLinesInRow(region, 0, 0);
    REQUIRE_EQ(lines.size(), 1);
    CHECK_EQ(lines[0], Line{0, width});
  }

  SUBCASE("Tiny line")
  {
    std::ranges::fill(region1D, TraversalState::Solid);
    region[0, 0, 0]  = TraversalState::NotSolid;
    const auto lines = GetLinesInRow(region, 0, 0);
    REQUIRE_EQ(lines.size(), 1);
    CHECK_EQ(lines[0], Line{0, 1});
  }

  SUBCASE("No line")
  {
    std::ranges::fill(region1D, TraversalState::Solid);
    const auto lines = GetLinesInRow(region, 0, 0);
    CHECK(lines.empty());
  }

  SUBCASE("Multiple lines")
  {
    std::ranges::fill(region1D, TraversalState::NotSolid);
    region[1, 0, 0]  = TraversalState::Solid;
    region[4, 0, 0]  = TraversalState::Solid;
    region[8, 0, 0]  = TraversalState::Solid;
    const auto lines = GetLinesInRow(region, 0, 0);
    REQUIRE_EQ(lines.size(), 4);
    CHECK_EQ(lines[0], Line{0, 1});
    CHECK_EQ(lines[1], Line{2, 2});
    CHECK_EQ(lines[2], Line{5, 3});
    CHECK_EQ(lines[3], Line{9, 1});
  }
}

TEST_CASE("GetRectsInSlab")
{
  constexpr int width = 10;
  constexpr int height = 10;

  auto region1D = std::vector<TraversalState>(width * height, TraversalState::NotTraversed);
  auto region   = std::mdspan(region1D.data(), width, height, 1);

  SUBCASE("Maximum rect")
  {
    std::ranges::fill(region1D, TraversalState::NotSolid);
    const auto rects = GetRectsInSlab(region, 0);
    REQUIRE_EQ(rects.size(), 1);
    CHECK_EQ(rects[0], Rect{{0, 0}, {width, height}});
  }

  SUBCASE("Tiny rect")
  {
    std::ranges::fill(region1D, TraversalState::Solid);
    region[1, 1, 0]  = TraversalState::NotSolid;
    const auto rects = GetRectsInSlab(region, 0);
    REQUIRE_EQ(rects.size(), 1);
    CHECK_EQ(rects[0], Rect{{1, 1}, {1, 1}});
  }

  SUBCASE("No rect")
  {
    std::ranges::fill(region1D, TraversalState::Solid);
    const auto rects = GetRectsInSlab(region, 0);
    CHECK(rects.empty());
  }

  SUBCASE("One big obstacle that leaves a row and a column")
  {
    std::ranges::fill(region1D, TraversalState::Solid);
    for (int i = 0; i < width; i++)
    {
      region[i, 0, 0] = TraversalState::NotSolid;
    }
    for (int i = 0; i < height; i++)
    {
      region[0, i, 0] = TraversalState::NotSolid;
    }
    const auto rects = GetRectsInSlab(region, 0);
    REQUIRE_EQ(rects.size(), 2);
    CHECK_EQ(rects[0], Rect{{0, 0}, {width, 1}});
    CHECK_EQ(rects[1], Rect{{0, 0}, {1, height}});
  }

  SUBCASE("One small obstacle at (1, 1)")
  {
    std::ranges::fill(region1D, TraversalState::NotSolid);
    region[1, 1, 0]  = TraversalState::Solid;
    const auto rects = GetRectsInSlab(region, 0);
    REQUIRE_EQ(rects.size(), 4);
  }

  SUBCASE("The Stackoverflow question")
  {
    // https://stackoverflow.com/questions/5931735/finding-largest-rectangle-in-2d-array
    /*
     * ----------------------
     * |    ------    ------|
     * |    ----        ----|
     * |                    |
     * |                --  |
     * |                --  |
     * |--    --            |
     * ----------------------
     */
    std::ranges::fill(region1D, TraversalState::NotTraversed);
    for (int y = 0; y < 6; y++)
    for (int x = 0; x < 10; x++)
    {
      region[x, y, 0] = TraversalState::NotSolid;
    }

    region[0, 0, 0] = TraversalState::Solid;
    region[3, 0, 0] = TraversalState::Solid;

    region[8, 1, 0] = TraversalState::Solid;
    region[8, 2, 0] = TraversalState::Solid;

    region[2, 4, 0] = TraversalState::Solid;
    region[3, 4, 0] = TraversalState::Solid;
    region[8, 4, 0] = TraversalState::Solid;
    region[9, 4, 0] = TraversalState::Solid;

    region[2, 5, 0] = TraversalState::Solid;
    region[3, 5, 0] = TraversalState::Solid;
    region[4, 5, 0] = TraversalState::Solid;
    region[7, 5, 0] = TraversalState::Solid;
    region[8, 5, 0] = TraversalState::Solid;
    region[9, 5, 0] = TraversalState::Solid;

    const auto rects = GetRectsInSlab(region, 0);
    REQUIRE_EQ(rects.size(), 14);
    CHECK(std::ranges::contains(rects, Rect{{0, 1}, {8, 3}})); // The largest rect in the region.
  }
}

TEST_CASE("GetPrismsInVolume")
{
  constexpr int width  = 5;
  constexpr int height = 5;
  constexpr int depth  = 5;

  auto region1D = std::vector<TraversalState>(width * height * depth, TraversalState::NotTraversed);
  auto region   = std::mdspan(region1D.data(), width, height, depth);

  SUBCASE("Maximum volume")
  {
    std::ranges::fill(region1D, TraversalState::NotSolid);
    const auto prisms = GetPrismsInVolume(region);
    REQUIRE_EQ(prisms.size(), 1);
    CHECK_EQ(prisms[0], Prism{{0, 0, 0}, {width, height, depth}});
  }

  SUBCASE("No volume")
  {
    std::ranges::fill(region1D, TraversalState::Solid);
    const auto prisms = GetPrismsInVolume(region);
    CHECK_EQ(prisms.size(), 0);
  }

  SUBCASE("One big obstacle that leaves three orthogonal slices")
  {
    std::ranges::fill(region1D, TraversalState::Solid);
    for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
    {
      region[x, y, 0] = TraversalState::NotSolid;
    }

    for (int z = 0; z < depth; z++)
    for (int x = 0; x < width; x++)
    {
      region[x, 0, z] = TraversalState::NotSolid;
    }

    for (int z = 0; z < height; z++)
    for (int y = 0; y < height; y++)
    {
      region[0, y, z] = TraversalState::NotSolid;
    }

    const auto prisms = GetPrismsInVolume(region);
    REQUIRE_GE(prisms.size(), 3);
    CHECK(std::ranges::contains(prisms, Prism{{0, 0, 0}, {width, height, 1}}));
    CHECK(std::ranges::contains(prisms, Prism{{0, 0, 0}, {width, 1, depth}}));
    CHECK(std::ranges::contains(prisms, Prism{{0, 0, 0}, {1, height, depth}}));
  }
}