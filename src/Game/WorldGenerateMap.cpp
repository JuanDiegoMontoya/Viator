#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Voxel/Grid.h"
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

  // Generate inSideLength^3 chunk of noise, then upscale it to outSideLength^3 with Filter.
  // Note: this upscaling is actually considerably less efficient than simply generating the equivalent volume of noise,
  // except in the case of very complex noise graphs.
  Core::Image<3, float> GenerateAndUpscale3D(const FastNoise::SmartNode<>& node, glm::ivec3 start, int seed, int inSideLength, int outSideLength, Core::Filter filter)
  {
    ZoneScoped;
    const int sideLength = (inSideLength == outSideLength) ? inSideLength : (inSideLength + 1);
    auto rawImage        = Core::Image<3, float>({sideLength, sideLength, sideLength});

    {
      ZoneScopedN("GenUniformGrid3D");
      const int sideCount = (inSideLength == outSideLength) ? inSideLength : inSideLength + 1;
      node->GenUniformGrid3D(rawImage.data(), start.x, start.y, start.z, sideCount, sideCount, sideCount, seed);
    }

    if (inSideLength == outSideLength)
    {
      return rawImage;
    }

    {
      ZoneScopedN("Upscale 3D");
      auto outImage = Core::Image<3, float>({outSideLength, outSideLength, outSideLength});
      auto* out = outImage.data();
      int i    = 0;
      for (int z = 0; z < outSideLength; z++)
      for (int y = 0; y < outSideLength; y++)
      for (int x = 0; x < outSideLength; x++)
      {
        const auto uv = (glm::vec3(x, y, z) + 0.5f) / (outSideLength + (float(outSideLength) / inSideLength));
        out[i++]      = rawImage.Sample({.filter = filter}, uv);
      }

      return outImage;
    }
  }

  Core::Image<2, float> GenerateAndUpscale2D(const FastNoise::SmartNode<>& node, glm::ivec2 start, int seed, int inSideLength, int outSideLength, Core::Filter filter)
  {
    ZoneScoped;
    const int sideLength = (inSideLength == outSideLength) ? inSideLength : (inSideLength + 1);
    auto rawImage        = Core::Image<2, float>({sideLength, sideLength});

    {
      ZoneScopedN("GenUniformGrid2D");
      const int sideCount = (inSideLength == outSideLength) ? inSideLength : inSideLength + 1;
      node->GenUniformGrid2D(rawImage.data(), start.x, start.y, sideCount, sideCount, seed);
    }

    if (inSideLength == outSideLength)
    {
      return rawImage;
    }

    {
      ZoneScopedN("Upscale 2D");
      auto outImage = Core::Image<2, float>({outSideLength, outSideLength});
      auto* out     = outImage.data();
      int i    = 0;
      for (int y = 0; y < outSideLength; y++)
      for (int x = 0; x < outSideLength; x++)
      {
        const auto uv = (glm::vec2(x, y) + 0.5f) / (outSideLength + (float(outSideLength) / inSideLength));
        out[i++]      = rawImage.Sample({.filter = filter}, uv);
      }

      return outImage;
    }
  }

  enum class SurfaceBiome : uint32_t
  {
    Desert,
    Corruption,
    Snow,
    Ocean,
    Rivers,
    // NOTE: the last biome in this list is the default!
    Forest,
    COUNT,
  };

  class SurfaceBiomeNoise
  {
  public:
    NO_COPY_NO_MOVE(SurfaceBiomeNoise);

    struct CreateInfo
    {
      BlockId surfaceBlockType = voxel_t::Null;
      BlockId subsurfaceBlockType = voxel_t::Null;
    };

    explicit SurfaceBiomeNoise(const CreateInfo& createInfo) : createInfo_(createInfo) {}
    virtual ~SurfaceBiomeNoise() = default;

    [[nodiscard]] virtual Core::Image<2, float> GenImageForChunk(glm::ivec2 posTL, const World::MapGenInfo& mapGenInfo) = 0;

    [[nodiscard]] virtual bool BroadPhase([[maybe_unused]] glm::ivec2 posTL)
    {
      return true;
    }

    [[nodiscard]] virtual float GetWeight(glm::ivec2 posWS) = 0;

    virtual void PlaceSurfaceFeatures(World& world, const World::MapGenInfo& mapGenInfo, glm::ivec3 posWS) = 0;

    [[nodiscard]] virtual int GetSurfaceThickness() const
    {
      return 1;
    }

    [[nodiscard]] virtual int GetSubsurfaceThickness() const
    {
      return 0;
    }

    [[nodiscard]] virtual BlockId GetSurfaceBlockType() const
    {
      return createInfo_.surfaceBlockType;
    }

    [[nodiscard]] virtual BlockId GetSubsurfaceBlockType() const
    {
      return createInfo_.subsurfaceBlockType;
    }

  private:
    CreateInfo createInfo_;
  };

  class ForestBiomeNoise final : public SurfaceBiomeNoise
  {
  public:
    ForestBiomeNoise(const CreateInfo& createInfo, glm::ivec2 worldDimsTL) : SurfaceBiomeNoise(createInfo)
    {
      globalMeadowImage = Core::Image<2, float>({worldDimsTL.x * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, worldDimsTL.y * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE});
    }

    float GetWeight([[maybe_unused]] glm::ivec2 posWS) override
    {
      PANIC;
    }

    int GetSubsurfaceThickness() const override
    {
      return 20;
    }

    Core::Image<2, float> GenImageForChunk(glm::ivec2 posTL, const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      auto terrainHeightImage = GenerateAndUpscale2D(terrainHeight2D,
        glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Linear);

      auto meadowImage = GenerateAndUpscale2D(meadowNoise,
        glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
        mapGenInfo.seed * 21,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Nearest);

      for (int y = 0; y < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; y++)
      for (int x = 0; x < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; x++)
      {
        const auto pModTl = glm::ivec2(x, y);
        const auto positionWS = pModTl + glm::ivec2(posTL.x, posTL.y) * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
        
        const auto meadowness = meadowImage.Load(pModTl);

        const auto heightScale = glm::mix(15, 4,  meadowness);
        const auto height = glm::floor(heightScale * terrainHeightImage.Load(pModTl));
        terrainHeightImage.Store(pModTl, height);
        globalMeadowImage.Store({positionWS.x, positionWS.y}, meadowness);
      }

      return terrainHeightImage;
    }

    void PlaceSurfaceFeatures(World& world, const World::MapGenInfo& mapGenInfo, glm::ivec3 posWS) override
    {
      ZoneScoped;
      auto& registry_ = world.GetRegistry();
      auto& grid      = registry_.ctx().get<Voxel::Grid>();
      auto& blocks    = registry_.ctx().get<Block::Registry>();

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

      const bool hasSolidFloor = grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air;
      const auto tree          = whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 4);
      if (hasSolidFloor && tree > glm::mix(0.99f, 0.998f, meadowness))
      {
        if (registry_.ctx().get<PCG::Rng>().RandFloat() < 0.9f)
        {
          registry_.ctx().get<PrefabRegistry>().Get("Tree").Instantiate(world, {x, y, z});
        }
        else
        {
          registry_.ctx().get<PrefabRegistry>().Get("Tree2").Instantiate(world, {x, y, z});
        }
      }
      else
      {
        if (hasSolidFloor &&
            shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 5) + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 9) * 0.2f < 0.03f)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("bush_01"));
        }

        if (hasSolidFloor &&
            shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 31) * 0.7f + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 30) * 0.3f >
              0.88f)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("bush_02"));
        }

        if (hasSolidFloor &&
            shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 16) * 0.7f + whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 17) * 0.3f > 0.93f)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("mushroom"));
        }
        else if (hasSolidFloor && meadowness * shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 24) * 0.7f +
                                      whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 25) * 0.3f >
                                    0.95f)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("rose"));
        }
        else if (hasSolidFloor && meadowness * shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 22) * 0.7f +
                                      whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 23) * 0.3f >
                                    0.93f)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("dandelion"));
        }
        else if (hasSolidFloor && whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 24) > 0.999f)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("rock_small"));
        }
        else // Because it's low priority, grass shouldn't override other foliage.
        {
          const auto grasss = meadowness * 0.1f + shrimplex2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 10) +
                              whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 11) * 0.3f;

          if (hasSolidFloor)
          {
            if (grasss > 0.6f)
            {
              grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_short"));
            }
            if (grasss > 0.7f)
            {
              grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_medium"));
            }
            if (grasss > 0.8f)
            {
              grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_long"));
            }
            if (grasss > 0.9f)
            {
              registry_.ctx().get<PrefabRegistry>().Get("Double Grass").Instantiate(world, {x, y, z});
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
    Core::Image<2, float> globalMeadowImage;
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

    Core::Image<2, float> GenImageForChunk(glm::ivec2 posTL, const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      auto terrainHeightImage = GenerateAndUpscale2D(terrainHeight2D,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        mapGenInfo.seed - 21,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Linear);

      for (int i = 0; i < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; i++)
      {
        terrainHeightImage.data()[i] = terrainHeightImage.data()[i] * 20.0f - 10;
      }

      return terrainHeightImage;
    }

    void PlaceSurfaceFeatures([[maybe_unused]] World& world, [[maybe_unused]] const World::MapGenInfo& mapGenInfo, [[maybe_unused]] glm::ivec3 posWS) override
    {
      ZoneScoped;
      auto& registry_ = world.GetRegistry();
      auto& grid      = registry_.ctx().get<Voxel::Grid>();
      auto& blocks    = registry_.ctx().get<Block::Registry>();

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

      const bool hasSolidFloor = grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air;

      if (hasSolidFloor &&
          shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 4) * 0.2f + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 10) * 0.8f > 0.96f)
      {
        grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("cactus_small"));
      }

      if (hasSolidFloor &&
          shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 5) * 0.2f + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 11) * 0.8f > 0.94f)
      {
        grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("bush_03"));
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

    void PlaceSurfaceFeatures([[maybe_unused]] World& world, [[maybe_unused]] const World::MapGenInfo& mapGenInfo, [[maybe_unused]] glm::ivec3 posWS) override
    {
    }

    float GetWeight(glm::ivec2 posWS) override
    {
      const auto biomePos = glm::ivec2(200, 200);
      return 1 - glm::smoothstep(0.0f, 40.0f, glm::max(0.0f, Math::SDF::Box(glm::vec2(posWS - biomePos), glm::vec2{30, 50})));
    }

    Core::Image<2, float> GenImageForChunk(glm::ivec2 posTL, const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      auto terrainHeightImage = GenerateAndUpscale2D(terrainHeight2D,
        glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
        mapGenInfo.seed - 22,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Linear);

      for (int i = 0; i < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; i++)
      {
        terrainHeightImage.data()[i] = terrainHeightImage.data()[i] * 20.0f - 15;
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
      scale = FastNoise::New<FastNoise::Constant>();
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

    Core::Image<2, float> GenImageForChunk(glm::ivec2 posTL, [[maybe_unused]] const World::MapGenInfo& mapGenInfo) override
    {
      return GenerateAndUpscale2D(multiply,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        123456,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Nearest);
    }

    void PlaceSurfaceFeatures([[maybe_unused]] World& world, [[maybe_unused]] const World::MapGenInfo& mapGenInfo, [[maybe_unused]] glm::ivec3 posWS) override
    {
    }

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

    Core::Image<2, float> GenImageForChunk([[maybe_unused]] glm::ivec2 posTL, [[maybe_unused]] const World::MapGenInfo& mapGenInfo) override
    {
      auto image = Core::Image<2, float>({Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE});
      image.Fill(-20);
      return image;
    }

    void PlaceSurfaceFeatures(World& world, const World::MapGenInfo& mapGenInfo, glm::ivec3 posWS) override
    {
      auto& grid = world.GetRegistry().ctx().get<Voxel::Grid>();
      const auto water = world.GetRegistry().ctx().get<Block::Registry>().Get("water_8");

      for (; posWS.y < mapGenInfo.seaLevel - 10; posWS.y++)
      {
        grid.SetVoxelAt(posWS, water);
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
      //return glm::min(100.0f, glm::smoothstep(0.6f, 1.2f, 1 - biomeWeight->GenSingle2D((float)posWS.x, (float)posWS.y, 99) * 1.0f) * 2.0f);
    }

    int GetSurfaceThickness() const override
    {
      return 2;
    }

    Core::Image<2, float> GenImageForChunk([[maybe_unused]] glm::ivec2 posTL, [[maybe_unused]] const World::MapGenInfo& mapGenInfo) override
    {
      return GenerateAndUpscale2D(terrainHeight,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        1212,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Nearest);
    }

    void PlaceSurfaceFeatures([[maybe_unused]] World& world, [[maybe_unused]] const World::MapGenInfo& mapGenInfo, [[maybe_unused]] glm::ivec3 posWS) override
    {
      //auto& grid       = world.GetRegistry().ctx().get<Voxel::Grid>();
      //const auto water = world.GetRegistry().ctx().get<Block::Registry>().Get("water_8");

      //for (; posWS.y < mapGenInfo.seaLevel - 15; posWS.y++)
      //{
      //  grid.SetVoxelAt(posWS, water);
      //}
    }

  private:
    FastNoise::SmartNode<> terrainHeight = FastNoise::NewFromEncodedNodeTree("FgMXBRkFBgAAwBVD//8CAACgQP8GAACgQf8="); // Note: should be same shape as biomeWeight, but scaled and translated.
    FastNoise::SmartNode<> biomeWeight = FastNoise::NewFromEncodedNodeTree("GQUGAADAFUP//w==");
  };
} // namespace

namespace
{
  enum class UndergroundBiome
  {
    DesertCaves,
    FunkyCaves,
    Corruption,
    // NOTE: the last biome in this enum is the default!
    SurfaceCaves,
    COUNT,
  };

  class UndergroundBiomeNoise
  {
  public:
    NO_COPY_NO_MOVE(UndergroundBiomeNoise);

    UndergroundBiomeNoise(BlockId substrateBlockType) : substrateBlockType_(substrateBlockType) {}
    virtual ~UndergroundBiomeNoise() = default;

    [[nodiscard]] virtual float GetWeight([[maybe_unused]] glm::ivec3 posWS) = 0;

    [[nodiscard]] virtual bool BroadPhase([[maybe_unused]] glm::ivec3 posTL)
    {
      return true;
    }

    [[nodiscard]] virtual BlockId GetSubstrateBlockType() const
    {
      return substrateBlockType_;
    }

    [[nodiscard]] virtual Core::Image<3, float> GenImageForChunk(glm::ivec3 posTL,
      [[maybe_unused]] glm::ivec3 dimsTL,
      [[maybe_unused]] const World::MapGenInfo& mapGenInfo) = 0;

  private:
    BlockId substrateBlockType_;
  };

  class SurfaceCaves final : public UndergroundBiomeNoise
  {
  public:
    SurfaceCaves(BlockId substrateBlockType) : UndergroundBiomeNoise(substrateBlockType)
    {
      surfaceCaves->SetSource(surfaceCavesA);
      surfaceCaves->SetScaling(1.5f);
    }

    float GetWeight([[maybe_unused]] glm::ivec3 posWS) override
    {
      PANIC;
    }

    Core::Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      return GenerateAndUpscale3D(surfaceCaves,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Linear);
    }

  private:
    FastNoise::SmartNode<> surfaceCavesA                      = FastNoise::NewFromEncodedNodeTree("HAUNBQY@ABSQgg@B///8DDwUXBQgAAIDIQv8C@BwP///w==");
    FastNoise::SmartNode<FastNoise::DomainScale> surfaceCaves = FastNoise::New<FastNoise::DomainScale>();
  };

  class DesertCaves final : public UndergroundBiomeNoise
  {
  public:
    using UndergroundBiomeNoise::UndergroundBiomeNoise;

    float GetWeight(glm::ivec3 posWS) override
    {
      const auto biomePos = glm::ivec3(50, 300, 20);
      return 1 - glm::smoothstep(0.0f, 20.0f, glm::max(0.0f, Math::SDF::Box(glm::vec3(posWS - biomePos), glm::vec3{30, 50, 40})));
    }

    Core::Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, [[maybe_unused]] const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      return GenerateAndUpscale3D(noise,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Linear);
    }

  private:
    FastNoise::SmartNode<> noise = FastNoise::NewFromEncodedNodeTree("Bg@AEhC/w==");
  };

  class FunkyCaves final : public UndergroundBiomeNoise
  {
  public:
    using UndergroundBiomeNoise::UndergroundBiomeNoise;

    float GetWeight(glm::ivec3 posWS) override
    {
      return 1 - glm::smoothstep(0.0f, biomeEdge, glm::max(0.0f, Math::SDF::Box(glm::vec3(posWS - biomePos), biomeHalfDims)));
    }

    bool BroadPhase([[maybe_unused]] glm::ivec3 posTL) override
    {
      const auto chunkMin = posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
      const auto chunkMax = chunkMin + Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
      return Math::Intersect::BoxVsBox(chunkMin, chunkMax, biomeAabbMin, biomeAabbMax);
    }

    Core::Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, [[maybe_unused]] const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      const auto dims = glm::ivec3{Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE};
      auto density    = Core::Image<3, float>(dims);

      for (int z = 0; z < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; z++)
      for (int y = 0; y < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; y++)
      for (int x = 0; x < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; x++)
      {
        using namespace glm;
        auto p      = vec3(x, y, z) + vec3(posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE);
        p           = {p.x, p.z, p.y};
        vec3 cSize  = vec3(1., 1., 1.3);
        float scale = 1.;
        for (int i = 0; i < 12; i++)
        {
          p        = 2.0f * clamp(p, -cSize, cSize) - p;
          float r2 = dot(p, p);
          float k  = max((2.f) / (r2), .027f);
          p *= k;
          scale *= k;
        }
        float l   = length(vec2(p.x, p.y));
        float rxy = l - 4.0f;
        float n   = l * p.z;
        rxy       = max(rxy, -(n) / 4.f);

        density.Store({x, y, z}, rxy / abs(scale));
      }

      return density;
    }

  private:
    glm::ivec3 biomePos          = {0, 200, 0};
    glm::vec3 biomeHalfDims      = {25, 25, 25};
    float biomeEdge              = 15;
    const glm::vec3 biomeAabbMin = glm::vec3(biomePos) - biomeHalfDims - biomeEdge;
    const glm::vec3 biomeAabbMax = glm::vec3(biomePos) + biomeHalfDims + biomeEdge;
  };

  class UndergroundCorruption final : public UndergroundBiomeNoise
  {
  public:
    explicit UndergroundCorruption(BlockId substrateBlockType) : UndergroundBiomeNoise(substrateBlockType)
    {
      shaftOffset->SetSource(shaftGenerator);
      shaftOffset->SetOffset<FastNoise::Dim::Y>(80 - 400); // Magic number = shaft depth - sea level

      combiner->SetLHS(densityGenerator);
      combiner->SetRHS(shaftOffset);
    }

    Core::Image<3, float> GenImageForChunk([[maybe_unused]] glm::ivec3 posTL,
      [[maybe_unused]] glm::ivec3 dimsTL,
      [[maybe_unused]] const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;

      return GenerateAndUpscale3D(combiner,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        -100,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Nearest);
    }

    float GetWeight(glm::ivec3 posWS) override
    {
      const auto pos2d = glm::vec2{posWS.x, posWS.z};
      return 1 - glm::smoothstep(160 * 160.0f, 200 * 200.0f, glm::max(0.0f, Math::Distance2(pos2d, glm::vec2(200, 220))));
    }

  private:
    FastNoise::SmartNode<> densityGenerator = FastNoise::NewFromEncodedNodeTree(
      "FAAC@BB@A4EAFEg@BRCBRwFIwUlBQs@ADIQwTNzMw9C@AIMAMAw@ABAC@BFAM@BYAg@BcJ@BI0IEH4XrPgiF61E//////wP/AQAG7FE4Pv8C@AgQf8CmpmZPgbNzEw//w==");
    FastNoise::SmartNode<> shaftGenerator = FastNoise::NewFromEncodedNodeTree(
      "GgUdBRUFDQUG@Bv0IEj8J1vQiPwnU9//8DFQUdBRcFGQUEBArXIzz//wIAAIC//wIAAIC//wIK16M9//8DHAUEBG8Sgzr/AycFHAUdBR@BCWQgUsBRQAAg@BQAAOBABRI@BFQgUcBSMFJQUL@BR0MEzczMPQgAACDADAM@BQAg@ABQD@BGAI@BHCQ@ADBCBFK4Hj8Ij8J1P/////8D/woABuxROD7/AgAAIEH/ApqZmT4GzcxMP///ApqZiUD/AgAAgL//AgAAgD//////AyQFE@AgMBCBSUABQ@BUXBQ@BCAv/8D/xQA//8CMzNLQv///w==");
    FastNoise::SmartNode<FastNoise::DomainOffset> shaftOffset = FastNoise::New<FastNoise::DomainOffset>();
    FastNoise::SmartNode<FastNoise::Max> combiner             = FastNoise::New<FastNoise::Max>();
  };
}

