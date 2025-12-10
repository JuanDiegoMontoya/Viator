#include "SurfaceBiome.h"
#include "Core/Image.h"
#include "Game/Voxel/Grid.h"
#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Globals.h"
#include "Game/Prefab.h"
#include "PCG.h"
#include "WorldGenHelpers.h"

#include "FastNoise/FastNoise.h"
#include "tracy/Tracy.hpp"

class ForestBiomeNoise final : public SurfaceBiomeNoise
{
public:
  ForestBiomeNoise(const CreateInfo& createInfo, glm::ivec2 worldDimsTL) : SurfaceBiomeNoise(createInfo)
  {
    globalMeadowImage = Core::DSP::Image<2, float>({worldDimsTL.x * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, worldDimsTL.y * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE});
  }

  float GetWeight([[maybe_unused]] glm::ivec2 posWS) override
  {
    PANIC;
  }

  int GetSubsurfaceThickness() const override
  {
    return 20;
  }

  Core::DSP::Image<2, float> GenImageForChunk(glm::ivec2 posTL, const MapGenInfo& mapGenInfo) override
  {
    ZoneScoped;
    auto terrainHeightImage = WorldGen::GenerateAndUpscale2D(terrainHeight2D,
      glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
      mapGenInfo.seed,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Linear);

    auto meadowImage = WorldGen::GenerateAndUpscale2D(meadowNoise,
      glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
      mapGenInfo.seed * 21,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Nearest);

    for (int y = 0; y < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; y++)
    for (int x = 0; x < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; x++)
    {
      const auto pModTl     = glm::ivec2(x, y);
      const auto positionWS = pModTl + glm::ivec2(posTL.x, posTL.y) * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;

      const auto meadowness = meadowImage.Load(pModTl);

      const auto heightScale = glm::mix(15, 4, meadowness);
      const auto height      = glm::floor(heightScale * terrainHeightImage.Load(pModTl));
      terrainHeightImage.Store(pModTl, height);
      globalMeadowImage.Store({positionWS.x, positionWS.y}, meadowness);
    }

    return terrainHeightImage;
  }

  void PlaceSurfaceFeatures(World& world, const MapGenInfo& mapGenInfo, glm::ivec3 posWS) override
  {
    ZoneScoped;
    auto& grid   = world.globals->grid;
    auto& blocks = world.globals->blockRegistry;

    const auto x = posWS.x;
    const auto y = posWS.y;
    const auto z = posWS.z;

    auto shrimplex = FastNoise::New<FastNoise::Simplex>();
    shrimplex->SetScale(25);
    shrimplex->SetOutputMin(0);

    auto shrimplex2 = FastNoise::New<FastNoise::Simplex>();
    shrimplex2->SetScale(8);
    shrimplex2->SetOutputMin(0);

    auto whiteNoise = FastNoise::New<FastNoise::White>();
    whiteNoise->SetOutputMin(0);
    auto whiteNoise2 = FastNoise::New<FastNoise::White>();

    const auto meadowness = globalMeadowImage.Load({x, z});

    const bool hasSolidFloor = grid->GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air;
    const auto tree          = whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 4);
    if (hasSolidFloor && tree > glm::mix(0.99f, 0.998f, meadowness))
    {
      if (world.globals->game->rng.RandFloat() < 0.9f)
      {
        world.globals->prefabRegistry->Get("Tree").Instantiate(world, {x, y, z});
      }
      else
      {
        world.globals->prefabRegistry->Get("Tree2").Instantiate(world, {x, y, z});
      }
    }
    else
    {
      if (hasSolidFloor &&
          shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 5) + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 9) * 0.2f < 0.03f)
      {
        grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("bush_01"));
      }

      if (hasSolidFloor &&
          shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 31) * 0.7f + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 30) * 0.3f > 0.88f)
      {
        grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("bush_02"));
      }

      if (hasSolidFloor &&
          shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 16) * 0.7f + whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 17) * 0.3f > 0.93f)
      {
        grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("mushroom"));
      }
      else if (hasSolidFloor && meadowness * shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 24) * 0.7f +
                                    whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 25) * 0.3f >
                                  0.95f)
      {
        grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("rose"));
      }
      else if (hasSolidFloor && meadowness * shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 22) * 0.7f +
                                    whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 23) * 0.3f >
                                  0.93f)
      {
        grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("dandelion"));
      }
      else if (hasSolidFloor && whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 24) > 0.999f)
      {
        grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("rock_small"));
      }
      else // Because it's low priority, grass shouldn't override other foliage.
      {
        const auto grasss = meadowness * 0.1f + shrimplex2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 10) +
                            whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 11) * 0.3f;

        if (hasSolidFloor)
        {
          if (grasss > 0.6f)
          {
            grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("grass_short"));
          }
          if (grasss > 0.7f)
          {
            grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("grass_medium"));
          }
          if (grasss > 0.8f)
          {
            grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("grass_long"));
          }
          if (grasss > 0.9f)
          {
            world.globals->prefabRegistry->Get("Double Grass").Instantiate(world, {x, y, z});
          }
        }
      }
    }
  }

  int GetSurfaceThickness() const override
  {
    return 1;
  }

