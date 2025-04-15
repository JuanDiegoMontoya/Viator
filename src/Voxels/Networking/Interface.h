#pragma once
#include "../ClassImplMacros.h"
#include "../Reflection.h"

#include "entt/fwd.hpp"
#include "entt/meta/meta.hpp"

#include <vector>
#include <optional>

class World;
typedef struct _ENetCompressor ENetCompressor;

namespace Networking
{
  struct RpcInfo
  {
    std::optional<entt::entity> owningConnection = std::nullopt;
    Core::Reflection::RpcTraits traits;
    entt::id_type funcId;
    std::vector<entt::meta_any> args;
  };

  class Interface
  {
  public:
    Interface() = default;
    NO_COPY_NO_MOVE(Interface);
    virtual ~Interface() = default;

    virtual void ProcessMessages(World&) = 0;
    virtual void SendMessages(World&)    = 0;
    virtual void EnqueueRPC(RpcInfo rpc) = 0;
    virtual bool IsEntityOwnedByRemote(entt::entity entity) = 0;
  };

  enum class ClientStatus
  {
    Resolving,
    Joining,
    Connected,
    Disconnected,
  };

  enum class PacketType : uint8_t
  {
    // std::vector<entt::entity> + serialized entities
    EntityBundle,

    // entt::id_type + serialized args
    Rpc,

    // std::vector<entt::entity>
    RemovedEntities,

    // InputState + InputLookState
    InputState,

    // TwoLevelGrid
    TwoLevelGrid,

    // Stream of modified components.
    ModifiedComponents,

    // Stream of components.
    InitialEntityState,

    // Server tick identifier. Used to synchronize events on the client and server in traces.
    TickNumber,

    // Stream of packets. Used for data that must arrive simultaneously (such as modified components and removed enitties).
    MultiPacket,

    // Bitwise AND this mask with a PacketType to extract the underlying packet type (one of the above enumerators).
    TypeMask = 0b0111'1111,

    // Indicates that the packet is compressed. Bitwise OR'd with another packet type.
    // Compressed packets contain a uint32 after the packet type to indicate their uncompressed size.
    Compressed = 0b1000'0000,
  };

  constexpr PacketType operator|(PacketType a, PacketType b)
  {
    return static_cast<PacketType>((uint32_t)a | (uint32_t)b);
  }

  constexpr PacketType operator&(PacketType a, PacketType b)
  {
    return static_cast<PacketType>((uint32_t)a & (uint32_t)b);
  }

  enum class Channel
  {
    // Modifications to the voxel world.
    Voxels,

    // Ordered (not necessarily reliable) entity state sent by the server.
    // Also includes reliable RPCs as it's likely these need to be sequenced with replicated state.
    Replicate,

    // Unimportant one-shot events.
    UnreliableRpc,

    NumChannels,
  };

  namespace detail
  {
    ENetCompressor GetCompressor();
  }
}
