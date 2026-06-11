#pragma once
#include "Game/CVar.h"

namespace Gui
{
  void CVarFloat(Game2::AutoCVar<Game2::cvar_float>& cvar);

  void CVarFloatCheckbox(Game2::AutoCVar<Game2::cvar_float>& cvar);

  void CVarVec3(Game2::AutoCVar<Game2::cvar_vec3>& cvar);

  void CVarString(Game2::AutoCVar<Game2::cvar_string>& cvar);
}