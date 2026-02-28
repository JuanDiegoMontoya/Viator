#include "Assets.h"
#include "Core/Assert2.h"
#include "Item.h"
#include "Physics/Physics.h"
#include "Prefab.h"
#include "VoxLoader.h"
#include "World.h"
#include "Game.h"
#include "Voxel/Grid.h"
#include "Game/Globals.h"

#include "FastNoise/FastNoise.h"
#include "tracy/Tracy.hpp"

namespace
{
  BlockId RegisterOreBlock(World& world, std::string_view tag, std::string_view name, std::string_view albedo, int tier)
  {
    const auto blockId = Block::CreateStandardBlock(world,
      {
        std::string(tag),
        std::string(name),
        Block::Component::Breakable{
          .initialHealth = 100,
          .damageTier    = tier,
          .damageFlags   = BlockDamageFlagBit::PICKAXE,
        },
        Block::Component::RenderAsTexturedCube{
          {
            .randomizeTexcoordRotation = true,
            .baseColorTexture          = std::string(albedo),
          },
        },
      });
    world.globals->blockRegistry->GetRegistry().emplace<Block::Component::Valuable>(entt::entity(blockId));
    return blockId;
  }
}

class DungeonPrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  void Instantiate(World& world, glm::ivec3 worldPos) const override
  {
    const auto& blocks = *world.globals->blockRegistry;
    const auto& items  = *world.globals->itemRegistry;
    const auto& air    = blocks.Get("Air");
    const auto& wood   = blocks.Get("Wood Plank");
    const auto& chest  = blocks.Get("Cheste");
    const auto& light  = blocks.Get("Light");
    
    constexpr int ds = 3;
    for (int z = -ds; z <= ds; z++)
    {
      for (int y = -ds; y <= ds; y++)
      {
        for (int x = -ds; x <= ds; x++)
        {
          const auto blockPos = worldPos + glm::ivec3(x, y, z);
          if (x == -ds || x == ds || y == -ds || y == ds || z == -ds || z == ds)
          {
            Block::OnTryPlaceBlock(world, blockPos, wood);
          }
          else if (x == 0 && y == -ds + 1 && z == 0)
          {
            if (Block::OnTryPlaceBlock(world, blockPos, chest))
            {
              auto chestEntity = world.GetBlockEntity(blockPos);
              ASSERT(chestEntity != entt::null);
              if (auto [e, i] = world.GetComponentFromDescendant<Inventory>(chestEntity); i)
              {
                i->slots[0][0] = {.id = items.Get("Electrum"), .count = 10};
                i->slots[0][1] = {.id = items.Get("Suspicious Coin"), .count = 1};
              }
            }
          }
          else if (x == 0 && y == ds - 1 && z == 0)
          {
            Block::OnTryPlaceBlock(world, blockPos, light);
          }
          else
          {
            Block::OnTryPlaceBlock(world, blockPos, air);
          }
        }
      }
    }
  }
};

class VinePrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  void Instantiate(World& world, glm::ivec3 worldPos) const override
  {
    const auto& blocks    = *world.globals->blockRegistry;
    const auto& vinesMain = blocks.Get("vines_main");
    const auto& vinesEnd  = blocks.Get("vines_end");

    // Count number of air blocks starting from worldPos.
    auto& grid = *world.globals->grid;
    int freeRealEstate = 0;
    for (int y = 0; y > -5; y--)
    {
      const auto testPos = worldPos + glm::ivec3{0, y, 0};
      if (!grid.IsPositionInGrid(testPos) || grid.GetVoxelAt(testPos) != voxel_t::Air)
      {
        break;
      }
      freeRealEstate++;
    }

    if (freeRealEstate <= 0)
    {
      return;
    }

    grid.SetVoxelAt(worldPos + glm::ivec3{0, -(freeRealEstate - 1), 0}, vinesEnd);

    for (int y = 0; y > -(freeRealEstate - 1); y--)
    {
      grid.SetVoxelAt(worldPos + glm::ivec3{0, y, 0}, vinesMain);
    }
  }
};

class RootPrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  void Instantiate(World& world, glm::ivec3 worldPos) const override
  {
    const auto& blocks    = *world.globals->blockRegistry;
    const auto& rootsMain = blocks.Get("roots_main");
    const auto& rootsEnd  = blocks.Get("roots_end");

    // Count number of air blocks starting from worldPos.
    auto& grid         = *world.globals->grid;
    int freeRealEstate = 0;
    for (int y = 0; y > -3; y--)
    {
      const auto testPos = worldPos + glm::ivec3{0, y, 0};
      if (!grid.IsPositionInGrid(testPos) || grid.GetVoxelAt(testPos) != voxel_t::Air)
      {
        break;
      }
      freeRealEstate++;
    }

    if (freeRealEstate <= 0)
    {
      return;
    }

    grid.SetVoxelAt(worldPos + glm::ivec3{0, -(freeRealEstate - 1), 0}, rootsEnd);

    for (int y = 0; y > -(freeRealEstate - 1); y--)
    {
      grid.SetVoxelAt(worldPos + glm::ivec3{0, y, 0}, rootsMain);
    }
  }
};

class AbandonedHousePrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  void Instantiate(World& world, glm::ivec3 worldPos) const override
  {
    ZoneScoped;
    auto& grid         = *world.globals->grid;
    const auto& blocks = *world.globals->blockRegistry;
    const auto& items  = *world.globals->itemRegistry;
    const auto& air    = blocks.Get("air");
    const auto& wood   = blocks.Get("plank_wood");
    
    constexpr int MIN_ROOM_DIM = 5;
    constexpr int MAX_ROOM_DIM = 8;
    constexpr int ROOM_HEIGHT  = 6;
    constexpr int MAX_SUPPORT_LENGTH = 10;

    auto& rng             = world.globals->game->rng;
    const auto roomWidth  = rng.RandU32(MIN_ROOM_DIM, MAX_ROOM_DIM + 1);
    const auto roomLength = rng.RandU32(MIN_ROOM_DIM, MAX_ROOM_DIM + 1);
    for (uint32_t zl = 0; zl < roomLength; zl++)
    for (uint32_t xl = 0; xl < roomWidth; xl++)
    {
      Block::OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, 0, zl), wood);
      const auto& block = xl == 0 || xl == roomWidth - 1 || zl == 0 || zl == roomLength - 1 ? wood : air;
      for (uint32_t yl = 1; yl < rng.RandU32(block == air ? 2 : 1, ROOM_HEIGHT); yl++)
      {
        // Fill edges with wood, interior with air.
        Block::OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, yl, zl), block);
      }

      // Supporting column
      if ((xl == 0 || xl == roomWidth - 1) && (zl == 0 || zl == roomLength - 1))
      {
        for (int yl = -1; yl > -MAX_SUPPORT_LENGTH; yl--)
        {
          const auto pos = worldPos + glm::ivec3(xl, yl, zl);
          const auto voxel = grid.GetVoxelAt(pos);
          if (voxel == voxel_t::Air)
          {
            Block::OnTryPlaceBlock(world, pos, wood);
          }
          else
          {
            break;
          }
        }
      }
    }

    // Furniture and loot
    if (rng.RandFloat() < 0.75f)
    {
      const auto xl = rng.RandU32(1, roomWidth - 1);
      const auto zl = rng.RandU32(1, roomLength - 1);
      Block::OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, 1, zl), blocks.Get("table"));
    }
    
    if (rng.RandFloat() < 0.75f)
    {
      const auto xl = rng.RandU32(1, roomWidth - 1);
      const auto zl = rng.RandU32(1, roomLength - 1);
      Block::OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, 1, zl), blocks.Get("chair"));
    }

    for (int i = 0; i < 2; i++)
    {
      if (rng.RandFloat() < 0.75f)
      {
        const auto xl = rng.RandU32(1, roomWidth - 1);
        const auto zl = rng.RandU32(1, roomLength - 1);
        Block::OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, 1, zl), blocks.Get("pot"));
      }
    }

    if (rng.RandFloat() < 0.65f)
    {
      const auto xl = rng.RandU32(1, roomWidth - 1);
      const auto zl = rng.RandU32(1, roomLength - 1);
      const auto blockPos = worldPos + glm::ivec3(xl, 1, zl);
      if (Block::OnTryPlaceBlock(world, blockPos, blocks.Get("chest")))
      {
        auto chestEntity = world.GetBlockEntity(blockPos);
        ASSERT(chestEntity != entt::null);
        if (auto [e, i] = world.GetComponentFromDescendant<Inventory>(chestEntity); i)
        {
          i->slots[0][0] = {.id = items.Get("item_electrum"), .count = int(rng.RandU32(1, 20))};
          i->slots[0][1] = {.id = items.Get("item_suspicious_coin"), .count = 1};
        }
      }
    }
  }
};

class FloatingIslandPrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  void Instantiate(World& world, glm::ivec3 worldPos) const override
  {
    ZoneScoped;
    auto cloudNoise = FastNoise::NewFromEncodedNodeTree(
      "JQAK@BBRUFFwUOBQsAAIDSQgRI4erACI/C9b7//wMNBQY@ADiQQT2KFw/CNejEED///8DFgMXBQUDFwUE@BgD//AwAAw/UoP///CxcF/wYAAwQIAACAP////wIK1yM8/waPwvU9////");
    constexpr auto regionSize = glm::ivec3(75, 32, 75);
    const auto& blocks        = *world.globals->blockRegistry;
    const auto& grid          = *world.globals->grid;
    const auto& cloudA        = blocks.Get("cloud");
    const auto& cloudB        = blocks.Get("cloud_b");
    const auto seed           = int(world.globals->game->rng.RandU32(0, 1 << 20));

    // Island shape
    for (int zl = -regionSize.z / 2; zl <= regionSize.z / 2; zl++)
    for (int yl = -regionSize.y / 2; yl <= regionSize.y / 2; yl++)
    for (int xl = -regionSize.x / 2; xl <= regionSize.x / 2; xl++)
    {
      const auto localPos = glm::ivec3(xl, yl, zl);
      const auto pos      = glm::vec3(localPos + worldPos);
      const auto density  = cloudNoise->GenSingle3D((float)localPos.x, (float)localPos.y, (float)localPos.z, seed);
      if (density <= -0.3f)
      {
        Block::OnTryPlaceBlock(world, pos, cloudB);
      }
      else if (density <= 0)
      {
        Block::OnTryPlaceBlock(world, pos, cloudA);
      }
    }

    // Find solid location to spawn a structure
    int structureY = 15;
    for (; structureY > 0; structureY--)
    {
      if (grid.GetVoxelAt(worldPos + glm::ivec3(0, structureY, 0)) != voxel_t::Air)
      {
        break;
      }
    }

    world.globals->prefabRegistry->Get("AbandonedHouse").Instantiate(world, worldPos + glm::ivec3(0, structureY, 0));
  }
};

Physics::Engine& World::GetPhysicsEngine()
{
  return *globals->physics;
}

const Physics::Engine& World::GetPhysicsEngine() const
{
  return *globals->physics;
}

void World::InitializeGameState()
{
  ticks_ = 0;

  for (auto e : registry_.view<entt::entity>())
  {
    registry_.destroy(e);
  }

  globals->game->npcSpawnDirector = {};
  globals->game->updateNpcSpawnDirector = true;
  // Reset RNG
  globals->game->rng                    = PCG::Rng(1234);
}

