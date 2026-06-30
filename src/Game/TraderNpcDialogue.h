#pragma once
#include "Crafting.h"

namespace Game2
{
  // This state should not be transmitted to clients, as clients
  // should be able to have parallel interactions with traders.
  struct TraderNpcDialogueState
  {
    enum class State
    {
      None,
      Greet,
      Trade,
    };

    State state = State::None;
  };

  namespace Comp
  {
    struct NPC {};

    struct HousingStatus
    {
      bool isHoused = false;
      float timeSinceHomeless = 0;
    };

    struct TraderNpcWares
    {
      Crafting crafting;
    };

    struct NpcBed {};
  } // namespace Comp
}