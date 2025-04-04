#include "Interface.h"

#include "glm/common.hpp"
#include "enet/enet.h"
#include "zstd.h"
#include "spdlog/spdlog.h"
#include "tracy/Tracy.hpp"

ENetCompressor Networking::detail::GetCompressor()
{
  return ENetCompressor{
    .context  = (void*)1, // ENet requires this to be a non-null pointer, so we'll use an arbitrary one (no pun intended).
    .compress = [](void*, const ENetBuffer* buffers, size_t inBufferCount, size_t inLimit, enet_uint8* outData, size_t outLimit) -> size_t
    {
      ZoneScopedN("Compress UDP Packet");
      auto tempBufferIn   = std::make_unique<char[]>(ZSTD_compressBound(inLimit));
      auto remainingBytes = inLimit;
      auto offset         = size_t{0};
      for (size_t i = 0; i < inBufferCount; i++)
      {
        auto toCopy = glm::min(remainingBytes, buffers[i].dataLength);
        std::memcpy(tempBufferIn.get() + offset, buffers[i].data, toCopy);
        remainingBytes -= toCopy;
        offset += toCopy;
      }

      auto tempBufferOut = std::make_unique<char[]>(ZSTD_compressBound(inLimit));
      auto ret           = ZSTD_compress(tempBufferOut.get(), ZSTD_compressBound(inLimit), tempBufferIn.get(), inLimit, ZSTD_CLEVEL_DEFAULT);

      if (ZSTD_isError(ret))
      {
        spdlog::error("Failed to compress packet: {}", ZSTD_getErrorName(ret));
        return 0;
      }

      if (ret > outLimit)
      {
        return 0;
      }

      std::memcpy(outData, tempBufferOut.get(), ret);
      return ret;
    },
    .decompress = [](void*, const enet_uint8* inData, size_t inLimit, enet_uint8* outData, size_t outLimit) -> size_t
    {
      ZoneScopedN("Decompress UDP Packet");
      auto ret = ZSTD_decompress(outData, outLimit, inData, inLimit);

      if (ZSTD_isError(ret))
      {
        spdlog::error("Failed to decompress packet: {}", ZSTD_getErrorName(ret));
        return 0;
      }

      return ret;
    },
    .destroy = nullptr,
  };
}
