#pragma once
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <vector>

namespace Vox
{
  struct Voxel
  {
    uint8_t x, y, z;
    uint8_t colorIndex;
  };

  struct RGBA8
  {
    uint8_t r, g, b, a;
  };

  struct String
  {
    uint32_t sizeBytes;
    std::unique_ptr<char[]> data;
  };

  using Rotation = uint8_t;

  struct KeyValue
  {
    String key, value;
  };

  struct Dict
  {
    uint32_t count;
    std::unique_ptr<KeyValue[]> pairs;
  };

  struct Model
  {
    int32_t modelId;
    Dict attributes;
  };

  struct Chunk
  {
    virtual ~Chunk() = default;
    char id[4];
    uint32_t sizeBytes;
    uint32_t childrenBytes;
    std::vector<std::unique_ptr<Chunk>> children;
  };

  struct Chunk_MAIN : Chunk
  {
  };

  struct Chunk_PACK : Chunk
  {
    uint32_t numModels;
  };

  struct Chunk_SIZE : Chunk
  {
    uint32_t sizeX, sizeY, sizeZ;
  };

  struct Chunk_XYZI : Chunk
  {
    uint32_t numVoxels;
    std::unique_ptr<Voxel[]> voxels;
  };

  struct Chunk_RGBA : Chunk
  {
    RGBA8 colors[256];
  };

  struct Chunk_nTRN : Chunk
  {
    int32_t nodeId;
    Dict attributes;
    int32_t childNodeId;
    int32_t reservedMustBeNegative1;
    int32_t layerId;
    uint32_t numFrames;
    std::unique_ptr<Dict[]> frames;
  };

  struct Chunk_nGRP : Chunk
  {
    int32_t nodeId;
    Dict attributes;
    uint32_t numChildren;
    std::unique_ptr<int32_t[]> childrenIds;
  };

  struct Chunk_nSHP : Chunk
  {
    int32_t nodeId;
    Dict attributes;
    uint32_t numModels;
    std::unique_ptr<Model[]> models;
  };

  struct Chunk_MATL : Chunk
  {
    int32_t materialId;
    Dict attributes;
  };

  struct Chunk_LAYR : Chunk
  {
    int32_t layerId;
    Dict attributes;
    int32_t reservedMustBeNegative1;
  };

  struct Chunk_rOBJ : Chunk
  {
    Dict attributes;
  };

  struct Chunk_rCAM : Chunk
  {
    int32_t cameraId;
    Dict attributes;
  };

  struct Chunk_NOTE : Chunk
  {
    uint32_t numNames;
    std::unique_ptr<String[]> names;
  };

  struct Chunk_IMAP : Chunk
  {
    uint8_t paletteIndices[256];
  };

  struct Chunk_Unknown : Chunk
  {
    std::unique_ptr<uint8_t[]> data;
  };

  std::unique_ptr<Chunk> LoadFromMemory(std::istream& stream);
  std::unique_ptr<Chunk> LoadFromFile(const std::filesystem::path& path);

  struct ProcessedModel
  {
    const Chunk_RGBA* paletteChunk = nullptr;
    const Chunk_IMAP* iMapChunk    = nullptr;
    const Chunk_SIZE* sizeChunk    = nullptr;
    const Chunk_XYZI* voxelChunk   = nullptr;
    std::vector<const Chunk_MATL*> materials;
  };

  // Locates important (for our purposes) chunks in the model.
  ProcessedModel ProcessModel(const Chunk& root);

  struct VoxMaterialEmissionInfo
  {
    float emission; // [0, 1]. _emit
    float power; // [0, 4]. _flux
    float ldr; // [0, 1]. ???
  };
  std::optional<VoxMaterialEmissionInfo> ParseEmissionInfoFromDict(const Dict& attribs);

  struct VoxMaterialGlassInfo
  {
    float transparency; // [0, 1]. _trans
    float indexOfRefraction; // [1, 10]. _ri
    float density; // [0, 1]. _d
  };
  std::optional<VoxMaterialGlassInfo> ParseGlassInfoFromDict(const Dict& attribs);
} // namespace Vox
