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

#include "Game/Rendering/Particle.h"
#include "Client/Fvog/Buffer2.h"
#include "Client/Fvog/Device.h"
#include "Client/Fvog/Rendering2.h"
#include "Game/Assets.h"
#include "shaders/Config.shared.h"

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

        writeDispatchParams = GetPipelineManager().EnqueueCompileComputePipeline({
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
        dispatchCommand.emplace(Fvog::TypedBufferCreateInfo{.flag = Fvog::BufferFlagThingy::NO_DESCRIPTOR}, "Particle update dispatch command");
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

            auto indices = std::vector<int32_t>(maxParticles);
            std::ranges::iota(std::ranges::views::reverse(indices), 0);
            freeParticles->UpdateDataExpensive(cmd, indices);
          });
      }

      ~ParticlesImpl() override = default;

      void RegisterArchetype(std::string name, const Game2::Render::ParticleArchetype& archetype) override
      {
        DEBUG_ASSERT(!nameToParticleArchetype.contains(name));

        const auto archetypeGpu = ::ParticleArchetype{
          .prototype         = GameParticleToRenderParticle(archetype.prototype),
          .positionOffsetMin = archetype.positionOffsetMin,
          .positionOffsetMax = archetype.positionOffsetMax,
          .velocityMin       = archetype.velocityMin,
          .velocityMax       = archetype.velocityMax,
          .accelerationMin   = archetype.accelerationMin,
          .accelerationMax   = archetype.accelerationMax,
        };

        nameToParticleArchetype.emplace(std::move(name), std::make_pair(numArchetypes, archetypeGpu));

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
            archetypesToSpawn.push_back(::ParticleArchetypeSpawnInfo{
              .archetypeIndex = GetArchetypeIndex(info.archetypeName),
              .count          = info.count,
              .positionWS     = info.positionWS,
              .velocity       = info.velocity,
            });
          }

          auto spawnInfosGpu = Fvog::GetDevice().AllocTransient<::ParticleArchetypeSpawnInfo>(archetypesToSpawn.size());
          std::ranges::copy(archetypesToSpawn, spawnInfosGpu.Get());

          auto archetypeListGpu = Fvog::GetDevice().AllocTransient<ParticleArchetypeSpawnInfoList_t>();
          *archetypeListGpu = {.size = (int32_t)archetypesToSpawn.size(), .spawnInfos = spawnInfosGpu.ptr};

          auto gpuParams = Fvog::GetDevice().AllocTransient<ParticleSpawnGpuParams_t>();

          *gpuParams = {
            .spawnSingleParticlesMode = 0,
            .singleParticlesToSpawn   = {},
            .archetypesToSpawn        = archetypeListGpu.ptr,
            .archetypeList            = particleArchetypes.value().GetDeviceAddress(),
            .particles                = particleList.value().GetDeviceAddress(),
            .liveParticles            = thisFrameLiveParticleList.value().GetDeviceAddress(),
            .freeParticles            = freeParticleList.value().GetDeviceAddress(),
            .frameNumber              = params.frameNumber,
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
            .spawnSingleParticlesMode = 1,
            .singleParticlesToSpawn   = particleToSpawnList.ptr,
            .archetypesToSpawn        = {},
            .archetypeList            = particleArchetypes.value().GetDeviceAddress(),
            .particles                = particleList.value().GetDeviceAddress(),
            .liveParticles            = thisFrameLiveParticleList.value().GetDeviceAddress(),
            .freeParticles            = freeParticleList.value().GetDeviceAddress(),
            .frameNumber              = params.frameNumber,
          };

          ctx.SetPushConstants(gpuParams);
          ctx.DispatchInvocations((uint32_t)particlesToSpawn.size(), 1, 1);
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
            .dispatchCommand  = dispatchCommand.value().GetDeviceAddress(),
            .liveParticleList = thisFrameLiveParticleList.value().GetDeviceAddress(),
          };

          ctx.BindComputePipeline(writeDispatchParams.GetPipeline());
          ctx.SetPushConstants(gpuParams);
          ctx.Dispatch(1, 1, 1);
        }

        ctx.TeenyBufferUpdate(nextFrameLiveParticleList.value(), int32_t{0}, offsetof(::IntList_t, size));

        ctx.Barrier();

        // Update particles
        {
          auto gpuParams = Fvog::GetDevice().AllocTransient<ParticlesUpdateGpuParams_t>();

          *gpuParams = {
            .particles              = particleList.value().GetDeviceAddress(),
            .thisFrameLiveParticles = thisFrameLiveParticleList.value().GetDeviceAddress(),
            .nextFrameLiveParticles = nextFrameLiveParticleList.value().GetDeviceAddress(),
            .freeParticles          = freeParticleList.value().GetDeviceAddress(),
            .globalUniforms         = params.globalUniforms,
          };

          ctx.BindComputePipeline(updateParticles.GetPipeline());
          ctx.SetPushConstants(gpuParams);
          ctx.DispatchIndirect(dispatchCommand.value());
        } 

        ctx.Barrier();

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
          .cameraRight   = {params.view_from_world[0][0], params.view_from_world[1][0], params.view_from_world[2][0]},
          .cameraUp      = {params.view_from_world[0][1], params.view_from_world[1][1], params.view_from_world[2][1]},
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
        return {
          .baseColorTexture              = renderer_.GetOrEmplaceCachedTexture(particle.baseColorTexture, true).ImageView().GetTexture2D(),
          .baseColorFactor               = particle.baseColorFactor,
          .position                      = particle.position,
          .velocity                      = particle.velocity,
          .acceleration                  = particle.acceleration,
          .isSolid                       = particle.isSolid,
          .spawnParticleOnHit            = particle.spawnParticleOnHit,
          .particleArchetypeToSpawnOnHit = particle.spawnParticleOnHit ? GetArchetypeIndex(particle.particleArchetypeToSpawnOnHit) : 0u,
          .initialScale                  = particle.initialScale,
          .currentScale                  = particle.currentScale,
          .finalScale                    = particle.finalScale,
          .initialLife                   = particle.initialLife,
          .lifeRemaining                 = particle.lifeRemaining,
        };
      }

      VoxelRenderer& renderer_;

      std::optional<Fvog::TypedBuffer<DrawIndirectCommand>> drawCommand;
      std::optional<Fvog::TypedBuffer<DispatchIndirectCommand>> dispatchCommand;
      std::optional<Fvog::TypedBuffer<::Particle>> particles;
      std::optional<Fvog::TypedBuffer<::ParticleList_t>> particleList;
      std::optional<Fvog::TypedBuffer<int32_t>> thisFrameLiveParticles;
      std::optional<Fvog::TypedBuffer<::IntList_t>> thisFrameLiveParticleList;
      std::optional<Fvog::TypedBuffer<int32_t>> nextFrameLiveParticles;
      std::optional<Fvog::TypedBuffer<::IntList_t>> nextFrameLiveParticleList;
      std::optional<Fvog::TypedBuffer<int32_t>> freeParticles;
      std::optional<Fvog::TypedBuffer<::IntList_t>> freeParticleList;
      std::optional<Fvog::TypedBuffer<::ParticleArchetype>> particleArchetypes;

      PipelineManager::ComputePipelineKey spawnParticles;
      PipelineManager::ComputePipelineKey writeDispatchParams;
      PipelineManager::ComputePipelineKey updateParticles;
      PipelineManager::ComputePipelineKey writeDrawCommand;
      PipelineManager::GraphicsPipelineKey renderParticles;

      static constexpr uint32_t maxParticles  = 1'000'000;
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