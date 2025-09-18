#pragma once
#include "Core/ClassImplMacros.h"
#include "Client/Fvog/detail/VmaFwd.h"
#include "Core/Assert2.h"

#ifndef GAME_HEADLESS
  #include "Client/Fvog/detail/VkFwd.h"
namespace Fvog
{
  class Buffer;
}
#endif

#include <string>
#include <memory>

// A CPU and GPU buffer pair, with CPU writes replicated on the GPU.
// Can be used to make index-based data structures "just work" on the GPU.
// Headless instances do not care about the GPU part, so that is disabled.
class SketchyBuffer
{
public:
  static std::unique_ptr<SketchyBuffer> Create(size_t bufferSize, std::string name = {});

  SketchyBuffer()          = default;
  virtual ~SketchyBuffer() = default;

  NO_COPY(SketchyBuffer);
  DEFAULT_MOVE(SketchyBuffer);

  struct Alloc
  {
    // Offset may differ from the one returned by
    size_t offset;
    VmaVirtualAllocation allocation;

    bool operator==(const Alloc&) const noexcept = default;
  };

  virtual Alloc Allocate(size_t size, size_t alignment) = 0;
  virtual void Free(Alloc alloc) = 0;

  // Get the address of the base object for indexing
  template<typename T>
  [[nodiscard]] T* GetBase()
  {
    return static_cast<T*>(GetCpuBuffer());
  }

  template<typename T>
  [[nodiscard]] const T* GetBase() const
  {
    return static_cast<const T*>(GetCpuBuffer());
  }

  [[nodiscard]] virtual VmaVirtualBlock GetAllocator() const = 0;

  [[nodiscard]] virtual size_t SizeBytes() const = 0;

  [[nodiscard]] virtual size_t PageSize() const = 0;

  [[nodiscard]] virtual void* GetCpuBuffer() = 0;
  [[nodiscard]] virtual const void* GetCpuBuffer() const = 0;

#ifndef GAME_HEADLESS
  virtual void FlushWritesToGPU(VkCommandBuffer cmd) = 0;

  virtual Fvog::Buffer& GetGpuBuffer() = 0;

  virtual void MarkRange(size_t offset, size_t size) = 0;

  // Update the object at an address that aliases the array returned by GetBase.
  template<typename T>
  void MarkDirtyPages(const T* address)
  {
    const auto* byteAddress = reinterpret_cast<const std::byte*>(address);
    ASSERT(byteAddress >= GetCpuBuffer());

    // Mark the pages affected by the write as dirty
    const auto offsetBytes = reinterpret_cast<intptr_t>(byteAddress) - reinterpret_cast<intptr_t>(GetCpuBuffer());
    MarkRange(offsetBytes, sizeof(T));
  }
#endif
};