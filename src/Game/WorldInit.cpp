#include "Assets.h"
#include "Core/Assert2.h"
#include "Item.h"
#include "Physics/Physics.h"
#include "Prefab.h"
#include "VoxLoader.h"
#include "World.h"

#include "FastNoise/FastNoise.h"
#include "tracy/Tracy.hpp"

#include <execution>

class DungeonPrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  void Instantiate(World& world, glm::ivec3 worldPos) const override
  {
    const auto& blocks = world.GetRegistry().ctx().get<BlockRegistry>();
    const auto& items  = world.GetRegistry().ctx().get<ItemRegistry>();
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
            wood.OnTryPlaceBlock(world, blockPos);
          }
          else if (x == 0 && y == -ds + 1 && z == 0)
          {
            if (chest.OnTryPlaceBlock(world, blockPos))
            {
              auto chestEntity = world.GetBlockEntity(blockPos);
              ASSERT(chestEntity != entt::null);
              if (auto [e, i] = world.GetComponentFromDescendant<Inventory>(chestEntity); i)
              {
                i->slots[0][0] = {.id = items.GetId("Electrum"), .count = 10};
                i->slots[0][1] = {.id = items.GetId("Suspicious Coin"), .count = 1};
              }
            }
          }
          else if (x == 0 && y == ds - 1 && z == 0)
          {
            light.OnTryPlaceBlock(world, blockPos);
          }
          else
          {
            air.OnTryPlaceBlock(world, blockPos);
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
    const auto& blocks    = world.GetRegistry().ctx().get<BlockRegistry>();
    const auto& vinesMain = blocks.Get("vines_main");
    const auto& vinesEnd  = blocks.Get("vines_end");

    // Count number of air blocks starting from worldPos.
    auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
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

    grid.SetVoxelAt(worldPos + glm::ivec3{0, -(freeRealEstate - 1), 0}, vinesEnd.GetBlockId());

    for (int y = 0; y > -(freeRealEstate - 1); y--)
    {
      grid.SetVoxelAt(worldPos + glm::ivec3{0, y, 0}, vinesMain.GetBlockId());
    }
  }
};

class RootPrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  void Instantiate(World& world, glm::ivec3 worldPos) const override
  {
    const auto& blocks    = world.GetRegistry().ctx().get<BlockRegistry>();
    const auto& rootsMain = blocks.Get("roots_main");
    const auto& rootsEnd  = blocks.Get("roots_end");

    // Count number of air blocks starting from worldPos.
    auto& grid         = world.GetRegistry().ctx().get<TwoLevelGrid>();
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

    grid.SetVoxelAt(worldPos + glm::ivec3{0, -(freeRealEstate - 1), 0}, rootsEnd.GetBlockId());

    for (int y = 0; y > -(freeRealEstate - 1); y--)
    {
      grid.SetVoxelAt(worldPos + glm::ivec3{0, y, 0}, rootsMain.GetBlockId());
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
    auto& grid         = world.GetRegistry().ctx().get<TwoLevelGrid>();
    const auto& blocks = world.GetRegistry().ctx().get<BlockRegistry>();
    const auto& items  = world.GetRegistry().ctx().get<ItemRegistry>();
    const auto& air    = blocks.Get("Air");
    const auto& wood   = blocks.Get("Wood Plank");
    
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
      wood.OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, 0, zl));
      const auto& block = xl == 0 || xl == roomWidth - 1 || zl == 0 || zl == roomLength - 1 ? wood : air;
      for (uint32_t yl = 1; yl < rng.RandU32(block.GetBlockId() == air.GetBlockId() ? 2 : 1, ROOM_HEIGHT); yl++)
      {
        // Fill edges with wood, interior with air.
        block.OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, yl, zl));
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
            wood.OnTryPlaceBlock(world, pos);
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
      blocks.Get("table").OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, 1, zl));
    }
    
    if (rng.RandFloat() < 0.75f)
    {
      const auto xl = rng.RandU32(1, roomWidth - 1);
      const auto zl = rng.RandU32(1, roomLength - 1);
      blocks.Get("chair").OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, 1, zl));
    }

    for (int i = 0; i < 2; i++)
    {
      if (rng.RandFloat() < 0.75f)
      {
        const auto xl = rng.RandU32(1, roomWidth - 1);
        const auto zl = rng.RandU32(1, roomLength - 1);
        blocks.Get("pot").OnTryPlaceBlock(world, worldPos + glm::ivec3(xl, 1, zl));
      }
    }

    if (rng.RandFloat() < 0.65f)
    {
      const auto xl = rng.RandU32(1, roomWidth - 1);
      const auto zl = rng.RandU32(1, roomLength - 1);
      const auto blockPos = worldPos + glm::ivec3(xl, 1, zl);
      if (blocks.Get("Cheste").OnTryPlaceBlock(world, blockPos))
      {
        auto chestEntity = world.GetBlockEntity(blockPos);
        ASSERT(chestEntity != entt::null);
        if (auto [e, i] = world.GetComponentFromDescendant<Inventory>(chestEntity); i)
        {
          i->slots[0][0] = {.id = items.GetId("Electrum"), .count = int(rng.RandU32(1, 20))};
          i->slots[0][1] = {.id = items.GetId("Suspicious Coin"), .count = 1};
        }
      }
    }
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
  auto& items                       = registry_.ctx().insert_or_assign<ItemRegistry>({});
  [[maybe_unused]] const auto gunId = items.Add(new Gun("M4", {}));

  [[maybe_unused]] const auto gun2Id = items.Add(new Gun("Frogun",
    {
      .model       = "frog",
      .scale       = 0.125f,
      .damage      = 10,
      .knockback   = 2,
      .fireRateRpm = 80,
      .bullets     = 9,
      .velocity    = 50,
      .accuracyMoa = 300,
      .vrecoil     = 10,
      .vrecoilDev  = 3,
      .hrecoil     = 1,
      .hrecoilDev  = 1,
    }));

  auto light      = GpuLight();
  light.color     = {1.0f, 0.4f, 0.2f};
  light.intensity = 500;
  light.type      = LIGHT_TYPE_POINT;
  light.range     = 200;

  [[maybe_unused]] const auto flareGunId = items.Add(new Gun("Flare Gun",
    {
      .model       = "ar15",
      .tint        = {1, 0.4f, 0.22f},
      .scale       = 1,
      .damage      = 10,
      .knockback   = 1,
      .fireRateRpm = 90,
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
    }));

  [[maybe_unused]] const auto coinItemId          = items.Add(new SpriteItem("Electrum", "coin"));
  [[maybe_unused]] const auto charcoalItemId      = items.Add(new SpriteItem("Charcoal", "charcoal"));
  [[maybe_unused]] const auto copperIngotItemId   = items.Add(new SpriteItem("Copper Ingot", "copper_ingot"));
  [[maybe_unused]] const auto stickItemId         = items.Add(new SpriteItem("Stick", "stick"));
  [[maybe_unused]] const auto coolStickItemId     = items.Add(new SpriteItem("Cool Stick", "stick", {1, 0, 0}));
  [[maybe_unused]] const auto wormSummonItemId    = items.Add(new SpawnBossItemDefinition("Suspicious Coin", "coin", {1, 0.1f, 0.1f}));
  [[maybe_unused]] const auto healingPotionItemId = items.Add(new HealingPotionDefinition("Healing Potion", "potion_healing"));
  [[maybe_unused]] const auto stoneAxeId          = items.Add(new ToolDefinition("Stone Axe", {"axe", {.2f, .2f, .2f}, 20, 2, BlockDamageFlagBit::AXE}));
  [[maybe_unused]] const auto copperAxeId         = items.Add(new ToolDefinition("Copper Axe", {"axe", {.78f, .51f, .27f}, 30, 2, BlockDamageFlagBit::AXE}));
  [[maybe_unused]] const auto stonePickaxeId = items.Add(new ToolDefinition("Stone Pickaxe", {"pickaxe", {.2f, .2f, .2f}, 20, 2, BlockDamageFlagBit::PICKAXE}));
  [[maybe_unused]] const auto copperPickaxeId =
    items.Add(new ToolDefinition("Copper Pickaxe", {"pickaxe", {.78f, .51f, .27f}, 30, 2, BlockDamageFlagBit::PICKAXE}));
  [[maybe_unused]] const auto opPickaxeId   = items.Add(new RainbowTool("OP Pickaxe", {"pickaxe", {1, 1, 1}, 1000, 100, BlockDamageFlagBit::ALL_TOOLS, 0.1f}));
  [[maybe_unused]] const auto stoneSpearId  = items.Add(new Spear("Stone Spear", {.tint = {0.2f, 0.2f, 0.2f}}));
  [[maybe_unused]] const auto copperSpearId = items.Add(new Spear("Copper Spear", {.damage = 20, .knockback = 5, .tint = {.78f, .51f, .27f}}));

  auto& blocks = registry_.ctx().insert_or_assign<BlockRegistry>(*this);

  const auto& stoneBlock = blocks.Get(blocks.Add(new BlockDefinition({
    .name        = "Stone",
    .damageTier  = 2,
    .damageFlags = BlockDamageFlagBit::PICKAXE,
    .voxelMaterialDesc =
      {
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "stone_albedo",
      },
  })));

  [[maybe_unused]] const auto& dirtBlock = blocks.Get(blocks.Add(new BlockDefinition({
    .name        = "Dirt",
    .initialHealth = 75,
    .damageTier  = 2,
    .damageFlags = BlockDamageFlagBit::PICKAXE,
    .voxelMaterialDesc =
      {
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "dirt_albedo",
      },
  })));

  [[maybe_unused]] const auto stoneBlockId = stoneBlock.GetItemId();

  [[maybe_unused]] const auto frogLightId = blocks
                                              .Get(blocks.Add(new BlockDefinition({
                                                .name              = "Frog light",
                                                .initialHealth     = 50,
                                                .voxelMaterialDesc = {.baseColorFactor = {0, 0, 0}, .emissionFactor = {1, 5, 1}},
                                              })))
                                              .GetItemId();

  [[maybe_unused]] const auto grassBlockId = blocks
                                               .Get(blocks.Add(new BlockDefinition({
                                                 .name          = "Grass",
                                                 .initialHealth = 50,
                                                 .damageTier    = 1,
                                                 .damageFlags   = BlockDamageFlagBit::PICKAXE,
                                                 .voxelMaterialDesc =
                                                   {
                                                     .randomizeTexcoordRotation = true,
                                                     .baseColorTexture          = "grass_albedo",
                                                   },
                                               })))
                                               .GetItemId();

  [[maybe_unused]] const auto malachiteBlockId = blocks
                                                   .Get(blocks.Add(new BlockDefinition({
                                                     .name          = "Malachite",
                                                     .initialHealth = 100,
                                                     .damageTier    = 2,
                                                     .damageFlags   = BlockDamageFlagBit::PICKAXE,
                                                     .voxelMaterialDesc =
                                                       {
                                                         .randomizeTexcoordRotation = true,
                                                         .baseColorTexture          = "malachite_albedo",
                                                       },
                                                   })))
                                                   .GetItemId();

  [[maybe_unused]] const auto forgeBlockItemId = blocks
                                                   .Get(blocks.Add(new BlockDefinition({
                                                     .name          = "Forge",
                                                     .initialHealth = 100,
                                                     .damageTier    = 1,
                                                     .damageFlags   = BlockDamageFlagBit::PICKAXE,
                                                     .voxelMaterialDesc =
                                                       {
                                                         .baseColorTexture = "forge_side_albedo",
                                                         .emissionTexture  = "forge_side_emission",
                                                         .emissionFactor   = {3, 3, 3},
                                                       },
                                                   })))
                                                   .GetItemId();

  [[maybe_unused]] const auto bombId = blocks
                                         .Get(blocks.Add(new ExplodeyBlockDefinition(
                                           {
                                             .name              = "Bomb",
                                             .initialHealth     = 40,
                                             .voxelMaterialDesc = {.baseColorFactor = {0.8f, 0.2f, 0.2f}, .emissionFactor = {0.1f, 0.01f, 0.01f}},
                                           },
                                           {
                                             .radius      = 3,
                                             .damage      = 100,
                                             .damageTier  = 0,
                                             .pushForce   = 8,
                                             .damageFlags = BlockDamageFlagBit::PICKAXE | BlockDamageFlagBit::AXE,
                                           })))
                                         .GetItemId();

  [[maybe_unused]] const auto stupidBombId = blocks
                                               .Get(blocks.Add(new ExplodeyBlockDefinition(
                                                 {
                                                   .name              = "Stupid Bomb",
                                                   .initialHealth     = 40,
                                                   .voxelMaterialDesc = {.baseColorFactor = {0.8f, 0.2f, 0.2f}, .emissionFactor = {0.5f, 0.1f, 0.1f}},
                                                 },
                                                 {
                                                   .radius      = 8,
                                                   .damage      = 100,
                                                   .damageTier  = 2,
                                                   .pushForce   = 10,
                                                   .damageFlags = BlockDamageFlagBit::PICKAXE | BlockDamageFlagBit::AXE | BlockDamageFlagBit::NO_LOOT,
                                                 })))
                                               .GetItemId();

  [[maybe_unused]] const auto woodBlockId = blocks.Add(new BlockDefinition({
    .name          = "Wood",
    .initialHealth = 100,
    .damageTier    = 1,
    .damageFlags   = BlockDamageFlagBit::AXE,
    .lootDrop      = "tree",
    .voxelMaterialDesc =
      {
        .baseColorFactor = {0.39f, 0.24f, 0.08f},
      },
  }));

  blocks.Add(new BlockDefinition({
    .name          = "Wood Plank",
    .initialHealth = 75,
    .damageTier    = 1,
    .damageFlags   = BlockDamageFlagBit::AXE,
    .voxelMaterialDesc =
      {
        .baseColorTexture = "wood_plank_albedo",
      },
  }));

  auto RegisterFoliageBlock = [&](const char* name, bool dropsSelf) -> BlockId
  {
    auto vox = Vox::LoadFromFile(GetAssetDirectory() / "voxels" / "models" / (std::string(name) + ".vox"));
    return blocks.Add(new BlockDefinition({
      .name          = name,
      .initialHealth = 10,
      .lootDrop = dropsSelf ? decltype(BlockDefinition::CreateInfo::lootDrop)(DropSelf{}) : decltype(BlockDefinition::CreateInfo::lootDrop)(std::monostate{}),
      .voxelMaterialDesc =
        VoxelMaterialDesc{
          .subGrid = VoxToSubGrid(*vox),
        },
      .isSolid = false,
    }));
  };

  RegisterFoliageBlock("test", true);
  RegisterFoliageBlock("grass_long", false);
  RegisterFoliageBlock("grass_medium", false);
  RegisterFoliageBlock("grass_short", false);
  RegisterFoliageBlock("mushroom", true);
  RegisterFoliageBlock("mushroom_glowing", true);
  RegisterFoliageBlock("rock_small", false);
  RegisterFoliageBlock("vines_end", false);
  RegisterFoliageBlock("vines_main", false);
  RegisterFoliageBlock("roots_end", false);
  RegisterFoliageBlock("roots_main", false);
  RegisterFoliageBlock("bush_01", false);
  RegisterFoliageBlock("grass_double_base", false);
  RegisterFoliageBlock("grass_double_top", false);
  RegisterFoliageBlock("leaves_01", false);
  RegisterFoliageBlock("dandelion", true);
  RegisterFoliageBlock("rose", true);
  RegisterFoliageBlock("pot", true);
  RegisterFoliageBlock("SM_Deccer_Cubes_Small", true);
  RegisterFoliageBlock("chair", true);
  RegisterFoliageBlock("table", true);

  constexpr auto szz = glm::ivec3{4, 4, 4};
  auto subGrid       = std::make_unique<TwoLevelGrid::SubVoxel[]>(szz.x * szz.y * szz.z);

  for (int z = 0; z < szz.z; z++)
    for (int y = 0; y < szz.y; y++)
      for (int x = 0; x < szz.x; x++)
      {
        auto& voxel = subGrid[TwoLevelGrid::FlattenGenericCoord(szz, {x, y, z})];
        if (z % 2 == 0 && y % 2 == 0 && x % 2 == 0)
        {
          voxel = TwoLevelGrid::SubVoxel(1);
        }
        else
        {
          voxel = TwoLevelGrid::SubVoxel::Air;
        }
      }

  [[maybe_unused]] const auto subBlockId = blocks.Add(new BlockDefinition({
    .name          = "Sub",
    .initialHealth = 100,
    .voxelMaterialDesc =
      VoxelMaterialDesc{
        .subGrid = std::make_shared<TwoLevelGrid::SubGrid>(TwoLevelGrid::SubGrid{
          .dimensions = szz,
          .grid       = std::move(subGrid),
          .materials  = {{glm::vec4(1, 1, 1, 1)}},
        }),
      },
  }));

  [[maybe_unused]] const auto lightBlockItemId = blocks
                                                   .Get(blocks.Add(new BlockDefinition({
                                                     .name = "Light",
                                                     .voxelMaterialDesc =
                                                       VoxelMaterialDesc{
                                                         //.baseColorTexture          =,
                                                         //.baseColorFactor           =,
                                                         //.emissionTexture           =,
                                                         .emissionFactor = {5, 3, 2},
                                                       },
                                                   })))
                                                   .GetItemId();

  const auto torchItemId = blocks
                             .Get(blocks.Add(new BlockEntityDefinition(
                               {.name = "Torch", .initialHealth = 10, .voxelMaterialDesc = VoxelMaterialDesc{.isInvisible = true}, .isSolid = false},
                               {.id = torchId})))
                             .GetItemId();
  const auto chestItemId =
    blocks.Get(blocks.Add(new BlockEntityDefinition({.name = "Cheste", .voxelMaterialDesc = VoxelMaterialDesc{.baseColorTexture = "chest"}}, {.id = chestId})))
      .GetItemId();
  const auto mushroomBlockItemId = blocks
                                     .Get(blocks.Add(new BlockEntityDefinition(
                                       {
                                         .name              = "Shroom",
                                         .initialHealth     = 10,
                                         .voxelMaterialDesc = VoxelMaterialDesc{.isInvisible = true},
                                         .isSolid           = false,
                                       },
                                       {.id = mushroomId})))
                                     .GetItemId();

  auto& prefabs = registry_.ctx().insert_or_assign<PrefabRegistry>({});
  // const auto grassId = blocks.Get("Grass").GetBlockId();
  // const auto frogLightBlockId = blocks.Get("Frog Light").GetBlockId();

  auto* tallGrass = new SimplePrefab({.name = "Double Grass"});
  tallGrass->voxels.emplace_back(glm::ivec3(0, 0, 0), blocks.Get("grass_double_base").GetBlockId());
  tallGrass->voxels.emplace_back(glm::ivec3(0, 1, 0), blocks.Get("grass_double_top").GetBlockId());
  prefabs.Add(tallGrass);

  auto* testTree = new SimplePrefab({.name = "Tree"});
  auto& binky    = testTree->voxels;
  for (int z = -3; z <= 3; z++)
  for (int y = -1; y <= 3; y++)
  for (int x = -3; x <= 3; x++)
  {
    if (Math::Distance2({x, y + 3, z}, {0, 3, 0}) < 9)
    {
      binky.emplace_back(glm::ivec3(x, y + 3, z), blocks.Get("leaves_01").GetBlockId());
    }
  }
  binky.emplace_back(glm::ivec3(0, 0, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 1, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 2, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 3, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 4, 0), woodBlockId);
  prefabs.Add(testTree);

  auto* testDungeon = new DungeonPrefab({.name = "Dungeon"});
  prefabs.Add(testDungeon);

  prefabs.Add(new VinePrefab({.name = "Vine"}));
  prefabs.Add(new RootPrefab({.name = "Root"}));
  prefabs.Add(new AbandonedHousePrefab({.name = "AbandonedHouse"}));

  auto& crafting = registry_.ctx().insert_or_assign<Crafting>({});
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 15}},
    {{forgeBlockItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}, {stickItemId, 1}},
    {{stoneSpearId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}, {stickItemId, 1}},
    {{stonePickaxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}, {stickItemId, 1}},
    {{stoneAxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stickItemId, 3}},
    {{charcoalItemId, 1}},
    blocks.Get("Forge").GetBlockId(),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stickItemId, 1}, {charcoalItemId, 1}},
    {{torchItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 1}, {charcoalItemId, 1}, {mushroomBlockItemId, 1}},
    {{healingPotionItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{malachiteBlockId, 5}, {charcoalItemId, 1}},
    {{copperIngotItemId, 1}},
    blocks.Get("Forge").GetBlockId(),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotItemId, 5}, {stickItemId, 1}},
    {{copperSpearId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotItemId, 5}, {stickItemId, 1}},
    {{copperPickaxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{copperIngotItemId, 5}, {stickItemId, 1}},
    {{copperAxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{coinItemId, 10}, {stickItemId, 20}},
    {{chestItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{gunId, 1}, {wormSummonItemId, 1}},
    {{flareGunId, 1}},
  });

  auto& loot        = registry_.ctx().insert_or_assign<LootRegistry>({});
  auto standardLoot = std::make_unique<LootDrops>();
  standardLoot->drops.emplace_back(RandomLootDrop{
    .item         = coinItemId,
    .count        = 6,
    .chanceForOne = 0.5f,
  });
  loot.Add("standard", std::move(standardLoot));

  auto treeLoot = std::make_unique<LootDrops>();
  treeLoot->drops.emplace_back(RandomLootDrop{
    .item         = stickItemId,
    .count        = 6,
    .chanceForOne = 0.65f,
  });
  treeLoot->drops.emplace_back(RandomLootDrop{
    .item         = coolStickItemId,
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
    .item         = coinItemId,
    .count        = 150,
    .chanceForOne = 0.5f,
  });
  loot.Add("worm", std::move(wormLoot));
}

