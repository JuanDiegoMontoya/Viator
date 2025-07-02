#include "PlayerAudio.h"
#include "Game/Assets.h"

#include "miniaudio.h"

#include "tracy/Tracy.hpp"
#include "spdlog/spdlog.h"

namespace
{
  struct Node
  {
    NO_COPY_NO_MOVE(Node);
    explicit Node() = default;
    virtual ~Node() = default;
  };

  struct DelayNode : Node
  {
    explicit DelayNode(ma_engine* engine, float delay, float decay)
    {
      const auto sampleRate = ma_engine_get_sample_rate(engine);
      const auto nodeConfig = ma_delay_node_config_init(ma_engine_get_channels(engine), sampleRate, ma_uint32(sampleRate * delay), decay);

      node = new ma_delay_node();
      if (ma_delay_node_init(ma_engine_get_node_graph(engine), &nodeConfig, nullptr, node) != MA_SUCCESS)
      {
        throw std::runtime_error("Failed to initialize delay node");
      }
      ma_node_attach_output_bus(node, 0, ma_engine_get_endpoint(engine), 0);
    }

    ~DelayNode() override
    {
      ma_delay_node_uninit(node, nullptr);
      delete node;
    }

    ma_delay_node* node;
  };

  struct PSoundHandle : Audio::SoundHandle
  {
    explicit PSoundHandle()
    {
      sound = new ma_sound();
    }

    ~PSoundHandle() override
    {
      ma_sound_uninit(sound);
      delete sound;
    }

    void SetPosition(glm::vec3 position) override
    {
      ma_sound_set_position(sound, position.x, position.y, position.z);
    }

    void SetVelocity(glm::vec3 velocity) override
    {
      ma_sound_set_velocity(sound, velocity.x, velocity.y, velocity.z);
    }

    Audio::Sound createInfo;
    ma_sound* sound{};
    std::vector<std::unique_ptr<Node>> nodes;
  };
}

PlayerAudio::PlayerAudio()
{
  ZoneScoped;
  spdlog::info("Initializing audio engine.");

  engine_ = new ma_engine();
  if (ma_engine_init(nullptr, engine_) != MA_SUCCESS)
  {
    throw std::runtime_error("Failed to initialize audio engine");
  }

  ma_engine_set_volume(engine_, 0.5f);
  ma_engine_listener_set_world_up(engine_, 0, 0, 1, 0);

  const auto soundsToLoad = std::vector<std::pair<std::string, std::string>>{
    {"coin", "coin.wav"},
    {"shot", "good2.wav"},
  };

  for (const auto& [name, path] : soundsToLoad)
  {
    auto* sound = new ma_sound();
    if (ma_sound_init_from_file(engine_, (GetAudioDirectory() / path).string().c_str(), 0, nullptr, nullptr, sound) != MA_SUCCESS)
    {
      throw std::runtime_error("Failed to initialize sound");
    }
    soundPrototypes_.emplace(name, sound); 
  }
}

PlayerAudio::~PlayerAudio()
{
  ZoneScoped;
  spdlog::info("Terminating audio engine.");

  activeSounds_.clear();

  for (auto& [_, sound] : soundPrototypes_)
  {
    ma_sound_uninit(sound);
    delete sound;
  }

  ma_engine_uninit(engine_);
  delete engine_;
}

void PlayerAudio::UpdateListener(glm::vec3 position, glm::vec3 direction, glm::vec3 velocity)
{
  ma_engine_listener_set_position(engine_, 0, position.x, position.y, position.z);
  ma_engine_listener_set_direction(engine_, 0, direction.x, direction.y, direction.z);
  ma_engine_listener_set_velocity(engine_, 0, velocity.x, velocity.y, velocity.z);
}

std::weak_ptr<Audio::SoundHandle> PlayerAudio::PlaySound(const Sound& sound)
{
  ZoneScoped;
  spdlog::debug("Playing sound {}.", sound.name);

  if (sound.highlander)
  {
    std::erase_if(activeSounds_,
      [&](const std::shared_ptr<Audio::SoundHandle>& sh)
      {
        auto* handle = static_cast<PSoundHandle*>(sh.get());
        return handle->createInfo.highlander && handle->createInfo.name == sound.name;
      });
  }

  auto handle = std::make_shared<PSoundHandle>();
  handle->createInfo = sound;
  
  if (ma_sound_init_copy(engine_, soundPrototypes_.at(sound.name), !sound.position ? MA_SOUND_FLAG_NO_SPATIALIZATION : 0, nullptr, handle->sound) != MA_SUCCESS)
  {
    throw std::runtime_error("Failed to copy sound");
  }

  ma_sound_set_pitch(handle->sound, sound.pitch);

  if (sound.position)
  {
    ma_sound_set_position(handle->sound, sound.position->x, sound.position->y, sound.position->z);
  }
  
  if (sound.position && !sound.velocity)
  {
    ma_sound_set_doppler_factor(handle->sound, 0);
  }

  if (sound.delayInfo)
  {
    auto delayNode = std::make_unique<DelayNode>(engine_, sound.delayInfo->delay, sound.delayInfo->decay);
    handle->nodes.push_back(std::move(delayNode));
    ma_node_attach_output_bus(handle->sound, 0, delayNode.get(), 0);
  }

  if (sound.delay > 0)
  {
    ma_sound_set_start_time_in_milliseconds(handle->sound, ma_engine_get_time_in_milliseconds(engine_) + int(sound.delay * 1000));
  }

  if (ma_sound_start(handle->sound) != MA_SUCCESS)
  {
    throw std::runtime_error("Failed to start sound");
  }
  
  activeSounds_.push_back(handle);

  return handle;
}

void PlayerAudio::FreeUnusedResources()
{
  ZoneScoped;
  std::erase_if(activeSounds_,
    [](const std::shared_ptr<Audio::SoundHandle>& sh)
    {
      auto* handle = static_cast<PSoundHandle*>(sh.get());
      if (ma_sound_at_end(handle->sound))
      {
        return true;
      }
      return false;
    });
}
