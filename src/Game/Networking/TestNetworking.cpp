#include "Game/Networking/Client.h"
#include "Game/Networking/Server.h"
#include "Game/TestGame.h"

#include "doctest.h"

#include <thread>

TEST_CASE("Simple client and server")
{
  auto sGame   = TestGame::Create();
  auto cGame   = TestGame::Create();

  auto server = Networking::Server::Create(sGame->GetWorld());
  auto client = Networking::Client::Create(cGame->GetWorld(), "localhost");

  CHECK(client->GetStatus() == Networking::ClientStatus::Resolving);

  {
    auto sThread = std::jthread([&] { server->ProcessMessages(sGame->GetWorld(), 500); });
    auto cThread = std::jthread([&] { client->ProcessMessages(cGame->GetWorld(), 500); });
  }

  CHECK(client->GetStatus() == Networking::ClientStatus::Connected);
  CHECK(server->GetNumberOfConnections() == 1);
}
