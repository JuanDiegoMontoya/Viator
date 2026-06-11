#include "Client/VoxelRenderer.h"
#include "Client/Gui/CVarWidgets.h"
#include "Game/World.h"

#include "imgui.h"
#include "Client/ImGui/imgui_impl_fvog.h"
#include "Game/Game.h"
#include "Game/Globals.h"
#include "Game/Rendering/Particle.h"

void VoxelRenderer::ShowGraphicsWindow(World& world)
{
  if (ImGui::Begin("Giraffics", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
  {
    if (ImGui::Button("Recompile all shaders"))
    {
      for (auto& shaderModule : GetPipelineManager().GetShaderModules())
      {
        GetPipelineManager().EnqueueRecompileShader(shaderModule->info);
      }
    }

    ImGui::Checkbox("Clear GPU Primitives", &debugClearGpuPrimtives);

    if (ImGui::BeginTabBar("Options"))
    {
      if (ImGui::BeginTabItem("Atmosphere"))
      {
        if (ImGui::CollapsingHeader("Fog"))
        {
          bool enabled = !debugDisableFog;
          ImGui::Checkbox("Enable fog", &enabled);
          debugDisableFog = !enabled;
          ImGui::SeparatorText("Fog self-shadowing");
          ImGui::SliderInt("Steps", &sunSelfShadowSteps, 0, 30, sunSelfShadowSteps > 0 ? "%d" : "%d (disabled)");
          ImGui::SliderFloat("Ray distance", &sunSelfShadowDist, 5, 300, "%.1f");
        }

        if (ImGui::CollapsingHeader("Sun appearance"))
        {
          ImGui::ColorEdit3("Color##sun", &sunColor[0], ImGuiColorEditFlags_Float);
          ImGui::SliderFloat("Brightness##sun", &sunBrightness, 0, 110'000, "%.1f", ImGuiSliderFlags_Logarithmic);
        }

        if (ImGui::CollapsingHeader("Sky parameters"))
        {
          static bool paramsChanged = true;
          if (ImGui::Button("Reset##sky_parameters"))
          {
            skyParameters = InitSkyConfig();
            paramsChanged = true;
          }

          paramsChanged |= ImGui::DragFloat3("mie scatter", &skyParameters.mie_scattering[0], 0.001f, 0, 0.1f, "%.5f", ImGuiSliderFlags_NoRoundToFormat);
          paramsChanged |= ImGui::DragFloat3("rayleigh scatter", &skyParameters.rayleigh_scattering[0], 0.001f, 0, 0.1f, "%.5f", ImGuiSliderFlags_NoRoundToFormat);

          auto DensityProfileUI = [&](const char* id, DensityProfileLayer& layer)
          {
            ImGui::PushID(id);
            if (ImGui::TreeNode(id))
            {
              ImGui::DragFloat("Const term", &layer.const_term, 0.01f, 0, 0, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
              ImGui::DragFloat("Exp scale", &layer.exp_scale, 0.01f, 0, 0, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
              ImGui::DragFloat("Exp term", &layer.exp_term, 0.01f, 0, 0, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
              ImGui::DragFloat("Layer width", &layer.layer_width, 0.01f, 0, 0, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
              ImGui::DragFloat("Linear term", &layer.lin_term, 0.01f, 0, 0, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
              ImGui::TreePop();
            }
            ImGui::PopID();
          };

          DensityProfileUI("Mie 0", skyParameters.mie_density[0]);
          // DensityProfileUI("Mie 1", skyParameters.mie_density[1]);
          DensityProfileUI("Rayleigh 0", skyParameters.rayleigh_density[0]);
          // DensityProfileUI("Rayleigh 1", skyParameters.rayleigh_density[1]);
        }

        if (ImGui::CollapsingHeader("Sky LUTs"))
        {
          static float scale0 = 1;
          ImGui::SliderFloat("Transmittance##0", &scale0, 0, 1);
          auto textureSampler = ImTextureSampler(sky_->GetTransmittanceLut().ImageView().GetSampledResourceHandle().index);
          textureSampler.SetAlphaIsOne(true);
          ImGui::Image(textureSampler, {100, 100}, {0, 0}, {1, 1}, {scale0, scale0, scale0, 1});

          static float scale1 = 1;
          ImGui::SliderFloat("Multiscattering##1", &scale1, 0, 1);
          textureSampler.SetTextureIndex(sky_->GetMultiscatteringLut().ImageView().GetSampledResourceHandle().index);
          ImGui::Image(textureSampler, {100, 100}, {0, 0}, {1, 1}, {scale1, scale1, scale1, 1});

          static float scale2 = 1;
          ImGui::SliderFloat("SkyView##2", &scale2, 0, 1);
          textureSampler.SetTextureIndex(sky_->GetSkyViewLut().ImageView().GetSampledResourceHandle().index);
          ImGui::Image(textureSampler, {100, 100}, {0, 0}, {1, 1}, {scale2, scale2, scale2, 1});
        }

        ImGui::EndTabItem();
      }

      const bool opened1 = ImGui::BeginTabItem("DDGI");
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("Dynamic diffuse global illumination.");
      }
      if (opened1)
      {
        const char* const names[] = {
          "None",
          "Luminance",
          "Illuminance",
          "Raw Depth",
          "Depth Moments",
          "Validity",
          "Average Luminance",
        };
        if (ImGui::BeginCombo("Visualize probes", names[int(ddgiDebugView_)]))
        {
          for (int i = 0; i < std::size(names); i++)
          {
            if (ImGui::Selectable(names[i], int(ddgiDebugView_) == i))
            {
              ddgiDebugView_ = Techniques::DDGIDebugMode(i);
            }
          }
          ImGui::EndCombo();
        }
        ImGui::SliderInt("Show Cascade", &ddgiDebugShowOnlyThisCascade_, -1, DDGI_NUM_CASCADES - 1, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Cascades as Color", &ddgiDebugShowCascadeIndexAsColor_);
        if (ImGui::RadioButton("None##Updates", !ddgiDebugPauseUpdates_ && !ddgiDebugFreezeGrid_))
        {
          ddgiDebugPauseUpdates_ = false;
          ddgiDebugFreezeGrid_   = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Pause Updates", ddgiDebugPauseUpdates_))
        {
          ddgiDebugPauseUpdates_ = true;
          ddgiDebugFreezeGrid_   = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Freeze Grid", ddgiDebugFreezeGrid_))
        {
          ddgiDebugPauseUpdates_ = false;
          ddgiDebugFreezeGrid_   = true;
        }
        ImGui::SliderFloat("Base Grid Scale", &ddgiBaseGridScale_, 1, 32, "%.0f");
        ImGui::SliderFloat("Probe Size", &ddgiDebugProbeSize_, 0.125f, 1.0f, "%.3f");

        ImGui::EndTabItem();
      }

      const bool opened2 = ImGui::BeginTabItem("RTAO");
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("Ray traced ambient occlusion.\nUsed with DDGI only.");
      }
      if (opened2)
      {
        ImGui::SliderInt("AO rays", &aoParams_.numRays, 1, 32);
        ImGui::SliderFloat("AO ray length", &aoParams_.rayLength, 0.125f, 10.0f);
        ImGui::SeparatorText("Upscaling");
        ImGui::SliderFloat("Phi (normal)", &aoParams_.phiNormal, 0, 2);
        ImGui::SliderFloat("Phi (depth)", &aoParams_.phiDepth, 0, 2);
        int factor = aoParams_.upscaleFactor;
        ImGui::SliderInt("Upscale factor", &factor, 1, 4, "%d", ImGuiSliderFlags_AlwaysClamp);
        aoParams_.upscaleFactor = uint32_t(factor);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Exposure"))
      {
        bool b = tonemapUniforms.curveExposure != 0;
        ImGui::Checkbox("Curve exposure (WIP)", &b);
        tonemapUniforms.curveExposure = b;
        ImGui::SliderFloat("Min exposure", &tonemapUniforms.minExposure, -20, tonemapUniforms.maxExposure, "%.1f");
        ImGui::SliderFloat("Max exposure", &tonemapUniforms.maxExposure, tonemapUniforms.minExposure, 20, "%.1f");
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("SSGI"))
      {
        ssgiParams_.debugCapture = false;
        if (ImGui::Button("Capture SSGI info"))
        {
          ssgiParams_.debugCapture = true;
        }

        ImGui::Checkbox("Enable##SSGI", &enableSsgi_);
        ImGui::SliderInt("Slice count", reinterpret_cast<int*>(&ssgiParams_.sliceCount), 1, 16);   // UB
        ImGui::SliderInt("Sample count", reinterpret_cast<int*>(&ssgiParams_.sampleCount), 1, 32); // UB
        ImGui::SliderFloat("Sample radius", &ssgiParams_.sampleRadius, 0.25f, 5.0f, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
        ImGui::SliderFloat("Hit thickness", &ssgiParams_.hitThickness, 0.1f, 2.0f, "%.2f", ImGuiSliderFlags_NoRoundToFormat);

        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("CSM"))
      {
        Gui::CVarFloatCheckbox(enableSunShadowPass);
        if (ImGui::RadioButton("256##CSM_Resolution", sunShadowResolution.x == 256))
        {
          sunShadowResolution = {256, 256};
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("512##CSM_Resolution", sunShadowResolution.x == 512))
        {
          sunShadowResolution = {512, 512};
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("1024##CSM_Resolution", sunShadowResolution.x == 1024))
        {
          sunShadowResolution = {1024, 1024};
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("2048##CSM_Resolution", sunShadowResolution.x == 2048))
        {
          sunShadowResolution = {2048, 2048};
        }

        ImGui::SliderInt("Cascades##CSM", &sunShadowNumCascades, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Frustum Width##CSM", &sunShadowFrustumSideLength, 10, 500, "%.0f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Frustum Depth##CSM", &sunShadowFrustumDepth, 1000, 3000, "%.0f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp);

        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Weather"))
      {
        ImGui::Checkbox("Override game weather", &enableWeatherOverride_);
        ImGui::BeginDisabled(!enableWeatherOverride_);
        ImGui::SliderFloat("Cloud bottom altitude", &weather_.cloudBottomAltitude, 200, 2000, "%.0fm");
        ImGui::SliderFloat("Cloud bottom falloff", &weather_.cloudBottomFalloffDistance, 0, 200, "%.1fm");
        ImGui::SliderFloat("Cloud height", &weather_.cloudHeight, 0, 1000, "%.0fm");
        ImGui::SliderFloat("Cloud coverage", &weather_.cloudCoverage, 0.01f, 1, "%.2f");
        ImGui::SliderFloat("Cloud density", &weather_.cloudDensity, 0, 1, "%.2f");
        ImGui::SliderFloat("Cloud frequency", &weather_.cloudFrequency, 0, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        ImGui::SliderFloat2("Wind velocity", &weather_.windVelocity[0], -10, 10, "%.2f");
        ImGui::DragFloat2("Cloud position offset", &weather_.cloudHorizontalOffset[0], 1, 0, 0, "%.2f");
        ImGui::DragFloat("Cloud time offset", &weather_.cloudTemporalOffset, 1, 0, 0, "%.2f");
        ImGui::SliderFloat("Earth scale", &weather_.earthSizeFactor, 1.0f / 10000.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        ImGui::EndDisabled();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Particles"))
      {
        if (const auto* transform = world.TryGetLocalPlayerTransform())
        {
          using Game2::Render::ParticleFlag;
          auto& rng      = world.globals->game->rng;
          const auto pos = transform->position + GetForward(transform->rotation) * 5.0f;

          static bool spam = false;
          ImGui::Checkbox("spam", &spam);
          if (ImGui::Button("Spawn test particles") || spam)
          {
            if (world.globals->game->gameState == GameState::GAME)
            {
              for (int i = 0; i < 3000; i++)
              {
                const auto offset   = glm::vec3(rng.RandFloat(-1, 1), rng.RandFloat(-1, 1), rng.RandFloat(-1, 1)) * 50.0f;
                const auto particle = Game2::Render::Particle{
                  .flags = ParticleFlag::Solid | ParticleFlag::UseSkyShadowMap | ParticleFlag::DestroyOnCollision | ParticleFlag::ForceUpPosY |
                           ParticleFlag::CollideWithTranslucent,
                  .baseColorTexture              = "rain",
                  .initialBaseColorFactor        = {1, 1, 1, 1},
                  .finalBaseColorFactor          = {1, 1, 1, 1},
                  .position                      = pos + offset,
                  .velocity                      = {rng.RandFloat(-1, 1), -10, rng.RandFloat(-1, 1)},
                  .acceleration                  = {0, 0, 0},
                  .particleArchetypeToSpawnOnHit = "rain_impact",
                  .initialScale                  = glm::vec2(0.01f, 0.05f),
                  .finalScale                    = glm::vec2(0.01f, 0.05f),
                  .life                          = 2 + rng.RandFloat(),
                };
                world.globals->head->SpawnParticles(std::span{&particle, 1});
              }
            }
          }

          static float speed = 4;
          static float life  = 3;
          ImGui::SliderFloat("speed", &speed, 0, 10);
          ImGui::SliderFloat("life", &life, 0, 10);
          if (ImGui::Button("Pew pew"))
          {
            const auto particle = Game2::Render::Particle{
              .flags                         = ParticleFlag::Solid | ParticleFlag::UseSkyShadowMap,
              .baseColorTexture              = "coin",
              .initialBaseColorFactor        = {1, 1, 1, 1},
              .finalBaseColorFactor          = {1, 1, 1, 1},
              .position                      = transform->position,
              .velocity                      = GetForward(transform->rotation) * speed,
              .acceleration                  = {0, 0, 0},
              .particleArchetypeToSpawnOnHit = "test",
              .initialScale                  = glm::vec2(0.1f),
              .finalScale                    = glm::vec2(0.1f),
              .life                          = life,
            };
            world.globals->head->SpawnParticles(std::span{&particle, 1});
          }

          if (ImGui::Button("Spawn test archetype"))
          {
            for (int i = 0; i < 1000; i++)
            {
              auto archetype = Game2::Render::ParticleArchetypeSpawnInfo{
                .archetypeName = "test",
                .count         = 10,
                .positionWS    = pos,
                .velocity      = {},
              };
              world.globals->head->SpawnParticleArchetypes(std::span(&archetype, 1));
            }
          }
        }
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}