#include "SketchyBuffer.h"

#ifndef GAME_HEADLESS
  #include "Client/Fvog/Buffer2.h"
  #include "volk.h"
  #include <unordered_set>
#endif

#include "Client/Fvog/detail/Common.h"
#include "tracy/Tracy.hpp"
#include "vk_mem_alloc.h"

#include <memory>
#include <bit>

namespace
{
  constexpr bool profileVoxelPool = true;

  [[maybe_unused]] constexpr auto poolTracyHeapName = "Voxel Storage (CPU & GPU)";
} // namespace

class SketchyBufferImpl final : public SketchyBuffer
{
public:
  explicit SketchyBufferImpl(size_t bufferSize, std::string name = {});
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
  size_t bufferSize_{};
  std::unique_ptr<std::byte[]> cpuBuffer_;
  VmaVirtualBlock allocator_{};

#ifndef GAME_HEADLESS
public:
  void FlushWritesToGPU(VkCommandBuffer cmd) override;

  Fvog::Buffer& GetGpuBuffer() override
  {
    return gpuBuffer_;
  }

  void MarkRange(size_t offset, size_t size) override
  {
    const auto minPage = offset / PAGE_SIZE;
    const auto maxPage = (offset + size) / PAGE_SIZE;
    for (auto i = minPage; i <= maxPage; i++)
    {
      dirtyPages_.insert(uint32_t(i));
    }
  }

private:
  Fvog::Buffer gpuBuffer_;
  std::unordered_set<uint32_t> dirtyPages_;
#endif
};

std::unique_ptr<SketchyBuffer> SketchyBuffer::Create(size_t bufferSize, std::string name)
{
  return std::make_unique<SketchyBufferImpl>(bufferSize, name);
}


SketchyBufferImpl::SketchyBufferImpl(size_t bufferSize, [[maybe_unused]] std::string name)
  : bufferSize_(bufferSize)
#ifndef GAME_HEADLESS
    , gpuBuffer_({.size = bufferSize, .flag = Fvog::BufferFlagThingy::NONE}, std::move(name))
#endif
{
  ZoneScoped;
  cpuBuffer_ = std::make_unique_for_overwrite<std::byte[]>(bufferSize);
  std::memset(cpuBuffer_.get(), 0xCD, bufferSize);
  Fvog::detail::CheckVkResult(vmaCreateVirtualBlock(Fvog::detail::Address(VmaVirtualBlockCreateInfo{.size = bufferSize}), &allocator_));
}

SketchyBufferImpl::~SketchyBufferImpl()
{
  if constexpr (profileVoxelPool)
  {
    TracyMemoryDiscard(poolTracyHeapName);
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
    dirtyPages_(std::move(old.dirtyPages_))
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
    TracyAllocN(allocation, size, poolTracyHeapName);
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
    TracyFreeN(alloc.allocation, poolTracyHeapName);
  }
  vmaVirtualFree(allocator_, alloc.allocation);
}

#ifndef GAME_HEADLESS
void SketchyBufferImpl::FlushWritesToGPU(VkCommandBuffer cmd)
{
  ZoneScoped;
  // All pages need to be flushed or the underlying data may have invalid references/indices
  for (uint32_t page : dirtyPages_)
  {
    auto offset = page * PAGE_SIZE;
    vkCmdUpdateBuffer(cmd, gpuBuffer_.Handle(), offset, PAGE_SIZE, cpuBuffer_.get() + offset);
  }

  dirtyPages_.clear();
}
#endif
