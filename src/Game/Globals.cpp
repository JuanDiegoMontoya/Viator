#include "Globals.h"
#include "Game/Game.h"
#include "Game/Voxel/Grid.h"
#include "Game/Prefab.h"
#include "Game/EntityPrefab.h"
#include "Game/Block.h"
#include "Game/Item.h"
#include "Game/Physics/Physics.h"
#include "Core/Image.h"

WorldGlobals::WorldGlobals()
{
  grid.reset(new Voxel::Grid());
  prefabRegistry.reset(new PrefabRegistry());
  blockRegistry.reset(new Block::Registry());
  itemRegistry.reset(new Item::Registry());
  entityPrefabRegistry.reset(new EntityPrefabRegistry());
  game.reset(new GameGlobals());
  waterQueue.reset(new WaterQueue());
  waterSet.reset(new WaterSet());
  globalSurfaceHeight.reset(new Core::DSP::Image<2, float>());
  globalSurfaceFog.reset(new Core::DSP::Image<2, float>());
}