#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Globals.h"
#include "Game/Voxel/grid.h"
#include "Game/World/SurfaceBiome.h"
#include "Game/World/UndergroundBiome.h"
#include "Game/World/WorldGenHelpers.h"
#include "Core/Assert2.h"
#include "Prefab.h"
#include "Core/Image.h"

#include "FastNoise/FastNoise.h"
#include "tracy/Tracy.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/component_wise.hpp"

#ifndef GAME_HEADLESS
#include "stb_image_write.h"
#endif

#include <execution>

namespace
{
  void ForEachPositionInTLBrick(glm::ivec3 topLevelBrickPos, const auto& function)
  {
    for (int c = 0; c < Voxel::Grid::TL_BRICK_SIDE_LENGTH; c++)
    {
      for (int b = 0; b < Voxel::Grid::TL_BRICK_SIDE_LENGTH; b++)
      {
        for (int a = 0; a < Voxel::Grid::TL_BRICK_SIDE_LENGTH; a++)
        {
          const auto bl = glm::ivec3{a, b, c};

          // Voxels
          for (int z = 0; z < Voxel::Grid::BL_BRICK_SIDE_LENGTH; z++)
          {
            for (int y = 0; y < Voxel::Grid::BL_BRICK_SIDE_LENGTH; y++)
            {
              for (int x = 0; x < Voxel::Grid::BL_BRICK_SIDE_LENGTH; x++)
              {
                const auto positionBLS = glm::ivec3{x, y, z};
                const auto positionWS  = topLevelBrickPos * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE + bl * Voxel::Grid::BL_BRICK_SIDE_LENGTH + positionBLS;
                function(positionWS);
              }
            }
          }
        }
      }
    }
  }
} // namespace