void World::GenerateMap(const MapGenInfo& mapGenInfo)
{
  ZoneScoped;
#ifndef GAME_HEADLESS
  auto& progressText = registry_.ctx().get<std::atomic<const char*>>("progressText"_hs);
  auto& progress     = registry_.ctx().get<std::atomic_int32_t>("progress"_hs);
  auto& total        = registry_.ctx().get<std::atomic_int32_t>("total"_hs);
#endif
  auto& blocks            = registry_.ctx().get<Block::Registry>();
  const auto& placeholder = blocks.Get("placeholder");
  const auto& dirt        = blocks.Get("dirt");
  [[maybe_unused]] const auto& malachite = blocks.Get("malachite");
  [[maybe_unused]] const auto& galena    = blocks.Get("galena");
  [[maybe_unused]] const auto& water8    = blocks.Get("water_8");

  constexpr auto samplesPerAxis = 64;
  constexpr auto sampleScale    = (float)samplesPerAxis / Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;

  auto& grid = registry_.ctx().get<Voxel::Grid>();

  auto tlBrickColCoords = std::vector<glm::ivec2>();
  for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
  {
    for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
    {
      tlBrickColCoords.emplace_back(k, i);
    }
  }

  auto globalSurfaceHeightImage = Core::Image<2, float>({grid.Dimensions().x, grid.Dimensions().z});
  auto globalSurfaceBiomeImage  = Core::Image<2, SurfaceBiome>({grid.Dimensions().x, grid.Dimensions().z});

  auto whiteNoise = FastNoise::New<FastNoise::White>();
  whiteNoise->SetOutputMin(0);
  auto whiteNoise2 = FastNoise::New<FastNoise::White>();

  auto shrimplex = FastNoise::New<FastNoise::Simplex>();
  shrimplex->SetScale(25);
  shrimplex->SetOutputMin(0);

  auto shrimplex2 = FastNoise::New<FastNoise::Simplex>();
  shrimplex2->SetScale(8);
  shrimplex2->SetOutputMin(0);

  auto surfaceBiomes                       = std::array<std::unique_ptr<SurfaceBiomeNoise>, int(SurfaceBiome::COUNT)>();
  //surfaceBiomes[int(SurfaceBiome::Desert)] = std::make_unique<DesertBiomeNoise>(SurfaceBiomeNoise::CreateInfo{blocks.Get("sand")});
  surfaceBiomes[int(SurfaceBiome::Snow)]   = std::make_unique<SnowBiomeNoise>(SurfaceBiomeNoise::CreateInfo{blocks.Get("snow"), blocks.Get("dirt")});
  //surfaceBiomes[int(SurfaceBiome::Corruption)] = std::make_unique<SurfaceCorruptionNoise>(SurfaceBiomeNoise::CreateInfo{blocks.Get("grass_corrupt"), blocks.Get("dirt")});
  //surfaceBiomes[int(SurfaceBiome::Ocean)]  = std::make_unique<Ocean>(SurfaceBiomeNoise::CreateInfo{blocks.Get("sand")});
  surfaceBiomes[int(SurfaceBiome::Rivers)]  = std::make_unique<Rivers>(SurfaceBiomeNoise::CreateInfo{blocks.Get("sand")});
  surfaceBiomes[int(SurfaceBiome::Forest)] = std::make_unique<ForestBiomeNoise>(SurfaceBiomeNoise::CreateInfo{blocks.Get("grass"), blocks.Get("dirt")},
    glm::ivec2{grid.topLevelBricksDims_.x, grid.topLevelBricksDims_.z});

  {
    ZoneScopedN("Surface");

    auto stoneInDirtA = FastNoise::NewFromEncodedNodeTree("GgUL@BIEEEAACAPwg@CDAM@AD/AwY@BgQQTNzEy+C@AoED//w==");
    auto stoneInDirt  = FastNoise::New<FastNoise::DomainScale>();
    stoneInDirt->SetSource(stoneInDirtA);
    stoneInDirt->SetScaling(1.0f / sampleScale);

    auto copperOre = FastNoise::NewFromEncodedNodeTree("FgLNzIw/BxUFBg@AFRBCK5HoT//Aws@BIQQgzM7M/D@CD///8=");
    auto leadOre = FastNoise::NewFromEncodedNodeTree("FgIAAIA/BxUFBg@BhBCK5HoT//AxAFCw@AFxBCJqZmT8M@CP8CAACAQf///w==");

#ifndef GAME_HEADLESS
    total.store((int32_t)grid.numTopLevelBricks_);
    progressText.store("Surface");
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

        auto biomeHeights = std::array<Core::Image<2, float>, int(SurfaceBiome::COUNT)>();

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

          auto biomeWeights   = std::array<float, int(SurfaceBiome::COUNT)>();
          auto biome          = SurfaceBiome::Forest;
          auto maxBiomeWeight = 0.0125f;
          auto sumWeights     = 0.0f;
          auto sumHeights     = 0.0f;
          
          for (int j = 0; j < int(SurfaceBiome::COUNT); j++)
          {
            // Skip biomes that failed broadphase check.
            if (biomeHeights[j].data() == nullptr)
            {
              continue;
            }

            float weight;
            // Last biome is the default (if weight of other biomes is low).
            if (j == int(SurfaceBiome::COUNT) - 1)
            {
              weight = 1 - glm::min(1.0f, sumWeights);
            }
            else
            {
              weight = surfaceBiomes[j]->GetWeight(positionWS);
            }

            biomeWeights[j] = weight;
            sumWeights += weight;
            sumHeights += weight * biomeHeights[j].Load(pModTl);

            if (weight > maxBiomeWeight)
            {
              biome = SurfaceBiome(j);
              maxBiomeWeight = weight;
            }
          }

          // Dither biome edges
          const auto target = sumWeights * shrimplex2->GenSingle2D((float)positionWS.x, (float)positionWS.y, 67);
          auto accumRng   = 0.0f;
          for (int j = 0; j < int(SurfaceBiome::COUNT); j++)
          {
            accumRng += biomeWeights[j];
            if (accumRng >= target)
            {
              biome = SurfaceBiome(j);
              break;
            }
          }

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
    progressText.store("Generate rivers");
    progress.store(0);
#endif

    auto kernelXGauss = std::vector<std::pair<glm::ivec2, float>>();
    auto kernelYGauss = std::vector<std::pair<glm::ivec2, float>>();

    constexpr int kernelWidth = 20;
    for (int i = -kernelWidth / 2; i <= kernelWidth / 2; i++)
    {
      kernelXGauss.emplace_back(glm::ivec2(i, 0), Math::GaussianNorm(float(i), 0, 5));
      kernelYGauss.emplace_back(glm::ivec2(0, i), Math::GaussianNorm(float(i), 0, 5));
    }

    auto kernelXBox = std::vector<std::pair<glm::ivec2, float>>();
    auto kernelYBox = std::vector<std::pair<glm::ivec2, float>>();

    constexpr int kernelBoxWidth = 40;
    for (int i = -kernelBoxWidth / 2; i <= kernelBoxWidth / 2; i++)
    {
      //kernelXBox.emplace_back(glm::ivec2(i, 0), 1.0f / (kernelWidth + 1));
      //kernelYBox.emplace_back(glm::ivec2(0, i), 1.0f / (kernelWidth + 1));
      kernelXBox.emplace_back(glm::ivec2(i, 0), Math::GaussianNorm(float(i), 0, 7));
      kernelYBox.emplace_back(glm::ivec2(0, i), Math::GaussianNorm(float(i), 0, 7));
    }

    const auto blur1 = globalSurfaceHeightImage.Convolve(kernelXBox);
    const auto blur2 = blur1.Convolve(kernelYBox);

    FastNoise::SmartNode<> riverWeight = FastNoise::NewFromEncodedNodeTree("GQUGAADAFUP//w==");

    const auto riverMask0 = GenerateAndUpscale2D(riverWeight, glm::ivec2(0, 0), 123456, grid.Dimensions().x, grid.Dimensions().z, Core::Filter::Nearest);

    const auto riverMask1 = riverMask0.Map([](float v) { return glm::smoothstep(0.8f, 1.0f, 1 - v); });
    const auto riverMask2 = riverMask1.Convolve(kernelXGauss);
    const auto riverMask3 = riverMask2.Convolve(kernelYGauss);

    const auto erodedTerrain = globalSurfaceHeightImage.Map(
      [&](glm::ivec2 pos, float originalHeight)
      {
        const auto blurredHeight = blur2.Load(pos);
        const auto riverness     = riverMask3.Load(pos);
        if (riverness > 0.2f)
        {
          globalSurfaceBiomeImage.Store(pos, SurfaceBiome::Rivers);
        }
        return glm::mix(originalHeight, blurredHeight - 15, riverness);
      });

    //const auto map  = [](float v) { return glm::min(255.0f, v * 256); };
    //const auto out1 = Map<uint8_t>(riverMask1, map);
    //const auto out2 = Map<uint8_t>(riverMask3, map);
    //const auto out3 = Map<uint8_t>(blur2, map);

    //const auto width = out1.ImageSize().x;
    //const auto height = out1.ImageSize().y;
    //stbi_write_png("out1.png", width, height, 1, out1.data(), width);
    //stbi_write_png("out2.png", width, height, 1, out2.data(), width);
    //stbi_write_png("out3.png", width, height, 1, out3.data(), width);

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
        for (int j = 0; j < grid.topLevelBricksDims_.y; j++) // Y last so we can compute heightmap once
        {
          ZoneScopedN("Top level brick");
          const auto tl = glm::ivec3{i, j, k};

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto height     = erodedTerrain.Load({positionWS.x, positionWS.z});
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
                grid.SetVoxelAtUncheckedNoDirty(positionWS, blockTypeToSet);
              }
            });

          grid.MarkTopLevelBrickAndChildrenDirty(tl);
          grid.CoalesceTopLevelBrickAndChildren(grid.GetTopLevelBrickPointerFromTopLevelPosition(tl));
