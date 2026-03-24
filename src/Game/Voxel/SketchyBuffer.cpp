#include "SketchyBuffer.h"

#ifndef GAME_HEADLESS
  #include "Client/Fvog/Buffer2.h"
  #include "Client/Fvog/Device.h"
  #include "Core/Container/BitArray.h"
  #include "volk.h"
  #include <bit>
#endif

#include "Client/Fvog/detail/Common.h"
#include "tracy/Tracy.hpp"
#include "vk_mem_alloc.h"
#include "spdlog/spdlog.h"

#include <memory>
#include <bit>
#include <optional>

namespace
{
  constexpr bool profileVoxelPool = true;
} // namespace

class SketchyBufferImpl final : public SketchyBuffer
{
public:
  explicit SketchyBufferImpl(size_t bufferSize, bool createGpuBuffer, std::string name = {});
  ~SketchyBufferImpl() override;

  NO_COPY(SketchyBufferImpl);

  SketchyBufferImpl(SketchyBufferImpl&& old) noexcept;
  SketchyBufferImpl& operator=(SketchyBufferImpl&& old) noexcept;

  Alloc Allocate(size_t size, size_t alignment) override;
  void Free(Alloc alloc) override;

  VmaVirtualBlock GetAllocator() const override
  {
    return allocator_;
  }

  size_t SizeBytes() const override
  {
    return bufferSize_;
  }

  size_t PageSize() const override
  {
    return PAGE_SIZE;
  }

  void* GetCpuBuffer() override
  {
    return cpuBuffer_.get();
  }

  const void* GetCpuBuffer() const override
  {
    return cpuBuffer_.get();
  }

private:
  static constexpr size_t PAGE_SIZE = 1024;
  static constexpr size_t L2_PAGE_SIZE = 512;
  static inline uint32_t nextNameId_{};
  std::string name_;
  size_t bufferSize_{};
  std::unique_ptr<std::byte[]> cpuBuffer_;
  VmaVirtualBlock allocator_{};

#ifndef GAME_HEADLESS
public:
  void FlushWritesToGPU(VkCommandBuffer cmd) override;

  Fvog::Buffer& GetGpuBuffer() override
  {
    DEBUG_ASSERT(gpuBuffer_.has_value());
    return *gpuBuffer_;
  }

  void MarkRange(size_t offset, size_t size) override
  {
    const auto minPage = offset / PAGE_SIZE;
    const auto maxPage = (offset + size - 1) / PAGE_SIZE;
    for (auto i = minPage; i <= maxPage; i++)
    {
      dirtyPages_.Set(i, true);
    }

    const auto minL2Page = minPage / L2_PAGE_SIZE;
    const auto maxL2Page = maxPage / L2_PAGE_SIZE;
    for (auto i = minL2Page; i <= maxL2Page; i++)
    {
      dirtyPagesL2_.Set(i, true);
    }
  }

private:
  std::optional<Fvog::Buffer> gpuBuffer_;

  // Two levels of dirty pages.
  // The first stores the status of individual dirty pages.
  // The second stores whether there is at least one dirty page per each L2_PAGE_SIZE pages.
  // This hierarchy can be updated in a fast, wait-free fashion, unlike the std::unordered_set that was previously used here.
  Core::BitArray dirtyPages_;
  Core::BitArray dirtyPagesL2_;
#endif
};

std::unique_ptr<SketchyBuffer> SketchyBuffer::Create(size_t bufferSize, bool createGpuBuffer, std::string name)
{
  return std::make_unique<SketchyBufferImpl>(bufferSize, createGpuBuffer, name);
}

SketchyBufferImpl::SketchyBufferImpl(size_t bufferSize, [[maybe_unused]] bool createGpuBuffer, [[maybe_unused]] std::string name)
  : bufferSize_(bufferSize)
{
  ZoneScoped;
#ifndef GAME_HEADLESS
  dirtyPages_.Resize(bufferSize / PAGE_SIZE);
  dirtyPagesL2_.Resize(dirtyPages_.Size() / L2_PAGE_SIZE + 1);
  if (createGpuBuffer && !Fvog::IsDeviceInitialized())
  {
    spdlog::trace("Fvog::Device is not initialized, overriding createGpuBuffer to false.");
    createGpuBuffer = false;
  }
#endif
  name_ = std::format("Voxel Storage {} ({})",
    nextNameId_++,
#ifndef GAME_HEADLESS
    createGpuBuffer ? "CPU & GPU" : "CPU"
#else
    "CPU"
#endif
  );
#ifndef GAME_HEADLESS
  if (createGpuBuffer)
  {
    gpuBuffer_.emplace(Fvog::BufferCreateInfo{.size = bufferSize, .flag = Fvog::BufferFlagThingy::NONE}, std::move(name));
  }
#endif
  cpuBuffer_ = std::make_unique_for_overwrite<std::byte[]>(bufferSize);
  {
    ZoneScopedN("memset");
    ZoneValue(bufferSize);
    std::memset(cpuBuffer_.get(), 0xCD, bufferSize);
  }
  {
    ZoneScopedN("vmaCreateVirtualBlock");
    Fvog::detail::CheckVkResult(vmaCreateVirtualBlock(Fvog::detail::Address(VmaVirtualBlockCreateInfo{.size = bufferSize}), &allocator_));
  }
}

