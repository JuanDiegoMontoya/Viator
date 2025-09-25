#include "Assets.h"
#include "Core/Assert2.h"
#include "Item.h"
#include "Physics/Physics.h"
#include "Prefab.h"
#include "VoxLoader.h"
#include "World.h"
#include "Voxel/Grid.h"

#include "FastNoise/FastNoise.h"
#include "tracy/Tracy.hpp"

#include <execution>

class DungeonPrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  void Instantiate(World& world, glm::ivec3 worldPos) const override
  {
    const auto& blocks = world.GetRegistry().ctx().get<Block::Registry>();
    const auto& items  = world.GetRegistry().ctx().get<Item::Registry>();
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
    const auto& blocks    = world.GetRegistry().ctx().get<Block::Registry>();
    const auto& vinesMain = blocks.Get("vines_main");
    const auto& vinesEnd  = blocks.Get("vines_end");

    // Count number of air blocks starting from worldPos.
    auto& grid = world.GetRegistry().ctx().get<Voxel::Grid>();
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
    const auto& blocks    = world.GetRegistry().ctx().get<Block::Registry>();
    const auto& rootsMain = blocks.Get("roots_main");
    const auto& rootsEnd  = blocks.Get("roots_end");

    // Count number of air blocks starting from worldPos.
    auto& grid         = world.GetRegistry().ctx().get<Voxel::Grid>();
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
    auto& grid         = world.GetRegistry().ctx().get<Voxel::Grid>();
    const auto& blocks = world.GetRegistry().ctx().get<Block::Registry>();
    const auto& items  = world.GetRegistry().ctx().get<Item::Registry>();
    const auto& air    = blocks.Get("air");
    const auto& wood   = blocks.Get("plank_wood");
    
    constexpr int MIN_ROOM_DIM = 5;
    constexpr int MAX_ROOM_DIM = 8;
    constexpr int ROOM_HEIGHT  = 6;
    constexpr int MAX_SUPPORT_LENGTH = 10;

    auto& rng             = world.GetRegistry().ctx().get<PCG::Rng>();
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
    const auto& blocks        = world.GetRegistry().ctx().get<Block::Registry>();
    const auto& grid          = world.GetRegistry().ctx().get<Voxel::Grid>();
    const auto& cloudA        = blocks.Get("cloud");
    const auto& cloudB        = blocks.Get("cloud_b");
    const auto seed           = int(world.GetRegistry().ctx().get<PCG::Rng>().RandU32(0, 1 << 20));

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

    world.GetRegistry().ctx().get<PrefabRegistry>().Get("AbandonedHouse").Instantiate(world, worldPos + glm::ivec3(0, structureY, 0));
  }
};

void World::InitializeGameState()
{
  ticks_ = 0;

  for (auto e : registry_.view<entt::entity>())
  {
    registry_.destroy(e);
  }

  registry_.ctx().insert_or_assign<NpcSpawnDirector>(NpcSpawnDirector{*this});
  registry_.ctx().insert_or_assign<bool>("UpdateNPCSpawnDirector"_hs, true);

  // Reset RNG
  registry_.ctx().insert_or_assign<PCG::Rng>(1234);
}

