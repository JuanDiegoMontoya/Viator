#include "UndergroundBiome.h"
#include "Core/Image.h"
#include "Game/Voxel/Grid.h"
#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Globals.h"
#include "Game/World/WorldGenHelpers.h"
#include "Game/Prefab.h"
#include "PCG.h"

#include "FastNoise/FastNoise.h"
#include "tracy/Tracy.hpp"

class SurfaceCaves final : public UndergroundBiomeNoise
{
public:
  explicit SurfaceCaves(const CreateInfo& createInfo) : UndergroundBiomeNoise(createInfo)
  {
    surfaceCaves->SetSource(surfaceCavesA);
    surfaceCaves->SetScaling(1.5f);
  }

  float GetWeight([[maybe_unused]] glm::ivec3 posWS) override
  {
    PANIC;
  }

  Core::DSP::Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, const MapGenInfo& mapGenInfo) override
  {
    ZoneScoped;
    return WorldGen::GenerateAndUpscale3D(surfaceCaves,
      posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      mapGenInfo.seed,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Linear);
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

  Core::DSP::Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, [[maybe_unused]] const MapGenInfo& mapGenInfo) override
  {
    ZoneScoped;
    return WorldGen::GenerateAndUpscale3D(noise,
      posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      mapGenInfo.seed,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Linear);
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

  Core::DSP::Image<3, float> GenImageForChunk(glm::ivec3 posTL, [[maybe_unused]] glm::ivec3 dimsTL, [[maybe_unused]] const MapGenInfo& mapGenInfo) override
  {
    ZoneScoped;
    const auto dims = glm::ivec3{Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE};
    auto density    = Core::DSP::Image<3, float>(dims);

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
  explicit UndergroundCorruption(const CreateInfo& createInfo) : UndergroundBiomeNoise(createInfo)
  {
    shaftOffset->SetSource(shaftGenerator);
    shaftOffset->SetOffset<FastNoise::Dim::Y>(80 - 400); // Magic number = shaft depth - sea level

    combiner->SetLHS(densityGenerator);
    combiner->SetRHS(shaftOffset);
  }

  Core::DSP::Image<3, float> GenImageForChunk([[maybe_unused]] glm::ivec3 posTL,
    [[maybe_unused]] glm::ivec3 dimsTL,
    [[maybe_unused]] const MapGenInfo& mapGenInfo) override
  {
    ZoneScoped;

    return WorldGen:: GenerateAndUpscale3D(combiner,
      posTL * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      mapGenInfo.seed - 100,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
      Core::DSP::Filter::Nearest);
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

std::array<std::unique_ptr<UndergroundBiomeNoise>, int(UndergroundBiome::COUNT)> GetUndergroundBiomeNoises(const World& world)
{
  auto undergroundBiomes = std::array<std::unique_ptr<UndergroundBiomeNoise>, int(UndergroundBiome::COUNT)>();

  const auto& blocks = world.globals->blockRegistry;

  // undergroundBiomes[int(UndergroundBiome::DesertCaves)] = std::make_unique<DesertCaves>(blocks->Get("sand"));
  undergroundBiomes[int(UndergroundBiome::FunkyCaves)]   = std::make_unique<FunkyCaves>(UndergroundBiomeNoise::CreateInfo{&world, blocks->Get("dirt")});
  undergroundBiomes[int(UndergroundBiome::Corruption)] =
    std::make_unique<UndergroundCorruption>(UndergroundBiomeNoise::CreateInfo{&world, blocks->Get("stone_corrupt")});
  undergroundBiomes[int(UndergroundBiome::SurfaceCaves)] = std::make_unique<SurfaceCaves>(UndergroundBiomeNoise::CreateInfo{&world, blocks->Get("stone")});

  return undergroundBiomes;
}

UndergroundBiome GetUndergroundBiomeAtPosition(const std::array<std::unique_ptr<UndergroundBiomeNoise>, int(UndergroundBiome::COUNT)>& biomes,
  glm::ivec3 positionWS,
  const std::function<void(UndergroundBiome, float)>& callback)
{
  const static auto shrimplex2 = []
  {
    auto noise = FastNoise::New<FastNoise::Simplex>();
    noise->SetScale(8);
    noise->SetOutputMin(0);
    return noise;
  }();

  auto biomeWeights   = std::array<float, int(UndergroundBiome::COUNT)>();
  auto biome          = UndergroundBiome(uint32_t(UndergroundBiome::COUNT) - 1);
  auto maxBiomeWeight = 0.0125f;
  auto sumWeights     = 0.0f;

  for (int m = 0; m < int(UndergroundBiome::COUNT); m++)
  {
    // Skip biomes that failed broadphase check.
    if (biomes[m] == nullptr)
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
      weight = biomes[m]->GetWeight(positionWS);
    }

    biomeWeights[m] = weight;
    sumWeights += weight;
    if (callback)
    {
      callback(UndergroundBiome(m), weight);
    }

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

  return biome;
}