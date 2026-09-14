#include "RpcReflection.h"

#include "angelscript.h"
#include "Game/Game.h"
#include "Game/World.h"
#include "Game/Globals.h"
#include "Game/Commands.h"
#include "Game/Voxel/Grid.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ReflectionUtilitiesInternal.h"
#include "Core/Reflection/ReflectionMacrosInternal.h"
#include "Game/Scripting.h"

#include "entt/meta/meta.hpp"
#include "entt/meta/factory.hpp"
#include "entt/core/hashed_string.hpp"
#include "entt/entity/registry.hpp"

#include <type_traits>

namespace
{
  void UpdatePlayerInput(World& world, entt::entity entity, InputState is, InputLookState ils)
  {
    world.GetRegistry().emplace_or_replace<InputState>(entity, is);
    world.GetRegistry().emplace_or_replace<InputLookState>(entity, ils);
  }

  void GiveLocalPlayerRPC(World& world, entt::entity entity)
  {
    if (!world.GetRegistry().all_of<LocalPlayer, InputState, InputLookState, LocalAuthoritative>(entity))
    {
      world.GetRegistry().emplace_or_replace<LocalPlayer>(entity);
      world.GetRegistry().emplace_or_replace<InputState>(entity);
      world.GetRegistry().emplace_or_replace<InputLookState>(entity);
      world.GetRegistry().emplace_or_replace<LocalAuthoritative>(entity);
    }
  }

  void UpdateTransformRPC(World& world, entt::entity entity, LocalTransform transform)
  {
    // TODO: Verify that the entity is owned by the client.
    if (world.GetRegistry().valid(entity))
    {
      world.GetRegistry().emplace_or_replace<LocalTransform>(entity, transform);
      world.UpdateLocalTransform(entity);
    }
  }

  void ExecuteCommand(World& world, const std::string& command)
  {
    world.globals->commandRegistry->ExecuteCommand(command);
  }

  BlockId GetVoxelAt(World& world, const glm::ivec3& voxelPosition)
  {
    return world.globals->grid->GetVoxelAt(voxelPosition);
  }

  void OnTryPlaceBlock(World& world, const glm::ivec3& voxelPosition, BlockId block)
  {
    Block::OnTryPlaceBlock(world, voxelPosition, block);
  }

  BlockId GetBlockId(World& world, const std::string& tag)
  {
    return world.globals->blockRegistry->Get(tag);
  }
} // namespace

namespace Core::Reflection
{
  void InitializeRpcReflection(Scripting& scripting)
  {
    asIScriptEngine* asEngine = &scripting.GetEngine();
    ASSERT(asEngine->RegisterGlobalFunction("void ExecuteCommand(World&, const string& in)", asFUNCTION(ExecuteCommand), asCALL_CDECL) >= 0);
    ASSERT(asEngine->RegisterGlobalFunction("entity GetVoxel(World&, const ivec3& in)", asFUNCTION(GetVoxelAt), asCALL_CDECL) >= 0);
    ASSERT(asEngine->RegisterGlobalFunction("void SetVoxel(World&, const ivec3& in, entity)", asFUNCTION(OnTryPlaceBlock), asCALL_CDECL) >= 0);
    ASSERT(asEngine->RegisterGlobalFunction("entity GetBlockId(World&, const string& in)", asFUNCTION(GetBlockId), asCALL_CDECL) >= 0);

    REGISTER_RPC(UpdatePlayerInput, RpcTraits::Server);
    REGISTER_RPC(GiveLocalPlayerRPC, RpcTraits::Client);
    REGISTER_RPC(UpdateTransformRPC, RpcTraits::Server);

    REGISTER_RPC(DropItemRPC, RpcTraits::Server);
    REGISTER_RPC(ThrowItemRPC, RpcTraits::Server);
    REGISTER_RPC(TryCraftRecipeRPC, RpcTraits::Server);
    REGISTER_RPC(SwapInventorySlotsRPC, RpcTraits::Server);
    REGISTER_RPC(SetActiveSlotRPC, RpcTraits::Server);
    REGISTER_RPC(SetVoxelAtRPC, RpcTraits::Broadcast | RpcTraits::UseVoxelChannel);
    REGISTER_RPC(TeleportPlayerRPC, RpcTraits::Client);
    REGISTER_RPC(ScrollHotbarRPC, RpcTraits::Server);
    REGISTER_RPC(SwapInventorySlotAndArmorSlotRPC, RpcTraits::Server);
    REGISTER_RPC(ThrowItemFromArmorRPC, RpcTraits::Server);
    REGISTER_RPC(DropItemFromArmorRPC, RpcTraits::Server);
    REGISTER_RPC(SwapArmorSlotsRPC, RpcTraits::Server);
    REGISTER_RPC(UpdateSimpleScriptableCodeRPC, RpcTraits::Server | RpcTraits::Remote);
  }
} // namespace Core::Reflection