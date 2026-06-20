#pragma once
#include "glm/fwd.hpp"

#include <expected>

class World;

namespace Game2
{
  class NpcDirector
  {
  public:

    void Update(float dt);

    struct HousingParams
    {
      int minWidth = 3;
      int maxWidth = 10;

      int minHeight = 2;
      int maxHeight = 5;
    };

    [[nodiscard]] static bool CheckIsValidHousing(World& world, glm::ivec3 originalPos, const HousingParams& params);
  };
}