#pragma once
#include "Interface.h"
#include "../Game.h"
#include "../Serialization.h"
#include "../Reflection.h"
#include "../Assert2.h"

#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"

#include "tracy/Tracy.hpp"
#include "spdlog/spdlog.h"

#include <sstream>
#include <span>

namespace Networking
{
  // Execute the RPC on the remote end of the connection only.
  template<typename... Args>
  void CallRPC(World& world, entt::id_type funcId, Args&& ...args)
  {
    ZoneScoped;
    using Core::Reflection::RpcTraits;

    auto meta = entt::resolve<RpcTraits>();
    auto func = meta.func(funcId);
    ASSERT(func, "RPC does not exist.");
    const auto traits = func.traits<RpcTraits>();
    ASSERT(bool(traits & (RpcTraits::Client | RpcTraits::Server | RpcTraits::Broadcast | RpcTraits::Remote)));

    // TODO: Validate argument types.

    bool execLocally = false;
    execLocally |= world.IsServer() && bool(traits & RpcTraits::Server);
    // TODO: Server should execute client RPCs when there isn't a client available.
    //execLocally |= world.IsServer() && bool(traits & RpcTraits::Client);
    execLocally |= world.IsClient() && bool(traits & RpcTraits::Client);
    execLocally |= world.IsClient() && bool(traits & RpcTraits::Broadcast);

    bool execRemotely = bool(traits & RpcTraits::Remote);
    execRemotely |= world.IsServer() && bool(traits & RpcTraits::Broadcast);
    execRemotely |= world.IsServer() && bool(traits & RpcTraits::Client);
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

    auto& networking = *world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>();
    if (!networking)
    {
      return;
    }
    
    auto stream = std::stringstream();

    Core::Serialization::SerializeObjectStream(stream, funcId);
    (Core::Serialization::SerializeObjectStream(stream, entt::forward_as_meta(args)), ...);

    networking->EnqueueRPC({traits, {std::istreambuf_iterator{stream}, std::istreambuf_iterator<char>{}}});
  }

  namespace detail
  {
    void InvokeSerializedRPC(World& world, std::span<const char> serializedRpc);
  }
}