void World::GenerateMap(const MapGenInfo& mapGenInfo)
{
  ZoneScoped;
#ifndef GAME_HEADLESS
  auto& progressText = globals->progressText;
  auto& progress     = globals->progress;
  auto& total        = globals->total;
#endif
  auto& blocks            = globals->blockRegistry;
  const auto& placeholder = blocks->Get("placeholder");
  const auto& dirt        = blocks->Get("dirt");
  [[maybe_unused]] const auto& malachite = blocks->Get("malachite");
  [[maybe_unused]] const auto& galena    = blocks->Get("galena");
  [[maybe_unused]] const auto& water8    = blocks->Get("water_8");

  constexpr auto samplesPerAxis = 64;
  constexpr auto sampleScale    = (float)samplesPerAxis / Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;

  auto& grid = globals->grid;

  auto tlBrickColCoords = std::vector<glm::ivec2>();
  for (int k = 0; k < grid->topLevelBricksDims_.z; k++)
  {
    for (int i = 0; i < grid->topLevelBricksDims_.x; i++)
    {
      tlBrickColCoords.emplace_back(k, i);
    }
  }

  auto globalSurfaceHeightImage = Core::DSP::Image<2, float>({grid->Dimensions().x, grid->Dimensions().z});
  auto globalSurfaceBiomeImage  = Core::DSP::Image<2, SurfaceBiome>({grid->Dimensions().x, grid->Dimensions().z});

  auto whiteNoise = FastNoise::New<FastNoise::White>();
  whiteNoise->SetOutputMin(0);
  auto whiteNoise2 = FastNoise::New<FastNoise::White>();

  auto shrimplex = FastNoise::New<FastNoise::Simplex>();
  shrimplex->SetScale(25);
  shrimplex->SetOutputMin(0);

  auto shrimplex2 = FastNoise::New<FastNoise::Simplex>();
  shrimplex2->SetScale(8);
  shrimplex2->SetOutputMin(0);

  *globals->surfaceBiomes   = GetSurfaceBiomeNoises(*this);
  const auto& surfaceBiomes = *globals->surfaceBiomes;

  {
    ZoneScopedN("Surface");

    auto stoneInDirtA = FastNoise::NewFromEncodedNodeTree("GgUL@BIEEEAACAPwg@CDAM@AD/AwY@BgQQTNzEy+C@AoED//w==");
    auto stoneInDirt  = FastNoise::New<FastNoise::DomainScale>();
    stoneInDirt->SetSource(stoneInDirtA);
    stoneInDirt->SetScaling(1.0f / sampleScale);

    auto copperOre = FastNoise::NewFromEncodedNodeTree("FgLNzIw/BxUFBg@AFRBCK5HoT//Aws@BIQQgzM7M/D@CD///8=");
    auto leadOre = FastNoise::NewFromEncodedNodeTree("FgIAAIA/BxUFBg@BhBCK5HoT//AxAFCw@AFxBCJqZmT8M@CP8CAACAQf///w==");

#ifndef GAME_HEADLESS
    total->store((int32_t)grid->numTopLevelBricks_);
    progressText->store("Surface");
#endif

    // Column of top level bricks
    std::for_each(std::execution::par,
      tlBrickColCoords.begin(),
      tlBrickColCoords.end(),
      [&](glm::ivec2 tlBrickColCoord)
      {
        ZoneScopedN("Top level brick column");
        const int k = tlBrickColCoord[0];
        const int i = tlBrickColCoord[1];

        auto biomeHeights = std::array<Core::DSP::Image<2, float>, int(SurfaceBiome::COUNT)>();

        for (int j = 0; j < int(SurfaceBiome::COUNT); j++)
        {
          if (surfaceBiomes[j] && surfaceBiomes[j]->BroadPhase({i, k}))
          {
            biomeHeights[j] = surfaceBiomes[j]->GenImageForChunk({i, k}, mapGenInfo);
          }
        }

        for (int y = 0; y < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; y++)
        for (int x = 0; x < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; x++)
        {
          const auto pModTl = glm::ivec2(x, y);
          const auto positionWS = pModTl + glm::ivec2(i, k) * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;

          auto sumWeights = 0.0f;
          auto sumHeights = 0.0f;

          const auto biome = GetSurfaceBiomeAtPosition(surfaceBiomes,
            positionWS,
            [&](SurfaceBiome inBiome, float weight)
            {
              sumWeights += weight;
              if (biomeHeights[int(inBiome)].Data())
              {
                sumHeights += weight * biomeHeights[int(inBiome)].Load(positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE);
              }
            });

          const auto height = glm::floor(mapGenInfo.seaLevel + sumHeights / sumWeights);
          globalSurfaceHeightImage.Store({positionWS.x, positionWS.y}, height);
          globalSurfaceBiomeImage.Store({positionWS.x, positionWS.y}, biome);
        }
      });
  }

  if (true)
  {
    ZoneScopedN("Generate rivers");
#ifndef GAME_HEADLESS
    progressText->store("Generate rivers");
    progress->store(0);
#endif

    const auto kernelXGauss = Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::X, 21, 5);
    const auto kernelYGauss = Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::Y, 21, 5);

    const auto kernelXGauss2 = Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::X, 41, 7);
    const auto kernelYGauss2 = Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::X, 41, 7);

    const auto blur1 = globalSurfaceHeightImage.Convolve(kernelXGauss2);
    const auto blur2 = blur1.Convolve(kernelYGauss2);

    FastNoise::SmartNode<> riverWeight = FastNoise::NewFromEncodedNodeTree("GQUGAADAFUP//w==");

    const auto riverMask0 = WorldGen::GenerateAndUpscale2D(riverWeight, glm::ivec2(0, 0), mapGenInfo.seed + 123456, grid->Dimensions().x, grid->Dimensions().z, Core::DSP::Filter::Nearest);

    const auto riverMask1 = riverMask0.Map([](float v) { return glm::smoothstep(0.8f, 1.0f, 1 - v); });
    const auto riverMask2 = riverMask1.Convolve(kernelXGauss);
    const auto riverMask3 = riverMask2.Convolve(kernelYGauss);

    auto erodedTerrain = globalSurfaceHeightImage.Map(
      [&](glm::ivec2 pos, float originalHeight)
      {
        const auto blurredHeight = blur2.Load(pos);
        const auto riverness     = riverMask3.Load(pos);
        if (riverness > 0.2f)
        {
          globalSurfaceBiomeImage.Store(pos, SurfaceBiome::Rivers);
        }
        return (float)(int)(glm::mix(originalHeight, blurredHeight - 16, riverness) + 0.5f);
      });

    globalSurfaceHeightImage = std::move(erodedTerrain);

    auto waterMutex = std::mutex();

    std::for_each(std::execution::par,
      tlBrickColCoords.begin(),
      tlBrickColCoords.end(),
      [&](glm::ivec2 tlBrickColCoord)
      {
        ZoneScopedN("Top level brick column");
        const int k = tlBrickColCoord[0];
        const int i = tlBrickColCoord[1];

        // Top level bricks
        for (int j = 0; j < grid->topLevelBricksDims_.y; j++) // Y last so we can compute heightmap once
        {
          ZoneScopedN("Top level brick");
          const auto tl = glm::ivec3{i, j, k};

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto height     = globalSurfaceHeightImage.Load({positionWS.x, positionWS.z});
              const auto biome      = globalSurfaceBiomeImage.Load({positionWS.x, positionWS.z});
              const auto& biomeInfo = surfaceBiomes[int(biome)];

              auto blockTypeToSet = voxel_t::Air;
              if (positionWS.y < height)
              {
                // 0 at sea level. 1 at cavern level.
                // const auto alphaCaverns = glm::clamp((mapGenInfo.seaLevel - positionWS.y) / float(mapGenInfo.surfaceThickness), 0.0f, 1.0f);

                if (positionWS.y <= height && positionWS.y >= height - biomeInfo->GetSurfaceThickness())
                {
                  blockTypeToSet = biomeInfo->GetSurfaceBlockType();
                }
                else if (positionWS.y < height && positionWS.y >= height - biomeInfo->GetSubsurfaceThickness())
                {
                  blockTypeToSet = biomeInfo->GetSubsurfaceBlockType();
                }
                else
                {
                  blockTypeToSet = placeholder;
                }
              }
              else if (biome == SurfaceBiome::Rivers && positionWS.y < blur2.Load({positionWS.x, positionWS.z}) - 4)
              {
                blockTypeToSet = water8;
                if (positionWS.y == (int)blur2.Load({positionWS.x, positionWS.z}) - 4)
                {
                  // Chance to excite block.
                  auto lk = std::lock_guard(waterMutex);
                  if (Rng().RandFloat() > 0.75f)
                  {
                    Block::QueueUpdateNeighbors(*this, positionWS);
                  }
                }
              }

              if (blockTypeToSet != voxel_t::Air)
              {
                grid->SetVoxelAtUncheckedNoDirty(positionWS, blockTypeToSet);
              }
            });

          grid->MarkTopLevelBrickAndChildrenDirty(tl);
          grid->CoalesceTopLevelBrickAndChildren(grid->GetTopLevelBrickPointerFromTopLevelPosition(tl));
#ifndef GAME_HEADLESS
          progress->fetch_add(1);
#endif
        }
      });

    if (mapGenInfo.settleLiquids)
    {
      ZoneScopedN("Settle liquids");
      constexpr int numIterations = 150;
#ifndef GAME_HEADLESS
      progressText->store("Settle liquids");
      progress->store(0);
      total->store(numIterations);
#endif
      for (int i = 0; i < numIterations; i++)
      {
        this->ProcessBlockTickQueue();
#ifndef GAME_HEADLESS
        progress->fetch_add(1);
#endif
      }
    }
  }

  *globals->globalFog = Core::DSP::Image<3, float>(grid->Dimensions() / 4);
  globals->globalFog->Fill(0);

  if (mapGenInfo.generateCaves)
  {
    ZoneScopedN("Underground Biomes");
#ifndef GAME_HEADLESS
    progressText->store("Caves");
    progress->store(0);
#endif

    *globals->undergroundBiomes = GetUndergroundBiomeNoises(*this);
    const auto& undergroundBiomes = *globals->undergroundBiomes;

    std::for_each(std::execution::par,
      tlBrickColCoords.begin(),
      tlBrickColCoords.end(),
      [&](glm::ivec2 tlBrickColCoord)
      {
        ZoneScopedN("Top level brick column");
        const int k = tlBrickColCoord[0];
        const int i = tlBrickColCoord[1];

        // Top level bricks
        for (int j = 0; j < grid->topLevelBricksDims_.y; j++)
        {
          ZoneScopedN("Top level brick");

          auto biomeDensities = std::array<Core::DSP::Image<3, float>, int(UndergroundBiome::COUNT)>();

          for (int m = 0; m < int(UndergroundBiome::COUNT); m++)
          {
            if (undergroundBiomes[m] != nullptr && undergroundBiomes[m]->BroadPhase({i, j, k}))
            {
              biomeDensities[m] = undergroundBiomes[m]->GenImageForChunk({i, j, k}, grid->topLevelBricksDims_, mapGenInfo);
            }
          }

          const auto tl = glm::ivec3{i, j, k};
          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              auto sumDensities = 0.0f;
              auto sumWeights   = 0.0f;

              const auto biome = GetUndergroundBiomeAtPosition(undergroundBiomes,
                positionWS,
                [&](UndergroundBiome inBiome, float weight)
                {
                  sumWeights += weight;
                  if (biomeDensities[int(inBiome)].Data())
                  {
                    sumDensities += weight * biomeDensities[int(inBiome)].Load(positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE);
                  }
                });

              const auto density = sumDensities / sumWeights;

              const auto ratio = grid->Dimensions() / globals->globalFog->Size();
              const auto surfaceHeight = globalSurfaceHeightImage.Load({positionWS.x, positionWS.z});
              if (positionWS % ratio == glm::ivec3(0) && positionWS.y < surfaceHeight && biome == UndergroundBiome::Corruption)
              {
                globals->globalFog->Store(positionWS / ratio, .25f * glm::smoothstep(10.0f, 70.0f, abs(positionWS.y - surfaceHeight)));
              }

              {
                const auto blockAtPos = grid->GetVoxelAtUnchecked(positionWS);
                const auto biomeAtPos = globalSurfaceBiomeImage.Load({positionWS.x, positionWS.z});
                if (density >= 0.0f && 
                  //grid->GetVoxelAt(positionWS + Block::DirectionToNeighbor(Block::Direction::Up)) != water8
                    !(blockAtPos == water8 || (biomeAtPos == SurfaceBiome::Rivers && blockAtPos == surfaceBiomes[int(biomeAtPos)]->GetSurfaceBlockType()))
                  )
                {
                  grid->SetVoxelAtUncheckedNoDirty(positionWS, voxel_t::Air);
                }
                else if (blockAtPos == placeholder)
                {
                  grid->SetVoxelAtUncheckedNoDirty(positionWS, undergroundBiomes[int(biome)]->GetSubstrateBlockType());
                }
              }
            });

          grid->MarkTopLevelBrickAndChildrenDirty(tl);
          grid->CoalesceTopLevelBrickAndChildren(grid->GetTopLevelBrickPointerFromTopLevelPosition(tl));
#ifndef GAME_HEADLESS
          progress->fetch_add(1);
#endif
        }
      });
  }

  #if 1
  if (true)
  {
#ifndef GAME_HEADLESS
    progressText->store("Surface foliage");
    total->store(grid->dimensions_.x * grid->dimensions_.z);
    progress->store(0);
#endif
    for (int z = 0; z < grid->dimensions_.z; z++)
    for (int x = 0; x < grid->dimensions_.x; x++)
    {
      const auto biome = globalSurfaceBiomeImage.Load({x, z});
      const auto y = (int)globalSurfaceHeightImage.Load({x, z});
      surfaceBiomes[int(biome)]->PlaceSurfaceFeatures(*this, mapGenInfo, {x, y, z});

#ifndef GAME_HEADLESS
      progress->fetch_add(1);
#endif
    }
  }
  #endif
