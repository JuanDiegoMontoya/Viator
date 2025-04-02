#include "Client.h"
#include "../TwoLevelGrid.h"
#include "../Serialization.h"
#include "../Game.h"
#include "../Assert2.h"
#include "RPC.h"

#define ENET_IMPLEMENTATION
#include "enet/enet.h"
#include "spdlog/spdlog.h"
#include "tracy/Tracy.hpp"

static std::string format_as(const ENetAddress& addr)
{
  char name[256] = {};
  ASSERT(enet_address_get_host_ip(&addr, name, 256) == 0);
  return fmt::format("[{}]:{}", name, addr.port);
}

Networking::Client::Client(World& world, const char* hostName)
  : world_(&world)
{
  spdlog::info("Creating client that will attempt to connect to {}", hostName);
  ASSERT(enet_initialize() == 0);

  localHost_ = enet_host_create(nullptr, 1, 2, 0, 0);
  
  ASSERT(localHost_);

  localHost_->maximumPacketSize  = 200 * 1024 * 1024;
  localHost_->maximumWaitingData = 500 * 1024 * 1024;
  localHost_->checksum           = &enet_crc32;

  auto address = ENetAddress{};
  ASSERT(enet_address_set_host(&address, hostName) == 0);
  address.port = 1234; // TODO: TEMP

  remotePeer_ = enet_host_connect(localHost_, &address, 1, 0);

  ASSERT(remotePeer_);

  world.GetRegistry().on_destroy<entt::entity>().connect<&Client::OnEntityDestroy>(*this);
}

Networking::Client::~Client()
{
  world_->GetRegistry().on_destroy<entt::entity>().disconnect<&Client::OnEntityDestroy>(*this);
  if (remotePeer_)
  {
    enet_peer_disconnect_now(remotePeer_, 0);
  }
  enet_host_destroy(localHost_);
  enet_deinitialize();
}

void Networking::Client::ProcessMessages([[maybe_unused]] World& world)
{
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
        status_     = ClientStatus::Joining;
        remotePeer_ = event.peer;
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE:
      {
        spdlog::trace("Message received from {} with {} bytes", event.peer->address, event.packet->dataLength);
        HandlePacket(world, *event.packet);
        enet_packet_destroy(event.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT:
      {
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

void Networking::Client::SendMessages(World& world)
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

void Networking::Client::EnqueueRPC(RpcInfo rpc)
{
  rpcs_.push_back(std::move(rpc));
}

void Networking::Client::FlushRPCs()
{
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

    enet_peer_send(remotePeer_, 0, packet);
  }
}

void Networking::Client::OnEntityDestroy(entt::registry&, entt::entity entity)
{
  if (auto it = localToRemoteEntity_.find(entity); it != localToRemoteEntity_.end())
  {
    ASSERT(remoteToLocalEntity_.contains(it->second));
    remoteToLocalEntity_.erase(it->second);
    localToRemoteEntity_.erase(it);
  }
}

void Networking::Client::HandlePacket(World& world, const ENetPacket& enetPacket)
{
  ZoneScoped;
  auto stream = std::stringstream(std::string(reinterpret_cast<const char*>(enetPacket.data), enetPacket.dataLength));
  auto packetType = Core::Serialization::DeserializeObjectStream<PacketType>(stream);

  if (packetType == PacketType::TwoLevelGrid)
  {
    spdlog::info("Finished downloading world.");
    status_   = ClientStatus::Connected;
    auto grid = Core::Serialization::DeserializeObjectStream<TwoLevelGrid>(stream);
    world.GetRegistry().ctx().insert_or_assign<TwoLevelGrid>(std::move(grid));
    world.GetRegistry().ctx().get<GameState>() = GameState::GAME;
  }
  else if (packetType == PacketType::EntityBundle)
  {
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
        UpdateLocalTransform({world.GetRegistry(), localEntity});
      }
    }
  }
  else if (packetType == PacketType::RemovedEntity)
  {
    // Delete entity.
    auto remoteEntity = Core::Serialization::DeserializeObjectStream<entt::entity>(stream);
    if (auto it = remoteToLocalEntity_.find(remoteEntity); it != remoteToLocalEntity_.end())
    {
      auto localEntity = it->second;
      //world.GetRegistry().destroy(localEntity);
      world.GetRegistry().emplace<DeferredDelete>(localEntity);
      localToRemoteEntity_.erase(localEntity);
      remoteToLocalEntity_.erase(it);
    }
  }
  else if (packetType == PacketType::Rpc)
  {
    detail::InvokeSerializedRPC(world, stream);
  }
  else
  {
    PANIC;
  }
}
