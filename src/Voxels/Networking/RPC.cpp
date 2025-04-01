#include "RPC.h"

#include <sstream>

void Networking::detail::InvokeSerializedRPC(World& world, std::stringstream& stream)
{
  auto id = Core::Serialization::DeserializeObjectStream<entt::id_type>(stream);
  ASSERT(id);
  auto meta = entt::resolve<Core::Reflection::RpcTraits>();
  ASSERT(meta);
  auto func = meta.func(id);
  ASSERT(func);
  auto args = std::vector<entt::meta_any>();
  args.emplace_back(entt::forward_as_meta(world));
  for (int i = 1; i < func.arity(); i++)
  {
    // TODO: If client, remap entity IDs to local.
    args.emplace_back(Core::Serialization::DeserializeObjectStream(stream, func.arg(i)));
  }
  auto result = func.invoke({}, args.data(), args.size());
  ASSERT(result);
}
