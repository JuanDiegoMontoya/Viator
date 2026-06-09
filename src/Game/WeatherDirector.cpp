#include "WeatherDirector.h"

#include "Game.h"
#include "Globals.h"
#include "World.h"
#include "Core/Defer.h"
#include "Rendering/Particle.h"

#include "glm/common.hpp"

#include <algorithm>
#include <array>

namespace Weather
{
  namespace
  {
    [[nodiscard]] Settings LerpSettings(const Settings& a, const Settings& b, float t)
    {
      return {
        .cloudBottomAltitude = glm::mix(a.cloudBottomAltitude, b.cloudBottomAltitude, t),
        .cloudHeight         = glm::mix(a.cloudHeight, b.cloudHeight, t),
        .cloudCoverage       = glm::mix(a.cloudCoverage, b.cloudCoverage, t),
        .cloudDensity        = glm::mix(a.cloudDensity, b.cloudDensity, t),
        .cloudFrequency      = glm::mix(a.cloudFrequency, b.cloudFrequency, t),
        .windVelocity        = glm::mix(a.windVelocity, b.windVelocity, t),
        .rainDensity         = glm::mix(a.rainDensity, b.rainDensity, t),
      };
    }

    constexpr Settings sunnyMin = Settings{
      .cloudBottomAltitude = 500,
      .cloudHeight         = 320,
      .cloudCoverage       = 0.3f,
      .cloudDensity        = -0.04f,
      .cloudFrequency      = 1.0f / 400.0f,
      .windVelocity        = {},
      .rainDensity         = 0,
    };

    constexpr Settings sunnyMax = Settings{
      .cloudBottomAltitude = 500,
      .cloudHeight         = 520,
      .cloudCoverage       = 0.4f,
      .cloudDensity        = 0.02f,
      .cloudFrequency      = 1.0f / 600.0f,
      .windVelocity        = {},
      .rainDensity         = 0,
    };

    constexpr Settings lightOvercast = Settings{
      .cloudBottomAltitude = 500,
      .cloudHeight         = 450,
      .cloudCoverage       = 0.4f,
      .cloudDensity        = 0.4f,
      .cloudFrequency      = 1.0f / 500.0f,
      .windVelocity        = {},
      .rainDensity         = 0,
    };

    constexpr Settings heavyOvercast = Settings{
      .cloudBottomAltitude = 500,
      .cloudHeight         = 400,
      .cloudCoverage       = 0.8f,
      .cloudDensity        = 0.8f,
      .cloudFrequency      = 1.0f / 500.0f,
      .windVelocity        = {},
      .rainDensity         = 0,
    };

    constexpr Settings lightRain = Settings{
      .cloudBottomAltitude = 500,
      .cloudHeight         = 300,
      .cloudCoverage       = 0.4f,
      .cloudDensity        = 0.4f,
      .cloudFrequency      = 1.0f / 500.0f,
      .windVelocity        = {},
      .rainDensity         = 0.05f,
    };

    constexpr Settings heavyRainMin = Settings{
      .cloudBottomAltitude = 500,
      .cloudHeight         = 1200,
      .cloudCoverage       = 0.9f,
      .cloudDensity        = 1.0f,
      .cloudFrequency      = 1.0f / 400.0f,
      .windVelocity        = {-10, -10},
      .rainDensity         = 1,
    };

    constexpr Settings heavyRainMax = Settings{
      .cloudBottomAltitude = 500,
      .cloudHeight         = 1500,
      .cloudCoverage       = 1.0f,
      .cloudDensity        = 1.0f,
      .cloudFrequency      = 1.0f / 800.0f,
      .windVelocity        = {10, 10},
      .rainDensity         = 1,
    };

    struct WeatherSettingsInfo
    {
      float weight = 1;
      Settings min{};
      Settings max{};
    };

    constexpr std::array<WeatherSettingsInfo, (int)Preset::COUNT> presetSettings = []
    {
      auto settings = std::array<WeatherSettingsInfo, (int)Preset::COUNT>();
      settings[(int)Preset::Sunny] = {
        .weight = 1,
        .min    = sunnyMin,
        .max    = sunnyMax,
      };

      settings[(int)Preset::LightOvercast] = {
        .weight = 0.4f,
        .min    = lightOvercast,
        .max    = lightOvercast,
      };

      settings[(int)Preset::HeavyOvercast] = {
        .weight = 0.2f,
        .min    = heavyOvercast,
        .max    = heavyOvercast,
      };

      settings[(int)Preset::LightRain] = {
        .weight = 0.2f,
        .min    = lightRain,
        .max    = lightRain,
      };

      settings[(int)Preset::HeavyRain] = {
        .weight = 0.1f,
        .min    = heavyRainMin,
        .max    = heavyRainMax,
      };

      return settings;
    }();
  }

