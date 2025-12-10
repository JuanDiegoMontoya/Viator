#pragma once
#include "Core/ClassImplMacros.h"
#include "Game/IncompleteTypeDeleter.h"

//// TEMP
#define GLM_ENABLE_EXPERIMENTAL
#include "Client/debug/Shapes.h"
#include "Core/Container/RingBuffer.h"
#include <atomic>
#include <glm/gtx/hash.hpp>
#include "Physics/Physics.h"
using WaterQueue = RingBuffer<glm::ivec3>;
using WaterSet   = std::unordered_set<glm::ivec3>;
//// END TEMP

namespace Networking
{
  class Interface;
}
class Head;
struct GameGlobals;
class Scripting;
class PrefabRegistry;

namespace Voxel
{
  struct Grid;
}

namespace Block
{
  class Registry;
}

namespace Item
{
  class Registry;
}

namespace Core::DSP
{
  template<size_t Dim, typename>
    requires(Dim >= 1 && Dim <= 3)
  class Image;
}

class EntityPrefabRegistry;

namespace Physics
{
  class Engine;
}

struct WorldGlobals
{
private:
  template<typename T>
  using unique_ptr = std::unique_ptr<T, IncompleteTypeDeleter<T>>;

public:
  WorldGlobals();
  DEFAULT_MOVE(WorldGlobals);
  std::string worldName;
  unique_ptr<Voxel::Grid> grid;
  unique_ptr<PrefabRegistry> prefabRegistry;
  unique_ptr<Block::Registry> blockRegistry;
  unique_ptr<Item::Registry> itemRegistry;
  unique_ptr<EntityPrefabRegistry> entityPrefabRegistry;
  unique_ptr<GameGlobals> game;
  unique_ptr<WaterQueue> waterQueue;
  unique_ptr<WaterSet> waterSet;
#ifndef GAME_HEADLESS
  std::vector<Debug::Line> debugLines;
#endif

  std::unique_ptr<std::atomic<const char*>> progressText;
  std::unique_ptr<std::atomic_int32_t> progress;
  std::unique_ptr<std::atomic_int32_t> total;

  Head* head;
  std::unique_ptr<Networking::Interface>* networking = nullptr;
  Scripting* scripting = nullptr;

  std::unique_ptr<Physics::Engine> physics;
  unique_ptr<Core::DSP::Image<2, float>> globalSurfaceHeight;
  unique_ptr<Core::DSP::Image<2, float>> globalSurfaceFog;
  unique_ptr<Core::DSP::Image<3, float>> globalFog;
  bool globalFogNeedsUpdate = false;
};