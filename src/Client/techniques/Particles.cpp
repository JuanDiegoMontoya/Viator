#include "Particles.h"

#include "Client/PipelineManager.h"
#include "Client/PipelineManager.h"
#include "Client/VoxelRenderer.h"
#include "shaders/particles/Particles.shared.h"
#include "shaders/particles/RenderParticle.shared.h"
#include "shaders/particles/ParticleSpawn.comp.glsl"
#include "shaders/particles/ParticleUpdate.comp.glsl"
#include "shaders/particles/WriteUpdateDispatchParams.comp.glsl"
#include "shaders/particles/WriteRenderParticleCommand.comp.glsl"
#include "shaders/particles/GarbageCollectParticles.comp.glsl"

#include "Game/Rendering/Particle.h"
#include "Client/Fvog/Buffer2.h"
#include "Client/Fvog/Device.h"
#include "Client/Fvog/Rendering2.h"
#include "Game/Assets.h"
#include "shaders/Config.shared.h"

#include "glm/packing.hpp"

#include <numeric>
#include <ranges>
#include <unordered_map>

namespace Techniques
{
  namespace
  {
    class ParticlesImpl : public Particles
    {
    public:
      explicit ParticlesImpl(const ParticlesCreateParams& params) : renderer_(*params.renderer)
      {
        spawnParticles = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "[Particles] Spawn pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "particles/ParticleSpawn.comp.glsl",
            },
        });

