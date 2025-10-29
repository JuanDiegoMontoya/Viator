#pragma once
#include "Interface.h"

#include <memory>

class World;

namespace Networking
{
  class Server : public Interface
  {
  public:
    [[nodiscard]] static std::unique_ptr<Server> Create(World& world);

    [[nodiscard]] virtual size_t GetNumberOfConnections() const = 0;
  };
} // namespace Networking