void World::InitializeGameDefinitions()
{
  ZoneScoped;
  // Reset entity prefab registry
  auto& entityPrefabs               = registry_.ctx().insert_or_assign<EntityPrefabRegistry>({});
  [[maybe_unused]] auto meleeFrogId = entityPrefabs.Add("Melee Frog", new MeleeFrogDefinition({.name = "Melee Frog", .spawnChance = 0.095f}));
  [[maybe_unused]] auto flyingFrogId =
    entityPrefabs.Add("Flying Frog", new FlyingFrogDefinition({.name = "Flying Frog", .spawnChance = 0.035f, .canSpawnFloating = true}));
  [[maybe_unused]] auto torchId    = entityPrefabs.Add("Torch", new TorchDefinition());
  [[maybe_unused]] auto chestId    = entityPrefabs.Add("Chest", new ChestDefinition({.isVisible = false}));
  [[maybe_unused]] auto mushroomId = entityPrefabs.Add("Mushroom", new ShrimpleMeshPrefabDefinition("mushroom", {.52f, .31f, .16f}));
  [[maybe_unused]] auto wormBossId = entityPrefabs.Add("Worm Boss", new WormBossDefinition());

  // Reset item registry
  auto& items = registry_.ctx().insert_or_assign<Item::Registry>({});

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

  auto& blocks = registry_.ctx().insert_or_assign<Block::Registry>({});

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
        Block::Component::RenderAsTexturedCube2{dirtMat, dirtMat, dirtMat, dirtMat, grassMat, dirtMat},
      }));

  const auto malachiteBlockId = Block::CreateStandardBlock(*this,
    {"malachite",
      "Malachite",
      Block::Component::Breakable{
        .initialHealth = 100,
        .damageTier    = 2,
        .damageFlags   = BlockDamageFlagBit::PICKAXE,
      },
      Block::Component::RenderAsTexturedCube{{
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "malachite_albedo",
      }}});
  blocks.GetRegistry().emplace<Block::Component::Valuable>(entt::entity(malachiteBlockId));

  const auto galenaBlockId = Block::CreateStandardBlock(*this,
    {
      "galena",
      "Galena",
      Block::Component::Breakable{
        .initialHealth = 100,
        .damageTier    = 3,
        .damageFlags   = BlockDamageFlagBit::PICKAXE,
      },
      Block::Component::RenderAsTexturedCube{{
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "galena_albedo",
      }},
    });
  blocks.GetRegistry().emplace<Block::Component::Valuable>(entt::entity(galenaBlockId));

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

  auto RegisterFoliageBlock = [&](const char* tag, const char* name, bool dropsSelf, bool isSolid = false) -> BlockId
  {
    auto vox       = Vox::LoadFromFile(GetAssetDirectory() / "voxels" / "models" / (std::string(tag) + ".vox"));
    using LootType = decltype(Block::Component::Breakable::dropWhenBroken);
    return Block::CreateStandardBlock(*this,
      {
        tag,
        name,
        Block::Component::Breakable{
          .initialHealth  = 10,
          .dropWhenBroken = dropsSelf ? LootType(Block::DropSelf{}) : LootType(std::monostate{}),
        },
        Block::Component::RenderAsSubGrid{
          .subGrid = VoxToSubGrid(*vox),
        },
        {
          .isSolid = isSolid,
        },
      });
  };

  RegisterFoliageBlock("test", "Test", true);
  RegisterFoliageBlock("grass_long", "Long Grass", false);
  RegisterFoliageBlock("grass_medium", "Medium Grass", false);
  RegisterFoliageBlock("grass_short", "Short Grass", false);
  RegisterFoliageBlock("mushroom", "Mushroom", true);
  RegisterFoliageBlock("mushroom_glowing", "Glowing Mushroom", true);
  RegisterFoliageBlock("rock_small", "Small Rock", false);
  RegisterFoliageBlock("vines_end", "Vines End", false);
  RegisterFoliageBlock("vines_main", "Vines Main", false);
  RegisterFoliageBlock("roots_end", "Roots End", false);
  RegisterFoliageBlock("roots_main", "Roots Main", false);
  RegisterFoliageBlock("bush_01", "Bush 1", false);
  RegisterFoliageBlock("bush_02", "Bush 2", false);
  RegisterFoliageBlock("grass_double_base", "Double Grass Base", false);
  RegisterFoliageBlock("grass_double_top", "Double Grass Top", false);
  RegisterFoliageBlock("leaves_01", "Leaves 1", false);
  RegisterFoliageBlock("leaves_02", "Leaves 2", false);
  RegisterFoliageBlock("dandelion", "Dandelion", true);
  RegisterFoliageBlock("rose", "Rose", true);
  RegisterFoliageBlock("pot", "Pot", true);
  RegisterFoliageBlock("SM_Deccer_Cubes_Small", "Deccer's Cubes", true);
  RegisterFoliageBlock("chair", "Chair", true);
  RegisterFoliageBlock("table", "Table", true);
  RegisterFoliageBlock("cloud", "Cloud", true);
  RegisterFoliageBlock("anvil_lead", "Lead Anvil", true, true);
  const auto arrow = RegisterFoliageBlock("north_arrow", "North Arrow", true);
  Block::CreateStandardRotatedVariants(*this, arrow);

  {
    const auto doorBottom = RegisterFoliageBlock("door_bottom", "Door", true, true);
    const auto doorTop = RegisterFoliageBlock("door_top", "door_top", false, true);

    blocks.GetRegistry().emplace<Block::Component::RequiresSupport>(entt::entity(doorBottom));
    blocks.GetRegistry().emplace<Block::Component::SpawnExtraBlockOnPlace>(entt::entity(doorBottom)) = {.block = doorTop, .direction = Block::Direction::Up};

    blocks.GetRegistry().emplace<Block::Component::InterlinkedBlock>(entt::entity(doorBottom), Block::Direction::Up);
    blocks.GetRegistry().emplace<Block::Component::InterlinkedBlock>(entt::entity(doorTop), Block::Direction::Down);
  }

  {
    const auto doorBottom = RegisterFoliageBlock("door_bottom_open", "door_bottom_open", true, true);
    const auto doorTop    = RegisterFoliageBlock("door_top_open", "door_top_open", false, true);

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
    auto waters = std::vector<BlockId>();

    for (int i = 1; i <= 8; i++)
    {
      auto id          = "water_" + std::to_string(i);
      const auto block = RegisterFoliageBlock(id.c_str(), id.c_str(), true);
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
        .entityPrefab       = Block::Component::SpawnDependentEntityPrefabWhenPlaced{.id = torchId},
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
        .entityPrefab = Block::Component::SpawnDependentEntityPrefabWhenPlaced{.id = chestId},
      }));

  auto& prefabs = registry_.ctx().insert_or_assign<PrefabRegistry>({});
  // const auto grassId = blocks.Get("Grass").GetBlockId();
  // const auto frogLightBlockId = blocks.Get("Frog Light").GetBlockId();

  auto* tallGrass = new SimplePrefab({.name = "Double Grass"});
  tallGrass->voxels.emplace_back(glm::ivec3(0, 0, 0), blocks.Get("grass_double_base"));
  tallGrass->voxels.emplace_back(glm::ivec3(0, 1, 0), blocks.Get("grass_double_top"));
  prefabs.Add(tallGrass);

  {
    auto* testTree = new SimplePrefab({.name = "Tree"});
    auto& binky    = testTree->voxels;
    for (int z = -3; z <= 3; z++)
      for (int y = -1; y <= 3; y++)
        for (int x = -3; x <= 3; x++)
        {
          if (Math::Distance2({x, y + 3, z}, {0, 3, 0}) < 9)
          {
            binky.emplace_back(glm::ivec3(x, y + 3, z), blocks.Get("leaves_01"));
          }
        }
    binky.emplace_back(glm::ivec3(0, 0, 0), woodBlockId);
    binky.emplace_back(glm::ivec3(0, 1, 0), woodBlockId);
    binky.emplace_back(glm::ivec3(0, 2, 0), woodBlockId);
    binky.emplace_back(glm::ivec3(0, 3, 0), woodBlockId);
    binky.emplace_back(glm::ivec3(0, 4, 0), woodBlockId);
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

  [[maybe_unused]] auto& crafting = registry_.ctx().insert_or_assign<Crafting>({});
  
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

  auto& loot        = registry_.ctx().insert_or_assign<LootRegistry>({});
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
  registry_.ctx().insert_or_assign(Voxel::Grid(numChunks));
  CreateRenderingMaterials();
}

