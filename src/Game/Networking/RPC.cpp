#include "RPC.h"
#include "Interface.h"
#include "Client.h"
#include "Core/Serialization.h"
#include "Core/Reflection.h"
#include "Core/Assert2.h"
#include "RpcInfo.h"
#include "Game/World.h"

#include "tracy/Tracy.hpp"
#include "spdlog/spdlog.h"
#include "entt/meta/resolve.hpp"

#include <span>
#include <ranges>
#include <sstream>

using namespace entt::literals;

void Networking::detail::InvokeSerializedRPC(World& world, std::stringstream& stream)
{
  ZoneScoped;
  auto id = Core::Serialization::DeserializeObjectStream<entt::id_type>(stream);
  ASSERT(id);
  auto meta = entt::resolve<Core::Reflection::RpcTraits>();
  ASSERT(meta);
  auto func = meta.func(id);
  ASSERT(func);
  if (auto* props = static_cast<const Core::Reflection::PropertiesMap*>(func.custom()))
  {
    if (auto it = props->find("name"_hs); it != props->end())
    {
      if (auto* name = it->second.try_cast<const char*>())
      {
        ZoneText(*name, std::strlen(*name));
        SPDLOG_TRACE("Executing serialized RPC {}", *name);
      }
    }
  }
  auto args = std::vector<entt::meta_any>();
  args.emplace_back(entt::forward_as_meta(world));
  for (int i = 1; i < func.arity(); i++)
  {
    auto& arg = args.emplace_back(Core::Serialization::DeserializeObjectStream(stream, func.arg(i)));
    // If client, remap remote entity IDs to local.
    if (world.IsClient() && func.arg(i).id() == entt::type_id<entt::entity>().hash())
    {
      const auto* networking    = world.GetRegistry().ctx().get<std::unique_ptr<Networking::Interface>*>();
      const auto& remoteToLocal = static_cast<Networking::Client*>(networking->get())->GetRemoteToLocalEntityMap();
      auto success = arg.assign(remoteToLocal.at(arg.cast<entt::entity>()));
      ASSERT(success);
    }
  }
  {
    ZoneScopedN("func.invoke()");
    auto result = func.invoke({}, args.data(), args.size());
    ASSERT(result);
  }
}

void Networking::detail::CallRPCInternal(entt::id_type funcId, std::optional<entt::entity> owningConnection, World& world, entt::meta_any* args, size_t numArgs)
{
  ZoneScoped;
  ASSERT(numArgs >= 1);
  ASSERT(args[0].type().id() == entt::type_id<World>().hash());

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
    ZoneScopedN("func.invoke()");
    // Directly invoking a function does NOT allow for overload resolution. This is fine as EnTT seemingly
    // does not offer a way to select an overload prior to its execution, which would be needed to query
    // its traits and therefore determine executability.
    auto ret = func.invoke({}, args, numArgs);
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
  networking->EnqueueRPC({owningConnection, traits, funcId, std::ranges::to<std::vector<entt::meta_any>>(std::span{args, numArgs}.subspan(1))});
}
