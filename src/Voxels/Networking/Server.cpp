#include "Server.h"
#include "../Serialization.h"
#include "../Game.h"
#include "../TwoLevelGrid.h"
#include "../Assert2.h"
#include "enet/enet.h"
#include "spdlog/spdlog.h"
#include "tracy/Tracy.hpp"

#include <unordered_set>
#include <span>

static std::string format_as(const ENetAddress& addr)
{
  char name[256] = {};
  ASSERT(enet_address_get_host_ip(&addr, name, 256) == 0);
  return fmt::format("[{}]:{}", name, addr.port);
}

Networking::Server::Server(World& world)
  : world_(&world)
{
  spdlog::info("Creating server");
  ASSERT(enet_initialize() == 0);

  const auto address = ENetAddress{
    .host = ENET_HOST_ANY,
    .port = 1234, // TODO: don't hardcode this
    .sin6_scope_id = 0,
  };

  localHost_ = enet_host_create(&address, 32, 2, 0, 0);

  ASSERT(localHost_);

  localHost_->maximumPacketSize  = 200 * 1024 * 1024;
  localHost_->maximumWaitingData = 500 * 1024 * 1024;

  world_->GetRegistry().on_destroy<entt::entity>().connect<&Server::OnEntityDestroy>(*this);
  spdlog::info("Created server bound to {}", address);
}

Networking::Server::~Server()
{
  spdlog::info("Shutting down server");
  world_->GetRegistry().on_destroy<entt::entity>().disconnect<&Server::OnEntityDestroy>(*this);
  for (auto& [peer, entity] : connections_)
  {
    // TODO: Destroy entity.
    enet_peer_disconnect_now(peer, 0);
  }
  enet_host_destroy(localHost_);
  enet_deinitialize();
}

void Networking::Server::ProcessMessages([[maybe_unused]] World& world)
{
  // TODO: "TEMP": Put host service here until it's moved to another thread.
  auto event = ENetEvent{};
  while (true)
  {
    if (int ret = enet_host_service(localHost_, &event, 5) > 0) // TODO: remove stinky 5ms wait
    {
      switch (event.type)
      {
      case ENET_EVENT_TYPE_CONNECT:
      {
        spdlog::info("Connected to {}", event.peer->address);
        auto& pair = *connections_.emplace(event.peer, ClientInfo{.entity = world.GetRegistry().create(), .status = ClientStatus::Joining}).first;

        // TODO: Send packet informing the client of their entity ID.

        auto packetInfo = Core::Serialization::Packet{
          .type  = entt::type_id<TwoLevelGrid>().hash(),
          .bytes = Core::Serialization::SerializeTwoLevelGrid(world.GetRegistry().ctx().get<TwoLevelGrid>()),
        };

        auto packetSerialized = Core::Serialization::SerializePacket(packetInfo);

        auto* packet = enet_packet_create(packetSerialized.data(), packetSerialized.size(), ENET_PACKET_FLAG_RELIABLE);
        packet->userData = &pair;

        enet_packet_set_free_callback(packet,
          [](ENetPacket* packet)
          {
            auto* ppair = static_cast<std::remove_reference_t<decltype(pair)>*>(packet->userData);
            ppair->second.status = ClientStatus::Connected;
            spdlog::info("World transmitted to {}", ppair->first->address);
          });

        enet_peer_send(event.peer, 0, packet);
        
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE:
      {
        spdlog::info("Message received from {} with {} bytes", event.peer->address, event.packet->dataLength);
        HandlePacket(world, event.peer, *event.packet);
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT:
      {
        spdlog::info("Disconnected from {}", event.peer->address);
        event.peer->data = nullptr;
        connections_.erase(event.peer);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
      {
        spdlog::info("Disconnected (timeout) from {}", event.peer->address);
        event.peer->data = nullptr; // Placeholder: this code should be replaced by actual peer cleanup.
        connections_.erase(event.peer);
        break;
      }
      case ENET_EVENT_TYPE_NONE: [[fallthrough]];
      default: PANIC;
      }
    }
    else if (ret < 0)
    {
      spdlog::error("[Server] Error processing event: {}", ret);
    }
    else // ret == 0
    {
      break;
    }
  }
}

static void AddEntityAndChildrenToVector(const entt::registry& registry, entt::entity entity, std::vector<entt::entity>& vec)
{
  vec.push_back(entity);
  if (auto* h = registry.try_get<Hierarchy>(entity))
  {
    for (auto child : h->children)
    {
      AddEntityAndChildrenToVector(registry, child, vec);
    }
  }
}

void Networking::Server::SendMessages([[maybe_unused]] World& world)
{
  ZoneScoped;
  if (connections_.empty())
  {
    return;
  }

  // Set of entities that have no parent.
  auto rootEntities = std::unordered_set<entt::entity>();

  for (auto entity : world.GetRegistry().view<LocalTransform, Player>())
  {
    rootEntities.emplace(world.GetRootEntityOfHierarchy(entity));
  }

  // For each root entity, create a serialized "bundle" containing it and all descendants, then send it.
  for (auto rootEntity : rootEntities)
  {
    auto bundle = Core::Serialization::SerializedEntityBundle{};
    AddEntityAndChildrenToVector(world.GetRegistry(), rootEntity, bundle.entities);
    auto string = std::string();
    for (auto entity : bundle.entities)
    {
      bundle.serializedEntities.emplace_back(Core::Serialization::SerializeEntity(world, entity));
      string += fmt::format("{}, ", entt::to_integral(entity));
    }
    spdlog::info("Sending entities: {}", string);

    auto packetInfo = Core::Serialization::Packet{
      .type  = entt::type_id<Core::Serialization::SerializedEntityBundle>().hash(),
      .bytes = Core::Serialization::SerializeEntityBundle(bundle),
    };

    auto packetSerialized = Core::Serialization::SerializePacket(packetInfo);

    auto* packet = enet_packet_create(packetSerialized.data(), packetSerialized.size(), ENET_PACKET_FLAG_RELIABLE);

    for (auto& [peer, clientInfo] : connections_)
    {
      if (clientInfo.status == ClientStatus::Connected)
      {
        enet_peer_send(peer, 0, packet);
      }
    }
  }
}

void Networking::Server::OnEntityDestroy(entt::registry&, entt::entity entity)
{
  if (connections_.empty())
  {
    return;
  }

  auto packetInfo = Core::Serialization::Packet{
    .type  = entt::type_id<entt::entity>().hash(),
    .bytes = Core::Serialization::SerializeEntityId(entity),
  };

  auto packetSerialized = Core::Serialization::SerializePacket(packetInfo);

  auto* packet = enet_packet_create(packetSerialized.data(), packetSerialized.size(), ENET_PACKET_FLAG_RELIABLE);

  for (auto& [peer, clientInfo] : connections_)
  {
    if (clientInfo.status == ClientStatus::Connected)
    {
      enet_peer_send(peer, 0, packet);
    }
  }
}

void Networking::Server::HandlePacket(World& world, ENetPeer* peer, const ENetPacket& enetPacket)
{
  ZoneScoped;
  auto peerIt = connections_.find(peer);
  ASSERT(peerIt != connections_.end());
  auto packet = Core::Serialization::DeserializePacket(std::span{(const char*)enetPacket.data, enetPacket.dataLength});

  if (packet.type == entt::type_id<InputState>().hash())
  {
    auto inputState = Core::Serialization::DeserializeInputState(packet.bytes);
    world.GetRegistry().emplace_or_replace<InputState>(peerIt->second.entity, inputState);
  }
}
