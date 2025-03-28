#pragma once
#include "../ClassImplMacros.h"

class World;

namespace Networking
{
  class Interface
  {
  public:
    Interface() = default;
    NO_COPY_NO_MOVE(Interface);
    virtual ~Interface() = default;

    virtual void ProcessMessages(World&) = 0;
    virtual void SendMessages(World&) = 0;
  };

  enum class ClientStatus
  {
    Resolving,
    Joining,
    Connected,
    Disconnected,
  };
}
