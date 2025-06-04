#pragma once
#include "Interface.h"
#include "Game/World.h"
#include "Core/Serialization.h"
#include "Core/Reflection.h"
#include "Core/Assert2.h"

#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"

#include "tracy/Tracy.hpp"
#include "spdlog/spdlog.h"

#include <sstream>
#include <span>

namespace Networking
{
  // If an owning connection (entity that is owned by a peer) is provided and it is
  // owned by a remote, then the RPC will be executed on only that remote for certain RPC types.
  template<typename... Args>
  void CallRPC2(entt::id_type funcId, std::optional<entt::entity> owningConnection, World& world, Args&&... args)
  {
    ZoneScoped;
    using Core::Reflection::RpcTraits;

    auto meta = entt::resolve<RpcTraits>();
    auto func = meta.func(funcId);
    ASSERT(func, "RPC does not exist.");
    const auto traits = func.traits<RpcTraits>();
    ASSERT(bool(traits & (RpcTraits::Client | RpcTraits::Server | RpcTraits::Broadcast | RpcTraits::Remote)));
    ASSERT(!(world.IsClient() && owningConnection), "If the caller is a client, owningConnection must be null.");

    // TODO: Validate argument types.

    auto& networking = *world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>();

    if (owningConnection && (!networking || !networking->IsEntityOwnedByRemote(*owningConnection)))
    {
      owningConnection = std::nullopt;
    }

    bool execLocally = bool(traits & RpcTraits::Broadcast);
    execLocally |= world.IsServer() && bool(traits & RpcTraits::Server);
    execLocally |= world.IsServer() && bool(traits & RpcTraits::Client) && !owningConnection;
    execLocally |= world.IsClient() && bool(traits & RpcTraits::Client);

    bool execRemotely = bool(traits & RpcTraits::Remote);
    execRemotely |= world.IsServer() && bool(traits & RpcTraits::Broadcast);
    execRemotely |= world.IsServer() && bool(traits & RpcTraits::Client) && owningConnection;
    execRemotely |= world.IsClient() && bool(traits & RpcTraits::Server);

    if (!execLocally && !execRemotely)
    {
      spdlog::warn("RPC dropped.");
      return;
    }

    if (execLocally)
    {
      // Directly invoking a function does NOT allow for overload resolution. This is fine as EnTT seemingly
      // does not offer a way to select an overload prior to its execution, which would be needed to query
      // its traits and therefore determine executability.
      auto ret = func.invoke({}, entt::forward_as_meta(world), entt::forward_as_meta(args)...);
      ASSERT(ret, "Failed to execute RPC locally.");

      if (!(world.IsServer() && bool(traits & RpcTraits::Broadcast)))
      {
        return;
      }
    }

    if (!networking)
    {
      return;
    }

    // Args are simply copied in to guard against use-after-free.
    networking->EnqueueRPC({owningConnection, traits, funcId, {args...}});
  }

  template<typename... Args>
  void CallRPC(entt::id_type funcId, World& world, Args&&... args)
  {
    CallRPC2(funcId, std::nullopt, world, std::forward<Args>(args)...);
  }

  namespace detail
  {
    void InvokeSerializedRPC(World& world, std::stringstream& stream);
  }
}
