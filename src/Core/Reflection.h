#pragma once

#include "entt/fwd.hpp"

#include <cstdint>
#include <unordered_map>

namespace Core::Reflection
{
  enum Traits : uint16_t
  {
    // The component or member will not be serialized.
    // When loaded or replicated on the network, it will be initialized with its default value.
    TRANSIENT = 1 << 0,

    // The component or member will be completely excluded from the editor.
    NO_EDITOR = 1 << 1,

    // The component or member will appear in the editor, but cannot be modified.
    // Component with this trait cannot be directly added from the "Add Component" dropdown.
    EDITOR_READ_ONLY = 1 << 2,

    // The type is a variant and has registered the following functions (see the VARIANT_FUNCS macro in Reflection.cpp):
    // "type_hash"_hs: returns the hash (with entt::type_id<T>().hash()) of its currently held type.
    // "const_value"_hs: returns a const ref to the currently held value.
    // "value"_hs: returns a ref to the currently held value.
    VARIANT = 1 << 3,

    // Indicates top-level component types. Components appear in the editor and serve
    // as the root for serialization (saving, loading, and network replication).
    COMPONENT = 1 << 4,

    // The component will be replicated to clients on the network.
    REPLICATED = 1 << 5,

    // The type can be trivially copied (e.g. by memcpy) instead of member-wise copied.
    // Takes precedence over specific serialize functions.
    TRIVIAL = 1 << 6,
  };

  constexpr Traits operator|(Traits a, Traits b)
  {
    return static_cast<Traits>((uint32_t)a | (uint32_t)b);
  }

  // https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine#matrixofrpcexecution
  enum class RpcTraits
  {
    // Executes on clients or the server if there's no applicable client connection.
    Client = 1 << 0,

    // Executes on the server only.
    Server = 1 << 1,

    // Executes on the remote side of the connection only.
    Remote = 1 << 2,

    // Executes on all clients and the server. If called by a client, only executes on that client.
    Broadcast = 1 << 3,

    // Low latency, but RPC may be dropped. RPCs are otherwise reliable.
    Unreliable = 1 << 4,

    // Intended for RPCs that interact with the voxel world. These go on another channel to not block other gameplay events.
    UseVoxelChannel = 1 << 5,
  };

  constexpr RpcTraits operator|(RpcTraits a, RpcTraits b)
  {
    return static_cast<RpcTraits>((uint32_t)a | (uint32_t)b);
  }

  constexpr RpcTraits operator&(RpcTraits a, RpcTraits b)
  {
    return static_cast<RpcTraits>((uint32_t)a & (uint32_t)b);
  }

  using PropertiesMap = std::unordered_map<entt::id_type, entt::meta_any>;
  
  const char* EnumToString(entt::meta_any value);

  void Initialize();
}