void World::CreateRenderingMaterials()
{
  auto voxelMats = std::vector<Voxel::Grid::Material>();
  const auto& blocks = registry_.ctx().get<Block::Registry>();
  const auto& blockMap = blocks.GetIdToTagMap();
  for (const auto& [id, tag] : blockMap)
  {
    voxelMats.emplace_back(Voxel::Grid::Material{
      .isVisible = Block::IsVisible(*this, id),
      .isSolid   = Block::IsSolid(*this, id),
      .subGrid   = Block::GetSubGrid(*this, id),
    });
  }
  registry_.ctx().get<Voxel::Grid>().SetMaterialArray(std::move(voxelMats));

  auto* head = registry_.ctx().get<Head*>();
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
      .shape      = Physics::Plane{{0, 1, 0}, 0},
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

namespace
{
  float TexelFetch3D(const auto& image, int imageSize, glm::ivec3 p)
  {
    p = glm::clamp(p, glm::ivec3(0), glm::ivec3(imageSize - 1));
    return image[p.x + p.y * imageSize + p.z * imageSize * imageSize];
  }

  void ImageStore3D(auto& image, int imageSize, glm::ivec3 p, float value)
  {
    DEBUG_ASSERT(glm::all(glm::greaterThanEqual(p, glm::ivec3(0))) && glm::all(glm::lessThan(p, glm::ivec3(imageSize))));
    image[p.x + p.y * imageSize + p.z * imageSize * imageSize] = value;
  }

  float TexelFetch2D(const auto& image, int imageSize, glm::ivec2 p)
  {
    p = glm::clamp(p, glm::ivec2(0), glm::ivec2(imageSize - 1));
    return image[p.x + p.y * imageSize];
  }

  void ImageStore2D(auto& image, int imageSize, glm::ivec2 p, float value)
  {
    DEBUG_ASSERT(glm::all(glm::greaterThanEqual(p, glm::ivec2(0))) && glm::all(glm::lessThan(p, glm::ivec2(imageSize))));
    image[p.x + p.y * imageSize] = value;
  }

  enum class Filter
  {
    Nearest,
    Linear,
  };

  auto SampleImage3D(const auto& image, int imageSize, glm::vec3 uv, Filter filter = Filter::Linear)
  {
    const auto unnormalized = uv * (float)imageSize;

    if (filter == Filter::Nearest)
    {
      return TexelFetch3D(image, imageSize, glm::ivec3(unnormalized));
    }

    const auto intCoord = glm::ivec3(unnormalized);

    const auto bln = TexelFetch3D(image, imageSize, intCoord + glm::ivec3(0, 0, 0));
    const auto brn = TexelFetch3D(image, imageSize, intCoord + glm::ivec3(1, 0, 0));
    const auto tln = TexelFetch3D(image, imageSize, intCoord + glm::ivec3(0, 1, 0));
    const auto trn = TexelFetch3D(image, imageSize, intCoord + glm::ivec3(1, 1, 0));
    const auto blf = TexelFetch3D(image, imageSize, intCoord + glm::ivec3(0, 0, 1));
    const auto brf = TexelFetch3D(image, imageSize, intCoord + glm::ivec3(1, 0, 1));
    const auto tlf = TexelFetch3D(image, imageSize, intCoord + glm::ivec3(0, 1, 1));
    const auto trf = TexelFetch3D(image, imageSize, intCoord + glm::ivec3(1, 1, 1));

    const auto weight = unnormalized - glm::vec3(intCoord);
    const auto n = glm::mix(glm::mix(bln, brn, weight.x), glm::mix(tln, trn, weight.x), weight.y);
    const auto f = glm::mix(glm::mix(blf, brf, weight.x), glm::mix(tlf, trf, weight.x), weight.y);
    return glm::mix(n, f, weight.z);
  };

  auto SampleImage2D(const auto& image, int imageSize, glm::vec2 uv, Filter filter = Filter::Linear)
  {
    const auto unnormalized = uv * (float)imageSize;

    if (filter == Filter::Nearest)
    {
      return TexelFetch2D(image, imageSize, glm::ivec2(unnormalized));
    }

    const auto intCoord = glm::ivec2(unnormalized);

    const auto bl = TexelFetch2D(image, imageSize, intCoord + glm::ivec2(0, 0));
    const auto br = TexelFetch2D(image, imageSize, intCoord + glm::ivec2(1, 0));
    const auto tl = TexelFetch2D(image, imageSize, intCoord + glm::ivec2(0, 1));
    const auto tr = TexelFetch2D(image, imageSize, intCoord + glm::ivec2(1, 1));

    const auto weight = unnormalized - glm::vec2(intCoord);
    return glm::mix(glm::mix(bl, br, weight.x), glm::mix(tl, tr, weight.x), weight.y);
  }

  void ForEachPositionInTLBrick(glm::ivec3 topLevelBrickPos, const auto& function)
  {
    for (int c = 0; c < Voxel::Grid::TL_BRICK_SIDE_LENGTH; c++)
    {
      for (int b = 0; b < Voxel::Grid::TL_BRICK_SIDE_LENGTH; b++)
      {
        for (int a = 0; a < Voxel::Grid::TL_BRICK_SIDE_LENGTH; a++)
        {
          const auto bl = glm::ivec3{a, b, c};

          // Voxels
          for (int z = 0; z < Voxel::Grid::BL_BRICK_SIDE_LENGTH; z++)
          {
            for (int y = 0; y < Voxel::Grid::BL_BRICK_SIDE_LENGTH; y++)
            {
              for (int x = 0; x < Voxel::Grid::BL_BRICK_SIDE_LENGTH; x++)
              {
                const auto positionBLS = glm::ivec3{x, y, z};
                const auto positionWS  = topLevelBrickPos * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE + bl * Voxel::Grid::BL_BRICK_SIDE_LENGTH + positionBLS;
                function(positionWS);
              }
            }
          }
        }
      }
    }
  }

  // Generate inSideLength^3 chunk of noise, then upscale it to outSideLength^3 with Filter.
  // Note: this upscaling is actually considerably less efficient than simply generating the equivalent volume of noise,
  // except in the case of very complex noise graphs.
  std::unique_ptr<float[]> GenerateAndUpscale3D(const FastNoise::SmartNode<>& node, glm::ivec3 start, int seed, int inSideLength, int outSideLength, Filter filter)
  {
    ZoneScoped;
    const int genCount = (inSideLength == outSideLength) ? inSideLength * inSideLength * inSideLength : (inSideLength + 1) * (inSideLength + 1) * (inSideLength + 1);
    auto raw = std::make_unique_for_overwrite<float[]>(genCount);

    {
      ZoneScopedN("GenUniformGrid3D");
      const int sideCount = (inSideLength == outSideLength) ? inSideLength : inSideLength + 1;
      node->GenUniformGrid3D(raw.get(), start.x, start.y, start.z, sideCount, sideCount, sideCount, seed);
    }

    if (inSideLength == outSideLength)
    {
      return raw;
    }

    {
      ZoneScopedN("Upscale 3D");
      auto out = std::make_unique_for_overwrite<float[]>(outSideLength * outSideLength * outSideLength);
      int i    = 0;
      for (int z = 0; z < outSideLength; z++)
      for (int y = 0; y < outSideLength; y++)
      for (int x = 0; x < outSideLength; x++)
      {
        const auto uv = (glm::vec3(x, y, z) + 0.5f) / (outSideLength + (float(outSideLength) / inSideLength));
        out[i++]      = SampleImage3D(raw, inSideLength + 1, uv, filter);
      }

      return out;
    }
  }

  std::unique_ptr<float[]> GenerateAndUpscale2D(const FastNoise::SmartNode<>& node, glm::ivec2 start, int seed, int inSideLength, int outSideLength, Filter filter)
  {
    ZoneScoped;
    const int genCount = (inSideLength == outSideLength) ? inSideLength * inSideLength : (inSideLength + 1) * (inSideLength + 1);
    auto raw = std::make_unique_for_overwrite<float[]>(genCount);

    {
      ZoneScopedN("GenUniformGrid2D");
      const int sideCount = (inSideLength == outSideLength) ? inSideLength : inSideLength + 1;
      node->GenUniformGrid2D(raw.get(), start.x, start.y, sideCount, sideCount, seed);
    }

    if (inSideLength == outSideLength)
    {
      return raw;
    }

    {
      ZoneScopedN("Upscale 2D");
      auto out = std::make_unique_for_overwrite<float[]>(outSideLength * outSideLength);
      int i    = 0;
      for (int y = 0; y < outSideLength; y++)
      for (int x = 0; x < outSideLength; x++)
      {
        const auto uv = (glm::vec2(x, y) + 0.5f) / (outSideLength + (float(outSideLength) / inSideLength));
        out[i++]      = SampleImage2D(raw, inSideLength + 1, uv, filter);
      }

      return out;
    }
  }
} // namespace

void World::GenerateMap(const MapGenInfo& mapGenInfo)
{
  ZoneScoped;
#ifndef GAME_HEADLESS
  auto& progressText = registry_.ctx().get<std::atomic<const char*>>("progressText"_hs);
  auto& progress     = registry_.ctx().get<std::atomic_int32_t>("progress"_hs);
  auto& total        = registry_.ctx().get<std::atomic_int32_t>("total"_hs);
#endif
  auto& blocks          = registry_.ctx().get<Block::Registry>();
  const auto& grass     = blocks.Get("grass");
  const auto& dirt     = blocks.Get("dirt");
  const auto& malachite = blocks.Get("malachite");
  const auto& galena = blocks.Get("galena");

  constexpr auto samplesPerAxis = 64;
  constexpr auto sampleScale    = (float)samplesPerAxis / Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;

  auto& grid = registry_.ctx().get<Voxel::Grid>();

  auto tlBrickColCoords = std::vector<glm::ivec2>();
  for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
  {
    for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
    {
      tlBrickColCoords.emplace_back(k, i);
    }
  }

  auto globalSurfaceHeightImage = std::vector<float>(grid.topLevelBricksDims_.x * grid.topLevelBricksDims_.z * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE);

  // Used to determine where meadows are. These are flatter areas with fewer trees.
  auto globalMeadowImage = std::vector<float>(grid.topLevelBricksDims_.x * grid.topLevelBricksDims_.z * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE);

  auto whiteNoise = FastNoise::New<FastNoise::White>();
  whiteNoise->SetOutputMin(0);
  auto whiteNoise2 = FastNoise::New<FastNoise::White>();

  auto valueNoise = FastNoise::New<FastNoise::Value>();
  valueNoise->SetOutputMin(0);
  valueNoise->SetScale(10);

  auto shrimplex = FastNoise::New<FastNoise::Simplex>();
  shrimplex->SetScale(25);
  shrimplex->SetOutputMin(0);

  auto shrimplex2 = FastNoise::New<FastNoise::Simplex>();
  shrimplex2->SetScale(8);
  shrimplex2->SetOutputMin(0);

  {
    ZoneScopedN("Surface");
    auto terrainHeight2Da = FastNoise::NewFromEncodedNodeTree("HAUNBQY@ACWQv//Aw8FFwUI/wI@ADA////");
    auto terrainHeight2D  = FastNoise::New<FastNoise::DomainScale>();
    terrainHeight2D->SetSource(terrainHeight2Da);
    terrainHeight2D->SetScaling(1.0f / sampleScale);

    auto meadowNoise = FastNoise::NewFromEncodedNodeTree("GgUbBRwFHQUXBRgDFgMdBRYCAACAPwcfAwsAAIDHQgQ@CGAQ@BHC@BKJBBB+F6z7//wbsUTg///8DAACamVk///8H/wQA/wcWAgAAgD8H/wQA//8CrkchQP//AgAAgD8GXI/CPv//AgAAgD//");

    auto stoneInDirtA = FastNoise::NewFromEncodedNodeTree("GgUL@BIEEEAACAPwg@CDAM@AD/AwY@BgQQTNzEy+C@AoED//w==");
    auto stoneInDirt  = FastNoise::New<FastNoise::DomainScale>();
    stoneInDirt->SetSource(stoneInDirtA);
    stoneInDirt->SetScaling(1.0f / sampleScale);

    auto copperOre = FastNoise::NewFromEncodedNodeTree("FgLNzIw/BxUFBg@AFRBCK5HoT//Aws@BIQQgzM7M/D@CD///8=");
    auto leadOre = FastNoise::NewFromEncodedNodeTree("FgIAAIA/BxUFBg@BhBCK5HoT//AxAFCw@AFxBCJqZmT8M@CP8CAACAQf///w==");

#ifndef GAME_HEADLESS
    total.store((int32_t)grid.numTopLevelBricks_);
    progressText.store("Surface");
#endif

    // Column of top level bricks
    std::for_each(std::execution::par,
      tlBrickColCoords.begin(),
      tlBrickColCoords.end(),
      [&](glm::ivec2 tlBrickColCoord)
      {
        ZoneScopedN("Top level brick column");
        const int k = tlBrickColCoord[0];
        const int i = tlBrickColCoord[1];

        auto terrainHeightImage = GenerateAndUpscale2D(terrainHeight2D,
          glm::ivec2(sampleScale * (glm::vec2(i, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
          mapGenInfo.seed,
          samplesPerAxis,
          Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
          Filter::Linear);

        auto meadowImage = GenerateAndUpscale2D(meadowNoise,
          glm::ivec2((glm::vec2(i, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
          mapGenInfo.seed * 21,
          Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
          Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
          Filter::Nearest);

        for (int y = 0; y < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; y++)
        for (int x = 0; x < Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE; x++)
        {
          const auto pModTl = glm::ivec2(x, y);
          const auto positionWS = pModTl + glm::ivec2(i, k) * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
          
          const auto meadowness = TexelFetch2D(meadowImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl);
          //float meadowness       = 0.1f;
          const auto heightScale = glm::mix(15, 4,  meadowness);
          const auto height = glm::floor(mapGenInfo.seaLevel + heightScale * TexelFetch2D(terrainHeightImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl));
          ImageStore2D(terrainHeightImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl, height);
          ImageStore2D(globalSurfaceHeightImage, grid.topLevelBricksDims_.x * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, {positionWS.x, positionWS.y}, height);
          ImageStore2D(globalMeadowImage, grid.topLevelBricksDims_.x * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, {positionWS.x, positionWS.y}, meadowness);
        }

        // Top level bricks
        for (int j = 0; j < grid.topLevelBricksDims_.y; j++) // Y last so we can compute heightmap once
        {
          ZoneScopedN("Top level brick");

          auto stoneInDirtImage = GenerateAndUpscale3D(stoneInDirt,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 1,
            samplesPerAxis,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Linear);

          auto fadeImage = GenerateAndUpscale3D(whiteNoise,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 2,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          auto copperImage = GenerateAndUpscale3D(copperOre,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 55,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          auto leadImage = GenerateAndUpscale3D(leadOre,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 56,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          const auto tl = glm::ivec3{i, j, k};

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto pModTl = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
              
              const auto height = TexelFetch2D(terrainHeightImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, {pModTl.x, pModTl.z});

              auto blockTypeToSet = voxel_t::Air;
              if (positionWS.y < height)
              {
                // 0 at sea level. 1 at cavern level.
                const auto alphaCaverns = glm::clamp((mapGenInfo.seaLevel - positionWS.y) / float(mapGenInfo.surfaceThickness), 0.0f, 1.0f);

                if (positionWS.y == height - 1)
                {
                  blockTypeToSet = grass;
                }
                // Surface and underground biomes' substrate is dirt
                else if (positionWS.y >= mapGenInfo.seaLevel - mapGenInfo.surfaceThickness)
                {
                  blockTypeToSet = dirt;

                  // Add stone blobs with increasing size as they get closer to caverns.
                  if (TexelFetch3D(stoneInDirtImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < glm::mix(0.0f, 0.1f, alphaCaverns))
                  {
                    blockTypeToSet = voxel_t(1);
                  }
                  // Dithered fade from dirt to stone, beginning 1/3 from the underground-cavern transition point.
                  else if (TexelFetch3D(fadeImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < alphaCaverns * 3 - 2)
                  {
                    blockTypeToSet = voxel_t(1);
                  }
                }
                // Cavern biome substrate is stone
                else
                {
                  blockTypeToSet = voxel_t(1);
                }
              }

              if (blockTypeToSet != voxel_t::Air && blockTypeToSet != grass)
              {
                if (TexelFetch3D(copperImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < 0.0f)
                {
                  blockTypeToSet = malachite;
                }
                else if (TexelFetch3D(leadImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < 0.0f)
                {
                  blockTypeToSet = galena;
                }
              }

              if (blockTypeToSet != voxel_t::Air)
              {
                grid.SetVoxelAtNoDirty(positionWS, blockTypeToSet);
              }
            });

          grid.MarkTopLevelBrickAndChildrenDirty(tl);
          grid.CoalesceTopLevelBrickAndChildren(grid.GetTopLevelBrickPointerFromTopLevelPosition(tl));
#ifndef GAME_HEADLESS
          progress.fetch_add(1);
#endif
        }
      });
  }

  {
#ifndef GAME_HEADLESS
    progressText.store("Caves");
    progress.store(0);
#endif

    auto surfaceCavesA = FastNoise::NewFromEncodedNodeTree("HAUNBQY@ABSQgg@B///8DDwUXBQgAAIDIQv8C@BwP///w==");
    auto surfaceCaves = FastNoise::New<FastNoise::DomainScale>();
    surfaceCaves->SetSource(surfaceCavesA);
    surfaceCaves->SetScaling(1.5f / sampleScale);

    std::for_each(std::execution::par,
      tlBrickColCoords.begin(),
      tlBrickColCoords.end(),
      [&](glm::ivec2 tlBrickColCoord)
      {
        ZoneScopedN("Top level brick column");
        const int k = tlBrickColCoord[0];
        const int i = tlBrickColCoord[1];

        // Top level bricks
        for (int j = 0; j < grid.topLevelBricksDims_.y; j++) // Y last so we can compute heightmap once
        {
          ZoneScopedN("Top level brick");
          auto densities = GenerateAndUpscale3D(surfaceCaves,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed,
            samplesPerAxis,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Linear);

          const auto tl = glm::ivec3{i, j, k};
          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              if (grid.GetVoxelAtUnchecked(positionWS) != voxel_t::Air)
              {
                const auto pModTl = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
                
                const auto density = TexelFetch3D(densities, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl);
                if (density >= 0.0f)
                {
                  grid.SetVoxelAtNoDirty(positionWS, voxel_t::Air);
                }
              }
            });

          grid.MarkTopLevelBrickAndChildrenDirty(tl);
          grid.CoalesceTopLevelBrickAndChildren(grid.GetTopLevelBrickPointerFromTopLevelPosition(tl));
#ifndef GAME_HEADLESS
          progress.fetch_add(1);
#endif
        }
      });
  }

#ifndef GAME_HEADLESS
  progressText.store("Surface foliage");
  total.store(grid.dimensions_.x * grid.dimensions_.z);
  progress.store(0);
#endif

  for (int z = 0; z < grid.dimensions_.z; z++)
  for (int x = 0; x < grid.dimensions_.x; x++)
  {
    const auto y = (int)TexelFetch2D(globalSurfaceHeightImage, grid.dimensions_.x, {x, z});
    const auto meadowness = TexelFetch2D(globalMeadowImage, grid.dimensions_.x, {x, z});

    const bool hasSolidFloor = grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air;
    const auto tree = whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 4);
    if (hasSolidFloor && tree > glm::mix(0.99f, 0.998f, meadowness))
    {
      if (registry_.ctx().get<PCG::Rng>().RandFloat() < 0.9f)
      {
        registry_.ctx().get<PrefabRegistry>().Get("Tree").Instantiate(*this, {x, y, z});
      }
      else
      {
        registry_.ctx().get<PrefabRegistry>().Get("Tree2").Instantiate(*this, {x, y, z});
      }
    }
    else
    {
      if (hasSolidFloor &&
          shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 5) + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 9) * 0.2f < 0.03f)
      {
        grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("bush_01"));
      }

      if (hasSolidFloor &&
          shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 31) * 0.7f + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 30) * 0.3f > 0.88f)
      {
        grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("bush_02"));
      }

      if (hasSolidFloor && shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 16) * 0.7f +
            whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 17) * 0.3f >
          0.93f)
      {
        grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("mushroom"));
      }
      else if (hasSolidFloor && meadowness * shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 24) * 0.7f +
                 whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 25) * 0.3f >
               0.95f)
      {
        grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("rose"));
      }
      else if (hasSolidFloor && meadowness * shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 22) * 0.7f +
                                    whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 23) * 0.3f >
               0.93f)
      {
        grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("dandelion"));
      }
      else if (hasSolidFloor && whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 24) > 0.999f)
      {
        grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("rock_small"));
      }
      else // Because it's low priority, grass shouldn't override other foliage.
      {
        const auto grasss = meadowness * 0.1f + shrimplex2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 10) +
                            whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 11) * 0.3f;

        if (hasSolidFloor)
        {
          if (grasss > 0.6f)
          {
            grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_short"));
          }
          if (grasss > 0.7f)
          {
            grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_medium"));
          }
          if (grasss > 0.8f)
          {
            grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_long"));
          }
          if (grasss > 0.9f)
          {
            registry_.ctx().get<PrefabRegistry>().Get("Double Grass").Instantiate(*this, {x, y, z});
          }
        }
      }
    }

    progress.fetch_add(1);
  }