        writeUpdateDispatchParams = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "[Particles] Write dispatch params pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "particles/WriteUpdateDispatchParams.comp.glsl",
            },
        });

        updateParticles = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "[Particles] Update pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "particles/ParticleUpdate.comp.glsl",
            },
        });

        garbageCollectParticles = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "[Particles] Garbage collection pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "particles/GarbageCollectParticles.comp.glsl",
            },
        });

        writeDrawCommand = GetPipelineManager().EnqueueCompileComputePipeline({
          .name = "[Particles] Write draw command pipeline",
          .shaderModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::COMPUTE_SHADER,
              .path  = GetShaderDirectory() / "particles/WriteRenderParticleCommand.comp.glsl",
            },
        });

        renderParticles = GetPipelineManager().EnqueueCompileGraphicsPipeline({
          .name = "[Particles] Render particles",
          .vertexModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::VERTEX_SHADER,
              .path  = GetShaderDirectory() / "particles/RenderParticle.vert.glsl",
            },
          .fragmentModuleInfo =
            PipelineManager::ShaderModuleCreateInfo{
              .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
              .path  = GetShaderDirectory() / "particles/RenderParticle.frag.glsl",
            },
          .state =
            {
              .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
              .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = FVOG_COMPARE_OP_NEARER_OR_EQUAL},
              .renderTargetFormats =
                {
                  .colorAttachmentFormats = params.gBufferFormats,
                  .depthAttachmentFormat  = params.gDepthFormat,
                },
            },
        });

        drawCommand.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Particle draw command");
        updateDispatchCommand.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Particle update dispatch command");
        spawnIndirectDispatchCommand.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR},
          "Particle spawn indirect dispatch command");
        indirectSpawnedParticles.emplace(Fvog::TypedBufferCreateInfo{.count = maxIndirectParticlesPerFrame, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR},
          "Indirect spawned particles");
        indirectSpawnedParticleVector.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Indirect spawned particle vector");
        particles.emplace(Fvog::TypedBufferCreateInfo{.count = maxParticles, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Particles");
        particleList.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Particle list");
        thisFrameLiveParticles.emplace(Fvog::TypedBufferCreateInfo{.count = maxParticles, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Live particles 1");
        thisFrameLiveParticleList.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Live particle list 1");
        nextFrameLiveParticles.emplace(Fvog::TypedBufferCreateInfo{.count = maxParticles, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Live particles 2");
        nextFrameLiveParticleList.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Live particle list 2");
        freeParticles.emplace(Fvog::TypedBufferCreateInfo{.count = maxParticles, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Free particles");
        freeParticleList.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Free particle list");
        particleArchetypes.emplace(Fvog::TypedBufferCreateInfo{.count = maxArchetypes, .flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Particle archetypes");

        Fvog::GetDevice().ImmediateSubmit(
          [&](VkCommandBuffer cmd)
          {
            auto ctx = Fvog::Context(cmd);
            ctx.TeenyBufferUpdate(particleList.value(), ::ParticleList_t{.size = maxParticles, .particles = particles.value().GetDeviceAddress()});
            ctx.TeenyBufferUpdate(thisFrameLiveParticleList.value(), ::IntList_t{.size = 0, .values = thisFrameLiveParticles.value().GetDeviceAddress()});
            ctx.TeenyBufferUpdate(nextFrameLiveParticleList.value(), ::IntList_t{.size = 0, .values = nextFrameLiveParticles.value().GetDeviceAddress()});
            ctx.TeenyBufferUpdate(freeParticleList.value(), ::ParticleList_t{.size = maxParticles, .particles = freeParticles.value().GetDeviceAddress()});
            ctx.TeenyBufferUpdate(indirectSpawnedParticleVector.value(),
              ::ParticleVector_t{
                .size      = 0,
                .capacity  = maxIndirectParticlesPerFrame,
                .particles = indirectSpawnedParticles.value().GetDeviceAddress(),
              });
            ctx.TeenyBufferUpdate(spawnIndirectDispatchCommand.value(), ::DispatchIndirectCommand{0, 1, 1});

            auto indices = std::vector<int32_t>(maxParticles);
            std::ranges::iota(std::ranges::views::reverse(indices), 0);
            freeParticles->UpdateDataExpensive(cmd, indices);
          });

        // TEMP
        ParticlesImpl::RegisterArchetype("test",
          {
            .prototype =
              Game2::Render::Particle{
                .flags                         = Game2::Render::ParticleFlag::Solid,
                .baseColorTexture              = "error_8",
                .initialBaseColorFactor        = {1, 1, 1, 1},
                .finalBaseColorFactor          = {1, 1, 1, 1},
                .position                      = {},
                .velocity                      = {},
                .acceleration                  = {0, -5, 0},
                .particleArchetypeToSpawnOnHit = {},
                .initialScale                  = {.1f, .1f},
                .finalScale                    = {.0f, .0f},
                .life                          = 2,
              },
            .positionOffsetMin = {},
            .positionOffsetMax = {},
            .velocityMin       = {-.4f, 0.8f, -.4f},
            .velocityMax       = {.4f, 2.0f, .4f},
            .accelerationMin   = {},
            .accelerationMax   = {},
          });

        ParticlesImpl::RegisterArchetype("rain_impact",
          {
            .prototype =
              Game2::Render::Particle{
                .flags                         = Game2::Render::ParticleFlag::ForceRightPosX | Game2::Render::ParticleFlag::ForceUpPosZ,
                .baseColorTexture              = "rain_impact",
                .initialBaseColorFactor        = {1, 1, 1, 1},
                .finalBaseColorFactor          = {1, 1, 1, 0},
                .position                      = {},
                .velocity                      = {},
                .acceleration                  = {0, 0, 0},
                .particleArchetypeToSpawnOnHit = {},
                .initialScale                  = {.0f, .0f},
                .finalScale                    = {.1f, .1f},
                .life                          = 0.3f,
              },
            .positionOffsetMin = {},
            .positionOffsetMax = {},
            .velocityMin       = {},
            .velocityMax       = {},
            .accelerationMin   = {},
            .accelerationMax   = {},
          });
      }

      ~ParticlesImpl() override = default;

      void RegisterArchetype(std::string name, const Game2::Render::ParticleArchetype& archetype) override
      {
        DEBUG_ASSERT(!nameToParticleArchetype.contains(name));

        auto& archetypeGpu = nameToParticleArchetype.try_emplace(std::move(name), std::make_pair(numArchetypes, ::ParticleArchetype{})).first->second.second;

        archetypeGpu = {
          .prototype         = GameParticleToRenderParticle(archetype.prototype),
          .positionOffsetMin = archetype.positionOffsetMin,
          .positionOffsetMax = archetype.positionOffsetMax,
          .velocityMin       = archetype.velocityMin,
          .velocityMax       = archetype.velocityMax,
          .accelerationMin   = archetype.accelerationMin,
          .accelerationMax   = archetype.accelerationMax,
        };

        Fvog::GetDevice().ImmediateSubmit(
          [&](VkCommandBuffer cmd)
          {
            Fvog::Context(cmd).TeenyBufferUpdate(particleArchetypes.value(), archetypeGpu, sizeof(archetypeGpu) * numArchetypes);
          });

        numArchetypes++;
      }

      void PushSingleParticles(std::span<const Game2::Render::Particle> singleParticles) override
      {
        singleParticlesToSpawn_.insert_range(singleParticlesToSpawn_.end(), singleParticles);
      }

      void PushParticleArchetypes(std::span<const Game2::Render::ParticleArchetypeSpawnInfo> archetypeSpawnInfos) override
      {
        particleArchetypesToSpawn_.insert_range(particleArchetypesToSpawn_.end(), archetypeSpawnInfos);
      }

      void Spawn(VkCommandBuffer cmd, const ParticlesSpawnParams& params) override
      {
        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Spawn particles");

        ctx.BindComputePipeline(spawnParticles.GetPipeline());

        // Instances of particle archetypes
        if (!particleArchetypesToSpawn_.empty())
        {
          auto archetypesToSpawn = std::vector<::ParticleArchetypeSpawnInfo>();
          for (const auto& info : particleArchetypesToSpawn_)
          {
            // Split spawns into smaller batches so they can be processed by multiple GPU threads.
            constexpr int BATCH_SIZE = 20;
            for (int remaining = info.count; remaining > 0; remaining -= BATCH_SIZE)
            {
              archetypesToSpawn.push_back(::ParticleArchetypeSpawnInfo{
                .archetypeIndex = GetArchetypeIndex(info.archetypeName),
                .count          = glm::min(remaining, BATCH_SIZE),
                .positionWS     = info.positionWS,
                .velocity       = info.velocity,
              });
            }
          }

          auto spawnInfosGpu = Fvog::GetDevice().AllocTransient<::ParticleArchetypeSpawnInfo>(archetypesToSpawn.size());
          std::ranges::copy(archetypesToSpawn, spawnInfosGpu.Get());

          auto archetypeListGpu = Fvog::GetDevice().AllocTransient<ParticleArchetypeSpawnInfoList_t>();
          *archetypeListGpu = {.size = (int32_t)archetypesToSpawn.size(), .spawnInfos = spawnInfosGpu.ptr};

          auto gpuParams = Fvog::GetDevice().AllocTransient<ParticleSpawnGpuParams_t>();

          *gpuParams = {
            .spawnParticlesMode       = PARTICLE_SPAWN_MODE_ARCHETYPE,
            .singleParticlesToSpawn   = {},
            .archetypesToSpawn        = archetypeListGpu.ptr,
            .indirectParticlesToSpawn = {},
            .archetypes               = particleArchetypes.value().GetDeviceAddress(),
            .particles                = particleList.value().GetDeviceAddress(),
            .liveParticles            = thisFrameLiveParticleList.value().GetDeviceAddress(),
            .freeParticles            = freeParticleList.value().GetDeviceAddress(),
            .frameNumber              = params.frameNumber,
            .skyShadowMap             = params.skyShadowMap,
            .skyBeerShadowMap         = params.skyBeerShadowMap,
          };

          ctx.SetPushConstants(gpuParams);
          ctx.DispatchInvocations((uint32_t)archetypesToSpawn.size(), 1, 1);
        }

        // Single particles
        if (!singleParticlesToSpawn_.empty())
        {
          auto particlesToSpawn = std::vector<::Particle>();
          for (const auto& particle : singleParticlesToSpawn_)
          {
            particlesToSpawn.push_back(GameParticleToRenderParticle(particle));
          }

          auto particlesToSpawnGpu = Fvog::GetDevice().AllocTransient<::Particle>(particlesToSpawn.size());
          std::ranges::copy(particlesToSpawn, particlesToSpawnGpu.Get());

          auto particleToSpawnList = Fvog::GetDevice().AllocTransient<ParticleList_t>();
          *particleToSpawnList     = {.size = (int32_t)particlesToSpawn.size(), .particles = particlesToSpawnGpu.ptr};

          auto gpuParams = Fvog::GetDevice().AllocTransient<ParticleSpawnGpuParams_t>();

          *gpuParams = {
            .spawnParticlesMode       = PARTICLE_SPAWN_MODE_SINGLE,
            .singleParticlesToSpawn   = particleToSpawnList.ptr,
            .archetypesToSpawn        = {},
            .indirectParticlesToSpawn = {},
            .archetypes               = particleArchetypes.value().GetDeviceAddress(),
            .particles                = particleList.value().GetDeviceAddress(),
            .liveParticles            = thisFrameLiveParticleList.value().GetDeviceAddress(),
            .freeParticles            = freeParticleList.value().GetDeviceAddress(),
            .frameNumber              = params.frameNumber,
            .skyShadowMap             = params.skyShadowMap,
            .skyBeerShadowMap         = params.skyBeerShadowMap,
          };

          ctx.SetPushConstants(gpuParams);
          ctx.DispatchInvocations((uint32_t)particlesToSpawn.size(), 1, 1);
        }

        // Indirect particles
        {
          auto gpuParams = Fvog::GetDevice().AllocTransient<ParticleSpawnGpuParams_t>();

          *gpuParams = {
            .spawnParticlesMode       = PARTICLE_SPAWN_MODE_INDIRECT,
            .singleParticlesToSpawn   = {},
            .archetypesToSpawn        = {},
            .indirectParticlesToSpawn = indirectSpawnedParticleVector.value().GetDeviceAddress(),
            .archetypes               = particleArchetypes.value().GetDeviceAddress(),
            .particles                = particleList.value().GetDeviceAddress(),
            .liveParticles            = thisFrameLiveParticleList.value().GetDeviceAddress(),
            .freeParticles            = freeParticleList.value().GetDeviceAddress(),
            .frameNumber              = params.frameNumber,
            .skyShadowMap             = params.skyShadowMap,
            .skyBeerShadowMap         = params.skyBeerShadowMap,
          };

          ctx.SetPushConstants(gpuParams);
          ctx.DispatchIndirect(spawnIndirectDispatchCommand.value());

          ctx.Barrier();

          spawnIndirectDispatchCommand.value().FillData(cmd,
            {
              .offset = offsetof(::DispatchIndirectCommand, x),
              .size   = sizeof(::DispatchIndirectCommand::x),
              .data   = 0,
            });

          indirectSpawnedParticleVector.value().FillData(cmd,
            {
              .offset = offsetof(::ParticleVector_t, size),
              .size   = sizeof(::ParticleVector_t::size),
              .data   = 0,
            });
        }

        singleParticlesToSpawn_.clear();
        particleArchetypesToSpawn_.clear();
      }

      void Update(VkCommandBuffer cmd, const ParticlesUpdateParams& params) override
      {
        auto ctx    = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Update particles");

        // Write indirect dispatch command
        {
          auto gpuParams = Fvog::GetDevice().AllocTransient<WriteUpdateDispatchParamsGpuParams_t>();

          *gpuParams = {
            .dispatchCommand  = updateDispatchCommand.value().GetDeviceAddress(),
            .liveParticleList = thisFrameLiveParticleList.value().GetDeviceAddress(),
          };

          ctx.BindComputePipeline(writeUpdateDispatchParams.GetPipeline());
          ctx.SetPushConstants(gpuParams);
          ctx.Dispatch(1, 1, 1);
        }

        ctx.TeenyBufferUpdate(nextFrameLiveParticleList.value(), int32_t{0}, offsetof(::IntList_t, size));

        ctx.Barrier();

        // Update particles
        {
          auto gpuParams = Fvog::GetDevice().AllocTransient<ParticlesUpdateGpuParams_t>();

          *gpuParams = {
            .particles                    = particleList.value().GetDeviceAddress(),
            .indirectParticles            = indirectSpawnedParticleVector.value().GetDeviceAddress(),
            .spawnIndirectDispatchCommand = spawnIndirectDispatchCommand.value().GetDeviceAddress(),
            .thisFrameLiveParticles       = thisFrameLiveParticleList.value().GetDeviceAddress(),
            .nextFrameLiveParticles       = nextFrameLiveParticleList.value().GetDeviceAddress(),
            .freeParticles                = freeParticleList.value().GetDeviceAddress(),
            .uniforms                     = params.globalUniforms,
            .archetypes                   = particleArchetypes.value().GetDeviceAddress(),
          };

          ctx.BindComputePipeline(updateParticles.GetPipeline());
          ctx.SetPushConstants(gpuParams);
          ctx.DispatchIndirect(updateDispatchCommand.value());
        } 

        ctx.Barrier();

        // Free dead particles
        {
          auto gpuParams = Fvog::GetDevice().AllocTransient<GarbageCollectParticlesGpuParams_t>();

          *gpuParams = {
            .liveParticles = thisFrameLiveParticleList.value().GetDeviceAddress(),
            .freeParticles = freeParticleList.value().GetDeviceAddress(),
            .particles     = particleList.value().GetDeviceAddress(),
          };

          ctx.BindComputePipeline(garbageCollectParticles.GetPipeline());
          ctx.SetPushConstants(gpuParams);
          ctx.DispatchIndirect(updateDispatchCommand.value()); // Operates on the same domain as Update.
        }

        // Write indirect draw command
        {
          auto gpuParams = Fvog::GetDevice().AllocTransient<WriteRenderParticleCommandGpuParams_t>();

          *gpuParams = {
            .drawCommand      = drawCommand.value().GetDeviceAddress(),
            .liveParticleList = thisFrameLiveParticleList.value().GetDeviceAddress(),
          };

          ctx.BindComputePipeline(writeDrawCommand.GetPipeline());
          ctx.SetPushConstants(gpuParams);
          ctx.Dispatch(1, 1, 1);
        }
      }

      void Render(VkCommandBuffer cmd, const ParticlesRenderParams& params) override
      {
        auto ctx = Fvog::Context(cmd);
        auto marker = ctx.MakeScopedDebugMarker("Render particles");

        auto gpuParams = Fvog::GetDevice().AllocTransient<ParticleRenderGpuParams_t>();

        *gpuParams = {
          .uniforms      = params.globalUniforms,
          .particleList  = particleList.value().GetDeviceAddress(),
          .liveParticles = thisFrameLiveParticleList.value().GetDeviceAddress(),
        };

        ctx.SetPushConstants(gpuParams);
        ctx.BindGraphicsPipeline(renderParticles.GetPipeline());
        ctx.DrawIndirect(drawCommand.value(), 0, 1, 0);
        std::swap(thisFrameLiveParticleList, nextFrameLiveParticleList);
      }

    private:
      uint32_t GetArchetypeIndex(std::string_view name)
      {
        if (auto it = nameToParticleArchetype.find(std::string(name)); it != nameToParticleArchetype.end())
        {
          return it->second.first;
        }

        DEBUG_ASSERT(false);
        return 0;
      }

      ::Particle GameParticleToRenderParticle(const Game2::Render::Particle& particle)
      {
        uint32_t behaviorFlags = particle.flags.flags;
        behaviorFlags |= particle.baseColorTexture ? PARTICLE_BEHAVIOR_USE_BASE_COLOR_TEXTURE : 0;
        behaviorFlags |= particle.particleArchetypeToSpawnOnHit ? PARTICLE_BEHAVIOR_SPAWN_ARCHETYPE_ON_COLLISION : 0;

        return {
          .behaviorFlags = behaviorFlags,
          .baseColorTexture =
            particle.baseColorTexture ? renderer_.GetOrEmplaceCachedTexture(*particle.baseColorTexture, true).ImageView().GetTexture2D() : shared::Texture2D{},
          .initialBaseColorFactor        = glm::packUnorm4x8(particle.initialBaseColorFactor),
          .currentBaseColorFactor        = glm::packUnorm4x8(particle.initialBaseColorFactor),
          .finalBaseColorFactor          = glm::packUnorm4x8(particle.finalBaseColorFactor),
          .position                      = particle.position,
          .velocity                      = particle.velocity,
          .acceleration                  = particle.acceleration,
          .particleArchetypeToSpawnOnHit = particle.particleArchetypeToSpawnOnHit ? GetArchetypeIndex(*particle.particleArchetypeToSpawnOnHit) : 0u,
          .initialScale                  = particle.initialScale,
          .currentScale                  = particle.initialScale,
          .finalScale                    = particle.finalScale,
          .initialLife                   = particle.life,
          .lifeRemaining                 = particle.life,
        };
      }

      VoxelRenderer& renderer_;

      std::optional<Fvog::TypedBuffer<DrawIndirectCommand>> drawCommand;
      std::optional<Fvog::TypedBuffer<DispatchIndirectCommand>> spawnIndirectDispatchCommand;
      std::optional<Fvog::TypedBuffer<DispatchIndirectCommand>> updateDispatchCommand;
      std::optional<Fvog::TypedBuffer<::Particle>> particles;
      std::optional<Fvog::TypedBuffer<::ParticleList_t>> particleList;
      std::optional<Fvog::TypedBuffer<::Particle>> indirectSpawnedParticles;
      std::optional<Fvog::TypedBuffer<::ParticleVector_t>> indirectSpawnedParticleVector;
      std::optional<Fvog::TypedBuffer<int32_t>> thisFrameLiveParticles;
      std::optional<Fvog::TypedBuffer<::IntList_t>> thisFrameLiveParticleList;
      std::optional<Fvog::TypedBuffer<int32_t>> nextFrameLiveParticles;
      std::optional<Fvog::TypedBuffer<::IntList_t>> nextFrameLiveParticleList;
      std::optional<Fvog::TypedBuffer<int32_t>> freeParticles;
      std::optional<Fvog::TypedBuffer<::IntList_t>> freeParticleList;
      std::optional<Fvog::TypedBuffer<::ParticleArchetype>> particleArchetypes;

      PipelineManager::ComputePipelineKey spawnParticles;
      PipelineManager::ComputePipelineKey writeUpdateDispatchParams;
      PipelineManager::ComputePipelineKey updateParticles;
      PipelineManager::ComputePipelineKey garbageCollectParticles;
      PipelineManager::ComputePipelineKey writeDrawCommand;
      PipelineManager::GraphicsPipelineKey renderParticles;

      static constexpr uint32_t maxParticles  = 1'000'000;
      static constexpr uint32_t maxIndirectParticlesPerFrame = 10'000;
      static constexpr uint32_t maxArchetypes = 1000;
      uint32_t numArchetypes                  = 0;
      std::unordered_map<std::string, std::pair<uint32_t, ::ParticleArchetype>> nameToParticleArchetype;

      std::vector<Game2::Render::Particle> singleParticlesToSpawn_;
      std::vector<Game2::Render::ParticleArchetypeSpawnInfo> particleArchetypesToSpawn_;
    };
  }


  std::unique_ptr<Particles> Particles::Create(const ParticlesCreateParams& params)
  {
    return std::make_unique<ParticlesImpl>(params);
  }
}