#pragma once
#include "Interface.h"

#include <memory>

class World;

namespace Networking
{
  class Server : public Interface
  {
  public:
    static [[nodiscard]] std::unique_ptr<Server> Create(World& world);

    virtual [[nodiscard]] size_t GetNumberOfConnections() const = 0;
  };
} // namespace Networking
