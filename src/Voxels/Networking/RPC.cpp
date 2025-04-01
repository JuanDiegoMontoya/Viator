#include "RPC.h"

void Networking::detail::InvokeSerializedRPC(World& world, std::span<const char> serializedRpc)
{
  auto stream = std::stringstream(std::string(serializedRpc.data(), serializedRpc.size()));
  auto funcId = Core::Serialization::DeserializeObjectStream(stream);
  ASSERT(funcId.type().id() == entt::type_id<entt::id_type>().hash());
  auto id = funcId.try_cast<entt::id_type>();
  ASSERT(id);
  auto meta = entt::resolve<Core::Reflection::RpcTraits>();
  ASSERT(meta);
  auto func = meta.func(*id);
  ASSERT(func);
  auto args = std::vector<entt::meta_any>();
  args.emplace_back(entt::forward_as_meta(world));
  for (int i = 1; i < func.arity(); i++)
  {
    args.emplace_back(Core::Serialization::DeserializeObjectStream(stream));
  }
  auto result = func.invoke({}, args.data(), args.size());
  ASSERT(result);
}
