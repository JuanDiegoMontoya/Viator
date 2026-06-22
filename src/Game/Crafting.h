#pragma once
#include "BlockFwd.h"
#include "ItemFwd.h"
#include "Voxel/VoxelType.h"

#include <optional>
#include <vector>
#include <string>

namespace Game2
{
  struct CraftingRecipe
  {
    std::vector<ItemIdAndCount> ingredients;
    std::vector<ItemIdAndCount> output;
    BlockId craftingStation = voxel_t(0);
    std::string name;
    std::string description;
  };

  struct Crafting
  {
    std::vector<CraftingRecipe> recipes;
    bool showUncraftableRecipes = true;
    std::optional<int> selectedRecipeIndex;
  };
} // namespace Game2