SketchyBufferImpl::~SketchyBufferImpl()
{
  if constexpr (profileVoxelPool)
  {
    TracyMemoryDiscard(name_.c_str());
  }

  if (allocator_)
  {
    vmaClearVirtualBlock(allocator_);
    vmaDestroyVirtualBlock(allocator_);
  }
}

SketchyBufferImpl::SketchyBufferImpl(SketchyBufferImpl&& old) noexcept
  : cpuBuffer_(std::move(old.cpuBuffer_)),
    allocator_(std::exchange(old.allocator_, nullptr))
#ifndef GAME_HEADLESS
    , gpuBuffer_(std::move(old.gpuBuffer_)),
    dirtyPages_(std::move(old.dirtyPages_)),
    dirtyPagesL2_(std::move(old.dirtyPagesL2_))
#endif
{}

SketchyBufferImpl& SketchyBufferImpl::operator=(SketchyBufferImpl&& old) noexcept
{
  if (&old == this)
    return *this;
  this->~SketchyBufferImpl();
  return *new (this) SketchyBufferImpl(std::move(old));
}

SketchyBufferImpl::Alloc SketchyBufferImpl::Allocate(size_t size, size_t alignment)
{
  ZoneScoped;
  auto vmaAlign = alignment;
  // Fixup alignment and size if alignment isn't a power of two, which is required for VMA
  if (!std::has_single_bit(alignment))
  {
    size += alignment;
    vmaAlign = std::bit_ceil(alignment * 2);
  }

  auto allocation = VmaVirtualAllocation{};
  auto offset     = VkDeviceSize{};
  Fvog::detail::CheckVkResult(vmaVirtualAllocate(allocator_,
    Fvog::detail::Address(VmaVirtualAllocationCreateInfo{
      .size      = size,
      .alignment = vmaAlign,
    }),
    &allocation,
    &offset));
  // We only expect to have one SketchyBuffer, so this should be fine.
  if constexpr (profileVoxelPool)
  {
    TracyAllocN(allocation, size, name_.c_str());
  }

  // Push offset forward to multiple of the true alignment, then subtract that amount from the remaining size
  auto offsetAmount = (alignment - (offset % alignment)) % alignment;
  offset += offsetAmount;
  assert(offset % alignment == 0);
  return {offset, allocation};
}

void SketchyBufferImpl::Free(Alloc alloc)
{
  ZoneScoped;
  if constexpr (profileVoxelPool)
  {
    TracyFreeN(alloc.allocation, name_.c_str());
  }
  vmaVirtualFree(allocator_, alloc.allocation);
}

#ifndef GAME_HEADLESS
void SketchyBufferImpl::FlushWritesToGPU(VkCommandBuffer cmd)
{
  ZoneScoped;
  DEBUG_ASSERT(gpuBuffer_.has_value());
  // All pages need to be flushed or the underlying data may have invalid references/indices.
  // If the GPU representation of voxels is not being properly synced with the CPU representation, this code is probably the culprit.
  for (int j = 0; j < (int)dirtyPagesL2_.NumChunks(); j++)
  {
    const auto chunkL2 = dirtyPagesL2_.GetChunk(j);
    for (int bitL2 = std::countr_zero(chunkL2); bitL2 < Core::BitArray::bits_per_chunk; bitL2++)
    {
      if (chunkL2 & (1 << bitL2))
      {
        const int chunkOffset = j * L2_PAGE_SIZE + bitL2;
        for (int i = chunkOffset; i < int(std::min(chunkOffset + L2_PAGE_SIZE, dirtyPages_.NumChunks())); i++)
        //for (int i = 0; i < int(dirtyPages_.NumChunks()); i++) // Comment out the preceding control flow 
        {
          const auto chunk = dirtyPages_.GetChunk(i);
          for (int bit = std::countr_zero(chunk); bit < (int)Core::BitArray::bits_per_chunk; bit++)
          {
            if (chunk & (1 << bit))
            {
              const auto page   = i * Core::BitArray::bits_per_chunk + bit;
              const auto offset = page * PAGE_SIZE;
              vkCmdUpdateBuffer(cmd, gpuBuffer_->Handle(), offset, PAGE_SIZE, cpuBuffer_.get() + offset);
            }
          }

          dirtyPages_.SetChunk(i, 0);
        }
      }
    }

    dirtyPagesL2_.SetChunk(j, 0);
  }
}
#endif