#ifndef GAME_HEADLESS
  progressText->store("Vines");
  total->store(grid->topLevelBricksDims_.x * grid->topLevelBricksDims_.y * grid->topLevelBricksDims_.z);
  progress->store(0);
#endif

  struct PrefabAndPosition
  {
    const PrefabDefinition* prefab;
    glm::ivec3 positionWS;
  };

  auto prefabs = std::vector<PrefabAndPosition>();
  prefabs.reserve(100'000);
  auto mutex = std::mutex();
  
  {
    ZoneScopedN("Generate vine positions");
    std::for_each(std::execution::par,
      tlBrickColCoords.begin(),
      tlBrickColCoords.end(),
      [&](glm::ivec2 tlBrickColCoord)
      {
        ZoneScopedN("Top level brick column");
        const int k = tlBrickColCoord[0];
        const int i = tlBrickColCoord[1];

        for (int j = 0; j < grid->topLevelBricksDims_.y; j++)
        {
          ZoneScopedN("Top level brick");

          const auto tl = glm::ivec3{i, j, k};

          auto simplexImage = WorldGen::GenerateAndUpscale3D(shrimplex2,
            tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 15,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Core::DSP::Filter::Nearest);

          auto whiteImage = WorldGen::GenerateAndUpscale3D(whiteNoise2,
            tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 16,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Core::DSP::Filter::Nearest);

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto tlLocal = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
              if (grid->GetVoxelAtUnchecked(positionWS) == voxel_t::Air)
              {
                const auto aboveWS = positionWS + glm::ivec3(0, 1, 0);
                const auto belowWS = positionWS + glm::ivec3(0, -1, 0);
                if (aboveWS.y < grid->dimensions_.y - 1)
                {
                  const auto aboveBlock = grid->GetVoxelAtUnchecked(aboveWS);
                  if (aboveBlock != voxel_t::Air)
                  {
                    if (simplexImage.Load(tlLocal) + whiteImage.Load(tlLocal) * 0.3f < 0.05f)
                    {
                      auto lk = std::unique_lock(mutex);
                      if (aboveBlock == dirt)
                      {
                        prefabs.emplace_back(&globals->prefabRegistry->Get("Root"), positionWS);
                      }
                      else
                      {
                        prefabs.emplace_back(&globals->prefabRegistry->Get("Vine"), positionWS);
                      }
                    }
                  }
                }

                if (belowWS.y > 0)
                {
                  const auto belowBlock = grid->GetVoxelAtUnchecked(belowWS);
                  if (belowBlock == dirt && whiteImage.Load(tlLocal) > 0.98f)
                  {
                    grid->SetVoxelAtNoDirty(positionWS, blocks->Get("pot"));
                  }
                }
              }
            });

#ifndef GAME_HEADLESS
          progress->fetch_add(1);
#endif
          grid->MarkTopLevelBrickAndChildrenDirty(tl);
          grid->CoalesceTopLevelBrickAndChildren(grid->GetTopLevelBrickPointerFromTopLevelPosition(tl));
        }
      });
  }

  {
#ifndef GAME_HEADLESS
    progressText->store("Instantiate prefabs");
    total->store(int(prefabs.size()));
    progress->store(0);
#endif

    ZoneScopedN("Instantiate Prefabs");
    for (const auto& [prefab, positionWS] : prefabs)
    {
      prefab->Instantiate(*this, positionWS);
#ifndef GAME_HEADLESS
      progress->fetch_add(1);
#endif
    }
  }

  if (mapGenInfo.spawnYggdrasil)
  {
    ZoneScopedN("Big Tree");
    auto bigTreeNoise = FastNoise::NewFromEncodedNodeTree(
      "FgMVBRoFGwUVBRcFBQMs@EFBgAAgAVDB@AoMAIAACgQP//BwQEAACAP/8LLAAC@BBSUF/wAA////AygJAABvEgM8/wAApptEPP8DLAAC@BBSw@EUlAAI@BFBgQ@C/////wY@C//8CAACAv/8CAACAv/8CAACAP/8DFwUbBQQEzcxMvQYAACDC//8DFgMbBQQEzczMvP8DFQUXBRsFGgUVBQQEj8L1PP8DJQAD@BBSwFBg@APZBB@DI@BP/////8CAACAP///AxUFGQU@CgL///wIK1yM8//8DFQUXBQUHBAQAAIA///8CbxKDOv8D/xsA////Bw8FE@BMZCBQY@ADJQv8CmpnKQv//////BxYCAACAPwcaBRsFHAUdBRUFE@AgDBDBSUABg@BUGAACAg0L//wMXBRUFFwUFBnuUFkP/AlJJnTn/AvYokkL/AmZmpkD//wP/LgD/AgrXo7z/AgAAgD//AgrXo7z/AgAAgD////8=");

    auto& rng          = globals->game->rng;
    const auto fractXZ = glm::vec2(rng.RandFloat(0.4f, 0.6f), rng.RandFloat(0.4f, 0.6f));
    const auto posXZ   = glm::ivec2(fractXZ * glm::vec2(grid->dimensions_.x, grid->dimensions_.z) + 0.5f);
    const auto posY    = globalSurfaceHeightImage.Load(posXZ);
    const auto pos     = glm::ivec3(posXZ[0], posY, posXZ[1]);
    const auto bot     = glm::ivec3(glm::vec3(pos / Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE) + 0.5f);
    const auto top     = bot + 1 + glm::ivec3(0, 1, 0);

#ifndef GAME_HEADLESS
    progressText->store("Yggdrasil");
    const auto dif = top - bot + 1;
    total->store(dif.x * dif.y * dif.z);
    progress->store(0);
#endif

    for (int tz = bot.z; tz <= top.z; tz++)
    for (int ty = bot.y; ty <= top.y; ty++)
    for (int tx = bot.x; tx <= top.x; tx++)
    {
      const auto tl    = glm::ivec3(tx, ty, tz);
      const auto image = WorldGen::GenerateAndUpscale3D(bigTreeNoise,
        tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE - pos,
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::DSP::Filter::Nearest);

      ForEachPositionInTLBrick(tl,
        [&](glm::ivec3 positionWS)
        {
          const auto pModTl = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
          const auto density = image.Load(pModTl);
          // Low-altitude behavior
          if (positionWS.y < mapGenInfo.seaLevel + 80)
          {
            if (density <= 0)
            {
              grid->SetVoxelAt(positionWS, blocks->Get("wood"));
            }
          }
          else
          {
            if (density <= -0.032f)
            {
              grid->SetVoxelAt(positionWS, blocks->Get("wood"));
            }
            else if (density <= 0.0f)
            {
              grid->SetVoxelAt(positionWS, blocks->Get("leaves_01"));
            }
          }
        });

#ifndef GAME_HEADLESS
      progress->fetch_add(1);
#endif
    }
  }

  {
    ZoneScopedN("Ruins");
    constexpr int DUNGEON_CELL_SIZE = 16; // One attempt per cell.
#ifndef GAME_HEADLESS
    progressText->store("Ruins");
    total->store((grid->dimensions_.x / DUNGEON_CELL_SIZE) * (grid->dimensions_.y / DUNGEON_CELL_SIZE) * (grid->dimensions_.z / DUNGEON_CELL_SIZE));
    progress->store(0);
#endif

    auto rng = PCG::Rng(mapGenInfo.seed);
    for (int zt = 0; zt < grid->dimensions_.z / DUNGEON_CELL_SIZE; zt++)
    for (int yt = 0; yt < grid->dimensions_.y / DUNGEON_CELL_SIZE; yt++)
    for (int xt = 0; xt < grid->dimensions_.x / DUNGEON_CELL_SIZE; xt++)
    {
#ifndef GAME_HEADLESS
      progress->fetch_add(1);
#endif
      if (rng.RandFloat() < 0.15f)
      {
        const auto posCell = glm::ivec3(xt, yt, zt);

        // Spawn prefab somewhere within the cell.
        for (int attempt = 0; attempt < 10; attempt++)
        {
          const auto posSub = glm::ivec3(rng.RandU32() % DUNGEON_CELL_SIZE, rng.RandU32() % DUNGEON_CELL_SIZE, rng.RandU32() % DUNGEON_CELL_SIZE);
          const auto posWS  = posCell * DUNGEON_CELL_SIZE + posSub;

          const auto surfaceHeight = int(globalSurfaceHeightImage.Load({posWS.x, posWS.z}));
          if (posWS.y <= surfaceHeight - 8 && posWS.y >= mapGenInfo.seaLevel - mapGenInfo.surfaceThickness)
          {
            globals->prefabRegistry->Get("AbandonedHouse").Instantiate(*this, posWS);
            break;
          }
        }
      }
    }
  }
  
  {
    ZoneScopedN("Floating Islands");
    constexpr int ISLAND_CELL_SIZE = 64; // One attempt per cell.
#ifndef GAME_HEADLESS
    progressText->store("Floating islands");
    total->store((grid->dimensions_.x / ISLAND_CELL_SIZE) * (grid->dimensions_.y / ISLAND_CELL_SIZE) * (grid->dimensions_.z / ISLAND_CELL_SIZE));
    progress->store(0);
#endif

    auto rng = PCG::Rng(mapGenInfo.seed);
    for (int zt = 0; zt < grid->dimensions_.z / ISLAND_CELL_SIZE; zt++)
    for (int yt = 0; yt < grid->dimensions_.y / ISLAND_CELL_SIZE; yt++)
    for (int xt = 0; xt < grid->dimensions_.x / ISLAND_CELL_SIZE; xt++)
    {
#ifndef GAME_HEADLESS
      progress->fetch_add(1);
#endif
      if (rng.RandFloat() < 0.1f)
      {
        const auto posCell = glm::ivec3(xt, yt, zt);

        for (int attempt = 0; attempt < 10; attempt++)
        {
          // Spawn prefab somewhere within the cell.
          const auto posSub = glm::ivec3(rng.RandU32() % ISLAND_CELL_SIZE, rng.RandU32() % ISLAND_CELL_SIZE, rng.RandU32() % ISLAND_CELL_SIZE);
          const auto posWS  = posCell * ISLAND_CELL_SIZE + posSub;
          const auto posFraction = glm::vec3(posWS) / glm::vec3(grid->dimensions_);

          const auto surfaceHeight = int(globalSurfaceHeightImage.Load({posWS.x, posWS.z}));
          if (posWS.y >= surfaceHeight + 125 && posFraction.x < 0.9f && posFraction.y < 0.9f && posFraction.z < 0.9f)
          {
            globals->prefabRegistry->Get("FloatingIsland").Instantiate(*this, posWS);
            break;
          }
        }
      }
    }
  }

  grid->CoalesceDirtyBricks();

  const auto fogMask = globalSurfaceBiomeImage.Map<float>(
    [](SurfaceBiome biome)
    {
      if (biome == SurfaceBiome::Rivers)
      {
        return 1;
      }

      if (biome == SurfaceBiome::Corruption)
      {
        return 3;
      }

      return 0;
    });

  const auto fog2 = fogMask.Convolve(Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::X, 41, 7));
  auto fog3 = fog2.Convolve(Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::Y, 41, 7));

  const auto height2 = globalSurfaceHeightImage.Convolve(Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::X, 15, 5));
  auto height3 = height2.Convolve(Core::DSP::CreateSeparableGaussianKernel2D(Core::DSP::SeparableKernelDirection::Y, 15, 5));

  *globals->globalSurfaceHeight = std::move(height3);
  *globals->globalSurfaceFog = std::move(fog3);
  globals->waterQueue->clear();
}