private:
  FastNoise::SmartNode<> terrainHeight2D = FastNoise::NewFromEncodedNodeTree(
    "FQUXBRgDFgMdBRYDDQUlAEM@BFBg@AG9CBM3MTD////8HHwMiBQsAAIDfQgQ@CC@AwD8YB@CcI@BsEEEj8J1Pv//AgBAGMMGAADsQv8GXI/CP///AwAAw/VoP///B/8IAP8HFgIAAIA/B/8IAP//Ag@AED/AxwFDQAE@BBQY@BWQ///Aw8AB@CUXBQg@ABIQ/8C@BwP////8=");
  FastNoise::SmartNode<> meadowNoise = FastNoise::NewFromEncodedNodeTree(
    "GgUbBRwFHQUXBRgDFgMdBRYCAACAPwcfAwsAAIDHQgQ@CGAQ@BHC@BKJBBB+F6z7//wbsUTg///8DAACamVk///8H/wQA/wcWAgAAgD8H/wQA//8CrkchQP//AgAAgD8GXI/CPv//AgAAgD//");

  // Used to determine where meadows are. These are flatter areas with fewer trees.
  Core::DSP::Image<2, float> globalMeadowImage;
};

class DesertBiomeNoise final : public SurfaceBiomeNoise
{
public:
  using SurfaceBiomeNoise::SurfaceBiomeNoise;

  float GetWeight(glm::ivec2 posWS) override
  {
    const auto desertPos = glm::vec2(100, 100);
    return 1 - glm::smoothstep(60.0f, 100.0f, glm::distance(glm::vec2(posWS), desertPos));
  }

  int GetSurfaceThickness() const override
  {
    return 4;
  }

  Core::DSP::Image<2, float> GenImageForChunk(glm::ivec2 posTL, const MapGenInfo& mapGenInfo) override
  {
    ZoneScoped;
    auto terrainHeightImage = WorldGen::GenerateAndUpscale2D(terrainHeight2D,
      posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      mapGenInfo.seed - 21,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Linear);

    for (int i = 0; i < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; i++)
    {
      terrainHeightImage.Data()[i] = terrainHeightImage.Data()[i] * 20.0f - 10;
    }

    return terrainHeightImage;
  }

  void PlaceSurfaceFeatures([[maybe_unused]] World& world, [[maybe_unused]] const MapGenInfo& mapGenInfo, [[maybe_unused]] glm::ivec3 posWS) override
  {
    ZoneScoped;
    auto& grid         = world.globals->grid;
    const auto& blocks = world.globals->blockRegistry;

    auto shrimplex = FastNoise::New<FastNoise::Simplex>();
    shrimplex->SetScale(25);
    shrimplex->SetOutputMin(0);

    auto shrimplex2 = FastNoise::New<FastNoise::Simplex>();
    shrimplex2->SetScale(8);
    shrimplex2->SetOutputMin(0);

    auto whiteNoise = FastNoise::New<FastNoise::White>();
    whiteNoise->SetOutputMin(0);
    auto whiteNoise2 = FastNoise::New<FastNoise::White>();

    const auto x = posWS.x;
    const auto y = posWS.y;
    const auto z = posWS.z;

    const bool hasSolidFloor = grid->GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air;

    if (hasSolidFloor &&
        shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 4) * 0.2f + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 10) * 0.8f > 0.96f)
    {
      grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("cactus_small"));
    }

    if (hasSolidFloor &&
        shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 5) * 0.2f + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 11) * 0.8f > 0.94f)
    {
      grid->SetVoxelAtUnchecked({x, y, z}, blocks->Get("bush_03"));
    }
  }

