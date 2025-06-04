#include "Assets.h"
#include "Item.h"
#include "Prefab.h"
#include "VoxLoader.h"
#include "World.h"
#include "Core/Assert2.h"
#include "Physics/Physics.h"

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
    const auto& wood   = blocks.Get("Wood");
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

  const auto& stoneBlock                   = blocks.Get(blocks.Add(new BlockDefinition({
                      .name        = "Stone",
                      .damageTier  = 2,
                      .damageFlags = BlockDamageFlagBit::PICKAXE,
                      .voxelMaterialDesc =
      {
                          .randomizeTexcoordRotation = true,
                          .baseColorTexture          = "stone_albedo",
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

  {
    auto vox = Vox::LoadFromFile(GetAssetDirectory() / "models" / "test.vox");

    [[maybe_unused]] const auto subBlockId = blocks.Add(new BlockDefinition({
      .name          = "Vox",
      .initialHealth = 100,
      .voxelMaterialDesc =
        VoxelMaterialDesc{
          .subGrid = VoxToSubGrid(*vox),
        },
    }));
  }

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

  auto* testTree = new SimplePrefab({.name = "Tree"});
  auto& binky    = testTree->voxels;
  binky.emplace_back(glm::ivec3(0, 0, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 1, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 2, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 3, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 4, 0), woodBlockId);
  prefabs.Add(testTree);

  auto* testDungeon = new DungeonPrefab({.name = "Dungeon"});
  prefabs.Add(testDungeon);

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

void World::GenerateMap(const MapGenInfo&)
{
  ZoneScoped;
#ifndef GAME_HEADLESS
  auto& progressText = registry_.ctx().get<std::atomic<const char*>>("progressText"_hs);
  auto& progress     = registry_.ctx().get<std::atomic_int32_t>("progress"_hs);
  auto& total        = registry_.ctx().get<std::atomic_int32_t>("total"_hs);
#endif
  auto& blocks          = registry_.ctx().get<BlockRegistry>();
  const auto& grass     = blocks.Get("Grass");
  const auto& malachite = blocks.Get("Malachite");

  auto noiseGraph =
    FastNoise::NewFromEncodedNodeTree("HgAZABsAJwABAAAAFgARAAAADQADAAAAAADAPxMAAADAPwgAAM3MzD4AAAAAAADNzMw9AQQAAAAAAAAAgD8AAAAAAAAAAAAAAADNzMy+"
                                      "AAAAAAAAAAABGQAbAB0AHgAEAAAAAACPwvU+"
                                      "AAAAAAAAAAAAAAAAMzMzPwAAAAAAAAAAAAAAAAAAAACAPwETAJqZmT4aAAERAAIAAAAAAOBAEAAAAIhBHwAWAAEAAAALAAMAAAACAAAAAwAAAA"
                                      "QAAAAAAAAAPwEUAP//CwAAAAAAPwAAAAA/AAAAAD8AAAAAPwEXAAAAgL8AAIA/PQoXQFK4HkATAAAAoEAGAACPwnU8AJqZmT4AAAAAAADhehQ/"
                                      "ARsADQAEAAAAAAAAQAgAAAAAAD8AAAAAAAEaAAAAAIA/AR4AHQAEAAAAAAAAAIA/"
                                      "AAAAAAAAAAAAAAAAzcxMPwAAAAAAAAAAAAAAgD8AAAAAAA==");
  auto copperGraph     = FastNoise::New<FastNoise::Simplex>();
  auto terrainHeight2D = FastNoise::NewFromEncodedNodeTree("FgARAAAADQADAAAAAAAAQBMAAADAPwgAAAAAAD8AAAAAAA==");
  auto surfaceCaves =
    FastNoise::NewFromEncodedNodeTree("GQAbAB0AHgAEAAAAAAAAAMA/"
                                      "AAAAAAAAAAAAAAAAmpmZPgAAAAAAAAAAAAAAAAAAAACAPwETAJqZmT4aAAERAAIAAAAAAOBAEAAAAIhBHwAWAAEAAAALAAMAAAACAAAAAwAAAA"
                                      "QAAAAAAAAAPwEUAP//AwAAAAAAPwAAAAA/AAAAAD8AAAAAPwEXAAAAgL8AAIA/PQoXQFK4HkATAAAAoEAGAACPwnU8AJqZmT4AAAAAAADhehQ/"
                                      "ARsADQAEAAAAAAAAQBMAAADAPwgAAAAAAD8AAAAAAAEaAAAAAIA/AR4AHQAEAAAAAAAAAIA/"
                                      "AAAAAAAAAAAAAAAAzcxMPwAAAAAAAAAAAAAAgD8AAAAAAA==");
  auto trees = FastNoise::New<FastNoise::Value>();

  constexpr auto samplesPerAxis = 32;
  constexpr auto sampleScale    = (float)samplesPerAxis / TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
  auto sampleNoise3D            = [samplesPerAxis](const auto& tlCellNoise, glm::vec3 uv)
  {
    const auto unnormalized = uv * (float)samplesPerAxis - 0.5f;
    const auto intCoord     = glm::ivec3(unnormalized);
    const auto weight       = unnormalized - glm::vec3(intCoord);

    auto texelFetch3D = [&](glm::ivec3 p)
    {
      p = glm::clamp(p, glm::ivec3(0), glm::ivec3(samplesPerAxis - 1));
      return tlCellNoise[p.x + p.y * samplesPerAxis + p.z * samplesPerAxis * samplesPerAxis];
    };

    const auto bln = texelFetch3D(intCoord + glm::ivec3(0, 0, 0));
    const auto brn = texelFetch3D(intCoord + glm::ivec3(1, 0, 0));
    const auto tln = texelFetch3D(intCoord + glm::ivec3(0, 1, 0));
    const auto trn = texelFetch3D(intCoord + glm::ivec3(1, 1, 0));
    const auto blf = texelFetch3D(intCoord + glm::ivec3(0, 0, 1));
    const auto brf = texelFetch3D(intCoord + glm::ivec3(1, 0, 1));
    const auto tlf = texelFetch3D(intCoord + glm::ivec3(0, 1, 1));
    const auto trf = texelFetch3D(intCoord + glm::ivec3(1, 1, 1));

    const auto n = glm::mix(glm::mix(bln, brn, weight.x), glm::mix(tln, trn, weight.x), weight.y);
    const auto f = glm::mix(glm::mix(blf, brf, weight.x), glm::mix(tlf, trf, weight.x), weight.y);
    return glm::mix(n, f, weight.z);
  };

  auto sampleNoise2D = [samplesPerAxis](const auto& tlCellNoise, glm::vec2 uv)
  {
    const auto unnormalized = uv * (float)samplesPerAxis - 0.5f;
    const auto intCoord     = glm::ivec2(unnormalized);
    const auto weight       = unnormalized - glm::vec2(intCoord);

    auto texelFetch2D = [&](glm::ivec2 p)
    {
      p = glm::clamp(p, glm::ivec2(0), glm::ivec2(samplesPerAxis - 1));
      return tlCellNoise[p.x + p.y * samplesPerAxis];
    };

    const auto bl = texelFetch2D(intCoord + glm::ivec2(0, 0));
    const auto br = texelFetch2D(intCoord + glm::ivec2(1, 0));
    const auto tl = texelFetch2D(intCoord + glm::ivec2(0, 1));
    const auto tr = texelFetch2D(intCoord + glm::ivec2(1, 1));

    return glm::mix(glm::mix(bl, br, weight.x), glm::mix(tl, tr, weight.x), weight.y);
  };

  auto& grid = registry_.ctx().get<TwoLevelGrid>();

#ifndef GAME_HEADLESS
  total.store((int32_t)grid.numTopLevelBricks_);
  progressText.store("Chunks");
#endif

  auto tlBrickColCoords = std::vector<glm::ivec2>();
  for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
  {
    for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
    {
      tlBrickColCoords.emplace_back(k, i);
    }
  }

  std::mutex coalesceMutex;
  auto treePositions    = std::vector<glm::ivec3>();
  auto dungeonPositions = std::vector<glm::ivec3>();

  // Top level bricks
  std::for_each(std::execution::par,
    tlBrickColCoords.begin(),
    tlBrickColCoords.end(),
    [&](glm::ivec2 tlBrickColCoord)
    // for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
    //{
    //   for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
    {
      const int k = tlBrickColCoord[0];
      const int i = tlBrickColCoord[1];

      auto tlTerrainHeight = std::vector<float>(samplesPerAxis * samplesPerAxis);
      {
        ZoneScopedN("terrainHeight");

        terrainHeight2D->GenUniformGrid2D(tlTerrainHeight.data(),
          int(sampleScale * (i * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
          int(sampleScale * (k * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
          samplesPerAxis,
          samplesPerAxis,
          0.008f / sampleScale,
          1234);
      }
      for (int j = 0; j < grid.topLevelBricksDims_.y; j++) // Y last so we can compute heightmap once
      {
        const auto tl      = glm::ivec3{i, j, k};
        auto tlCellNoise   = std::vector<float>(samplesPerAxis * samplesPerAxis * samplesPerAxis);
        auto tlCopperNoise = std::vector<float>(samplesPerAxis * samplesPerAxis * samplesPerAxis);
        {
          ZoneScopedN("noiseGraph->GenUniformGrid3D");
          surfaceCaves->GenUniformGrid3D(tlCellNoise.data(),
            int(sampleScale * (tl.x * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            int(sampleScale * (tl.y * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE - 180)),
            int(sampleScale * (tl.z * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            samplesPerAxis,
            samplesPerAxis,
            samplesPerAxis,
            0.008f / sampleScale,
            1234);

          copperGraph->GenUniformGrid3D(tlCopperNoise.data(),
            int(sampleScale * (tl.x * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            int(sampleScale * (tl.y * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE - 180)),
            int(sampleScale * (tl.z * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            samplesPerAxis,
            samplesPerAxis,
            samplesPerAxis,
            0.018f * 5 / sampleScale,
            1234);
        }

        // Bottom level bricks
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
                    const auto local  = glm::ivec3{x, y, z};
                    const auto p      = tl * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE + bl * TwoLevelGrid::BL_BRICK_SIDE_LENGTH + local;
                    const auto pModTl = p % TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
                    // if (de2(glm::vec3(p) / 10.f + 2.0f) < 0.011f)
                    // if (de2(glm::vec3(p) / 10.f + 2.0f) < 0.011f)
                    //  if (de3(p) < 0.011f)
                    // if (tlCellNoise[pModTl.x + pModTl.y * 64 + pModTl.z * 64 * 64] < 0)
                    const auto noiseUv3 = (glm::vec3(pModTl) + 0.5f) / (float)TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
                    const auto noiseUv2 = glm::vec2(noiseUv3.x, noiseUv3.z);
                    const auto height   = (int)(sampleNoise2D(tlTerrainHeight, noiseUv2) * 10 + 260);
                    if (p.y < height && sampleNoise3D(tlCellNoise, noiseUv3) < 0)
                    // if (p.y < height)
                    {
                      if (p.y == height - 1 && trees->GenSingle2D((float)p.x, (float)p.z, 123456) > 0.98f)
                      {
                        auto lock = std::unique_lock(coalesceMutex);
                        treePositions.emplace_back(p);
                      }
                      else if (p.y > height - 2)
                      {
                        grid.SetVoxelAtNoDirty(p, grass.GetBlockId());
                      }
                      else
                      {
                        const auto pf = glm::vec3(p) * 0.018f;
                        if (sampleNoise3D(tlCopperNoise, noiseUv3) + 0.81f < 0)
                        {
                          grid.SetVoxelAtNoDirty(p, malachite.GetBlockId());
                        }
                        else
                        {
                          grid.SetVoxelAtNoDirty(p, voxel_t(1));
                        }
                        if (trees->GenSingle3D((float)p.x, (float)p.y, (float)p.z, 123321) > 0.99999f)
                        {
                          auto lock = std::unique_lock(coalesceMutex);
                          dungeonPositions.emplace_back(p);
                        }
                      }
                    }
                    else
                    {
                      grid.SetVoxelAtNoDirty(p, voxel_t::Air);
                    }
                  }
                }
              }
            }
          }
        }

        grid.MarkTopLevelBrickAndChildrenDirty(tl);
      // TODO: coalesce top-level brick
#ifndef GAME_HEADLESS
        progress.fetch_add(1);
#endif
        // auto lock = std::unique_lock(coalesceMutex);
      }
    });
  //}

  // constexpr int MAX_MUSHROOMS = 50'000;
  constexpr int MAX_MUSHROOMS = 1;
#ifndef GAME_HEADLESS
  progressText.store("MUSHROOM");
  progress.store(0);
  total.store(MAX_MUSHROOMS);
#endif
  const auto& MUSHROOM = blocks.Get("Shroom");
  for (int i = 0; i < MAX_MUSHROOMS; i++)
  {
    const auto numVoxels = grid.topLevelBricksDims_ * grid.TL_BRICK_VOXELS_PER_SIDE;
    const auto pos       = glm::ivec3(Rng().RandU32() % numVoxels.x, Rng().RandU32() % numVoxels.y, Rng().RandU32() % numVoxels.z);
    const auto testPos   = pos - glm::ivec3(0, 1, 0);
    if (grid.IsPositionInGrid(testPos) && grid.GetVoxelAt(pos) == voxel_t::Air && grid.GetVoxelAt(testPos) != voxel_t::Air)
    {
      MUSHROOM.OnTryPlaceBlock(*this, pos);
    }
#ifndef GAME_HEADLESS
    progress.fetch_add(1);
#endif
  }

#ifndef GAME_HEADLESS
  progressText.store("Prefabs");
#endif
  for (auto treePos : treePositions)
  {
    registry_.ctx().get<PrefabRegistry>().Get("Tree").Instantiate(*this, treePos);
  }

  for (auto dungeonPos : dungeonPositions)
  {
    registry_.ctx().get<PrefabRegistry>().Get("Dungeon").Instantiate(*this, dungeonPos);
  }

  grid.CoalesceDirtyBricks();
}