#pragma once
#include "ClassImplMacros.h"

#include "glm/vec3.hpp"

#include <optional>
#include <string>
#include <memory>

class Audio
{
public:
  NO_COPY_NO_MOVE(Audio);
  explicit Audio() = default;
  virtual ~Audio() = default;

  struct Sound
  {
    struct DelayInfo
    {
      float delay = 0.01f;
      float decay = 0.01f;
    };

    std::string name;
    float pitch = 1; // Pitch factor.
    float delay = 0;
    bool highlander = false; // THERE CAN BE ONLY ONE
    std::optional<glm::vec3> position; // nullopt = no spatialization
    std::optional<glm::vec3> velocity; // nullopt = no doppler
    std::optional<DelayInfo> delayInfo; // Reverb-ish effect
  };

  struct SoundHandle
  {
    explicit SoundHandle() = default;
    NO_COPY_NO_MOVE(SoundHandle);
    virtual ~SoundHandle() = default;
    virtual void SetPosition(glm::vec3 position) = 0;
    virtual void SetVelocity(glm::vec3 velocity) = 0;
  };

  virtual void UpdateListener(glm::vec3 position, glm::vec3 direction, glm::vec3 velocity) = 0;
  virtual std::weak_ptr<SoundHandle> PlaySound(const Sound& sound) = 0;

  // Garbage collection: runs every
  virtual void FreeUnusedResources() = 0;
};

class NullAudio : public Audio
{
public:
  void UpdateListener(glm::vec3 position, glm::vec3 direction, glm::vec3 velocity) override;
  std::weak_ptr<Audio::SoundHandle> PlaySound(const Sound& sound) override;
  void FreeUnusedResources() override;
};

struct SoundEmitter
{
  std::weak_ptr<Audio::SoundHandle> handle;
};