#ifndef GAME_HEADLESS
  progressText.store("Vines");
  total.store(grid.topLevelBricksDims_.x * grid.topLevelBricksDims_.y * grid.topLevelBricksDims_.z);
  progress.store(0);
#endif

  struct PrefabAndPosition
  {
    const PrefabDefinition* prefab;
    glm::ivec3 positionWS;
  };

  auto prefabs = std::vector<PrefabAndPosition>();
  prefabs.reserve(100'000);
  auto mutex = std::mutex();
  
  {
    ZoneScopedN("Generate vine positions");
    std::for_each(std::execution::par,
      tlBrickColCoords.begin(),
      tlBrickColCoords.end(),
      [&](glm::ivec2 tlBrickColCoord)
      {
        ZoneScopedN("Top level brick column");
        const int k = tlBrickColCoord[0];
        const int i = tlBrickColCoord[1];

        for (int j = 0; j < grid.topLevelBricksDims_.y; j++)
        {
          ZoneScopedN("Top level brick");

          const auto tl = glm::ivec3{i, j, k};

          auto simplexImage = GenerateAndUpscale3D(shrimplex2,
            tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 15,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          auto whiteImage = GenerateAndUpscale3D(whiteNoise2,
            tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 16,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto tlLocal = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
              if (grid.GetVoxelAtUnchecked(positionWS) == voxel_t::Air)
              {
                const auto aboveWS = positionWS + glm::ivec3(0, 1, 0);
                const auto belowWS = positionWS + glm::ivec3(0, -1, 0);
                if (aboveWS.y < grid.dimensions_.y - 1)
                {
                  const auto aboveBlock = grid.GetVoxelAtUnchecked(aboveWS);
                  if (aboveBlock != voxel_t::Air)
                  {
                    if (TexelFetch3D(simplexImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, tlLocal) +
                          TexelFetch3D(whiteImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, tlLocal) * 0.3f <
                        0.05f)
                    {
                      auto lk = std::unique_lock(mutex);
                      if (aboveBlock == dirt)
                      {
                        prefabs.emplace_back(&registry_.ctx().get<PrefabRegistry>().Get("Root"), positionWS);
                      }
                      else
                      {
                        prefabs.emplace_back(&registry_.ctx().get<PrefabRegistry>().Get("Vine"), positionWS);
                      }
                    }
                  }
                }

                if (belowWS.y > 0)
                {
                  const auto belowBlock = grid.GetVoxelAtUnchecked(belowWS);
                  if (belowBlock == dirt && TexelFetch3D(whiteImage, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, tlLocal) > 0.98f)
                  {
                    grid.SetVoxelAtNoDirty(positionWS, blocks.Get("pot"));
                  }
                }
              }
            });

          progress.fetch_add(1);
          grid.MarkTopLevelBrickAndChildrenDirty(tl);
          grid.CoalesceTopLevelBrickAndChildren(grid.GetTopLevelBrickPointerFromTopLevelPosition(tl));
        }
      });
  }

  {
#ifndef GAME_HEADLESS
    progressText.store("Instantiate prefabs");
    total.store(int(prefabs.size()));
    progress.store(0);
#endif

    ZoneScopedN("Instantiate Prefabs");
    for (const auto& [prefab, positionWS] : prefabs)
    {
      prefab->Instantiate(*this, positionWS);
      progress.fetch_add(1);
    }
  }

  if (mapGenInfo.spawnYggdrasil)
  {
    ZoneScopedN("Big Tree");
    auto bigTreeNoise = FastNoise::NewFromEncodedNodeTree(
      "FgMVBRoFGwUVBRcFBQMs@EFBgAAgAVDB@AoMAIAACgQP//BwQEAACAP/8LLAAC@BBSUF/wAA////AygJAABvEgM8/wAApptEPP8DLAAC@BBSw@EUlAAI@BFBgQ@C/////wY@C//8CAACAv/8CAACAv/8CAACAP/8DFwUbBQQEzcxMvQYAACDC//8DFgMbBQQEzczMvP8DFQUXBRsFGgUVBQQEj8L1PP8DJQAD@BBSwFBg@APZBB@DI@BP/////8CAACAP///AxUFGQU@CgL///wIK1yM8//8DFQUXBQUHBAQAAIA///8CbxKDOv8D/xsA////Bw8FE@BMZCBQY@ADJQv8CmpnKQv//////BxYCAACAPwcaBRsFHAUdBRUFE@AgDBDBSUABg@BUGAACAg0L//wMXBRUFFwUFBnuUFkP/AlJJnTn/AvYokkL/AmZmpkD//wP/LgD/AgrXo7z/AgAAgD//AgrXo7z/AgAAgD////8=");

    auto& rng          = registry_.ctx().get<PCG::Rng>();
    const auto fractXZ = glm::vec2(rng.RandFloat(0.4f, 0.6f), rng.RandFloat(0.4f, 0.6f));
    const auto posXZ   = glm::ivec2(fractXZ * glm::vec2(grid.dimensions_.x, grid.dimensions_.z) + 0.5f);
    const auto posY    = TexelFetch2D(globalSurfaceHeightImage, grid.dimensions_.x, posXZ);
    const auto pos     = glm::ivec3(posXZ[0], posY, posXZ[1]);
    const auto bot     = glm::ivec3(glm::vec3(pos / Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE) + 0.5f);
    const auto top     = bot + 1 + glm::ivec3(0, 1, 0);

#ifndef GAME_HEADLESS
    progressText.store("Yggdrasil");
    const auto dif = top - bot + 1;
    total.store(dif.x * dif.y * dif.z);
    progress.store(0);
#endif

    for (int tz = bot.z; tz <= top.z; tz++)
    for (int ty = bot.y; ty <= top.y; ty++)
    for (int tx = bot.x; tx <= top.x; tx++)
    {
      const auto tl    = glm::ivec3(tx, ty, tz);
      const auto image = GenerateAndUpscale3D(bigTreeNoise,
        tl * Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE - pos,
        mapGenInfo.seed,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE,
        Filter::Nearest);

      ForEachPositionInTLBrick(tl,
        [&](glm::ivec3 positionWS)
        {
          const auto pModTl = positionWS % Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE;
          const auto density = TexelFetch3D(image, Voxel::Grid::TL_BRICK_VOXELS_PER_SIDE, pModTl);
          // Low-altitude behavior
          if (positionWS.y < mapGenInfo.seaLevel + 80)
          {
            if (density <= 0)
            {
              grid.SetVoxelAt(positionWS, blocks.Get("wood"));
            }
          }
          else
          {
            if (density <= -0.032f)
            {
              grid.SetVoxelAt(positionWS, blocks.Get("wood"));
            }
            else if (density <= 0.0f)
            {
              grid.SetVoxelAt(positionWS, blocks.Get("leaves_01"));
            }
          }
        });

      progress.fetch_add(1);
    }
  }

  {
    ZoneScopedN("Ruins");
    constexpr int DUNGEON_CELL_SIZE = 16; // One attempt per cell.
#ifndef GAME_HEADLESS
    progressText.store("Ruins");
    total.store((grid.dimensions_.x / DUNGEON_CELL_SIZE) * (grid.dimensions_.y / DUNGEON_CELL_SIZE) * (grid.dimensions_.z / DUNGEON_CELL_SIZE));
    progress.store(0);
#endif

    auto rng = PCG::Rng(mapGenInfo.seed);
    for (int zt = 0; zt < grid.dimensions_.z / DUNGEON_CELL_SIZE; zt++)
    for (int yt = 0; yt < grid.dimensions_.y / DUNGEON_CELL_SIZE; yt++)
    for (int xt = 0; xt < grid.dimensions_.x / DUNGEON_CELL_SIZE; xt++)
    {
      progress.fetch_add(1);
      if (rng.RandFloat() < 0.15f)
      {
        const auto posCell = glm::ivec3(xt, yt, zt);

        // Spawn prefab somewhere within the cell.
        for (int attempt = 0; attempt < 10; attempt++)
        {
          const auto posSub = glm::ivec3(rng.RandU32() % DUNGEON_CELL_SIZE, rng.RandU32() % DUNGEON_CELL_SIZE, rng.RandU32() % DUNGEON_CELL_SIZE);
          const auto posWS  = posCell * DUNGEON_CELL_SIZE + posSub;

          const auto surfaceHeight = int(TexelFetch2D(globalSurfaceHeightImage, grid.dimensions_.x, {posWS.x, posWS.z}));
          if (posWS.y <= surfaceHeight - 8 && posWS.y >= mapGenInfo.seaLevel - mapGenInfo.surfaceThickness)
          {
            registry_.ctx().get<PrefabRegistry>().Get("AbandonedHouse").Instantiate(*this, posWS);
            break;
          }
        }
      }
    }
  }
  
  {
    ZoneScopedN("Floating Islands");
    constexpr int ISLAND_CELL_SIZE = 64; // One attempt per cell.
#ifndef GAME_HEADLESS
    progressText.store("Floating islands");
    total.store((grid.dimensions_.x / ISLAND_CELL_SIZE) * (grid.dimensions_.y / ISLAND_CELL_SIZE) * (grid.dimensions_.z / ISLAND_CELL_SIZE));
    progress.store(0);
#endif

    auto rng = PCG::Rng(mapGenInfo.seed);
    for (int zt = 0; zt < grid.dimensions_.z / ISLAND_CELL_SIZE; zt++)
    for (int yt = 0; yt < grid.dimensions_.y / ISLAND_CELL_SIZE; yt++)
    for (int xt = 0; xt < grid.dimensions_.x / ISLAND_CELL_SIZE; xt++)
    {
      progress.fetch_add(1);
      if (rng.RandFloat() < 0.1f)
      {
        const auto posCell = glm::ivec3(xt, yt, zt);

        for (int attempt = 0; attempt < 10; attempt++)
        {
          // Spawn prefab somewhere within the cell.
          const auto posSub = glm::ivec3(rng.RandU32() % ISLAND_CELL_SIZE, rng.RandU32() % ISLAND_CELL_SIZE, rng.RandU32() % ISLAND_CELL_SIZE);
          const auto posWS  = posCell * ISLAND_CELL_SIZE + posSub;
          const auto posFraction = glm::vec3(posWS) / glm::vec3(grid.dimensions_);

          const auto surfaceHeight = int(TexelFetch2D(globalSurfaceHeightImage, grid.dimensions_.x, {posWS.x, posWS.z}));
          if (posWS.y >= surfaceHeight + 125 && posFraction.x < 0.9f && posFraction.y < 0.9f && posFraction.z < 0.9f)
          {
            registry_.ctx().get<PrefabRegistry>().Get("FloatingIsland").Instantiate(*this, posWS);
            break;
          }
        }
      }
    }
  }

  grid.CoalesceDirtyBricks();
}