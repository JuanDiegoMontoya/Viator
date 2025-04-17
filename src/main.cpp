#include <cstring>
#include <iostream>
#include <stdexcept>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "Game/Game.h"

int main()
{
  auto game = Game(30);
  game.Run();

  return 0;
}
