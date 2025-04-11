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
#include "zstd.h"

#include <unordered_set>
#include <span>
#include <memory>

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

  localHost_ = enet_host_create(&address, 32, int(Channel::NumChannels), 0, 0);

  ASSERT(localHost_);

  localHost_->maximumPacketSize  = 200 * 1024 * 1024;
  localHost_->maximumWaitingData = 500 * 1024 * 1024;
  localHost_->checksum           = &enet_crc32;
  
  auto compressor = detail::GetCompressor();
  enet_host_compress(localHost_, &compressor);

  world_->GetRegistryRaw().on_destroy<entt::entity>().connect<&Server::OnEntityDestroy>(*this);
  spdlog::info("Created server bound to {}", address);
}

Networking::Server::~Server()
{
  spdlog::info("Shutting down server");
  world_->GetRegistryRaw().on_destroy<entt::entity>().disconnect<&Server::OnEntityDestroy>(*this);
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
  ZoneScoped;
  auto event = ENetEvent{};
  while (true)
  {
    if (int ret = enet_host_service(localHost_, &event, 0) > 0)
    {
      switch (event.type)
      {
      case ENET_EVENT_TYPE_CONNECT:
      {
        ZoneScopedN("ENET_EVENT_TYPE_CONNECT");
        spdlog::info("Connected to {}", event.peer->address);
        const auto clientEntity = world.CreatePlayer();
        auto& pair              = *connections_.emplace(event.peer, ClientInfo{.entity = clientEntity, .status = ClientStatus::Connected}).first;
        entityToConnection_.emplace(clientEntity, event.peer);

        // 1. Send TwoLevelGrid.
        {
          auto stream = std::stringstream();
          Core::Serialization::SerializeObjectStream(stream, PacketType::TwoLevelGrid | PacketType::Compressed);
          const auto bytes             = Core::Serialization::SerializeObject(entt::forward_as_meta(world.GetRegistry().ctx().get<TwoLevelGrid>()));
          const auto maxCompressedSize = ZSTD_compressBound(bytes.size());

          auto tempBuffer = std::make_unique<char[]>(maxCompressedSize);
          auto zret       = ZSTD_compress(tempBuffer.get(), maxCompressedSize, bytes.data(), bytes.size(), ZSTD_CLEVEL_DEFAULT);
          ASSERT(!ZSTD_isError(zret), "Failed to compress TwoLevelGrid.");
          stream.write(tempBuffer.get(), zret);
          spdlog::info("Compressed world from {} bytes to {} bytes.", bytes.size(), zret);

          auto* packet     = enet_packet_create(stream.view().data(), stream.view().size(), ENET_PACKET_FLAG_RELIABLE);
          auto* ppair      = new auto(pair);
          packet->userData = ppair;
          
          enet_packet_set_free_callback(packet,
            [](ENetPacket* packet)
            {
              auto* ppair2 = static_cast<decltype(ppair)>(packet->userData);
              // ppair->second.status = ClientStatus::Connected;
              spdlog::info("World transmitted to {}", ppair2->first->address);
              delete ppair2;
            });

          enet_peer_send(event.peer, uint8_t(Channel::Voxels), packet);
        }

        // 2. Send entities.
        {
          auto stream = std::stringstream();
          Core::Serialization::SerializeObjectStream(stream, PacketType::InitialEntityState);
          Core::Serialization::SerializeAllEntitiesForNetwork(world, stream);
          auto* packet = enet_packet_create(stream.view().data(), stream.view().size(), ENET_PACKET_FLAG_RELIABLE);
          auto ppair = new auto(std::make_pair(&world, clientEntity));
          packet->userData = ppair;

          enet_packet_set_free_callback(packet,
            [](ENetPacket* packet)
            {
              // 3. Inform client which entity it owns.
              auto& [world, clientEntity] = *static_cast<decltype(ppair)>(packet->userData);
              CallRPC2("GiveLocalPlayerRPC"_hs, clientEntity, *world, clientEntity);
              delete static_cast<decltype(ppair)>(packet->userData);
            });

          enet_peer_send(event.peer, uint8_t(Channel::Replicate), packet);
        }
        
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE:
      {
        ZoneScopedN("ENET_EVENT_TYPE_RECEIVE");
        ZoneTextF("Packet number %u: %zu bytes", enet_host_get_packets_received(localHost_), event.packet->dataLength);
        SPDLOG_TRACE("Message received from {} with {} bytes", event.peer->address, event.packet->dataLength);
        HandlePacket(world, event.peer, *event.packet);
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT:
      {
        ZoneScopedN("ENET_EVENT_TYPE_DISCONNECT");
        spdlog::info("Disconnected from {}", event.peer->address);
        event.peer->data = nullptr;
        auto connection  = connections_.at(event.peer);
        world.GetRegistry().emplace<DeferredDelete>(connection.entity);
        entityToConnection_.erase(connection.entity);
        connections_.erase(event.peer);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
      {
        ZoneScopedN("ENET_EVENT_TYPE_DISCONNECT_TIMEOUT");
        spdlog::info("Disconnected (timeout) from {}", event.peer->address);
        event.peer->data = nullptr; // Placeholder: this code should be replaced by actual peer cleanup.
        auto connection  = connections_.at(event.peer);
        world.GetRegistry().emplace<DeferredDelete>(connection.entity);
        entityToConnection_.erase(connection.entity);
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
    removedEntities_.clear();
    return;
  }

  auto stream = std::stringstream();
  Core::Serialization::SerializeObjectStream(stream, PacketType::MultiPacket);

  {
    ZoneScopedN("Send current tick");
    ZoneTextF("Sending server tick %llu", world.GetTicks());
    spdlog::debug("Sending server tick {}", world.GetTicks());
    Core::Serialization::SerializeObjectStream(stream, PacketType::TickNumber);
    Core::Serialization::SerializeObjectStream(stream, world.GetTicks());
  }

  {
    ZoneScopedN("Replicate modified state");
    Core::Serialization::SerializeObjectStream(stream, PacketType::ModifiedComponents);
    Core::Serialization::SerializeModifiedComponents(world, stream, world.GetRegistry().GetModifiedComponents());
  }

  {
    ZoneScopedN("Send removed entities");
    if (!removedEntities_.empty())
    {
      Core::Serialization::SerializeObjectStream(stream, PacketType::RemovedEntities);
      Core::Serialization::SerializeObjectStream(stream, removedEntities_);
      removedEntities_.clear();
    }
  }

  auto* packet = enet_packet_create(stream.view().data(), stream.view().size(), ENET_PACKET_FLAG_RELIABLE);
  enet_host_broadcast(localHost_, uint8_t(Channel::Replicate), packet);

  FlushRPCs();
}

void Networking::Server::EnqueueRPC(RpcInfo rpc)
{
  rpcs_.push_back(std::move(rpc));
}

void Networking::Server::FlushRPCs()
{
  ZoneScoped;
  while (!rpcs_.empty())
  {
    auto rpc = rpcs_.pop_front();

    using Core::Reflection::RpcTraits;

    auto stream = std::stringstream();

    Core::Serialization::SerializeObjectStream(stream, PacketType::Rpc);
    Core::Serialization::SerializeObjectStream(stream, rpc.funcId);
    for (auto& arg : rpc.args)
    {
      Core::Serialization::SerializeObjectStream(stream, arg.as_ref());
    }

    const auto packetFlags = bool(rpc.traits & RpcTraits::Unreliable) ? ENET_PACKET_FLAG_UNSEQUENCED : ENET_PACKET_FLAG_RELIABLE;
    auto* packet           = enet_packet_create(stream.view().data(), stream.view().size(), packetFlags);
    const auto channel     = enet_uint8(bool(rpc.traits & RpcTraits::Unreliable) ? Channel::UnreliableRpc : Channel::Replicate);

    if (rpc.owningConnection && entityToConnection_.contains(*rpc.owningConnection) && bool(rpc.traits & (RpcTraits::Client | RpcTraits::Remote)))
    {
      // Send to single peer if client owns this entity.
      auto peer = entityToConnection_.at(*rpc.owningConnection);
      if (packetFlags & ENET_PACKET_FLAG_RELIABLE || connections_.at(peer).status == ClientStatus::Connected)
      {
        enet_peer_send(peer, channel, packet);
      }
    }
    else
    {
      // Broadcast to all connected clients.
      for (auto& [peer, clientInfo] : connections_)
      {
        if (packetFlags & ENET_PACKET_FLAG_RELIABLE || clientInfo.status == ClientStatus::Connected)
        {
          enet_peer_send(peer, channel, packet);
        }
      }
    }
  }
}

void Networking::Server::OnEntityDestroy(entt::registry&, entt::entity entity)
{
  ZoneScoped;
  if (connections_.empty())
  {
    return;
  }
  removedEntities_.emplace_back(entity);
}

int32_t Networking::Server::HandlePacket(World& world, ENetPeer* peer, const ENetPacket& enetPacket)
{
  ZoneScoped;
  auto peerIt = connections_.find(peer);
  ASSERT(peerIt != connections_.end());

  const char* const pData = reinterpret_cast<const char*>(enetPacket.data);
  auto stream = std::stringstream(std::string(reinterpret_cast<const char*>(enetPacket.data), enetPacket.dataLength));
  auto packetType = Core::Serialization::DeserializeObjectStream<PacketType>(stream);

  if (bool(packetType & PacketType::Compressed))
  {
    ZoneScopedN("PacketType::Compressed");
    auto outSize = ZSTD_getFrameContentSize(pData + stream.tellg(), enetPacket.dataLength - stream.tellg());
    ASSERT(outSize != ZSTD_CONTENTSIZE_UNKNOWN);
    ASSERT(outSize != ZSTD_CONTENTSIZE_ERROR);
    auto outStr = std::string();
    outStr.resize(outSize);
    auto ret = ZSTD_decompress(outStr.data(), outSize, pData + stream.tellg(), enetPacket.dataLength - stream.tellg());
    if (ZSTD_isError(ret))
    {
      spdlog::warn("Failed to decompress packet: {}", ZSTD_getErrorName(ret));
      return static_cast<int32_t>(stream.tellg());
    }
    ASSERT(ret == outSize);
    // Reset the stream with the uncompressed data.
    stream = std::stringstream(std::move(outStr));
  }

  if ((packetType & PacketType::TypeMask) == PacketType::MultiPacket)
  {
    ZoneScopedN("PacketType::MultiPacket");
    auto readBytes = static_cast<int32_t>(stream.tellg());
    while (enetPacket.dataLength - readBytes > 0)
    {
      readBytes += HandlePacket(world, peer, {.data = enetPacket.data + readBytes, .dataLength = enetPacket.dataLength - readBytes});
    }
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::InputState)
  {
    ZoneScopedN("PacketType::InputState");
    auto inputState = Core::Serialization::DeserializeObjectStream<InputState>(stream);
    auto inputLookState = Core::Serialization::DeserializeObjectStream<InputLookState>(stream);
    //world.GetRegistry().emplace_or_replace<InputState>(peerIt->second.entity, inputState);
    world.GetRegistry().emplace_or_replace<InputState>(world.TryGetLocalPlayer(), inputState);
    world.GetRegistry().emplace_or_replace<InputLookState>(world.TryGetLocalPlayer(), inputLookState);
  }
  else if (bool(packetType & PacketType::Rpc))
  {
    detail::InvokeSerializedRPC(world, stream);
  }
  return static_cast<int32_t>(stream.tellg());
}
