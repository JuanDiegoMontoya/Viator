#pragma once
#include "Core/ClassImplMacros.h"
#include "Game/Voxel/VoxelType.h"

#include <memory>

namespace Voxel
{
  struct Grid;
}

class TestGame
{
public:
  static std::unique_ptr<TestGame> Create();

  NO_COPY_NO_MOVE(TestGame);
  TestGame()                                    = default;
  virtual ~TestGame()                           = default;
  virtual void Tick(float dt)                   = 0;
  [[nodiscard]] virtual class World& GetWorld() = 0;
  [[nodiscard]] virtual Voxel::Grid& GetGrid()  = 0;

  // Common grid configurations.
  virtual void InitEmptyGrid(voxel_t voxel = voxel_t::Air) = 0;
  virtual void InitFlatGrid(voxel_t voxel = voxel_t(1))    = 0;
};