  void Director::Update(World& world, float dt, float timeOfDay)
  {
    // TODO: broadcast state update to clients
    const float alpha = glm::clamp(1 - glm::exp(-dt * transitionSpeed), 0.0f, 1.0f);
    currentState.settings = LerpSettings(currentState.settings, nextSettings, alpha);

    currentState.cloudTemporalOffset += dt;
    currentState.cloudHorizontalOffset += currentState.settings.windVelocity * dt;
    world.globals->head->SetWeather(currentState);

    auto& rng = world.globals->game->rng;

    constexpr uint32_t numParticlesToSpawnPerSecondNorm = 2'500'000;

    const uint32_t amountToSpawn = uint32_t((float)numParticlesToSpawnPerSecondNorm * currentState.settings.rainDensity * dt);
    if (amountToSpawn > 0)
    {
      const auto pos  = world.TryGetLocalPlayerTransform()->position;
      const auto info = Game2::Render::ParticleArchetypeSpawnInfo{
        .archetypeName = "rain",
        .count         = (int32_t)amountToSpawn,
        .positionWS    = pos,
        .velocity      = glm::vec3{currentState.settings.windVelocity.x, 0, currentState.settings.windVelocity.y},
      };
      world.globals->head->SpawnParticleArchetypes({&info, 1});
    }

    auto _ = Defer([&] { prevTimeOfDay = timeOfDay; });

    // Always change the weather at the beginning of the day
    if (prevTimeOfDay < 0.1f && timeOfDay >= 0.1f)
    {
      PickNewWeather(rng);
      return;
    }

    // Chance to change the weather at other times of the day
    if (prevTimeOfDay < 0.5f && timeOfDay >= 0.5f && rng.RandFloat() < 0.1f)
    {
      PickNewWeather(rng);
      return;
    }

    if (prevTimeOfDay < 1.0f && timeOfDay >= 1.0f && rng.RandFloat() < 0.1f)
    {
      PickNewWeather(rng);
      return;
    }

    if (prevTimeOfDay < 1.5f && timeOfDay >= 1.5f && rng.RandFloat() < 0.1f)
    {
      PickNewWeather(rng);
      return;
    }
  }

  void Director::PickNewWeather(PCG::Rng& rng)
  {
    const float sumWeights = std::ranges::fold_left(presetSettings, 0.0f, [](float sum, const auto& info) { return sum + info.weight; });
    const auto xi          = rng.RandFloat(0, sumWeights);
    float sum              = 0;
    const auto selected    = std::ranges::find_if(presetSettings, [&](const auto& info) { return (sum += info.weight) >= xi; });
    ASSERT(selected != presetSettings.end());
    SetWeatherToPreset((Preset)std::distance(selected, presetSettings.begin()), rng);
  }

  void Director::SetWeatherToPreset(Preset preset, PCG::Rng& rng)
  {
    const auto selected = presetSettings[(int)preset];
    nextSettings = Settings{
      .cloudBottomAltitude = glm::mix(selected.min.cloudBottomAltitude, selected.max.cloudBottomAltitude, rng.RandFloat()),
      .cloudHeight         = glm::mix(selected.min.cloudHeight, selected.max.cloudHeight, rng.RandFloat()),
      .cloudCoverage       = glm::mix(selected.min.cloudCoverage, selected.max.cloudCoverage, rng.RandFloat()),
      .cloudDensity        = glm::mix(selected.min.cloudDensity, selected.max.cloudDensity, rng.RandFloat()),
      .cloudFrequency      = glm::mix(selected.min.cloudFrequency, selected.max.cloudFrequency, rng.RandFloat()),
      .windVelocity        = glm::mix(selected.min.windVelocity, selected.max.windVelocity, rng.RandFloat()),
      .rainDensity         = glm::mix(selected.min.rainDensity, selected.max.rainDensity, rng.RandFloat()),
    };
  }

  void Director::SetCurrentAndNextWeather(const Settings& settings)
  {
    currentState.settings = settings;
    nextSettings          = settings;
  }

  void Director::SetNextWeather(const Settings& settings)
  {
    nextSettings = settings;
  }
}