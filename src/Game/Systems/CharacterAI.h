#pragma once

class World;

namespace Systems
{
  void UpdateInputForBirds(World& world, float dt);
  void UpdateInputForPathfindingCharacters(World& world, float dt);
}