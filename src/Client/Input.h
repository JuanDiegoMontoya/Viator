#pragma once
#include "Core/ClassImplMacros.h"
#include "glm/vec2.hpp"

class World;
struct DeltaTime;

class InputSystem
{
public:
  NO_COPY_NO_MOVE(InputSystem);
  InputSystem(struct GLFWwindow* window);
  void VariableUpdatePre(DeltaTime dt, World& world, bool swapchainOk);

  void CursorPosCallback(double currentCursorX, double currentCursorY);
  void CursorEnterCallback(int entered);
  void ScrollCallback(double xOffset, double yOffset);

private:
  GLFWwindow* window_;
  glm::dvec2 cursorPos{};
  glm::dvec2 cursorFrameOffset{};
  glm::dvec2 scrollOffset{};
  bool cursorJustEnteredWindow = true;
};
