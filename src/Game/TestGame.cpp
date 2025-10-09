#include "TestGame.h"
#include "Game/Game.h"
#include "Game/World.h"
#include "Game/Voxel/Grid.h"

// TODO: TEMP
#include "Physics/Physics.h"

#include "tracy/Tracy.hpp"

#include <memory>

class TestGameImpl final : public TestGame
{
public:
  NO_COPY_NO_MOVE(TestGameImpl);

  TestGameImpl() : head_(std::make_unique<NullHead>()), game_({.head = head_.get()})
  {
    ZoneScoped;
    // Initialize the grid with one chunk and two materials (air = 0, solid = 1).
    auto& grid = game_.world_->GetRegistry().ctx().emplace<Voxel::Grid>(glm::ivec3(1, 1, 1), 5'000'000, false);
    grid.SetMaterialArray({Voxel::Grid::Material{.isVisible = false, .isSolid = false}, {.isVisible = true, .isSolid = true}});

    auto& registry                  = game_.world_->GetRegistry();
    registry.ctx().get<GameState>() = GameState::GAME;

    // TODO: remove
    auto ve                          = registry.create();
    registry.emplace<Name>(ve).name = "Voxels";
    registry.emplace<VoxelsComponent>(ve);
    registry.emplace<Physics::RigidBodySettings>(ve,
      Physics::RigidBodySettings{
        .shape      = {Physics::UseTwoLevelGrid{}},
        .activate   = false,
        .motionType = JPH::EMotionType::Static,
        .layer      = Physics::Layers::WORLD,
      });
  }

  ~TestGameImpl() override = default;

  void Tick(float dt) override
  {
    game_.Tick(dt);
  }

  World& GetWorld() override
  {
    ASSERT(game_.world_);
    return *game_.world_;
  }

  Voxel::Grid& GetGrid() override
  {
    ASSERT(game_.world_);
    auto* grid = game_.world_->GetRegistry().ctx().find<Voxel::Grid>();
    ASSERT(grid);
    return *grid;
  }

  void InitEmptyGrid(voxel_t voxel) override
  {
    ZoneScoped;
    auto& grid = GetGrid();
    for (int z = 0; z < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; z++)
    for (int y = 0; y < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; y++)
    for (int x = 0; x < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; x++)
    {
      grid.SetVoxelAtUncheckedNoDirty({x, y, z}, voxel);
    }
  }

  void InitFlatGrid(voxel_t voxel) override
  {
    ZoneScoped;
    InitEmptyGrid(voxel_t::Air);

    auto& grid = GetGrid();
    for (int z = 0; z < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; z++)
    for (int x = 0; x < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; x++)
    {
      grid.SetVoxelAtUncheckedNoDirty({x, 0, z}, voxel);
    }
  }

private:
  std::unique_ptr<Head> head_;
  Game game_;
};

std::unique_ptr<TestGame> TestGame::Create()
{
  return std::make_unique<TestGameImpl>();
}