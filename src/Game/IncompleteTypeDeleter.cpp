#include "Game/IncompleteTypeDeleter.h"

#include "Game/Globals.h"
#include "Game/Game.h"
#include "Game/Voxel/Grid.h"
#include "Game/Prefab.h"
#include "Game/EntityPrefab.h"
#include "Game/Block.h"
#include "Game/Item.h"
#include "Game/Physics/Physics.h"
#include "Core/Image.h"
#include "Game/HashGrid.h"
#include "Game/Pathfinding.h"
#include "Game/World.h"

template<typename T>
void IncompleteTypeDeleter<T>::operator()(T* p) const
{
  delete p;
}

template struct IncompleteTypeDeleter<WorldGlobals>;
template struct IncompleteTypeDeleter<Voxel::Grid>;
template struct IncompleteTypeDeleter<PrefabRegistry>;
template struct IncompleteTypeDeleter<Block::Registry>;
template struct IncompleteTypeDeleter<Item::Registry>;
template struct IncompleteTypeDeleter<EntityPrefabRegistry>;
template struct IncompleteTypeDeleter<GameGlobals>;
template struct IncompleteTypeDeleter<WaterQueue>;
template struct IncompleteTypeDeleter<WaterSet>;
template struct IncompleteTypeDeleter<Core::DSP::Image<2, float>>;
template struct IncompleteTypeDeleter<Physics::Engine>;