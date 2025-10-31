#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Voxel/Grid.h"
#include "Core/Assert2.h"
#include "Prefab.h"

#include "FastNoise/FastNoise.h"
#include "tracy/Tracy.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/component_wise.hpp"

#include <execution>
#include <ranges>
#include <concepts>

namespace
{
  enum class Filter
  {
    Nearest,
    Linear,
  };

  struct Sampler
  {
    Filter filter;
  };

  template<typename T>
  concept Filterable = requires(T x)
  {
    { x + x * x } -> std::same_as<T>;
  };

  template<size_t Dim, typename T>
    requires (Dim >= 1 && Dim <= 3)
  class Image
  {
  public:
    using value_type      = T;
    using dimensions_type = std::conditional_t<Dim == 1, int, std::conditional_t<Dim == 2, glm::ivec2, glm::ivec3>>;
    using uv_type         = std::conditional_t<Dim == 1, float, std::conditional_t<Dim == 2, glm::vec2, glm::vec3>>;

    Image() = default;

    explicit Image(dimensions_type imageSize) : imageSize_(imageSize)
    {
      image_ = std::make_unique<T[]>(glm::compMul(imageSize_));
    }

    [[nodiscard]] T TexelFetch(dimensions_type p) const noexcept
    {
      p = glm::clamp(p, dimensions_type(0), dimensions_type(imageSize_ - 1));
      return image_[TexCoordToIndex(p)];
    }
    
    void ImageStore(dimensions_type p, T value) noexcept
    {
      DEBUG_ASSERT(glm::all(glm::greaterThanEqual(p, dimensions_type(0))) && glm::all(glm::lessThan(p, dimensions_type(imageSize_))));
      image_[TexCoordToIndex(p)] = value;
    }

    [[nodiscard]] T Sample(Sampler sampler, uv_type uv) const noexcept
      requires Filterable<T>
    {
      const auto unnormalized = uv * uv_type(imageSize_);

      if (sampler.filter == Filter::Nearest)
      {
        return TexelFetch(dimensions_type(unnormalized));
      }

      const auto intCoord = dimensions_type(unnormalized);

      if constexpr (Dim == 1)
      {
        const auto l = TexelFetch(intCoord + dimensions_type(0));
        const auto r = TexelFetch(intCoord + dimensions_type(1));

        const auto weight = unnormalized - uv_type(intCoord);
        return glm::mix(l, r, weight);
      }

      if constexpr (Dim == 2)
      {
        const auto bl = TexelFetch(intCoord + dimensions_type(0, 0));
        const auto br = TexelFetch(intCoord + dimensions_type(1, 0));
        const auto tl = TexelFetch(intCoord + dimensions_type(0, 1));
        const auto tr = TexelFetch(intCoord + dimensions_type(1, 1));

        const auto weight = unnormalized - uv_type(intCoord);
        return glm::mix(glm::mix(bl, br, weight.x), glm::mix(tl, tr, weight.x), weight.y);
      }

      if constexpr (Dim == 3)
      {
        const auto bln = TexelFetch(intCoord + dimensions_type(0, 0, 0));
        const auto brn = TexelFetch(intCoord + dimensions_type(1, 0, 0));
        const auto tln = TexelFetch(intCoord + dimensions_type(0, 1, 0));
        const auto trn = TexelFetch(intCoord + dimensions_type(1, 1, 0));
        const auto blf = TexelFetch(intCoord + dimensions_type(0, 0, 1));
        const auto brf = TexelFetch(intCoord + dimensions_type(1, 0, 1));
        const auto tlf = TexelFetch(intCoord + dimensions_type(0, 1, 1));
        const auto trf = TexelFetch(intCoord + dimensions_type(1, 1, 1));

        const auto weight = unnormalized - uv_type(intCoord);
        const auto n      = glm::mix(glm::mix(bln, brn, weight.x), glm::mix(tln, trn, weight.x), weight.y);
        const auto f      = glm::mix(glm::mix(blf, brf, weight.x), glm::mix(tlf, trf, weight.x), weight.y);
        return glm::mix(n, f, weight.z);
      }
    }

    T* data() noexcept
    {
      return image_.get();
    }

