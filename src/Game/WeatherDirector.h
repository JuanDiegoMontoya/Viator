#pragma once
#include "glm/vec2.hpp"

namespace PCG
{
  struct Rng;
}

class World;

namespace Weather
{
  enum class Preset
  {
    Sunny,
    LightOvercast,
    HeavyOvercast,
    LightRain,
    HeavyRain,
    COUNT,
  };

  struct Settings
  {
    float cloudBottomAltitude{500};
    float cloudHeight{200};
    float cloudCoverage{};
    float cloudDensity{};
    float cloudFrequency{};
    glm::vec2 windVelocity{};

    float rainDensity{};
  };

  struct State
  {
    Settings settings;
    glm::vec2 cloudHorizontalOffset{};
    float cloudTemporalOffset{};
  };

  class Director
  {
  public:
    void Update(World& world, float dt, float timeOfDay);

    void PickNewWeather(PCG::Rng& rng);
    void SetWeatherToPreset(Preset preset, PCG::Rng& rng);

    void SetCurrentAndNextWeather(const Settings& settings);
    void SetNextWeather(const Settings& settings);

    // Public for serialization
    float prevTimeOfDay = 1;
    float transitionSpeed = 0.2f;
    State currentState{};
    Settings nextSettings{};
  };
} // namespace Weather