#include "Globals.h"
#include "Game/Game.h"
#include "Game/Commands.h"
#include "Game/Voxel/Grid.h"
#include "Game/Prefab.h"
#include "Game/EntityPrefab.h"
#include "Game/Block.h"
#include "Game/Item.h"
#include "Game/Physics/Physics.h"
#include "Core/Image.h"

WorldGlobals::WorldGlobals()
{
  commandRegistry.reset(new Game2::CommandRegistry());
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
  globalFog.reset(new Core::DSP::Image<3, float>());
  surfaceBiomes.reset(new typename decltype(surfaceBiomes)::element_type());
  undergroundBiomes.reset(new typename decltype(undergroundBiomes)::element_type());
}