#include "Game/IncompleteTypeDeleter.h"

#include "Game/Commands.h"
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
#include "Game/WeatherDirector.h"
#include "Game/Globals.h"

template<typename T>
void IncompleteTypeDeleter<T>::operator()(T* p) const
{
  delete p;
}

template struct IncompleteTypeDeleter<WorldGlobals>;