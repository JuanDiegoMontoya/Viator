#include "Client.h"
#include "Game/Voxel/Grid.h"
#include "Core/Serialization.h"
#include "Game/Game.h"
#include "Game/Globals.h"
#include "Game/World.h"
#include "Core/Assert2.h"
#include "RPC.h"
#include "ThreadSafeQueue.h"
#include "Core/ClassImplMacros.h"
#include "RpcInfo.h"

#define ENET_IMPLEMENTATION
#include "enet/enet.h"
#include "spdlog/spdlog.h"
#include "tracy/Tracy.hpp"
#include "zstd.h"

static std::string format_as(const ENetAddress& addr)
{
  char name[256] = {};
  ASSERT(enet_address_get_host_ip(&addr, name, 256) == 0);
  return fmt::format("[{}]:{}", name, addr.port);
}

namespace Networking
{
  class ClientImpl final : public Client
  {
  public:
    explicit ClientImpl(World& world, const char* hostName);
    ~ClientImpl() override;
    NO_COPY_NO_MOVE(ClientImpl);

    void ProcessMessages(World& world, std::uint32_t timeoutMs) override;
    void SendMessages(World& world) override;
    void EnqueueRPC(RpcInfo rpc) override;
    bool IsEntityOwnedByRemote(entt::entity entity) override;

    [[nodiscard]] ClientStatus GetStatus() const override
    {
      return status_;
    }

    [[nodiscard]] const std::unordered_map<entt::entity, entt::entity>& GetRemoteToLocalEntityMap() const override
    {
      return remoteToLocalEntity_;
    }

  private:
    void FlushRPCs();
    void OnEntityDestroy(entt::registry&, entt::entity entity);
    int32_t HandlePacket(World& world, const ENetPacket& packet);

    World* world_{};
    ENetHost* localHost_{};
    ENetPeer* remotePeer_{};
    ClientStatus status_ = ClientStatus::Resolving;
    std::unordered_map<entt::entity, entt::entity> remoteToLocalEntity_;
    std::unordered_map<entt::entity, entt::entity> localToRemoteEntity_;
    ThreadSafeQueue<RpcInfo> rpcs_;
  };

  std::unique_ptr<Client> Client::Create(World& world, const char* hostName)
  {
    return std::make_unique<ClientImpl>(world, hostName);
  }
}

Networking::ClientImpl::ClientImpl(World& world, const char* hostName)
  : world_(&world)
{
  spdlog::info("Creating client that will attempt to connect to {}", hostName);
  ASSERT(enet_initialize() == 0);

  localHost_ = enet_host_create(nullptr, 1, int(Channel::NumChannels), 0, 0);
  
  ASSERT(localHost_);
  
  localHost_->maximumPacketSize  = 200 * 1024 * 1024;
  localHost_->maximumWaitingData = 500 * 1024 * 1024;
  localHost_->checksum           = &enet_crc32;

  auto compressor = detail::GetCompressor();
  enet_host_compress(localHost_, &compressor);

  auto address = ENetAddress{};
  ASSERT(enet_address_set_host(&address, hostName) == 0);
  address.port = 1234; // TODO: TEMP

  remotePeer_ = enet_host_connect(localHost_, &address, int(Channel::NumChannels), 0);

  ASSERT(remotePeer_);

  world.GetRegistryRaw().on_destroy<entt::entity>().connect<&ClientImpl::OnEntityDestroy>(*this);
}

Networking::ClientImpl::~ClientImpl()
{
  world_->GetRegistryRaw().on_destroy<entt::entity>().disconnect<&ClientImpl::OnEntityDestroy>(*this);
  if (remotePeer_)
  {
    enet_peer_disconnect_now(remotePeer_, 0);
  }
  enet_host_destroy(localHost_);
  enet_deinitialize();
}

