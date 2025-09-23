#pragma once
#include "Interface.h"

#include "entt/entity/fwd.hpp"

#include <unordered_map>

namespace Networking
{
  class Client : public Interface
  {
  public:
    static std::unique_ptr<Client> Create(World& world, const char* hostName);

    [[nodiscard]] virtual ClientStatus GetStatus() const = 0;
    [[nodiscard]] virtual const std::unordered_map<entt::entity, entt::entity>& GetRemoteToLocalEntityMap() const = 0;
  };
}