void World::CreateGrid(glm::ivec3 numChunks)
{
  auto& grid = registry_.ctx().insert_or_assign(TwoLevelGrid(numChunks));

  auto voxelMats = std::vector<TwoLevelGrid::Material>();
  auto blockDefs = registry_.ctx().get<BlockRegistry>().GetAllDefinitions();
  for (const auto& def : blockDefs)
  {
    voxelMats.emplace_back(TwoLevelGrid::Material{
      .isVisible = !def->GetMaterialDesc().isInvisible,
      .isSolid   = def->GetIsSolid(),
      .subGrid   = def->GetSubGrid(),
    });
  }
  grid.SetMaterialArray(std::move(voxelMats));

  auto* head = registry_.ctx().get<Head*>();
  head->CreateRenderingMaterials(blockDefs);
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
      .shape      = Physics::UseTwoLevelGrid{},
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
    for (int c = 0; c < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; c++)
    {
      for (int b = 0; b < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; b++)
      {
        for (int a = 0; a < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; a++)
        {
          const auto bl = glm::ivec3{a, b, c};

          // Voxels
          for (int z = 0; z < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; z++)
          {
            for (int y = 0; y < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; y++)
            {
              for (int x = 0; x < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; x++)
              {
                const auto positionBLS = glm::ivec3{x, y, z};
                const auto positionWS  = topLevelBrickPos * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE + bl * TwoLevelGrid::BL_BRICK_SIDE_LENGTH + positionBLS;
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
  auto& blocks          = registry_.ctx().get<BlockRegistry>();
  const auto& grass     = blocks.Get("Grass");
  const auto& dirt     = blocks.Get("Dirt");
//  const auto& malachite = blocks.Get("Malachite");

  constexpr auto samplesPerAxis = 64;
  constexpr auto sampleScale    = (float)samplesPerAxis / TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;

  auto& grid = registry_.ctx().get<TwoLevelGrid>();

  auto tlBrickColCoords = std::vector<glm::ivec2>();
  for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
  {
    for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
    {
      tlBrickColCoords.emplace_back(k, i);
    }
  }

  auto globalSurfaceHeightImage = std::vector<float>(grid.topLevelBricksDims_.x * grid.topLevelBricksDims_.z * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE);

  // Used to determine where meadows are. These are flatter areas with fewer trees.
  auto globalMeadowImage = std::vector<float>(grid.topLevelBricksDims_.x * grid.topLevelBricksDims_.z * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE);

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
          glm::ivec2(sampleScale * (glm::vec2(i, k) * (float)TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
          mapGenInfo.seed,
          samplesPerAxis,
          TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
          Filter::Linear);

        auto meadowImage = GenerateAndUpscale2D(meadowNoise,
          glm::ivec2((glm::vec2(i, k) * (float)TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
          mapGenInfo.seed * 21,
          TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
          TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
          Filter::Nearest);

        for (int y = 0; y < TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE; y++)
        for (int x = 0; x < TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE; x++)
        {
          const auto pModTl = glm::ivec2(x, y);
          const auto positionWS = pModTl + glm::ivec2(i, k) * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
          
          const auto meadowness = TexelFetch2D(meadowImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, pModTl);
          //float meadowness       = 0.1f;
          const auto heightScale = glm::mix(15, 4,  meadowness);
          const auto height = glm::floor(mapGenInfo.seaLevel + heightScale * TexelFetch2D(terrainHeightImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, pModTl));
          ImageStore2D(terrainHeightImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, pModTl, height);
          ImageStore2D(globalSurfaceHeightImage, grid.topLevelBricksDims_.x * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, {positionWS.x, positionWS.y}, height);
          ImageStore2D(globalMeadowImage, grid.topLevelBricksDims_.x * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, {positionWS.x, positionWS.y}, meadowness);
        }

        // Top level bricks
        for (int j = 0; j < grid.topLevelBricksDims_.y; j++) // Y last so we can compute heightmap once
        {
          ZoneScopedN("Top level brick");

          auto stoneInDirtImage = GenerateAndUpscale3D(stoneInDirt,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 1,
            samplesPerAxis,
            TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Linear);

          auto fadeImage = GenerateAndUpscale3D(whiteNoise,
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed + 2,
            TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          const auto tl = glm::ivec3{i, j, k};

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto pModTl = positionWS % TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
              
              const auto height = TexelFetch2D(terrainHeightImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, {pModTl.x, pModTl.z});

              auto blockTypeToSet = voxel_t::Air;
              if (positionWS.y < height)
              {
                // 0 at sea level. 1 at cavern level.
                const auto alphaCaverns = glm::clamp((mapGenInfo.seaLevel - positionWS.y) / float(mapGenInfo.surfaceThickness), 0.0f, 1.0f);

                if (positionWS.y == height - 1)
                {
                  blockTypeToSet = grass.GetBlockId();
                }
                // Surface and underground biomes' substrate is dirt
                else if (positionWS.y >= mapGenInfo.seaLevel - mapGenInfo.surfaceThickness)
                {
                  blockTypeToSet = dirt.GetBlockId();

                  // Add stone blobs with increasing size as they get closer to caverns.
                  if (TexelFetch3D(stoneInDirtImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < glm::mix(0.0f, 0.1f, alphaCaverns))
                  {
                    blockTypeToSet = voxel_t(1);
                  }
                  // Dithered fade from dirt to stone, beginning 1/3 from the underground-cavern transition point.
                  else if (TexelFetch3D(fadeImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, pModTl) < alphaCaverns * 3 - 2)
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

              grid.SetVoxelAtNoDirty(positionWS, blockTypeToSet);
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
            glm::ivec3(sampleScale * (glm::vec3(i, j, k) * (float)TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            mapGenInfo.seed,
            samplesPerAxis,
            TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Linear);

          const auto tl = glm::ivec3{i, j, k};
          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              if (grid.GetVoxelAtUnchecked(positionWS) != voxel_t::Air)
              {
                const auto pModTl = positionWS % TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
                
                const auto density = TexelFetch3D(densities, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, pModTl);
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

    if (whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 4) > glm::mix(0.99f, 0.998f, meadowness))
    {
      if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
      {
        registry_.ctx().get<PrefabRegistry>().Get("Tree").Instantiate(*this, {x, y, z});
      }
    }
    else
    {
      if (shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 5) + whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 9) * 0.2f < 0.03f)
      {
        if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("bush_01").GetBlockId());
        }
      }

      if (shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 16) * 0.7f + whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 17) * 0.3f > 0.93f)
      {
        if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("mushroom").GetBlockId());
        }
      }
      else if (meadowness * shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 24) * 0.7f +
                 whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 25) * 0.3f >
               0.95f)
      {
        if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("rose").GetBlockId());
        }
      }
      else if (meadowness * shrimplex->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 22) * 0.7f + whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 23) * 0.3f >
               0.93f)
      {
        if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("dandelion").GetBlockId());
        }
      }
      else if (whiteNoise->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 24) > 0.999f)
      {
        if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
        {
          grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("rock_small").GetBlockId());
        }
      }
      else // Because it's low priority, grass shouldn't override other foliage.
      {
        const auto grasss = meadowness * 0.1f + shrimplex2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 10) +
                            whiteNoise2->GenSingle2D((float)x, (float)z, mapGenInfo.seed + 11) * 0.3f;

        if (grasss > 0.6f)
        {
          if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
          {
            grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_short").GetBlockId());
          }
        }
        if (grasss > 0.7f)
        {
          if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
          {
            grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_medium").GetBlockId());
          }
        }
        if (grasss > 0.8f)
        {
          if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
          {
            grid.SetVoxelAtUnchecked({x, y, z}, blocks.Get("grass_long").GetBlockId());
          }
        }
        if (grasss > 0.9f)
        {
          if (grid.GetVoxelAtUnchecked({x, y - 1, z}) != voxel_t::Air)
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
            tl * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 15,
            TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          auto whiteImage = GenerateAndUpscale3D(whiteNoise2,
            tl * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            mapGenInfo.seed + 16,
            TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE,
            Filter::Nearest);

          ForEachPositionInTLBrick(tl,
            [&](glm::ivec3 positionWS)
            {
              const auto tlLocal = positionWS % TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
              if (grid.GetVoxelAtUnchecked(positionWS) == voxel_t::Air)
              {
                const auto aboveWS = positionWS + glm::ivec3(0, 1, 0);
                const auto belowWS = positionWS + glm::ivec3(0, -1, 0);
                if (aboveWS.y < grid.dimensions_.y - 1)
                {
                  const auto aboveBlock = grid.GetVoxelAtUnchecked(aboveWS);
                  if (aboveBlock != voxel_t::Air)
                  {
                    if (TexelFetch3D(simplexImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, tlLocal) +
                          TexelFetch3D(whiteImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, tlLocal) * 0.3f <
                        0.05f)
                    {
                      auto lk = std::unique_lock(mutex);
                      if (aboveBlock == dirt.GetBlockId())
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
                  if (belowBlock == dirt.GetBlockId() && TexelFetch3D(whiteImage, TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE, tlLocal) > 0.98f)
                  {
                    grid.SetVoxelAtNoDirty(positionWS, blocks.Get("pot").GetBlockId());
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

  {
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

  grid.CoalesceDirtyBricks();
}