void Networking::ClientImpl::ProcessMessages([[maybe_unused]] World& world, std::uint32_t timeoutMs)
{
  ZoneScoped;
  auto event = ENetEvent{};
  while (true)
  {
    if (int ret = enet_host_service(localHost_, &event, timeoutMs) > 0)
    {
      switch (event.type)
      {
      case ENET_EVENT_TYPE_CONNECT:
      {
        ZoneScopedN("ENET_EVENT_TYPE_CONNECT");
        spdlog::info("Client connected to {}", event.peer->address);
        status_     = ClientStatus::Joining;
        remotePeer_ = event.peer;
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE:
      {
        ZoneScopedN("ENET_EVENT_TYPE_RECEIVE");
        ZoneTextF("Packet number %u: %zu bytes", enet_host_get_packets_received(localHost_), event.packet->dataLength);
        SPDLOG_TRACE("Message received from {} with {} bytes", event.peer->address, event.packet->dataLength);
        HandlePacket(world, *event.packet);
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT:
      {
        ZoneScopedN("ENET_EVENT_TYPE_DISCONNECT");
        spdlog::info("Disconnected from {}", event.peer->address);
        status_          = ClientStatus::Disconnected;
        event.peer->data = nullptr;
        remotePeer_      = nullptr;
        remoteToLocalEntity_.clear();
        localToRemoteEntity_.clear();
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
      {
        ZoneScopedN("ENET_EVENT_TYPE_DISCONNECT_TIMEOUT");
        spdlog::info("Disconnected (timeout) from {}", event.peer->address);
        status_          = ClientStatus::Disconnected;
        event.peer->data = nullptr; // Placeholder: this code should be replaced by actual peer cleanup.
        remotePeer_      = nullptr;
        remoteToLocalEntity_.clear();
        localToRemoteEntity_.clear();
        break;
      }
      case ENET_EVENT_TYPE_NONE: [[fallthrough]];
      default: PANIC;
      }
    }
    else if (ret < 0)
    {
      spdlog::error("[Client] Error processing event: {}", ret);
    }
    else // ret == 0
    {
      break;
    }
  }
}

void Networking::ClientImpl::SendMessages(World& world)
{
  if (status_ != ClientStatus::Connected)
  {
    rpcs_.clear();
    return;
  }

  auto localPlayer = world.TryGetLocalPlayer();
  if (localPlayer != entt::null)
  {
    if (world.GetRegistry().all_of<InputState, InputLookState>(localPlayer))
    {
      auto [is, ils] = world.GetRegistry().get<InputState, InputLookState>(localPlayer);
      auto stream = std::stringstream();
      CallRPC("UpdatePlayerInput"_hs, world, localPlayer, is, ils);
    }
  }

  FlushRPCs();
}

void Networking::ClientImpl::EnqueueRPC(RpcInfo rpc)
{
  rpcs_.push_back(std::move(rpc));
}

bool Networking::ClientImpl::IsEntityOwnedByRemote(entt::entity entity)
{
  return !world_->AncestorHasComponent<LocalAuthoritative>(entity);
}

void Networking::ClientImpl::FlushRPCs()
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
      // Remap local entities to server entities.
      if (arg.type().id() == entt::type_id<entt::entity>().hash())
      {
        auto success = arg.assign(localToRemoteEntity_.at(arg.cast<entt::entity>()));
        ASSERT(success);
      }
      Core::Serialization::SerializeObjectStream(stream, arg.as_ref());
    }
    
    const auto packetFlags = bool(rpc.traits & RpcTraits::Unreliable) ? ENET_PACKET_FLAG_UNSEQUENCED : ENET_PACKET_FLAG_RELIABLE;
    auto* packet           = enet_packet_create(stream.view().data(), stream.view().size(), packetFlags);

    enet_peer_send(remotePeer_, enet_uint8(bool(rpc.traits & RpcTraits::Unreliable) ? Channel::UnreliableRpc : Channel::Replicate), packet);
  }
}

void Networking::ClientImpl::OnEntityDestroy(entt::registry&, entt::entity entity)
{
  ZoneScoped;
  if (auto it = localToRemoteEntity_.find(entity); it != localToRemoteEntity_.end())
  {
    DEBUG_ASSERT(remoteToLocalEntity_.contains(it->second));
    remoteToLocalEntity_.erase(it->second);
    localToRemoteEntity_.erase(it);
  }
}

