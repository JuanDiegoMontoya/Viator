#pragma once
#include "Core/ClassImplMacros.h"

#include <memory>
#include <string>
#include <span>

struct DeltaTime;
class World;
class Audio;

namespace Game2::Render
{
  struct Particle;
  struct ParticleArchetypeSpawnInfo;
  struct ParticleArchetype;
}

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

  virtual void CreateRenderingMaterials([[maybe_unused]] const World& world) {}

  virtual void RegisterParticleArchetype([[maybe_unused]] std::string name, [[maybe_unused]] const Game2::Render::ParticleArchetype& archetype) {}
  virtual void SpawnParticles([[maybe_unused]] std::span<const Game2::Render::Particle> particles) {}
  virtual void SpawnParticleArchetypes([[maybe_unused]] std::span<const Game2::Render::ParticleArchetypeSpawnInfo> archetypeSpawnInfos) {}

  virtual Audio* GetAudio() = 0;
};

// Implementation of Head that does nothing. Could be used for a "head"less server.
class NullHead final : public Head
{
public:
  NullHead();
  ~NullHead() override;
  void VariableUpdatePre(DeltaTime, World&) override;
  void VariableUpdatePost(DeltaTime, World&) override;
  Audio* GetAudio() override;

private:
  std::unique_ptr<Audio> audio_;
};