void World::InitializeGameDefinitions()
{
  ZoneScoped;
  // Reset entity prefab registry
  auto& entityPrefabs = *globals->entityPrefabRegistry = {};
  RegisterDefaultEntityPrefabs(entityPrefabs);

  // Reset item registry
  auto& items = *globals->itemRegistry = {};

  const auto gunId = Item::CreateGun(items, "weapon_m4", "M4", 800, {});
  Item::CreateGun(items,
    "weapon_frogun",
    "Frogun",
    80,
    {
      .model       = "frog",
      .scale       = 0.125f,
      .damage      = 10,
      .knockback   = 2,
      .bullets     = 9,
      .velocity    = 50,
      .accuracyMoa = 300,
      .vrecoil     = 10,
      .vrecoilDev  = 3,
      .hrecoil     = 1,
      .hrecoilDev  = 1,
    });

  auto light      = GpuLight();
  light.color     = {1.0f, 0.4f, 0.2f};
  light.intensity = 500;
  light.type      = LIGHT_TYPE_POINT;
  light.range     = 200;
  const auto flareGunId = Item::CreateGun(items,
    "weapon_flaregun",
    "Flare Gun",
    90,
    {
      .model       = "ar15",
      .tint        = {1, 0.4f, 0.22f},
      .scale       = 1,
      .damage      = 10,
      .knockback   = 1,
      .bullets     = 1,
      .velocity    = 60,
      .accuracyMoa = 40,
      //.vrecoil = ,
      //.vrecoilDev = ,
      //.hrecoil = ,
      //.hrecoilDev = ,
      .light      = light,
      .sticky     = true,
      .stickyDist = 0.125f,
      .maxBounces = 10,
    });

  const auto stonePickaxeId = Item::CreateTool(items, "tool_stone_pickaxe", "Stone Pickaxe", "pickaxe", {.2f, .2f, .2f}, 0.3f, {20, 2, BlockDamageFlagBit::PICKAXE});
  const auto stoneAxeId = Item::CreateTool(items, "tool_stone_axe", "Stone Axe", "axe", {.2f, .2f, .2f}, 0.3f, {20, 2, BlockDamageFlagBit::AXE});
  const auto stoneSpearId = Item::CreateSpear(items, "weapon_stone_spear", "Stone Spear", "spear", {.2f, .2f, .2f}, 0.55f, 15, 5);

  const auto copperPickaxeId = Item::CreateTool(items, "tool_copper_pickaxe", "Copper Pickaxe", "pickaxe", {.78f, .51f, .27f}, 0.3f, {30, 3, BlockDamageFlagBit::PICKAXE});
  const auto copperAxeId = Item::CreateTool(items, "tool_copper_axe", "Copper Axe", "axe", {.78f, .51f, .27f}, 0.3f, {30, 3, BlockDamageFlagBit::AXE});
  const auto copperSpearId = Item::CreateSpear(items, "weapon_copper_spear", "Copper Spear", "spear", {.78f, .51f, .27f}, 0.55f, 20, 5);

  const auto leadPickaxeId = Item::CreateTool(items, "tool_lead_pickaxe", "Lead Pickaxe", "pickaxe", {.21f, .34f, .40f}, 0.3f, {35, 4, BlockDamageFlagBit::PICKAXE});
  const auto leadAxeId = Item::CreateTool(items, "tool_lead_axe", "Lead Axe", "axe", {.21f, .34f, .40f}, 0.3f, {35, 4, BlockDamageFlagBit::AXE});
  const auto leadSpearId = Item::CreateSpear(items, "weapon_lead_spear", "Lead Spear", "spear", {.21f, .34f, .40f}, 0.55f, 25, 5);

  const auto coinId = Item::CreateSimpleSpriteItem(items, "item_electrum", "Electrum", "coin", 999);
  const auto charcoalId = Item::CreateSimpleSpriteItem(items, "item_charcoal", "Charcoal", "charcoal", 999);
  const auto copperIngotId = Item::CreateSimpleSpriteItem(items, "item_copper_ingot", "Copper Ingot", "copper_ingot", 999);
  const auto leadIngotId = Item::CreateSimpleSpriteItem(items, "item_lead_ingot", "Lead Ingot", "lead_ingot", 999);
  const auto stickId = Item::CreateSimpleSpriteItem(items, "item_stick", "Stick", "stick", 999);
  const auto coolStickId = Item::CreateSimpleSpriteItem(items, "item_cool_stick", "Cool Stick", "stick", 999, {1, .1f, .1f});
  const auto susCoin = Item::CreateSimpleSpriteItem(items, "item_suspicious_coin", "Suspicious Coin", "coin", 999, {1, .1f, .1f});
  items.GetRegistry().emplace<Item::Component::SpawnEntityPrefabOnUse>(susCoin, "Worm Boss");
  items.GetRegistry().emplace<Item::Component::Usable>(susCoin, 1.0f);

  const auto healingPotion = Item::CreateSimpleSpriteItem(items, "item_healing_potion", "Healing Potion", "potion_healing", 999);
  items.GetRegistry().emplace<Item::Component::Usable>(healingPotion, 1.0f);
  items.GetRegistry().emplace<Item::Component::HealUserOnUse>(healingPotion, 30.0f);

  const auto fogSucc = Item::CreateSimpleSpriteItem(items, "item_fog_absorber", "Fog Vacuum", "potion_healing");
  items.GetRegistry().emplace<Item::Component::Usable>(fogSucc, 0.0f);
  items.GetRegistry().emplace<Item::Component::AbsorbFogOnUse>(fogSucc);

  const auto fogEmit = Item::CreateSimpleSpriteItem(items, "item_fog_emitter", "Fog Emit", "potion_healing", 1, {0, 0, 0});
  items.GetRegistry().emplace<Item::Component::Usable>(fogEmit, 0.0f);
  items.GetRegistry().emplace<Item::Component::EmitFogOnUse>(fogEmit);

  const auto opPickaxe = Item::CreateTool(items,
    "tool_op_pickaxe",
    "OP Pickaxe",
    "pickaxe",
    {1, 1, 1},
    0.1f,
    {
      .blockDamage      = 1000,
      .blockDamageTier  = 100,
      .blockDamageFlags = BlockDamageFlagBit::ALL_TOOLS,
    });
  items.GetRegistry().emplace<Item::Component::Rainbow>(opPickaxe);

  const auto healthRegenId = Item::CreateEffector(items, "effect_regeneration", "Health Regeneration", Item::EffectType::HealthRegeneration);
  const auto shineId       = Item::CreateEffector(items, "effect_shine", "Shine", Item::EffectType::Shine);
  const auto swiftnessId   = Item::CreateEffector(items, "effect_swiftness", "Swiftness", Item::EffectType::MovementSpeedModifier, 0, 1.25f);
  const auto ironSkinId    = Item::CreateEffector(items, "effect_ironskin", "Ironskin", Item::EffectType::ArmorModifier, 8);
  const auto spelunkerId   = Item::CreateEffector(items, "effect_spelunker", "Spelunker", Item::EffectType::Spelunker);

  Item::CreateEffectGranter(items, "potion_health_regeneration", "Potion of Health Regeneration", healthRegenId, 300, "potion_healing", {1, 1, 1});
  Item::CreateEffectGranter(items, "potion_shine", "Potion of Shine", shineId, 600, "potion_healing", {1, 1, 0.4f});
  Item::CreateEffectGranter(items, "potion_swiftness", "Potion of Swiftness", swiftnessId, 300, "potion_healing", {.1f, 1, 1.0f});
  Item::CreateEffectGranter(items, "potion_ironskin", "Potion of Ironskin", ironSkinId, 480, "potion_healing", {.4f, 1, .1f});
  Item::CreateEffectGranter(items, "potion_spelunker", "Spelunker Potion", spelunkerId, 300, "potion_healing", {1, .9f, .01f});

  const auto copperHelmetId = Item::CreateArmor(items, "armor_copper_helmet", "Copper Cap", Item::Component::AllowedSlots::Head, 2, "potion_healing");
  const auto copperShirtId  = Item::CreateArmor(items, "armor_copper_chest", "Copper Polo", Item::Component::AllowedSlots::Body, 2, "potion_healing");
  const auto copperPantsId  = Item::CreateArmor(items, "armor_copper_leg", "Copper Shoes", Item::Component::AllowedSlots::Legs, 2, "potion_healing");

  const auto leadHelmetId = Item::CreateArmor(items, "armor_lead_helmet", "Lead Cap", Item::Component::AllowedSlots::Head, 3, "potion_healing");
  const auto leadShirtId  = Item::CreateArmor(items, "armor_lead_chest", "Lead Polo", Item::Component::AllowedSlots::Body, 3, "potion_healing");
  const auto leadPantsId  = Item::CreateArmor(items, "armor_lead_leg", "Lead Shoes", Item::Component::AllowedSlots::Legs, 3, "potion_healing");

  const auto addLeadArmorModifiers = [&](ItemId id)
  {
    auto& effects = items.GetRegistry().get_or_emplace<Item::Component::StaticEffects>(id).effects;

    effects.append_range(std::vector<Item::Component::StaticEffect>{
      {
        .condition    = Item::EffectCondition::OnWorn,
        .quantityType = Item::EffectQuantityType::Multiplicative,
        .type         = Item::EffectType::MovementSpeedModifier,
        .amount       = 0.95f,
      },
      {
        .condition    = Item::EffectCondition::OnWorn,
        .quantityType = Item::EffectQuantityType::Additive,
        .type         = Item::EffectType::WaterGravityModifier,
        .amount       = -2.0f,
      },
      {
        .condition    = Item::EffectCondition::OnWorn,
        .quantityType = Item::EffectQuantityType::Multiplicative,
        .type         = Item::EffectType::WaterAccelerationModifier,
        .amount       = 0.95f,
      },
    });
  };

  addLeadArmorModifiers(leadHelmetId);
  addLeadArmorModifiers(leadShirtId);
  addLeadArmorModifiers(leadPantsId);

  const auto flippersId = Item::CreateArmor(items, "armor_flippers", "Flippers", Item::Component::AllowedSlots::Accessory, 0, "potion_healing");
  items.GetRegistry().get<Item::Component::StaticEffects>(flippersId).effects.append_range(std::vector<Item::Component::StaticEffect>{
    {
      .condition    = Item::EffectCondition::OnWorn,
      .quantityType = Item::EffectQuantityType::Additive,
      .type         = Item::EffectType::WaterJumpControlTimeModifier,
      .amount       = 4.6f,
    },
    {
      .condition    = Item::EffectCondition::OnWorn,
      .quantityType = Item::EffectQuantityType::Additive,
      .type         = Item::EffectType::WaterAccelerationModifier,
      .amount       = 15.0f,
    },
    {
      .condition    = Item::EffectCondition::OnWorn,
      .quantityType = Item::EffectQuantityType::Additive,
      .type         = Item::EffectType::WaterMaxSpeedModifier,
      .amount       = 1.0f,
    },
  });

  const auto ropeId = Item::CreateSimpleSpriteItem(items, "item_rope", "Rope", "potion_healing", 100, {0.5f, 0.25f, 0.1f});
  items.GetRegistry().emplace<Item::Component::Usable>(ropeId, 0.25f);
  items.GetRegistry().emplace<Item::Component::Rope>(ropeId);

  const auto grappleId = Item::CreateSimpleSpriteItem(items, "item_grappling_hook", "Grappling Hook", "potion_healing");
  items.GetRegistry().emplace<Item::Component::Usable>(grappleId, 0.25f);
  items.GetRegistry().emplace<Item::Component::GrapplingHookLauncher>(grappleId) = {
    .maxDistance      = 10,
    .launchVelocity   = 30,
    .pullAcceleration = 10,
  };

  auto& blocks = *globals->blockRegistry = {};

  const auto stoneBlock = Block::CreateStandardBlock(*this,
    {
      "stone",
      "Stone",
      Block::Component::Breakable{
        .initialHealth = 100,
        .damageTier    = 2,
        .damageFlags   = BlockDamageFlagBit::PICKAXE,
      },
      Block::Component::RenderAsTexturedCube{{
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "stone_albedo",
      }},
    });
  
  [[maybe_unused]] const auto cstoneBlock = Block::CreateStandardBlock(*this,
    {
      "stone_corrupt",
      "Corrupt Stone",
      Block::Component::Breakable{
        .initialHealth = 100,
        .damageTier    = 4,
        .damageFlags   = BlockDamageFlagBit::PICKAXE,
      },
      Block::Component::RenderAsTexturedCube{{
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "stone_corrupt_albedo",
      }},
    });

  [[maybe_unused]] const auto dirtBlock = Block::CreateStandardBlock(*this,
    {
      "dirt",
      "Dirt",
      Block::Component::Breakable{
        .initialHealth = 75,
        .damageTier    = 2,
        .damageFlags   = BlockDamageFlagBit::PICKAXE,
      },
      Block::Component::RenderAsTexturedCube{{
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "dirt_albedo",
      }},
    });

  [[maybe_unused]] const auto placeholderBlock = Block::CreateStandardBlock(*this,
    {
      "placeholder",
      "Placeholder",
      Block::Component::Breakable{
        .initialHealth = 1,
        .damageTier    = 0,
        .damageFlags   = BlockDamageFlagBit::ALL_TOOLS,
      },
      Block::Component::RenderAsTexturedCube{{
        .baseColorTexture = "error_8",
      }},
    });

  Block::CreateStandardBlock(*this,
    {
      "cloud_b",
      "Cloud B",
      Block::Component::Breakable{
        .initialHealth = 50,
        .damageTier    = 1,
      },
      Block::Component::RenderAsTexturedCube{{
        .baseColorFactor = {0.85f, 0.85f, 0.85f},
      }},
    });

  [[maybe_unused]] const auto stoneBlockId = Block::GetItemId(*this, stoneBlock);

  [[maybe_unused]] const auto frogLightId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        "frog_light",
        "Frog Light",
        Block::Component::Breakable{
          .initialHealth = 50,
        },
        Block::Component::RenderAsTexturedCube{{
          .baseColorFactor = {0, 0, 0},
          .emissionFactor  = {1, 5, 1},
        }},
      }));

  const auto grassMat = Block::CubeFaceMaterial{
    .randomizeTexcoordRotation = true,
    .baseColorTexture          = "grass_albedo",
  };
  const auto grassSideMat = Block::CubeFaceMaterial{
    .randomizeTexcoordRotation = false,
    .baseColorTexture          = "grass_side_albedo",
  };
  const auto dirtMat = Block::CubeFaceMaterial{
    .randomizeTexcoordRotation = true,
    .baseColorTexture          = "dirt_albedo",
  };
  [[maybe_unused]] const auto grassBlockId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        "grass",
        "Grass",
        Block::Component::Breakable{
          .initialHealth = 50,
          .damageTier    = 1,
          .damageFlags   = BlockDamageFlagBit::PICKAXE,
        },
        Block::Component::RenderAsTexturedCube2{grassSideMat, grassSideMat, grassSideMat, grassSideMat, grassMat, dirtMat},
      }));

  const auto cgrassMat = Block::CubeFaceMaterial{
    .randomizeTexcoordRotation = true,
    .baseColorTexture          = "grass_corrupt_albedo",
  };
  const auto cgrassSideMat = Block::CubeFaceMaterial{
    .randomizeTexcoordRotation = false,
    .baseColorTexture          = "grass_corrupt_side_albedo",
  };
  [[maybe_unused]] const auto cgrassBlockId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        "grass_corrupt",
        "Corrupt Grass",
        Block::Component::Breakable{
          .initialHealth = 50,
          .damageTier    = 1,
          .damageFlags   = BlockDamageFlagBit::PICKAXE,
        },
        Block::Component::RenderAsTexturedCube2{cgrassSideMat, cgrassSideMat, cgrassSideMat, cgrassSideMat, cgrassMat, dirtMat},
      }));

  [[maybe_unused]] const auto sandBlockId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        "sand",
        "Sand",
        Block::Component::Breakable{
          .initialHealth = 50,
          .damageTier    = 1,
          .damageFlags   = BlockDamageFlagBit::PICKAXE,
        },
        Block::Component::RenderAsTexturedCube{{
          .randomizeTexcoordRotation = true,
          .baseColorTexture          = "sand_albedo",
        }},
      }));

  [[maybe_unused]] const auto snowBlockId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        "snow",
        "Snow",
        Block::Component::Breakable{
          .initialHealth = 50,
          .damageTier    = 1,
          .damageFlags   = BlockDamageFlagBit::PICKAXE,
        },
        Block::Component::RenderAsTexturedCube{{
          .randomizeTexcoordRotation = true,
          .baseColorTexture          = "snow_albedo",
        }},
      }));

  RegisterOreBlock(*this, "coal", "Coal", "coal_albedo", 2);
  RegisterOreBlock(*this, "sulfur", "Sulfur", "sulfur_albedo", 2);
  const auto malachiteBlockId = RegisterOreBlock(*this, "malachite", "Malachite", "malachite_albedo", 2);
  RegisterOreBlock(*this, "copper", "Copper", "copper_albedo", 2);
  const auto galenaBlockId = RegisterOreBlock(*this, "galena", "Galena", "galena_albedo", 3);
  RegisterOreBlock(*this, "cassiterite", "Cassiterite", "cassiterite_albedo", 3);
  RegisterOreBlock(*this, "blood_ore", "Blood Ore", "blood_ore_albedo", 6);

  const auto forgeFace = Block::CubeFaceMaterial{
    .baseColorTexture = "forge_side_albedo",
    .emissionTexture  = "forge_side_emission",
    .emissionFactor   = {3, 3, 3},
  };
  const auto stoneFace = Block::CubeFaceMaterial{
    .baseColorTexture = "stone_albedo",
  };
  [[maybe_unused]] const auto forgeBlockItemId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        "forge",
        "Forge",
        Block::Component::Breakable{
          .initialHealth = 100,
          .damageTier    = 1,
          .damageFlags   = BlockDamageFlagBit::PICKAXE,
        },
        Block::Component::RenderAsTexturedCube2{forgeFace, stoneFace, stoneFace, stoneFace, stoneFace, stoneFace},
      }));
  Block::CreateStandardRotatedVariants(*this, blocks.Get("forge"));

  const auto orientId = Block::CreateStandardBlock(*this,
    {
      "orient",
      "Orient",
      Block::Component::Breakable{
        .initialHealth = 100,
        .damageTier    = 1,
        .damageFlags   = BlockDamageFlagBit::PICKAXE,
      },
      Block::Component::RenderAsTexturedCube2{
        Block::CubeFaceMaterial{.baseColorTexture = "north"},
        {.baseColorTexture = "south"},
        {.baseColorTexture = "east"},
        {.baseColorTexture = "west"},
        {.baseColorTexture = "up"},
        {.baseColorTexture = "down"},
      },
    });
  Block::CreateStandardRotatedVariants(*this, orientId);

  const auto bombBlockId = Block::CreateStandardBlock(*this,
    {
      .tag = "bomb",
      .name = "Bomb",
      .breakable = Block::Component::Breakable{
        .initialHealth = 40,
        .dropWhenBroken = std::monostate{},
      },
      .render = Block::Component::RenderAsTexturedCube{{
        .baseColorFactor = {0.8f, 0.2f, 0.2f},
        .emissionFactor  = {0.1f, 0.01f, 0.01f},
      }},
      .explode = Block::Component::ExplodeWhenBroken{
        .radius      = 3,
        .damage      = 100,
        .damageTier  = 0,
        .pushForce   = 8,
        .damageFlags = BlockDamageFlagBit::PICKAXE | BlockDamageFlagBit::AXE,
      }
    });
  
  [[maybe_unused]] const auto bombId = Block::GetItemId(*this, bombBlockId);

  const auto bigBombBlockId = Block::CreateStandardBlock(*this,
    {
      .tag = "bomb_big",
      .name = "Big Bomb",
      .breakable = Block::Component::Breakable{
        .initialHealth = 40,
        .dropWhenBroken = std::monostate{},
      },
      .render = Block::Component::RenderAsTexturedCube{{
        .baseColorFactor = {0.8f, 0.2f, 0.2f},
        .emissionFactor  = {0.5f, 0.1f, 0.1f},
      }},
      .explode = Block::Component::ExplodeWhenBroken{
        .radius      = 8,
        .damage      = 100,
        .damageTier  = 2,
        .pushForce   = 10,
        .damageFlags = BlockDamageFlagBit::PICKAXE | BlockDamageFlagBit::AXE | BlockDamageFlagBit::NO_LOOT,
      }
    });
  
  [[maybe_unused]] const auto bigBombId = Block::GetItemId(*this, bigBombBlockId);

  [[maybe_unused]] const auto woodBlockId = Block::CreateStandardBlock(*this,
    {
      "wood",
      "Wood",
      Block::Component::Breakable{
        .initialHealth  = 100,
        .damageTier     = 1,
        .damageFlags    = BlockDamageFlagBit::AXE,
        .dropWhenBroken = "tree",
      },
      Block::Component::RenderAsTexturedCube{{
        .baseColorFactor = {0.39f, 0.24f, 0.08f},
      }},
    });

  Block::CreateStandardBlock(*this,
    {
      "plank_wood",
      "Wood Plank",
      Block::Component::Breakable{
        .initialHealth  = 75,
        .damageTier     = 1,
        .damageFlags    = BlockDamageFlagBit::AXE,
        .dropWhenBroken = "tree",
      },
      Block::Component::RenderAsTexturedCube{
        {
          .baseColorTexture = "wood_plank_albedo",
        }},
    });

  struct FoliageBlockParams
  {
    const char* tag{};
    const char* name{};
    bool dropsSelf = true;
    bool isSolid = false;
    std::optional<Block::Component::RequiresSupport> support = Block::Component::RequiresSupport{Block::Direction::Down};
  };
  auto RegisterFoliageBlock = [&](const FoliageBlockParams& params) -> BlockId
  {
    auto vox       = Vox::LoadFromFile(GetAssetDirectory() / "voxels" / "models" / (std::string(params.tag) + ".vox"));
    using LootType = decltype(Block::Component::Breakable::dropWhenBroken);
    const auto id  = Block::CreateStandardBlock(*this,
       {
         .tag  = params.tag,
         .name = params.name,
         .breakable =
          Block::Component::Breakable{
             .initialHealth  = 10,
             .dropWhenBroken = params.dropsSelf ? LootType(Block::DropSelf{}) : LootType(std::monostate{}),
          },
         .render             = Block::Component::RenderAsSubGrid{.subGrid = VoxToSubGrid(*vox)},
         .physicalProperties = {.isSolid = params.isSolid},
         .support            = params.support,
      });
    return id;
  };

  RegisterFoliageBlock({.tag = "test", .name = "Test", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "grass_long", .name = "Long Grass", .dropsSelf = false});
  RegisterFoliageBlock({.tag = "grass_medium", .name = "Medium Grass", .dropsSelf = false});
  RegisterFoliageBlock({.tag = "grass_short", .name = "Short Grass", .dropsSelf = false});
  RegisterFoliageBlock({.tag = "mushroom", .name = "Mushroom", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "mushroom_glowing", .name = "Glowing Mushroom", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "rock_small", .name = "Small Rock", .dropsSelf = false});
  const auto vineEnd   = RegisterFoliageBlock({.tag = "vines_end", .name = "Vines End", .dropsSelf = false, .support = std::nullopt});
  const auto vineMain = RegisterFoliageBlock({.tag = "vines_main", .name = "Vines Main", .dropsSelf = false, .support = std::nullopt});
  const auto rootsEnd = RegisterFoliageBlock({.tag = "roots_end", .name = "Roots End", .dropsSelf = false, .support = std::nullopt});
  const auto rootsMain = RegisterFoliageBlock({.tag = "roots_main", .name = "Roots Main", .dropsSelf = false, .support = std::nullopt});
  RegisterFoliageBlock({.tag = "bush_01", .name = "Bush 1", .dropsSelf = false});
  RegisterFoliageBlock({.tag = "bush_02", .name = "Bush 2", .dropsSelf = false});
  const auto grassBase = RegisterFoliageBlock({.tag = "grass_double_base", .name = "Double Grass Base", .dropsSelf = false});
  const auto grassTop = RegisterFoliageBlock({.tag = "grass_double_top", .name = "Double Grass Top", .dropsSelf = false, .support = std::nullopt});
  blocks.GetRegistry().emplace<Block::Component::RequiresSupportByBlocks>(entt::entity(grassTop)).blocks[int(Block::Direction::Down)] = grassBase;
  const auto leaves01 = RegisterFoliageBlock({.tag = "leaves_01", .name = "Leaves 1", .dropsSelf = false, .support = std::nullopt});
  const auto leaves02 = RegisterFoliageBlock({.tag = "leaves_02", .name = "Leaves 2", .dropsSelf = false, .support = std::nullopt});
  RegisterFoliageBlock({.tag = "dandelion", .name = "Dandelion", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "rose", .name = "Rose", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "pot", .name = "Pot", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "SM_Deccer_Cubes_Small", .name = "Deccer's Cubes", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "chair", .name = "Chair", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "table", .name = "Table", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "cloud", .name = "Cloud", .dropsSelf = true, .support = std::nullopt});
  RegisterFoliageBlock({.tag = "anvil_lead", .name = "Lead Anvil", .dropsSelf = true, .isSolid = true});
  RegisterFoliageBlock({.tag = "cactus_small", .name = "Small Cactus", .dropsSelf = true});
  RegisterFoliageBlock({.tag = "bush_03", .name = "Bush 3", .dropsSelf = false});
  RegisterFoliageBlock({.tag = "remains_generic", .name = "Corpse", .dropsSelf = false});
  const auto leaves03 = RegisterFoliageBlock({.tag = "leaves_burnwillow_01", .name = "BW Leaves", .dropsSelf = false, .support = std::nullopt});
  const auto vineEndB = RegisterFoliageBlock({.tag = "vines_end_burnwillow", .name = "Vines End BW", .dropsSelf = false, .support = std::nullopt});
  const auto vineMainB = RegisterFoliageBlock({.tag = "vines_main_burnwillow", .name = "Vines Main BW", .dropsSelf = false, .support = std::nullopt});

  using RSA = Block::Component::RequiresSupportAdvanced;

  blocks.GetRegistry().emplace<RSA>(entt::entity(vineEnd)).supports[int(Block::Direction::Up)]   = RSA::Support{RSA::SolidSupport{}, leaves01, leaves02, vineMain};
  blocks.GetRegistry().emplace<RSA>(entt::entity(vineMain)).supports[int(Block::Direction::Up)]  = RSA::Support{RSA::SolidSupport{}, leaves01, leaves02, vineMain};
  blocks.GetRegistry().emplace<RSA>(entt::entity(rootsEnd)).supports[int(Block::Direction::Up)]  = RSA::Support{RSA::SolidSupport{}, leaves01, leaves02, rootsMain};
  blocks.GetRegistry().emplace<RSA>(entt::entity(rootsMain)).supports[int(Block::Direction::Up)] = RSA::Support{RSA::SolidSupport{}, leaves01, leaves02, rootsMain};
  blocks.GetRegistry().emplace<RSA>(entt::entity(vineEndB)).supports[int(Block::Direction::Up)]  = RSA::Support{RSA::SolidSupport{}, leaves03, vineMainB};
  blocks.GetRegistry().emplace<RSA>(entt::entity(vineMainB)).supports[int(Block::Direction::Up)] = RSA::Support{RSA::SolidSupport{}, leaves03, vineMainB};

  const auto arrow = RegisterFoliageBlock({.tag = "north_arrow", .name = "North Arrow", .dropsSelf = true});
  Block::CreateStandardRotatedVariants(*this, arrow);

  {
    const auto doorBottom = RegisterFoliageBlock({.tag = "door_bottom", .name = "Door", .dropsSelf = true, .isSolid = true, .support = std::nullopt});
    const auto doorTop = RegisterFoliageBlock({.tag = "door_top", .name = "door_top", .dropsSelf = false, .isSolid = true, .support = std::nullopt});

    blocks.GetRegistry().emplace<Block::Component::RequiresSupport>(entt::entity(doorBottom));
    blocks.GetRegistry().emplace<Block::Component::SpawnExtraBlockOnPlace>(entt::entity(doorBottom)) = {.block = doorTop, .direction = Block::Direction::Up};

    blocks.GetRegistry().emplace<Block::Component::InterlinkedBlock>(entt::entity(doorBottom), Block::Direction::Up);
    blocks.GetRegistry().emplace<Block::Component::InterlinkedBlock>(entt::entity(doorTop), Block::Direction::Down);
  }

  {
    const auto doorBottom = RegisterFoliageBlock({.tag = "door_bottom_open", .name = "door_bottom_open", .dropsSelf = true, .isSolid = true, .support = std::nullopt});
    const auto doorTop    = RegisterFoliageBlock({.tag = "door_top_open", .name = "door_top_open", .dropsSelf = false, .isSolid = true, .support = std::nullopt});

    blocks.GetRegistry().emplace<Block::Component::RequiresSupport>(entt::entity(doorBottom));
    blocks.GetRegistry().emplace<Block::Component::SpawnExtraBlockOnPlace>(entt::entity(doorBottom)) = {.block = doorTop, .direction = Block::Direction::Up};

    blocks.GetRegistry().emplace<Block::Component::InterlinkedBlock>(entt::entity(doorBottom), Block::Direction::Up);
    blocks.GetRegistry().emplace<Block::Component::InterlinkedBlock>(entt::entity(doorTop), Block::Direction::Down);
  }

  blocks.GetRegistry().emplace<Block::Component::TransformWhenUsed>(entt::entity(blocks.Get("door_bottom")), blocks.Get("door_bottom_open"));
  blocks.GetRegistry().emplace<Block::Component::TransformWhenUsed>(entt::entity(blocks.Get("door_bottom_open")), blocks.Get("door_bottom"));
  blocks.GetRegistry().emplace<Block::Component::TransformWhenUsed>(entt::entity(blocks.Get("door_top")), blocks.Get("door_top_open"));
  blocks.GetRegistry().emplace<Block::Component::TransformWhenUsed>(entt::entity(blocks.Get("door_top_open")), blocks.Get("door_top"));

  blocks.GetRegistry().get<Block::Component::Breakable>(entt::entity(blocks.Get("door_bottom"))).dropWhenBroken =
    ItemState{.id = blocks.GetRegistry().get<Block::Component::CorrespondingItem>(entt::entity(blocks.Get("door_bottom"))).item};
  blocks.GetRegistry().get<Block::Component::Breakable>(entt::entity(blocks.Get("door_bottom_open"))).dropWhenBroken =
    ItemState{.id = blocks.GetRegistry().get<Block::Component::CorrespondingItem>(entt::entity(blocks.Get("door_bottom"))).item};

  Block::CreateStandardRotatedVariants(*this, blocks.Get("door_bottom"));
  Block::CreateStandardRotatedVariants(*this, blocks.Get("door_bottom_open"));
  Block::CreateStandardRotatedVariants(*this, blocks.Get("door_top"));
  Block::CreateStandardRotatedVariants(*this, blocks.Get("door_top_open"));

  Block::UpdateTransformedForRotatedVariants(*this, blocks.Get("door_bottom"));
  Block::UpdateTransformedForRotatedVariants(*this, blocks.Get("door_bottom_open"));
  Block::UpdateTransformedForRotatedVariants(*this, blocks.Get("door_top"));
  Block::UpdateTransformedForRotatedVariants(*this, blocks.Get("door_top_open"));

  {
    RegisterFoliageBlock({.tag = "cargo_side", .name = "Cargo Side", .dropsSelf = true, .isSolid = true, .support = std::nullopt});
    RegisterFoliageBlock({.tag = "cargo_top", .name = "Cargo Top", .dropsSelf = true, .isSolid = true, .support = std::nullopt});
    RegisterFoliageBlock({.tag = "cargo_bottom", .name = "Cargo Bottom", .dropsSelf = true, .isSolid = true, .support = std::nullopt});
    RegisterFoliageBlock({.tag = "cargo_runner_top", .name = "Cargo Runner Top", .dropsSelf = true, .isSolid = true, .support = std::nullopt});
    RegisterFoliageBlock({.tag = "cargo_runner_bottom", .name = "Cargo Runner Bottom", .dropsSelf = true, .isSolid = true, .support = std::nullopt});
    RegisterFoliageBlock({.tag = "cargo_runner_vertical", .name = "Cargo Runner Vertical", .dropsSelf = true, .isSolid = true, .support = std::nullopt});
    Block::CreateStandardRotatedVariants(*this, blocks.Get("cargo_side"));
    Block::CreateStandardRotatedVariants(*this, blocks.Get("cargo_top"));
    Block::CreateStandardRotatedVariants(*this, blocks.Get("cargo_bottom"));
    Block::CreateStandardRotatedVariants(*this, blocks.Get("cargo_runner_top"));
    Block::CreateStandardRotatedVariants(*this, blocks.Get("cargo_runner_bottom"));
    Block::CreateStandardRotatedVariants(*this, blocks.Get("cargo_runner_vertical"));
  }

  {
    RegisterFoliageBlock({.tag = "saltpeter_spike_base", .name = "Saltpeter Spike Base", .dropsSelf = true, .isSolid = true});
    RegisterFoliageBlock({.tag = "saltpeter_spike_top", .name = "Saltpeter Spike Top", .dropsSelf = true, .isSolid = false});
    RegisterFoliageBlock({.tag = "saltpeter_spike_small", .name = "Saltpeter Spike Small", .dropsSelf = true, .isSolid = false});
  }

  {
    auto waters = std::vector<BlockId>();

    for (int i = 1; i <= 8; i++)
    {
      auto id          = "water_" + std::to_string(i);
      const auto block = RegisterFoliageBlock({.tag = id.c_str(), .name = id.c_str(), .dropsSelf = false, .support = std::nullopt});
      waters.push_back(block);
    }

    blocks.GetRegistry().emplace<Block::Component::BaseFlow>(entt::entity(blocks.Get("water_8")), waters);

    const auto water8 = blocks.Get("water_8");
    for (int i = 1; i <= 8; i++)
    {
      auto id = "water_" + std::to_string(i);
      blocks.GetRegistry().emplace<Block::Component::Flows>(entt::entity(blocks.Get(id)), water8);
    }
  }

  Item::CreateGun(items,
    "weapon_watergun",
    "Water Gun",
    1000,
    {
      .model           = "ar15",
      .damage          = 1,
      .knockback       = 1,
      .bullets         = 9,
      .velocity        = 150,
      .accuracyMoa     = 300,
      .vrecoil         = 0,
      .vrecoilDev      = 0,
      .hrecoil         = 0,
      .hrecoilDev      = 0,
      .spawnBlockOnHit = blocks.Get("water_8"),
      .particles = false,
    });

  constexpr auto szz = glm::ivec3{4, 4, 4};
  auto subGrid       = std::make_unique<Voxel::SubVoxel[]>(szz.x * szz.y * szz.z);

  for (int z = 0; z < szz.z; z++)
    for (int y = 0; y < szz.y; y++)
      for (int x = 0; x < szz.x; x++)
      {
        auto& voxel = subGrid[Voxel::Grid::FlattenGenericCoord(szz, {x, y, z})];
        if (z % 2 == 0 && y % 2 == 0 && x % 2 == 0)
        {
          voxel = Voxel::SubVoxel(1);
        }
        else
        {
          voxel = Voxel::SubVoxel::Air;
        }
      }

  [[maybe_unused]] const auto subBlockId = Block::CreateStandardBlock(*this,
    {
      "sub",
      "Sub",
      Block::Component::Breakable{
        .initialHealth = 100,
      },
      Block::Component::RenderAsSubGrid{
        .subGrid = std::make_shared<Voxel::SubGrid>(Voxel::SubGrid{
          .dimensions = szz,
          .grid       = std::move(subGrid),
          .materials  = {{glm::vec4(1, 1, 1, 1)}},
        }),
      },
    });

  [[maybe_unused]] const auto lightBlockItemId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        "light",
        "Light",
        {},
        Block::Component::RenderAsTexturedCube{{.emissionFactor = {5, 3, 2}}},
      }));

  [[maybe_unused]] const auto torchItemId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        .tag                = "torch",
        .name               = "Torch",
        .breakable          = Block::Component::Breakable{.initialHealth = 10},
        .render             = std::nullopt,
        .physicalProperties = {.isSolid = false},
        .entityPrefab       = Block::Component::SpawnDependentEntityPrefabWhenPlaced{.id = entityPrefabs.GetId("Torch")},
      }));
  blocks.GetRegistry().emplace<Block::Component::RequiresSupport>(entt::entity(blocks.Get("torch")));

  [[maybe_unused]] const auto chestItemId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        .tag                = "chest",
        .name               = "Chest",
        .breakable = Block::Component::Breakable{},
        .render =
          Block::Component::RenderAsSubGrid{
            .subGrid = VoxToSubGrid(*Vox::LoadFromFile(GetAssetDirectory() / "voxels" / "models" / "chest.vox")),
          },
        .entityPrefab = Block::Component::SpawnDependentEntityPrefabWhenPlaced{.id = entityPrefabs.GetId("Chest")},
      }));

  [[maybe_unused]] const auto scriptingBlockId = Block::GetItemId(*this,
    Block::CreateStandardBlock(*this,
      {
        .tag = "scripting",
        .name = "Scripting Block",
        .breakable = Block::Component::Breakable{
          .initialHealth = 1,
          .damageTier    = 10,
          .damageFlags   = BlockDamageFlagBit::ALL_TOOLS,
        },
        .render = Block::Component::RenderAsTexturedCube{{
          .randomizeTexcoordRotation = true,
          .baseColorTexture          = "snow_albedo",
          .baseColorFactor           = {3, .4f, 2},
        }},
        .entityPrefab = Block::Component::SpawnDependentEntityPrefabWhenPlaced{.id = entityPrefabs.GetId("SimpleScriptable")},
      }));

  auto prevFireBlockId = entt::entity(entt::null);
  for (int i = 7; i >= 1; i--)
  {
    const auto fireBlockId = RegisterFoliageBlock({.tag = ("fire" + std::to_string(i)).c_str(), .name = ("Fire" + std::to_string(i)).c_str(), .dropsSelf = false, .isSolid = false});
    blocks.GetRegistry().emplace<Block::Component::Fire>(entt::entity(fireBlockId),
      Block::Component::Fire{
        .blockToSpawnOnPropagate         = i == 7 ? fireBlockId : BlockId(prevFireBlockId),
        .chanceToPropagateOnRandomUpdate = 0.015f / i,
        .chanceToDespawnOnRandomUpdate   = 0.002f * (i * 0.25f),
      });
    blocks.GetRegistry().remove<Block::Component::RequiresSupport>(entt::entity(fireBlockId));
    prevFireBlockId = entt::entity(fireBlockId);
  }

  auto& prefabs = *globals->prefabRegistry = {};
  // const auto grassId = blocks.Get("Grass").GetBlockId();
  // const auto frogLightBlockId = blocks.Get("Frog Light").GetBlockId();

  auto* tallGrass = new SimplePrefab({.name = "Double Grass"});
  tallGrass->voxels.emplace_back(glm::ivec3(0, 0, 0), blocks.Get("grass_double_base"));
  tallGrass->voxels.emplace_back(glm::ivec3(0, 1, 0), blocks.Get("grass_double_top"));
  prefabs.Add(tallGrass);

  {
    auto* testTree = new SimplePrefab({.name = "Tree"});
    auto& voxels   = testTree->voxels;
    for (int z = -3; z <= 3; z++)
      for (int y = -1; y <= 3; y++)
        for (int x = -3; x <= 3; x++)
        {
          if (Math::Distance2({x, y + 3, z}, {0, 3, 0}) < 9)
          {
            voxels.emplace_back(glm::ivec3(x, y + 3, z), blocks.Get("leaves_01"));
          }
        }
    voxels.emplace_back(glm::ivec3(0, 0, 0), woodBlockId);
    voxels.emplace_back(glm::ivec3(0, 1, 0), woodBlockId);
    voxels.emplace_back(glm::ivec3(0, 2, 0), woodBlockId);
    voxels.emplace_back(glm::ivec3(0, 3, 0), woodBlockId);
    voxels.emplace_back(glm::ivec3(0, 4, 0), woodBlockId);
    prefabs.Add(testTree);
  }

  {
    auto* testTree = new SimplePrefab({.name = "Tree2"});
    auto& binky    = testTree->voxels;
    for (int z = -3; z <= 3; z++)
      for (int y = -1; y <= 3; y++)
        for (int x = -3; x <= 3; x++)
        {
          if (Math::Distance2({x, y + 3, z}, {0, 3, 0}) < 9)
          {
            binky.emplace_back(glm::ivec3(x, y + 3, z), blocks.Get("leaves_02"));
          }
        }
    binky.emplace_back(glm::ivec3(0, 0, 0), woodBlockId);
    binky.emplace_back(glm::ivec3(0, 1, 0), woodBlockId);
    binky.emplace_back(glm::ivec3(0, 2, 0), woodBlockId);
    binky.emplace_back(glm::ivec3(0, 3, 0), woodBlockId);
    binky.emplace_back(glm::ivec3(0, 4, 0), woodBlockId);
    prefabs.Add(testTree);
  }

  auto* testDungeon = new DungeonPrefab({.name = "Dungeon"});
  prefabs.Add(testDungeon);

  prefabs.Add(new VinePrefab({.name = "Vine"}));
  prefabs.Add(new RootPrefab({.name = "Root"}));
  prefabs.Add(new AbandonedHousePrefab({.name = "AbandonedHouse"}));
  prefabs.Add(new FloatingIslandPrefab({.name = "FloatingIsland"}));

  [[maybe_unused]] auto& crafting = globals->game->crafting = {};
  
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 15}},
    {{forgeBlockItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}, {stickId, 1}},
    {{stoneSpearId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}, {stickId, 1}},
    {{stonePickaxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}, {stickId, 1}},
    {{stoneAxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stickId, 3}},
    {{charcoalId, 1}},
    blocks.Get("forge"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stickId, 1}, {charcoalId, 1}},
    {{torchItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 1}, {charcoalId, 1}, {Block::GetItemId(*this, blocks.Get("mushroom")), 1}},
    {{healingPotion, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{Block::GetItemId(*this, malachiteBlockId), 4}, {charcoalId, 1}},
    {{copperIngotId, 1}},
    blocks.Get("forge"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{Block::GetItemId(*this, galenaBlockId), 4}, {charcoalId, 1}},
    {{leadIngotId, 1}},
    blocks.Get("forge"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{leadIngotId, 10}, {charcoalId, 1}},
    {{Block::GetItemId(*this, blocks.Get("anvil_lead")), 1}},
    blocks.Get("forge"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotId, 5}, {stickId, 1}},
    {{copperSpearId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotId, 5}, {stickId, 1}},
    {{copperPickaxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotId, 5}, {stickId, 1}},
    {{copperAxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{leadIngotId, 5}, {stickId, 1}},
    {{leadSpearId, 1}},
    blocks.Get("anvil_lead"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{leadIngotId, 5}, {stickId, 1}},
    {{leadPickaxeId, 1}},
    blocks.Get("anvil_lead"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{leadIngotId, 5}, {stickId, 1}},
    {{leadAxeId, 1}},
    blocks.Get("anvil_lead"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotId, 5}, {stickId, 1}},
    {{copperHelmetId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotId, 5}, {stickId, 1}},
    {{copperShirtId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotId, 5}, {stickId, 1}},
    {{copperPantsId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{leadIngotId, 5}, {stickId, 1}},
    {{leadHelmetId, 1}},
    blocks.Get("anvil_lead"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{leadIngotId, 5}, {stickId, 1}},
    {{leadShirtId, 1}},
    blocks.Get("anvil_lead"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{leadIngotId, 5}, {stickId, 1}},
    {{leadPantsId, 1}},
    blocks.Get("anvil_lead"),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{coinId, 10}, {stickId, 20}},
    {{chestItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{gunId, 1}, {susCoin, 1}},
    {{flareGunId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {},
    {{flareGunId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    .ingredients = {{stickId, 1}},
    .output      = {{chestItemId, 1}},
    .name        = "Chest Test",
    .description = "Makes a really useful item for storing stuff. This block can be placed and interacted with by pressing F.",
  });

  auto& loot        = globals->game->lootRegistry = {};
  auto standardLoot = std::make_unique<LootDrops>();
  standardLoot->drops.emplace_back(RandomLootDrop{
    .item         = coinId,
    .count        = 6,
    .chanceForOne = 0.5f,
  });
  loot.Add("standard", std::move(standardLoot));

  auto treeLoot = std::make_unique<LootDrops>();
  treeLoot->drops.emplace_back(RandomLootDrop{
    .item         = stickId,
    .count        = 6,
    .chanceForOne = 0.65f,
  });
  treeLoot->drops.emplace_back(RandomLootDrop{
    .item         = coolStickId,
    .count        = 1,
    .chanceForOne = 0.01f,
  });
  loot.Add("tree", std::move(treeLoot));

  auto wormLoot = std::make_unique<LootDrops>();
  wormLoot->drops.emplace_back(RandomLootDrop{
    .item         = gunId,
    .count        = 1,
    .chanceForOne = 1,
  });
  wormLoot->drops.emplace_back(RandomLootDrop{
    .item         = coinId,
    .count        = 150,
    .chanceForOne = 0.5f,
  });
  loot.Add("worm", std::move(wormLoot));
}

void World::CreateGrid(glm::ivec3 numChunks)
{
  *globals->grid = Voxel::Grid(numChunks);
  CreateRenderingMaterials();
}

void World::CreateRenderingMaterials()
{
  ZoneScoped;

  *globals->surfaceBiomes     = GetSurfaceBiomeNoises(*this);
  *globals->undergroundBiomes = GetUndergroundBiomeNoises(*this);

  auto voxelMats = std::vector<Voxel::Grid::Material>();
  const auto& blocks = *globals->blockRegistry;
  const auto& blockMap = blocks.GetIdToTagMap();
  for (const auto& [id, tag] : blockMap)
  {
    voxelMats.emplace_back(Voxel::Grid::Material{
      .isVisible = Block::IsVisible(*this, id),
      .isSolid   = Block::IsSolid(*this, id),
      .subGrid   = Block::GetSubGrid(*this, id),
    });
  }
  globals->grid->SetMaterialArray(std::move(voxelMats));

  auto* head = globals->head;
  head->CreateRenderingMaterials(*this);
}

void World::CreateInitialEntities()
{
  // Make player entity
  auto p = CreatePlayer();
  registry_.emplace<LocalPlayer>(p);

  auto e                          = CreateRenderableEntity({0, 0, 0});
  registry_.emplace<Name>(e).name = "Test";
  registry_.emplace<Mesh>(e).name = "frog";

  auto pe                              = CreateRenderableEntity({});
  registry_.emplace<Name>(pe).name     = "Death Floor";
  registry_.emplace<ContactDamage>(pe) = {.damage = 1000, .knockback = 0};
  registry_.emplace<Physics::RigidBodySettings>(pe,
    Physics::RigidBodySettings{
      .shape      = {Physics::Plane{{0, 1, 0}, 0}},
      .activate   = false,
      .isSensor   = true,
      .motionType = JPH::EMotionType::Static,
      .layer      = Physics::Layers::HURTBOX,
    });

  auto ve                          = registry_.create();
  registry_.emplace<Name>(ve).name = "Voxels";
  registry_.emplace<VoxelsComponent>(ve);
  registry_.emplace<Physics::RigidBodySettings>(ve,
    Physics::RigidBodySettings{
      .shape      = {Physics::UseTwoLevelGrid{}},
      .activate   = false,
      .motionType = JPH::EMotionType::Static,
      .layer      = Physics::Layers::WORLD,
    });
}