#ifndef GAME_HEADLESS
          progress.fetch_add(1);
#endif
        }
      });

    constexpr int numIterations = 150;
#ifndef GAME_HEADLESS
    progressText.store("Settle water");
    progress.store(0);
    total.store(numIterations);
#endif
    for (int i = 0; i < numIterations; i++)
    {
      this->ProcessBlockTickQueue();
#ifndef GAME_HEADLESS
      progress.fetch_add(1);
#endif
    }

    registry_.ctx().get<World::WaterQueue>().clear();
  }

  if (true)
  {
    ZoneScopedN("Underground Biomes");
#ifndef GAME_HEADLESS
    progressText.store("Caves");
    progress.store(0);
#endif

    auto undergroundBiomes = std::array<std::unique_ptr<UndergroundBiomeNoise>, int(UndergroundBiome::COUNT)>();
    //undergroundBiomes[int(UndergroundBiome::DesertCaves)] = std::make_unique<DesertCaves>(blocks.Get("sand"));
    undergroundBiomes[int(UndergroundBiome::FunkyCaves)] = std::make_unique<FunkyCaves>(blocks.Get("dirt"));
    undergroundBiomes[int(UndergroundBiome::Corruption)] = std::make_unique<UndergroundCorruption>(blocks.Get("stone_corrupt"));
    undergroundBiomes[int(UndergroundBiome::SurfaceCaves)] = std::make_unique<SurfaceCaves>(blocks.Get("stone"));

    std::for_each(std::execution::par,
      tlBrickColCoords.begin(),
      tlBrickColCoords.end(),
      [&](glm::ivec2 tlBrickColCoord)
      {
        ZoneScopedN("Top level brick column");
        const int k = tlBrickColCoord[0];
        const int i = tlBrickColCoord[1];

        // Top level bricks
        for (int j = 0; j < grid.topLevelBricksDims_.y; j++)
        {
          ZoneScopedN("Top level brick");

          auto biomeDensities = std::array<Core::Image<3, float>, int(UndergroundBiome::COUNT)>();

          for (int m = 0; m < int(UndergroundBiome::COUNT); m++)
          {
            if (undergroundBiomes[m] != nullptr && undergroundBiomes[m]->BroadPhase({i, j, k}))
            {
              biomeDensities[m] = undergroundBiomes[m]->GenImageForChunk({i, j, k}, grid.topLevelBricksDims_, mapGenInfo);
            }
          }

          //auto densities = std::make_unique_for_overwrite<float[]>(samplesPerAxis * samplesPerAxis * samplesPerAxis);
          //auto biomes = std::make_unique_for_overwrite<UndergroundBiome[]>(samplesPerAxis * samplesPerAxis * samplesPerAxis);

          const auto tl = glm::ivec3{i, j, k};
          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              auto biomeWeights   = std::array<float, int(UndergroundBiome::COUNT)>();
              auto biome          = UndergroundBiome::SurfaceCaves;
              auto maxBiomeWeight = 0.0125f;
              auto sumWeights     = 0.0f;
              auto sumDensities   = 0.0f;

              for (int m = 0; m < int(UndergroundBiome::COUNT); m++)
              {
                // Skip biomes that failed broadphase check.
                if (biomeDensities[m].data() == nullptr)
                {
                  continue;
                }

                float weight;
                // Last biome is the default (if weight of other biomes is low).
                if (m == int(UndergroundBiome::COUNT) - 1)
                {
                  weight = 1 - glm::min(1.0f, sumWeights);
                }
                else
                {
                  weight = undergroundBiomes[m]->GetWeight(positionWS);
                }

                biomeWeights[m] = weight;
                sumWeights += weight;
                sumDensities +=
                  weight * biomeDensities[m].Load(positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE);

                if (weight > maxBiomeWeight)
                {
                  biome          = UndergroundBiome(m);
                  maxBiomeWeight = weight;
                }
              }

              // Dither biome edges
              const auto target = sumWeights * shrimplex2->GenSingle3D((float)positionWS.x, (float)positionWS.y, (float)positionWS.z, 68);
              auto accumRng     = 0.0f;
              for (int m = 0; m < int(UndergroundBiome::COUNT); m++)
              {
                accumRng += biomeWeights[m];
                if (accumRng >= target)
                {
                  biome = UndergroundBiome(m);
                  break;
                }
              }

              const auto density = sumDensities / sumWeights;

              {
                const auto blockAtPos = grid.GetVoxelAtUnchecked(positionWS);
                const auto biomeAtPos = globalSurfaceBiomeImage.Load({positionWS.x, positionWS.z});
                if (density >= 0.0f && 
                  //grid.GetVoxelAt(positionWS + Block::DirectionToNeighbor(Block::Direction::Up)) != water8
                    !(blockAtPos == water8 || (biomeAtPos == SurfaceBiome::Rivers && blockAtPos == surfaceBiomes[int(biomeAtPos)]->GetSurfaceBlockType()))
                  )
                {
                  grid.SetVoxelAtUncheckedNoDirty(positionWS, voxel_t::Air);
                }
                else if (blockAtPos == placeholder)
                {
                  grid.SetVoxelAtUncheckedNoDirty(positionWS, undergroundBiomes[int(biome)]->GetSubstrateBlockType());
                }
              }
            });

          grid.MarkTopLevelBrickAndChildrenDirty(tl);
          grid.CoalesceTopLevelBrickAndChildren(grid.GetTopLevelBrickPointerFromTopLevelPosition(tl));
#ifndef GAME_HEADLESS
          progress.fetch_add(1);
#endif
        }
      });
  }

  #if 1
  if (true)
  {
#ifndef GAME_HEADLESS
    progressText.store("Surface foliage");
    total.store(grid.dimensions_.x * grid.dimensions_.z);
    progress.store(0);
#endif
    for (int z = 0; z < grid.dimensions_.z; z++)
    for (int x = 0; x < grid.dimensions_.x; x++)
    {
      const auto biome = globalSurfaceBiomeImage.Load({x, z});
      const auto y = (int)globalSurfaceHeightImage.Load({x, z});
      surfaceBiomes[int(biome)]->PlaceSurfaceFeatures(*this, mapGenInfo, {x, y, z});

#ifndef GAME_HEADLESS
      progress.fetch_add(1);
#endif
    }
  }
  #endif