int32_t Networking::ClientImpl::HandlePacket(World& world, const ENetPacket& enetPacket)
{
  ZoneScoped;
  const char* const pData = reinterpret_cast<const char*>(enetPacket.data);
  auto stream = std::stringstream(std::string(pData, enetPacket.dataLength));
  auto packetType = Core::Serialization::DeserializeObjectStream<PacketType>(stream);

  if (bool(packetType & PacketType::Compressed))
  {
    ZoneScopedN("PacketType::Compressed");
    auto outSize = ZSTD_getFrameContentSize(pData + stream.tellg(), enetPacket.dataLength - stream.tellg());
    ASSERT(outSize != ZSTD_CONTENTSIZE_UNKNOWN);
    ASSERT(outSize != ZSTD_CONTENTSIZE_ERROR);
    auto outStr  = std::string();
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
      readBytes += HandlePacket(world, {.data = enetPacket.data + readBytes, .dataLength = enetPacket.dataLength - readBytes});
    }
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::VoxelGrid)
  {
    ZoneScopedN("PacketType::VoxelGrid");
    spdlog::info("Finished downloading world.");
    status_   = ClientStatus::Connected;
    auto grid = Core::Serialization::DeserializeObjectStream<Voxel::Grid>(stream);
    *world.globals->grid = std::move(grid);
    world.globals->game->gameState = GameState::GAME;
    
    world.CreateRenderingMaterials();
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::TickNumber)
  {
    ZoneScopedN("PacketType::TickNumber");
    [[maybe_unused]] auto tick = Core::Serialization::DeserializeObjectStream<decltype(world.GetTicks())>(stream);
    ZoneTextF("Server tick: %llu", tick);
    spdlog::debug("Received server tick {}", tick);
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::EntityBundle)
  {
    ZoneScopedN("PacketType::EntityBundle");
    auto entities = Core::Serialization::DeserializeObjectStream<std::vector<entt::entity>>(stream);

    for (auto remoteEntity : entities)
    {
      if (auto it = remoteToLocalEntity_.find(remoteEntity); it == remoteToLocalEntity_.end())
      {
        auto localEntity = world.GetRegistry().create();
        remoteToLocalEntity_.emplace(remoteEntity, localEntity);
        localToRemoteEntity_.emplace(localEntity, remoteEntity);
      }
    }

    for ([[maybe_unused]] auto _ : entities)
    {
      Core::Serialization::DeserializeEntity(stream, world, remoteToLocalEntity_);
    }

    for (auto remoteEntity : entities)
    {
      auto localEntity = remoteToLocalEntity_.at(remoteEntity);
      if (world.GetRegistry().all_of<LocalTransform, Hierarchy>(localEntity))
      {
        world.UpdateLocalTransform(localEntity);
      }
    }
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::RemovedEntities)
  {
    ZoneScopedN("PacketType::RemovedEntity");
    // Delete entities.
    auto remoteEntities = Core::Serialization::DeserializeObjectStream<std::vector<entt::entity>>(stream);
    for (auto remoteEntity : remoteEntities)
    {
      if (auto it = remoteToLocalEntity_.find(remoteEntity); it != remoteToLocalEntity_.end())
      {
        auto localEntity = it->second;
        SPDLOG_TRACE("Received request to remove entity {} (remote: {})", entt::to_integral(localEntity), entt::to_integral(remoteEntity));
        // Do NOT add DeferredDelete here. Adding it will attempt to remove the entity from its parent,
        // but the server will have handled that already in the ModifiedComponents packet for this frame.
        // Therefore, all we need to do is immediately delete the entity.
        world.GetRegistry().destroy(localEntity);
      }
    }
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::Rpc)
  {
    detail::InvokeSerializedRPC(world, stream);
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::ModifiedComponents)
  {
    ZoneScopedN("PacketType::ModifiedComponents");
    Core::Serialization::DeserializeComponentStream(world, stream, remoteToLocalEntity_, localToRemoteEntity_, true, true);
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::InitialEntityState)
  {
    ZoneScopedN("PacketType::InitialEntityState");
    Core::Serialization::DeserializeComponentStream(world, stream, remoteToLocalEntity_, localToRemoteEntity_, false, true);
  }
  else if ((packetType & PacketType::TypeMask) == PacketType::NetworkInfo)
  {
    ZoneScopedN("PacketType::NetworkInfo");
    clientNetworkInfos_ = Core::Serialization::DeserializeObjectStream<std::vector<ClientNetworkInfo>>(stream);
  }
  else
  {
    PANIC;
  }

  return static_cast<int32_t>(stream.tellg());
}
