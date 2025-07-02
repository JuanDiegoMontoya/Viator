#include "Audio.h"

void NullAudio::UpdateListener(glm::vec3, glm::vec3, glm::vec3) {}
std::weak_ptr<Audio::SoundHandle> NullAudio::PlaySound(const Sound&)
{
  return {};
}
void NullAudio::FreeUnusedResources() {}
