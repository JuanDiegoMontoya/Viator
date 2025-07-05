#pragma once
#include "Game/Audio.h"

#include <unordered_map>
#include <vector>

typedef struct ma_engine ma_engine;
typedef struct ma_sound ma_sound;

class PlayerAudio : public Audio
{
public:
  explicit PlayerAudio();
  ~PlayerAudio() override;

  void UpdateListener(glm::vec3 position, glm::vec3 direction, glm::vec3 velocity) override;
  std::weak_ptr<SoundHandle> PlaySound(const Sound& sound) override;
  void FreeUnusedResources() override;

  void DrawDebugUI();

private:
  ma_engine* engine_;
  std::unordered_map<std::string, ma_sound*> soundPrototypes_;
  std::vector<std::shared_ptr<SoundHandle>> activeSounds_;
};