#ifndef GAME_HEADLESS
  progressText.store("Vines");
  total.store(grid.topLevelBricksDims_.x * grid.topLevelBricksDims_.y * grid.topLevelBricksDims_.z);
  progress.store(0);
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

        for (int j = 0; j < grid.topLevelBricksDims_.y; j++)
        {
          ZoneScopedN("Top level brick");

          const auto tl = glm::ivec3{i, j, k};

          auto simplexImage = GenerateAndUpscale3D(shrimplex2,
            tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 15,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Core::Filter::Nearest);

          auto whiteImage = GenerateAndUpscale3D(whiteNoise2,
            tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 16,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Core::Filter::Nearest);

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto tlLocal = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
              if (grid.GetVoxelAtUnchecked(positionWS) == voxel_t::Air)
              {
                const auto aboveWS = positionWS + glm::ivec3(0, 1, 0);
                const auto belowWS = positionWS + glm::ivec3(0, -1, 0);
                if (aboveWS.y < grid.dimensions_.y - 1)
                {
                  const auto aboveBlock = grid.GetVoxelAtUnchecked(aboveWS);
                  if (aboveBlock != voxel_t::Air)
                  {
                    if (simplexImage.Load(tlLocal) + whiteImage.Load(tlLocal) * 0.3f < 0.05f)
                    {
                      auto lk = std::unique_lock(mutex);
                      if (aboveBlock == dirt)
                      {
                        prefabs.emplace_back(&registry_.ctx().get<PrefabRegistry>().Get("Root"), positionWS);
                      }
                      else
                      {
                        prefabs.emplace_back(&registry_.ctx().get<PrefabRegistry>().Get("Vine"), positionWS);
                      }
                    }
                  }
                }

                if (belowWS.y > 0)
                {
                  const auto belowBlock = grid.GetVoxelAtUnchecked(belowWS);
                  if (belowBlock == dirt && whiteImage.Load(tlLocal) > 0.98f)
                  {
                    grid.SetVoxelAtNoDirty(positionWS, blocks.Get("pot"));
                  }
                }
              }
            });

