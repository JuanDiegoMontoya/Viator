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

  struct TraderNpcWares
  {
    Crafting crafting;
  };
}