  private:
    [[nodiscard]] int TexCoordToIndex(dimensions_type p) const
    {
      if constexpr (Dim == 1)
      {
        return p;
      }
      if constexpr (Dim == 2)
      {
        return p.x + p.y * imageSize_.x;
      }
      if constexpr (Dim == 3)
      {
        return p.x + p.y * imageSize_.x + p.z * imageSize_.y * imageSize_.x;
      }
    }

    dimensions_type imageSize_{};
    std::unique_ptr<T[]> image_;
  };

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
  Image<3, float> GenerateAndUpscale3D(const FastNoise::SmartNode<>& node, glm::ivec3 start, int seed, int inSideLength, int outSideLength, Filter filter)
  {
    ZoneScoped;
    const int sideLength = (inSideLength == outSideLength) ? inSideLength : (inSideLength + 1);
    auto rawImage        = Image<3, float>({sideLength, sideLength, sideLength});

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
      auto outImage = Image<3, float>({outSideLength, outSideLength, outSideLength});
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

  Image<2, float> GenerateAndUpscale2D(const FastNoise::SmartNode<>& node, glm::ivec2 start, int seed, int inSideLength, int outSideLength, Filter filter)
  {
    ZoneScoped;
    const int sideLength = (inSideLength == outSideLength) ? inSideLength : (inSideLength + 1);
    auto rawImage        = Image<2, float>({sideLength, sideLength});

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
      auto outImage = Image<2, float>({outSideLength, outSideLength});
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
    Snow,
    // NOTE: the last biome in this list is the default!
    Forest,
    COUNT,
  };

  class SurfaceBiomeNoise
  {
  public:
    NO_COPY_NO_MOVE(SurfaceBiomeNoise);

    SurfaceBiomeNoise(BlockId surfaceBlockType) : surfaceBlockType_(surfaceBlockType) {}
    virtual ~SurfaceBiomeNoise() = default;

    [[nodiscard]] virtual Image<2, float> GenImageForChunk(glm::ivec2 posTL, const World::MapGenInfo& mapGenInfo) = 0;

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

    [[nodiscard]] virtual BlockId GetSurfaceBlockType() const
    {
      return surfaceBlockType_;
    }

  private:
    BlockId surfaceBlockType_;
  };

  class ForestBiomeNoise final : public SurfaceBiomeNoise
  {
  public:
    ForestBiomeNoise(BlockId surfaceBlockType, glm::ivec2 worldDimsTL) : SurfaceBiomeNoise(surfaceBlockType)
    {
      globalMeadowImage = Image<2, float>({worldDimsTL.x * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, worldDimsTL.y * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE});
    }

    float GetWeight([[maybe_unused]] glm::ivec2 posWS) override
    {
      PANIC;
    }

    Image<2, float> GenImageForChunk(glm::ivec2 posTL, const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      auto terrainHeightImage = GenerateAndUpscale2D(terrainHeight2D,
        glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Filter::Linear);

      auto meadowImage = GenerateAndUpscale2D(meadowNoise,
        glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
        mapGenInfo.seed * 21,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Filter::Nearest);

      for (int y = 0; y < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; y++)
      for (int x = 0; x < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; x++)
      {
        const auto pModTl = glm::ivec2(x, y);
        const auto positionWS = pModTl + glm::ivec2(posTL.x, posTL.y) * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
        
        const auto meadowness = meadowImage.TexelFetch(pModTl);

        const auto heightScale = glm::mix(15, 4,  meadowness);
        const auto height = glm::floor(heightScale * terrainHeightImage.TexelFetch(pModTl));
        terrainHeightImage.ImageStore(pModTl, height);
        globalMeadowImage.ImageStore({positionWS.x, positionWS.y}, meadowness);
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

      const auto meadowness = globalMeadowImage.TexelFetch({x, z});

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
    Image<2, float> globalMeadowImage;
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

    Image<2, float> GenImageForChunk(glm::ivec2 posTL, const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      auto terrainHeightImage = GenerateAndUpscale2D(terrainHeight2D,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        mapGenInfo.seed - 21,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Filter::Linear);

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

    Image<2, float> GenImageForChunk(glm::ivec2 posTL, const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      auto terrainHeightImage = GenerateAndUpscale2D(terrainHeight2D,
        glm::ivec2(glm::vec2(posTL.x, posTL.y) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE),
        mapGenInfo.seed - 22,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Filter::Linear);

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

  class CorruptionBiome final : public SurfaceBiomeNoise
  {
  public:

  private:
  };
} // namespace

namespace
{
  enum class UndergroundBiome
  {
    DesertCaves,
    FunkyCaves,
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

    [[nodiscard]] virtual Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, [[maybe_unused]] const World::MapGenInfo& mapGenInfo) = 0;

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

    Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      return GenerateAndUpscale3D(surfaceCaves,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Filter::Linear);
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

    Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, [[maybe_unused]] const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      return GenerateAndUpscale3D(noise,
        posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Filter::Linear);
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

    Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, [[maybe_unused]] const World::MapGenInfo& mapGenInfo) override
    {
      ZoneScoped;
      const auto dims = glm::ivec3{Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE};
      auto density = Image<3, float>(dims);

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

        density.ImageStore({x, y, z}, rxy / abs(scale));
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
  [[maybe_unused]] const auto& malachite   = blocks.Get("malachite");
  [[maybe_unused]] const auto& galena    = blocks.Get("galena");

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

  auto globalSurfaceHeightImage = Image<2, float>({grid.Dimensions().x, grid.Dimensions().z});
  auto globalSurfaceBiomeImage  = Image<2, SurfaceBiome>({grid.Dimensions().x, grid.Dimensions().z});

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
  surfaceBiomes[int(SurfaceBiome::Desert)] = std::make_unique<DesertBiomeNoise>(blocks.Get("sand"));
  surfaceBiomes[int(SurfaceBiome::Snow)]   = std::make_unique<SnowBiomeNoise>(blocks.Get("snow"));
  surfaceBiomes[int(SurfaceBiome::Forest)] = std::make_unique<ForestBiomeNoise>(blocks.Get("grass"), glm::ivec2{grid.topLevelBricksDims_.x, grid.topLevelBricksDims_.z});

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

        auto biomeHeights = std::array<Image<2, float>, int(SurfaceBiome::COUNT)>();

        for (int j = 0; j < int(SurfaceBiome::COUNT); j++)
        {
          if (surfaceBiomes[j]->BroadPhase({i, k}))
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
            sumHeights += weight * biomeHeights[j].TexelFetch(pModTl);

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
          globalSurfaceHeightImage.ImageStore({positionWS.x, positionWS.y}, height);
          globalSurfaceBiomeImage.ImageStore({positionWS.x, positionWS.y}, biome);
        }

        // Top level bricks
        for (int j = 0; j < grid.topLevelBricksDims_.y; j++) // Y last so we can compute heightmap once
        {
          ZoneScopedN("Top level brick");

          auto stoneInDirtImage = GenerateAndUpscale3D(stoneInDirt,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 1,
            samplesPerAxis,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Linear);

          auto fadeImage = GenerateAndUpscale3D(whiteNoise,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 2,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          auto copperImage = GenerateAndUpscale3D(copperOre,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 55,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          auto leadImage = GenerateAndUpscale3D(leadOre,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 56,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          const auto tl = glm::ivec3{i, j, k};

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto pModTl = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
              
              //const auto height = TexelFetch2D(terrainHeightImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, {pModTl.x, pModTl.z});
              const auto height = globalSurfaceHeightImage.TexelFetch({positionWS.x, positionWS.z});
              const auto biome  = globalSurfaceBiomeImage.TexelFetch({positionWS.x, positionWS.z});
              const auto& biomeInfo = surfaceBiomes[int(biome)];

              auto blockTypeToSet = voxel_t::Air;
              if (positionWS.y < height)
              {
                // 0 at sea level. 1 at cavern level.
                //const auto alphaCaverns = glm::clamp((mapGenInfo.seaLevel - positionWS.y) / float(mapGenInfo.surfaceThickness), 0.0f, 1.0f);

                if (positionWS.y <= height && positionWS.y >= height - biomeInfo->GetSurfaceThickness())
                {
                  blockTypeToSet = biomeInfo->GetSurfaceBlockType();
                }
                else
                {
                  blockTypeToSet = placeholder;
                }
              //  // Surface and underground biomes' substrate is dirt
              //  else if (positionWS.y >= mapGenInfo.seaLevel - mapGenInfo.surfaceThickness)
              //  {
              //    blockTypeToSet = dirt;

              //    // Add stone blobs with increasing size as they get closer to caverns.
              //    if (TexelFetch3D(stoneInDirtImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < glm::mix(0.0f, 0.1f, alphaCaverns))
              //    {
              //      blockTypeToSet = voxel_t(1);
              //    }
              //    // Dithered fade from dirt to stone, beginning 1/3 from the underground-cavern transition point.
              //    else if (TexelFetch3D(fadeImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < alphaCaverns * 3 - 2)
              //    {
              //      blockTypeToSet = voxel_t(1);
              //    }
              //  }
              //  // Cavern biome substrate is stone
              //  else
              //  {
              //    blockTypeToSet = voxel_t(1);
              //  }
              }

              //if (blockTypeToSet != voxel_t::Air && positionWS.y < height - biomeInfo->GetSurfaceThickness())
              //{
              //  if (TexelFetch3D(copperImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < 0.0f)
              //  {
              //    blockTypeToSet = malachite;
              //  }
              //  else if (TexelFetch3D(leadImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < 0.0f)
              //  {
              //    blockTypeToSet = galena;
              //  }
              //}

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
  }

  if (true)
  {
    ZoneScopedN("Underground Biomes");
#ifndef GAME_HEADLESS
    progressText.store("Caves");
    progress.store(0);
#endif

    auto undergroundBiomes = std::array<std::unique_ptr<UndergroundBiomeNoise>, int(UndergroundBiome::COUNT)>();
    undergroundBiomes[int(UndergroundBiome::DesertCaves)] = std::make_unique<DesertCaves>(blocks.Get("sand"));
    undergroundBiomes[int(UndergroundBiome::FunkyCaves)] = std::make_unique<FunkyCaves>(blocks.Get("dirt"));
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

          auto biomeDensities = std::array<Image<3, float>, int(UndergroundBiome::COUNT)>();

          for (int m = 0; m < int(UndergroundBiome::COUNT); m++)
          {
            if (undergroundBiomes[m]->BroadPhase({i, j, k}))
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
                  weight * biomeDensities[m].TexelFetch(positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE);

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
              // ImageStore2D(globalSurfaceHeightImage, grid.topLevelBricksDims_.x * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, {positionWS.x, positionWS.y}, height);
              // ImageStore2D(globalSurfaceBiomeImage, grid.topLevelBricksDims_.x * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, {positionWS.x, positionWS.y}, biome);


              if (grid.GetVoxelAtUnchecked(positionWS) == placeholder)
              {
                if (density >= 0.0f)
                {
                  grid.SetVoxelAtUncheckedNoDirty(positionWS, voxel_t::Air);
                }
                else
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
      const auto biome = globalSurfaceBiomeImage.TexelFetch({x, z});
      const auto y = (int)globalSurfaceHeightImage.TexelFetch({x, z});
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
            Filter::Nearest);

          auto whiteImage = GenerateAndUpscale3D(whiteNoise2,
            tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 16,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

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
                    if (simplexImage.TexelFetch(tlLocal) + whiteImage.TexelFetch(tlLocal) * 0.3f < 0.05f)
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
                  if (belowBlock == dirt && whiteImage.TexelFetch(tlLocal) > 0.98f)
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
    const auto posY    = globalSurfaceHeightImage.TexelFetch(posXZ);
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
        Filter::Nearest);

      ForEachPositionInTLBrick(tl,
        [&](glm::ivec3 positionWS)
        {
          const auto pModTl = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
          const auto density = image.TexelFetch(pModTl);
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

          const auto surfaceHeight = int(globalSurfaceHeightImage.TexelFetch({posWS.x, posWS.z}));
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

          const auto surfaceHeight = int(globalSurfaceHeightImage.TexelFetch({posWS.x, posWS.z}));
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