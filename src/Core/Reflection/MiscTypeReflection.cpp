  #include "MiscTypeReflection.h"
#include "Core/Reflection/ReflectionUtilitiesInternal.h"

#include "Reflection.h"
#include "ReflectionMacrosInternal.h"
#include "angelscript.h"
#include "Game/Game.h"
#include "Game/Globals.h"
#include "Game/NpcDirector.h"
#include "Game/Voxel/VoxelType.h"
#include "Game/Prefab.h"
#include "Core/Image.h"
#include "Game/Voxel/Grid.h"
#include "Game/Item.h"
#include "Game/Scripting.h"
#include "Game/HashGrid.h"
#include "Game/Pathfinding.h"

#include "entt/meta/factory.hpp"

static asIScriptEngine* asEngine{};

namespace Core::Reflection
{
  void InitializeMiscTypeReflection(Scripting& scripting)
  {
    asEngine = &scripting.GetEngine();

    REFLECT_ENUM(voxel_t)
      ENUMERATOR(voxel_t, Air)
      ENUMERATOR(voxel_t, Null);

    using namespace Physics;

    REFLECT_ENUM(Item::EffectType)
      ENUMERATOR(Item::EffectType, MovementSpeedModifier, PROP_DISPLAY_NAME("Speed"))
      ENUMERATOR(Item::EffectType, JumpImpulseModifier, PROP_DISPLAY_NAME("Jump Impulse"))
      ENUMERATOR(Item::EffectType, WaterJumpControlTimeModifier, PROP_DISPLAY_NAME("Water Jump Control Time"))
      ENUMERATOR(Item::EffectType, WaterAccelerationModifier, PROP_DISPLAY_NAME("Water Acceleration"))
      ENUMERATOR(Item::EffectType, WaterMaxSpeedModifier, PROP_DISPLAY_NAME("Water Max Speed"))
      ENUMERATOR(Item::EffectType, WaterGravityModifier, PROP_DISPLAY_NAME("Water Gravity"))
      ENUMERATOR(Item::EffectType, ArmorModifier, PROP_DISPLAY_NAME("Armor"))
      ENUMERATOR(Item::EffectType, BaseDamage, PROP_DISPLAY_NAME("Damage"))
      ENUMERATOR(Item::EffectType, Knockback, PROP_DISPLAY_NAME("Knockback"))
      ENUMERATOR(Item::EffectType, HealthRegeneration, PROP_DISPLAY_NAME("Health Regeneration"))
      ENUMERATOR(Item::EffectType, Shine, PROP_DISPLAY_NAME("Shine"))
      ENUMERATOR(Item::EffectType, Spelunker, PROP_DISPLAY_NAME("Spelunker"));

    REFLECT_TYPE(std::monostate);
    REFLECT_TYPE(UseTwoLevelGrid);

    REFLECT_TYPE(Sphere)
      TRAITS(EDITOR_READ_ONLY)
      DATA(Sphere, radius);

    REFLECT_TYPE(Capsule)
      TRAITS(EDITOR_READ_ONLY)
      DATA(Capsule, radius)
      DATA(Capsule, cylinderHalfHeight);

    REFLECT_TYPE(Box)
      TRAITS(EDITOR_READ_ONLY)
      DATA(Box, halfExtent);

    REFLECT_TYPE(Plane)
      TRAITS(EDITOR_READ_ONLY)
      DATA(Plane, normal)
      DATA(Plane, constant);

    REFLECT_TYPE(PolyShape)
      VARIANT_FUNCS(PolyShape);

    REFLECT_ENUM(BlockDamageFlagBit)
      ENUMERATOR(BlockDamageFlagBit, NONE)
      ENUMERATOR(BlockDamageFlagBit, PICKAXE)
      ENUMERATOR(BlockDamageFlagBit, AXE)
      ENUMERATOR(BlockDamageFlagBit, ALL_TOOLS)
      ENUMERATOR(BlockDamageFlagBit, NO_LOOT)
      ENUMERATOR(BlockDamageFlagBit, NO_LOOT_95_PERCENT);

    REFLECT_TYPE(BlockDamageFlags)
      DATA_BASE(BlockDamageFlags, flags);

    REFLECT_ENUM(ActionType);

    REFLECT_ENUM(Networking::ClientStatus)
      ENUMERATOR(Networking::ClientStatus, Resolving)
      ENUMERATOR(Networking::ClientStatus, Joining)
      ENUMERATOR(Networking::ClientStatus, Connected)
      ENUMERATOR(Networking::ClientStatus, Disconnected);

    REFLECT_TYPE(Networking::ClientNetworkInfo)
      DATA(Networking::ClientNetworkInfo, entity)
      DATA(Networking::ClientNetworkInfo, status)
      DATA(Networking::ClientNetworkInfo, roundTripTime)
      DATA(Networking::ClientNetworkInfo, roundTripTimeVariance)
      DATA(Networking::ClientNetworkInfo, packetLoss);

    REFLECT_ENUM(GameState)
      ENUMERATOR(GameState, MENU)
      ENUMERATOR(GameState, GAME)
      ENUMERATOR(GameState, PAUSED)
      ENUMERATOR(GameState, WORLD_SELECT)
      ENUMERATOR(GameState, LOADING_SP)
      ENUMERATOR(GameState, LOADING_MP)
      ENUMERATOR(GameState, MENU_SETTINGS)
      ENUMERATOR(GameState, PAUSED_SETTINGS)
      ENUMERATOR(GameState, SERVER_SELECT)
      ENUMERATOR(GameState, SERVER_SELECT_ADD_SERVER);

    entt::meta_factory<const char*>().conv<std::string>().conv<std::string_view>();
    entt::meta_factory<char*>().conv<std::string>().conv<std::string_view>();

    REFLECT_ENUM(Networking::PacketType);

    // TODO: Grid reflection should be removed
    REFLECT_TYPE(Voxel::Grid::TopLevelBrickPtr)
      DATA(Voxel::Grid::TopLevelBrickPtr, voxelsDoBeAllSame)
      DATA_BASE(Voxel::Grid::TopLevelBrickPtr, voxelIfAllSame);

    REFLECT_TYPE(Voxel::Grid::TopLevelBrick)
      DATA_BASE(Voxel::Grid::TopLevelBrick, bricks);

    REFLECT_TYPE(Voxel::Grid::BottomLevelBrickPtr)
      DATA(Voxel::Grid::BottomLevelBrickPtr, voxelsDoBeAllSame)
      DATA_BASE(Voxel::Grid::BottomLevelBrickPtr, voxelIfAllSame);

    REFLECT_TYPE(Voxel::Grid::BottomLevelBrick)
    DATA_BASE(Voxel::Grid::BottomLevelBrick, occupancy)
    DATA_BASE(Voxel::Grid::BottomLevelBrick, voxels);

    REFLECT_TYPE(Voxel::Grid::OccupancyBitmask)
      DATA_BASE(Voxel::Grid::OccupancyBitmask, bitmask);

    using IvecVoxelPair = std::pair<glm::ivec3, voxel_t>;
    REFLECT_TYPE(IvecVoxelPair)
      DATA_BASE(IvecVoxelPair, first)
      DATA_BASE(IvecVoxelPair, second);

    BEGIN_REFLECT_TYPE(SerializableSimplePrefab)
      MEMBER(voxelToName);
      MEMBER(voxels);
    END_REFLECT

    BEGIN_REFLECT_NON_OBJECT_TYPE(SunInfo)
      MEMBER(timeOfDay);
      MEMBER(azimuth);
      MEMBER(dayLength);
      MEMBER(pauseDayNightCycle);
    END_REFLECT

    BEGIN_REFLECT_NON_OBJECT_TYPE(GameGlobals)
      MEMBER(gameState) TRAITS(TRANSIENT);
      MEMBER(rng);
      MEMBER(debugging);
      MEMBER(timeScale);
      MEMBER(tickRate);
      MEMBER(time);
      MEMBER(hashGrid) TRAITS(TRANSIENT);
      MEMBER(npcSpawnDirector);
      MEMBER(updateNpcSpawnDirector);
      MEMBER(npcsIgnorePlayers)
      MEMBER(disableNpcPathfinding)
      MEMBER(sunInfo);
      MEMBER(pathCache) TRAITS(TRANSIENT);
      MEMBER(lootRegistry) TRAITS(TRANSIENT);
      MEMBER(crafting) TRAITS(TRANSIENT);
    END_REFLECT

    BEGIN_REFLECT_TYPE(NpcSpawnDirector)
      MEMBER(maxEnemiesBase);
      MEMBER(maxEnemiesPerAdditionalPlayer);
      MEMBER(timeBetweenSpawns);
      MEMBER(accumulator);
    END_REFLECT

    BEGIN_REFLECT_NON_OBJECT_TYPE(WorldGlobals)
      MEMBER(worldName);
      MEMBER(grid) TRAITS(TRANSIENT);
      MEMBER(prefabRegistry) TRAITS(TRANSIENT);
      MEMBER(blockRegistry) TRAITS(TRANSIENT);
      MEMBER(itemRegistry) TRAITS(TRANSIENT);
      MEMBER(entityPrefabRegistry) TRAITS(TRANSIENT);
      MEMBER(game);
      MEMBER(waterQueue);
      MEMBER(waterSet);
      MEMBER(globalSurfaceHeight);
      MEMBER(globalSurfaceFog);
      MEMBER(globalFog);
      MEMBER(globalFogNeedsUpdate) TRAITS(TRANSIENT);
      MEMBER(undergroundBiomes) TRAITS(TRANSIENT);
      MEMBER(surfaceBiomes) TRAITS(TRANSIENT);
    END_REFLECT
  }
} // namespace Core::Reflection