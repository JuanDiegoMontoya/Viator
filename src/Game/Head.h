#pragma once
#include "ClassImplMacros.h"

#include <memory>
#include <span>

class BlockDefinition;
struct DeltaTime;
class World;
class Audio;

// A head implements windowing, input polling, and rendering, if applicable.
class Head
{
public:
  NO_COPY_NO_MOVE(Head);
  explicit Head() = default;
  virtual ~Head() = default;

  // Before FixedUpdate
  virtual void VariableUpdatePre(DeltaTime dt, World& world) = 0;

  // After FixedUpdate
  virtual void VariableUpdatePost(DeltaTime dt, World& world) = 0;

  virtual void CreateRenderingMaterials(std::span<const std::unique_ptr<BlockDefinition>>) {}

  virtual Audio* GetAudio() = 0;
};

// Implementation of Head that does nothing. Could be used for a "head"less server.
class NullHead final : public Head
{
public:
  void VariableUpdatePre(DeltaTime, World&) override;
  void VariableUpdatePost(DeltaTime, World&) override;
  Audio* GetAudio() override;
};