private:
  FastNoise::SmartNode<> terrainHeight2D =
    FastNoise::NewFromEncodedNodeTree("FQUdBQ0FBgAAgKJCBK5HYT7//wMPAAI@BFJ@BMA/BSUAog@BUGAADAB0ME@Bvwg@B//////wbNzMw+/wMlAOz///8FC@AQB1DBJqZGb8ImpkZP////w==");
};

class SnowBiomeNoise final : public SurfaceBiomeNoise
{
public:
  using SurfaceBiomeNoise::SurfaceBiomeNoise;

  void PlaceSurfaceFeatures([[maybe_unused]] World& world, [[maybe_unused]] const MapGenInfo& mapGenInfo, [[maybe_unused]] glm::ivec3 posWS) override {}

  float GetWeight(glm::ivec2 posWS) override
  {
    const auto biomePos = glm::ivec2(200, 200);
    return 1 - glm::smoothstep(0.0f, 40.0f, glm::max(0.0f, Math::SDF::Box(glm::vec2(posWS - biomePos), glm::vec2{30, 50})));
  }

  Core::DSP::Image<2, float> GenImageForChunk(glm::ivec2 posTL, const MapGenInfo& mapGenInfo) override
  {
    ZoneScoped;
    auto terrainHeightImage = WorldGen::GenerateAndUpscale2D(terrainHeight2D,
      glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
      mapGenInfo.seed - 22,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Linear);

    for (int i = 0; i < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; i++)
    {
      terrainHeightImage.Data()[i] = terrainHeightImage.Data()[i] * 20.0f - 15;
    }

    return terrainHeightImage;
  }

  int GetSurfaceThickness() const override
  {
    return 2;
  }

private:
  FastNoise::SmartNode<> terrainHeight2D = FastNoise::NewFromEncodedNodeTree("DQUlAEM@BFBg@AG9CBM3MTD////8=");
};

class SurfaceCorruptionNoise final : public SurfaceBiomeNoise
{
public:
  explicit SurfaceCorruptionNoise(const CreateInfo& createInfo) : SurfaceBiomeNoise(createInfo)
  {
    scale    = FastNoise::New<FastNoise::Constant>();
    multiply = FastNoise::New<FastNoise::Multiply>();
    scale->SetValue(40);
    multiply->SetLHS(terrainHeight2D);
    multiply->SetRHS(scale);
  }

  float GetWeight(glm::ivec2 posWS) override
  {
    return 1 - glm::smoothstep(40.0f * 40.0f, 50.0f * 50.0f, glm::max(0.0f, Math::Distance2(posWS, glm::vec2(100, 20))));
  }

  int GetSubsurfaceThickness() const override
  {
    return 20;
  }

  Core::DSP::Image<2, float> GenImageForChunk(glm::ivec2 posTL, [[maybe_unused]] const MapGenInfo& mapGenInfo) override
  {
    return WorldGen::GenerateAndUpscale2D(multiply,
      posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      mapGenInfo.seed + 123455,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Nearest);
  }

  void PlaceSurfaceFeatures([[maybe_unused]] World& world, [[maybe_unused]] const MapGenInfo& mapGenInfo, [[maybe_unused]] glm::ivec3 posWS) override {}

private:
  FastNoise::SmartNode<> terrainHeight2D = FastNoise::NewFromEncodedNodeTree("DQUQBQY@BWQwQ@AC/C@BD//AykFJQCO////BQYAAIB6Q///DgAAyEL///8=");
  FastNoise::SmartNode<FastNoise::Multiply> multiply;
  FastNoise::SmartNode<FastNoise::Constant> scale;
};

class Ocean final : public SurfaceBiomeNoise
{
public:
  using SurfaceBiomeNoise::SurfaceBiomeNoise;

  float GetWeight(glm::ivec2 posWS) override
  {
    return 1 - glm::smoothstep(20.0f, 45.0f, glm::distance(glm::vec2(posWS), glm::vec2(50, 50)));
  }

  int GetSurfaceThickness() const override
  {
    return 10;
  }