#ifndef GAME_HEADLESS
          progress.fetch_add(1);
#endif
          grid.MarkTopLevelBrickAndChildrenDirty(tl);
          grid.CoalesceTopLevelBrickAndChildren(grid.GetTopLevelBrickPointerFromTopLevelPosition(tl));
        }
      });
  }

  {
#ifndef GAME_HEADLESS
    progressText.store("Instantiate prefabs");
    total.store(int(prefabs.size()));
    progress.store(0);
#endif

    ZoneScopedN("Instantiate Prefabs");
    for (const auto& [prefab, positionWS] : prefabs)
    {
      prefab->Instantiate(*this, positionWS);
#ifndef GAME_HEADLESS
      progress.fetch_add(1);
#endif
    }
  }

  if (mapGenInfo.spawnYggdrasil)
  {
    ZoneScopedN("Big Tree");
    auto bigTreeNoise = FastNoise::NewFromEncodedNodeTree(
      "FgMVBRoFGwUVBRcFBQMs@EFBgAAgAVDB@AoMAIAACgQP//BwQEAACAP/8LLAAC@BBSUF/wAA////AygJAABvEgM8/wAApptEPP8DLAAC@BBSw@EUlAAI@BFBgQ@C/////wY@C//8CAACAv/8CAACAv/8CAACAP/8DFwUbBQQEzcxMvQYAACDC//8DFgMbBQQEzczMvP8DFQUXBRsFGgUVBQQEj8L1PP8DJQAD@BBSwFBg@APZBB@DI@BP/////8CAACAP///AxUFGQU@CgL///wIK1yM8//8DFQUXBQUHBAQAAIA///8CbxKDOv8D/xsA////Bw8FE@BMZCBQY@ADJQv8CmpnKQv//////BxYCAACAPwcaBRsFHAUdBRUFE@AgDBDBSUABg@BUGAACAg0L//wMXBRUFFwUFBnuUFkP/AlJJnTn/AvYokkL/AmZmpkD//wP/LgD/AgrXo7z/AgAAgD//AgrXo7z/AgAAgD////8=");

    auto& rng          = registry_.ctx().get<PCG::Rng>();
    const auto fractXZ = glm::vec2(rng.RandFloat(0.4f, 0.6f), rng.RandFloat(0.4f, 0.6f));
    const auto posXZ   = glm::ivec2(fractXZ * glm::vec2(grid.dimensions_.x, grid.dimensions_.z) + 0.5f);
    const auto posY    = globalSurfaceHeightImage.Load(posXZ);
    const auto pos     = glm::ivec3(posXZ[0], posY, posXZ[1]);
    const auto bot     = glm::ivec3(glm::vec3(pos / Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE) + 0.5f);
    const auto top     = bot + 1 + glm::ivec3(0, 1, 0);

#ifndef GAME_HEADLESS
    progressText.store("Yggdrasil");
    const auto dif = top - bot + 1;
    total.store(dif.x * dif.y * dif.z);
    progress.store(0);
#endif

    for (int tz = bot.z; tz <= top.z; tz++)
    for (int ty = bot.y; ty <= top.y; ty++)
    for (int tx = bot.x; tx <= top.x; tx++)
    {
      const auto tl    = glm::ivec3(tx, ty, tz);
      const auto image = GenerateAndUpscale3D(bigTreeNoise,
        tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE - pos,
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Core::Filter::Nearest);

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
              grid.SetVoxelAt(positionWS, blocks.Get("wood"));
            }
          }
          else
          {
            if (density <= -0.032f)
            {
              grid.SetVoxelAt(positionWS, blocks.Get("wood"));
            }
            else if (density <= 0.0f)
            {
              grid.SetVoxelAt(positionWS, blocks.Get("leaves_01"));
            }
          }
        });

