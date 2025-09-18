#pragma once
#include "Core/Reflection.h"

#include "entt/meta/meta.hpp"
#include "entt/entity/entity.hpp"

#include <optional>
#include <vector>

namespace Networking
{
  struct RpcInfo
  {
    std::optional<entt::entity> owningConnection = std::nullopt;
    Core::Reflection::RpcTraits traits;
    entt::id_type funcId;
    std::vector<entt::meta_any> args;
  };
} // namespace Networking