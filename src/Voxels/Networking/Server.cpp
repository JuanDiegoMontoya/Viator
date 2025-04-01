#include "Server.h"
#include "../Serialization.h"
#include "../Reflection.h"
#include "../Game.h"
#include "../TwoLevelGrid.h"
#include "../Assert2.h"
#include "RPC.h"

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
        auto stream = std::stringstream();

        Core::Serialization::SerializeObjectStream(stream, PacketType::TwoLevelGrid);
        Core::Serialization::SerializeObjectStream(stream, entt::forward_as_meta(world.GetRegistry().ctx().get<TwoLevelGrid>()));

        auto* packet = enet_packet_create(stream.view().data(), stream.view().size(), ENET_PACKET_FLAG_RELIABLE);
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
        spdlog::trace("Message received from {} with {} bytes", event.peer->address, event.packet->dataLength);
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
    rpcs_.clear();
    return;
  }

  // Flush RPCs
  while (!rpcs_.empty())
  {
    auto rpc = rpcs_.pop_front();

    using Core::Reflection::RpcTraits;

    auto stream = std::stringstream();

    Core::Serialization::SerializeObjectStream(stream, PacketType::Rpc);
    stream.write(rpc.serializedRpc.data(), rpc.serializedRpc.size());

    const auto packetFlags = bool(rpc.traits & RpcTraits::Unreliable) ? ENET_PACKET_FLAG_UNSEQUENCED : ENET_PACKET_FLAG_RELIABLE;
    auto* packet = enet_packet_create(stream.view().data(), stream.view().size(), packetFlags);

    for (auto& [peer, clientInfo] : connections_)
    {
      // TODO: Filter RPC based on connection type.
      if (clientInfo.status == ClientStatus::Connected)
      {
        enet_peer_send(peer, 0, packet);
      }
    }
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
    auto stream = std::stringstream();

    Core::Serialization::SerializeObjectStream(stream, PacketType::EntityBundle);

    auto entities = std::vector<entt::entity>();
    AddEntityAndChildrenToVector(world.GetRegistry(), rootEntity, entities);
    Core::Serialization::SerializeObjectStream(stream, entities);
    //auto string = std::string();
    for (auto entity : entities)
    {
      Core::Serialization::SerializeEntity(stream, world, entity);
      //string += fmt::format("{}, ", entt::to_integral(entity));
    }
    //spdlog::info("Sending entities: {}", string);

    auto* packet = enet_packet_create(stream.view().data(), stream.view().size(), ENET_PACKET_FLAG_RELIABLE);

    for (auto& [peer, clientInfo] : connections_)
    {
      if (clientInfo.status == ClientStatus::Connected)
      {
        enet_peer_send(peer, 0, packet);
      }
    }
  }
}

void Networking::Server::EnqueueRPC(RpcInfo rpc)
{
  rpcs_.push_back(std::move(rpc));
}

void Networking::Server::OnEntityDestroy(entt::registry&, entt::entity entity)
{
  if (connections_.empty())
  {
    return;
  }

  auto stream = std::stringstream();
  Core::Serialization::SerializeObjectStream(stream, PacketType::RemovedEntity);
  Core::Serialization::SerializeObjectStream(stream, entity);

  auto* packet = enet_packet_create(stream.view().data(), stream.view().size(), ENET_PACKET_FLAG_RELIABLE);

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

  auto stream = std::stringstream(std::string(reinterpret_cast<const char*>(enetPacket.data), enetPacket.dataLength));

  auto packetType = Core::Serialization::DeserializeObjectStream<PacketType>(stream);

  if (packetType == PacketType::InputState)
  {
    auto inputState = Core::Serialization::DeserializeObjectStream<InputState>(stream);
    world.GetRegistry().emplace_or_replace<InputState>(peerIt->second.entity, inputState);
  }
  else if (packetType == PacketType::Rpc)
  {
    detail::InvokeSerializedRPC(world, stream);
  }
}
