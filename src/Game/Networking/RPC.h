#pragma once
#include "entt/entity/fwd.hpp"
#include "entt/meta/meta.hpp"

#include <iosfwd>
#include <optional>

class World;

namespace Networking
{
  namespace detail
  {
    void InvokeSerializedRPC(World& world, std::stringstream& stream);
    // Pre: args is an array with numArgs elements.
    // Pre: the first element of args is always World&.
    void CallRPCInternal(entt::id_type funcId, std::optional<entt::entity> owningConnection, World& world, entt::meta_any* args, size_t numArgs);
  }

  // If an owning connection (entity that is owned by a peer) is provided and it is
  // owned by a remote, then the RPC will be executed on only that remote for certain RPC types.
  template<typename... Args>
  void CallRPC2(entt::id_type funcId, std::optional<entt::entity> owningConnection, World& world, Args&&... args)
  {
    detail::CallRPCInternal(funcId,
      owningConnection,
      world,
      std::array<entt::meta_any, 1 + sizeof...(Args)>{entt::forward_as_meta(world), entt::meta_any{std::forward<Args>(args)}...}.data(),
      1 + sizeof...(Args));
  }

  template<typename... Args>
  void CallRPC(entt::id_type funcId, World& world, Args&&... args)
  {
    CallRPC2(funcId, std::nullopt, world, std::forward<Args>(args)...);
  }
}