  Core::DSP::Image<2, float> GenImageForChunk([[maybe_unused]] glm::ivec2 posTL, [[maybe_unused]] const MapGenInfo& mapGenInfo) override
  {
    auto image = Core::DSP::Image<2, float>({Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE});
    image.Fill(-20);
    return image;
  }

  void PlaceSurfaceFeatures(World& world, const MapGenInfo& mapGenInfo, glm::ivec3 posWS) override
  {
    auto& grid       = world.globals->grid;
    const auto water = world.globals->blockRegistry->Get("water_8");

    for (; posWS.y < mapGenInfo.seaLevel - 10; posWS.y++)
    {
      grid->SetVoxelAt(posWS, water);
    }
  }

private:
};

class Rivers final : public SurfaceBiomeNoise
{
public:
  using SurfaceBiomeNoise::SurfaceBiomeNoise;

  float GetWeight([[maybe_unused]] glm::ivec2 posWS) override
  {
    return 0;
    // return glm::min(100.0f, glm::smoothstep(0.6f, 1.2f, 1 - biomeWeight->GenSingle2D((float)posWS.x, (float)posWS.y, 99) * 1.0f) * 2.0f);
  }

  int GetSurfaceThickness() const override
  {
    return 2;
  }

  Core::DSP::Image<2, float> GenImageForChunk([[maybe_unused]] glm::ivec2 posTL, [[maybe_unused]] const MapGenInfo& mapGenInfo) override
  {
    return WorldGen::GenerateAndUpscale2D(terrainHeight,
      posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      mapGenInfo.seed + 1212,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Nearest);
  }

  void PlaceSurfaceFeatures([[maybe_unused]] World& world, [[maybe_unused]] const MapGenInfo& mapGenInfo, [[maybe_unused]] glm::ivec3 posWS) override
  {
    // auto& grid       = world.globals->grid;
    // const auto water = world.globals->blockRegistry->Get("water_8");

    // for (; posWS.y < mapGenInfo.seaLevel - 15; posWS.y++)
    //{
    //   grid->SetVoxelAt(posWS, water);
    // }
  }

private:
  FastNoise::SmartNode<> terrainHeight =
    FastNoise::NewFromEncodedNodeTree("FgMXBRkFBgAAwBVD//8CAACgQP8GAACgQf8="); // Note: should be same shape as biomeWeight, but scaled and translated.
  FastNoise::SmartNode<> biomeWeight = FastNoise::NewFromEncodedNodeTree("GQUGAADAFUP//w==");
};

std::array<std::unique_ptr<SurfaceBiomeNoise>, int(SurfaceBiome::COUNT)> GetSurfaceBiomeNoises(const World& world)
{
  auto surfaceBiomes = std::array<std::unique_ptr<SurfaceBiomeNoise>, int(SurfaceBiome::COUNT)>();
  auto& blocks       = world.globals->blockRegistry;
  auto& grid         = world.globals->grid;

  // surfaceBiomes[int(SurfaceBiome::Desert)] = std::make_unique<DesertBiomeNoise>(SurfaceBiomeNoise::CreateInfo{blocks->Get("sand")});
  surfaceBiomes[int(SurfaceBiome::Snow)] = std::make_unique<SnowBiomeNoise>(SurfaceBiomeNoise::CreateInfo{&world, blocks->Get("snow"), blocks->Get("dirt")});
  surfaceBiomes[int(SurfaceBiome::Corruption)] =
    std::make_unique<SurfaceCorruptionNoise>(SurfaceBiomeNoise::CreateInfo{&world, blocks->Get("grass_corrupt"), blocks->Get("dirt")});
  // surfaceBiomes[int(SurfaceBiome::Ocean)]  = std::make_unique<Ocean>(SurfaceBiomeNoise::CreateInfo{&world, blocks->Get("sand")});
  surfaceBiomes[int(SurfaceBiome::Rivers)] = std::make_unique<Rivers>(SurfaceBiomeNoise::CreateInfo{&world, blocks->Get("sand")});
  surfaceBiomes[int(SurfaceBiome::Forest)] = std::make_unique<ForestBiomeNoise>(SurfaceBiomeNoise::CreateInfo{&world, blocks->Get("grass"), blocks->Get("dirt")},
    glm::ivec2{grid->topLevelBricksDims_.x, grid->topLevelBricksDims_.z});

  return surfaceBiomes;
}