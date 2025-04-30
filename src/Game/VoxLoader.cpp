#include "VoxLoader.h"

#include "spdlog/spdlog.h"

#include <exception>
#include <fstream>
#include <type_traits>
#include <utility>

namespace
{
  using namespace Vox;

  struct Header
  {
    char magic[4];
    uint32_t version;
  };

  template<typename T>
  void Read(std::istream& stream, T& v)
    requires std::is_trivially_copyable_v<T>
  {
    stream.read(reinterpret_cast<char*>(&v), sizeof(v));
  }

  String ReadString(std::istream& stream)
  {
    String s;
    Read(stream, s.sizeBytes);
    s.data = std::make_unique<char[]>(s.sizeBytes);
    stream.read(s.data.get(), s.sizeBytes);
    return s;
  }

  Dict ReadDict(std::istream& stream)
  {
    Dict d;
    Read(stream, d.count);
    d.pairs = std::make_unique<KeyValue[]>(d.count);
    for (uint32_t i = 0; i < d.count; i++)
    {
      d.pairs[i].key = ReadString(stream);
      d.pairs[i].value = ReadString(stream);
    }
    return d;
  }

  struct VoxLoadContext
  {
    // Warnings
    uint32_t unknownChunks = 0;
  };

  std::unique_ptr<Chunk> LoadChunk(std::istream& stream, VoxLoadContext& context)
  {
    auto common = Chunk{};
    Read(stream, common.id);
    Read(stream, common.sizeBytes);
    Read(stream, common.childrenBytes);

    auto chunk = std::unique_ptr<Chunk>();
    if (std::memcmp(common.id, "MAIN", 4) == 0)
    {
      chunk.reset(new Chunk_MAIN);
    }
    else if (std::memcmp(common.id, "PACK", 4) == 0)
    {
      auto c = std::make_unique<Chunk_PACK>();
      Read(stream, c->numModels);
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "SIZE", 4) == 0)
    {
      auto c = std::make_unique<Chunk_SIZE>();
      Read(stream, c->sizeX);
      Read(stream, c->sizeY);
      Read(stream, c->sizeZ);
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "XYZI", 4) == 0)
    {
      auto c = std::make_unique<Chunk_XYZI>();
      Read(stream, c->numVoxels);
      c->voxels = std::make_unique<Voxel[]>(c->numVoxels);
      stream.read(reinterpret_cast<char*>(c->voxels.get()), c->numVoxels * sizeof(Voxel));
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "RGBA", 4) == 0)
    {
      auto c = std::make_unique<Chunk_RGBA>();
      Read(stream, c->colors);
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "nTRN", 4) == 0)
    {
      auto c = std::make_unique<Chunk_nTRN>();
      Read(stream, c->nodeId);
      c->attributes = ReadDict(stream);
      Read(stream, c->childNodeId);
      Read(stream, c->reservedMustBeNegative1);
      Read(stream, c->layerId);
      Read(stream, c->numFrames);
      c->frames = std::make_unique<Dict[]>(c->numFrames);
      for (uint32_t i = 0; i < c->numFrames; i++)
      {
        c->frames[i] = ReadDict(stream);
      }
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "nGRP", 4) == 0)
    {
      auto c = std::make_unique<Chunk_nGRP>();
      Read(stream, c->nodeId);
      c->attributes = ReadDict(stream);
      Read(stream, c->numChildren);
      c->childrenIds = std::make_unique<int32_t[]>(c->numChildren);
      stream.read(reinterpret_cast<char*>(c->childrenIds.get()), c->numChildren * sizeof(c->childrenIds[0]));
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "nSHP", 4) == 0)
    {
      auto c = std::make_unique<Chunk_nSHP>();
      Read(stream, c->nodeId);
      c->attributes = ReadDict(stream);
      Read(stream, c->numModels);
      c->models = std::make_unique<Model[]>(c->numModels);
      for (uint32_t i = 0; i < c->numModels; i++)
      {
        Read(stream, c->models[i].modelId);
        c->models[i].attributes = ReadDict(stream);
      }
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "MATL", 4) == 0)
    {
      auto c = std::make_unique<Chunk_MATL>();
      Read(stream, c->materialId);
      c->attributes = ReadDict(stream);
      chunk         = std::move(c);
    }
    else if (std::memcmp(common.id, "LAYR", 4) == 0)
    {
      auto c = std::make_unique<Chunk_LAYR>();
      Read(stream, c->layerId);
      c->attributes = ReadDict(stream);
      Read(stream, c->reservedMustBeNegative1);
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "rOBJ", 4) == 0)
    {
      auto c        = std::make_unique<Chunk_rOBJ>();
      c->attributes = ReadDict(stream);
      chunk         = std::move(c);
    }
    else if (std::memcmp(common.id, "rCAM", 4) == 0)
    {
      auto c = std::make_unique<Chunk_rCAM>();
      Read(stream, c->cameraId);
      c->attributes = ReadDict(stream);
      chunk         = std::move(c);
    }
    else if (std::memcmp(common.id, "NOTE", 4) == 0)
    {
      auto c = std::make_unique<Chunk_NOTE>();
      Read(stream, c->numNames);
      c->names = std::make_unique<String[]>(c->numNames);
      for (uint32_t i = 0; i < c->numNames; i++)
      {
        c->names[i] = ReadString(stream);
      }
      chunk = std::move(c);
    }
    else if (std::memcmp(common.id, "IMAP", 4) == 0)
    {
      auto c = std::make_unique<Chunk_IMAP>();
      Read(stream, c->paletteIndices);
      chunk = std::move(c);
    }
    else
    {
      context.unknownChunks++;
      auto c = std::make_unique<Chunk_Unknown>();
      c->data = std::make_unique<uint8_t[]>(chunk->sizeBytes);
      stream.read(reinterpret_cast<char*>(c->data.get()), chunk->sizeBytes);
      chunk = std::move(c);
    }

    std::memcpy(chunk->id, common.id, sizeof(common.id));
    chunk->sizeBytes = common.sizeBytes;
    chunk->childrenBytes = common.childrenBytes;

    auto cursorPosBeforeChildren = stream.tellg();
    while (stream.tellg() - cursorPosBeforeChildren < common.childrenBytes)
    {
      chunk->children.emplace_back(LoadChunk(stream, context));
    }

    return chunk;
  }
} // namespace

namespace Vox
{
  std::unique_ptr<Chunk> LoadFromMemory(std::istream& stream)
  {
    auto header = Header{};
    Read(stream, header);
    if (std::memcmp(header.magic, "VOX ", 4) != 0)
    {
      throw std::runtime_error("Invalid magic");
    }
    if (header.version != 200)
    {
      spdlog::warn("Voxel model is unrecognized version {} (supported version: 200). Unexpected behavior may ensue.", header.version);
    }
    auto context = VoxLoadContext{};
    auto result  = LoadChunk(stream, context);
    if (context.unknownChunks > 0)
    {
      spdlog::warn("Voxel model contained {} unknown chunks.");
    }
    return result;
  }

  std::unique_ptr<Chunk> LoadFromFile(const std::filesystem::path& path)
  {
    spdlog::debug("Loading voxel model {} from file.", path.stem());
    auto file = std::ifstream(path, std::fstream::binary | std::fstream::in);
    if (!file)
    {
      throw std::runtime_error("Failed to open file: " + path.string());
    }
    return LoadFromMemory(file);
  }
} // namespace Vox