#ifndef GAME_HEADLESS
      progress.fetch_add(1);
#endif
    }
  }

  {
    ZoneScopedN("Ruins");
    constexpr int DUNGEON_CELL_SIZE = 16; // One attempt per cell.
#ifndef GAME_HEADLESS
    progressText.store("Ruins");
    total.store((grid.dimensions_.x / DUNGEON_CELL_SIZE) * (grid.dimensions_.y / DUNGEON_CELL_SIZE) * (grid.dimensions_.z / DUNGEON_CELL_SIZE));
    progress.store(0);
#endif

    auto rng = PCG::Rng(mapGenInfo.seed);
    for (int zt = 0; zt < grid.dimensions_.z / DUNGEON_CELL_SIZE; zt++)
    for (int yt = 0; yt < grid.dimensions_.y / DUNGEON_CELL_SIZE; yt++)
    for (int xt = 0; xt < grid.dimensions_.x / DUNGEON_CELL_SIZE; xt++)
    {
#ifndef GAME_HEADLESS
      progress.fetch_add(1);
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
            registry_.ctx().get<PrefabRegistry>().Get("AbandonedHouse").Instantiate(*this, posWS);
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
    progressText.store("Floating islands");
    total.store((grid.dimensions_.x / ISLAND_CELL_SIZE) * (grid.dimensions_.y / ISLAND_CELL_SIZE) * (grid.dimensions_.z / ISLAND_CELL_SIZE));
    progress.store(0);
#endif

    auto rng = PCG::Rng(mapGenInfo.seed);
    for (int zt = 0; zt < grid.dimensions_.z / ISLAND_CELL_SIZE; zt++)
    for (int yt = 0; yt < grid.dimensions_.y / ISLAND_CELL_SIZE; yt++)
    for (int xt = 0; xt < grid.dimensions_.x / ISLAND_CELL_SIZE; xt++)
    {
#ifndef GAME_HEADLESS
      progress.fetch_add(1);
#endif
      if (rng.RandFloat() < 0.1f)
      {
        const auto posCell = glm::ivec3(xt, yt, zt);

        for (int attempt = 0; attempt < 10; attempt++)
        {
          // Spawn prefab somewhere within the cell.
          const auto posSub = glm::ivec3(rng.RandU32() % ISLAND_CELL_SIZE, rng.RandU32() % ISLAND_CELL_SIZE, rng.RandU32() % ISLAND_CELL_SIZE);
          const auto posWS  = posCell * ISLAND_CELL_SIZE + posSub;
          const auto posFraction = glm::vec3(posWS) / glm::vec3(grid.dimensions_);

          const auto surfaceHeight = int(globalSurfaceHeightImage.Load({posWS.x, posWS.z}));
          if (posWS.y >= surfaceHeight + 125 && posFraction.x < 0.9f && posFraction.y < 0.9f && posFraction.z < 0.9f)
          {
            registry_.ctx().get<PrefabRegistry>().Get("FloatingIsland").Instantiate(*this, posWS);
            break;
          }
        }
      }
    }
  }

  grid.CoalesceDirtyBricks();
}