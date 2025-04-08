#include "RPC.h"
#include "Client.h"

#include "tracy/Tracy.hpp"

#include <sstream>

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
  auto result = func.invoke({}, args.data(), args.size());
  ASSERT(result);
}
