#include "Serialization.h"
#include "Reflection.h"
#include "TwoLevelGrid.h"
#include "Game.h"
#include "Assert2.h"

#include "entt/meta/container.hpp"
#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"
#include "entt/meta/factory.hpp"
#include "entt/core/hashed_string.hpp"

#include "cereal/cereal.hpp"
#include "cereal/archives/binary.hpp"
//#include "cereal/archives/xml.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"

#include "tracy/Tracy.hpp"

#include "spdlog/spdlog.h"
#include "spdlog/fmt/std.h"

#include <cstdint>
#include <fstream>
#include <memory>

namespace Core::Serialization
{
  using namespace Reflection;
  using namespace entt::literals;

  namespace
  {
    // This context is used for the deserialization of objects that require non-local context, as in remote-to-local entity ID remapping.
    struct SerializationContext
    {
      bool remapRemoteEntities = false;
      std::unordered_map<entt::entity, entt::entity>* remoteToLocalEntity;
    };

    template<typename Archive, typename T>
    void Serialize2(Archive& ar, T& value, const SerializationContext& = {})
    {
      ar(value);
    }

    template<typename Archive>
    void Serialize3(Archive& ar, entt::entity& value, const SerializationContext& context)
    {
      ar(value);
      if (context.remapRemoteEntities && value != entt::null)
      {
        DEBUG_ASSERT(context.remoteToLocalEntity);
        if (auto it = context.remoteToLocalEntity->find(value); it != context.remoteToLocalEntity->end())
        {
          value = it->second;
        }
        else
        {
          spdlog::warn("Failed to map remote entity {} to a local entity. Mapping to null instead.");
          value = entt::null;
        }
      }
    }
  }

  template<bool Save, typename Archive>
  static void Serialize(Archive& ar, entt::meta_any value, const SerializationContext& context = {})
  {
    ZoneScoped;

    if (value.type().traits<Traits>() & Traits::TRIVIAL)
    {
      // Spooky const_cast! Technically only needed when loading, and should be well-defined as the underlying data is mutable...
      auto* vp = const_cast<void*>(value.base().data());
      auto binary = cereal::binary_data(vp, value.type().size_of());
      ar(binary);
      return;
    }

    // First, check if the type already has a bespoke serialization function.
    const auto archiveHash = entt::type_id<Archive>();
    for (auto [id, func] : value.type().func())
    {
      if (func.arg(0).info() == archiveHash)
      {
        auto ret = func.invoke({}, entt::forward_as_meta(ar), value.as_ref(), entt::forward_as_meta(context));
        ASSERT(ret);
        return;
      }
    }

    if (value.type().is_sequence_container())
    {
      auto sequence = value.as_sequence_container();

      if constexpr (Save)
      {
        auto size = (uint32_t)sequence.size();
        Serialize<Save>(ar, entt::forward_as_meta(size), context);
      }
      else
      {
        auto size = uint32_t();
        Serialize<Save>(ar, entt::forward_as_meta(size), context);
        sequence.resize(size); // Returns false for un-resizable containers such as std::array.
      }
      
      if (sequence.size() > 0 && sequence.value_type().traits<Traits>() & Traits::TRIVIAL)
      {
        // Note: we are assuming here that the sequence container is contiguous (e.g. vector or array) so it can be treated as a binary blob.
        auto* vp = const_cast<void*>(sequence.begin().base().data());
        auto binary = cereal::binary_data(vp, sequence.size() * sequence.value_type().size_of());
        ar(binary);
      }
      else
      {
        for (auto element : sequence)
        {
          Serialize<Save>(ar, element.as_ref(), context);
        }
      }

      return;
    }

    if (value.type().traits<Traits>() & Traits::VARIANT)
    {
      if constexpr (Save)
      {
        auto idFunc = value.type().func("type_hash"_hs);
        ASSERT(idFunc);
        Serialize<Save>(ar, idFunc.invoke({}, value), context);
        auto valueFunc = value.type().func("const_value"_hs);
        ASSERT(valueFunc);
        Serialize<Save>(ar, valueFunc.invoke({}, value.as_ref()), context);
      }
      else
      {
        auto id = entt::id_type();
        Serialize<Save>(ar, entt::forward_as_meta(id), context);
        auto variantMeta = entt::resolve(id);
        ASSERT(variantMeta);
        auto variantTypeInstance = variantMeta.construct();
        Serialize<Save>(ar, variantTypeInstance.as_ref(), context);
        [[maybe_unused]] auto succ = value.assign(value.type().construct(variantTypeInstance));
        ASSERT(succ);
      }
      return;
    }

    if (value.type().is_enum())
    {
      auto toUnderlyingFunc = value.type().func("to_underlying"_hs);
      ASSERT(toUnderlyingFunc);
      if constexpr (Save)
      {
        Serialize<Save>(ar, toUnderlyingFunc.invoke({}, value), context);
      }
      else
      {
        auto underlying = toUnderlyingFunc.ret().construct();
        Serialize<Save>(ar, underlying.as_ref(), context);
        value.assign(underlying);
      }

      return;
    }

    // Serialize data members.
    for (auto [id, data] : value.type().data())
    {
      if (!(data.traits<Traits>() & Traits::TRANSIENT))
      {
        if (data.traits<Traits>() & Traits::TRIVIAL)
        {
          auto* vp    = const_cast<void*>(data.get(value.as_ref()).base().data());
          auto binary = cereal::binary_data(vp, data.type().size_of());
          ar(binary);
        }
        else
        {
          Serialize<Save>(ar, data.get(value).as_ref(), context);
        }
      }
    }
  }


  // Ugly manual serialization to improve TwoLevelGrid serialization perf.
  namespace
  {
    template<typename Archive>
    void Serialize2(Archive& ar, TwoLevelGrid::BottomLevelBrick& blBrick)
    {
      for (auto& bits : blBrick.occupancy.bitmask)
      {
        Serialize2(ar, bits);
      }

      for (auto& voxel : blBrick.voxels)
      {
        Serialize2(ar, voxel);
      }
    }

    template<typename Archive>
    void Serialize2(Archive& ar, const TwoLevelGrid::BottomLevelBrick& blBrick)
    {
      for (auto& bits : blBrick.occupancy.bitmask)
      {
        Serialize2(ar, bits);
      }

      for (auto& voxel : blBrick.voxels)
      {
        Serialize2(ar, voxel);
      }
    }

    template<typename Archive>
    void Serialize2(Archive& ar, TwoLevelGrid::BottomLevelBrickPtr& blBrickPtr)
    {
      Serialize2(ar, blBrickPtr.voxelsDoBeAllSame);
      Serialize2(ar, blBrickPtr.bottomLevelBrick);
    }

    template<typename Archive>
    void Serialize2(Archive& ar, const TwoLevelGrid::BottomLevelBrickPtr& blBrickPtr)
    {
      Serialize2(ar, blBrickPtr.voxelsDoBeAllSame);
      Serialize2(ar, blBrickPtr.bottomLevelBrick);
    }

    template<typename Archive>
    void Serialize2(Archive& ar, TwoLevelGrid::TopLevelBrick& tlBrick)
    {
      for (auto& blBrickPtr : tlBrick.bricks)
      {
        Serialize2(ar, blBrickPtr);
      }
    }

    template<typename Archive>
    void Serialize2(Archive& ar, const TwoLevelGrid::TopLevelBrick& tlBrick)
    {
      for (auto& blBrickPtr : tlBrick.bricks)
      {
        Serialize2(ar, blBrickPtr);
      }
    }
  } // namespace

  template<bool Save, typename Archive>
  static void SerializeGrid(Archive& ar, std::conditional_t<Save, const TwoLevelGrid, TwoLevelGrid>& grid, const SerializationContext& = {})
  {
    ZoneScoped;
    if constexpr (Save)
    {
      Serialize<Save>(ar, grid.materials_);
      Serialize<Save>(ar, grid.topLevelBricksDims_);
    }
    else
    {
      auto materials = std::vector<TwoLevelGrid::Material>();
      Serialize<Save>(ar, entt::forward_as_meta(materials));
      auto dims = glm::ivec3();
      Serialize<Save>(ar, entt::forward_as_meta(dims));
      grid = TwoLevelGrid(dims);
      grid.SetMaterialArray(std::move(materials));
    }

    for (int tz = 0; tz < grid.topLevelBricksDims_.z; tz++)
    for (int ty = 0; ty < grid.topLevelBricksDims_.y; ty++)
    for (int tx = 0; tx < grid.topLevelBricksDims_.x; tx++)
    {
      const auto tlBrickIndex = grid.FlattenTopLevelBrickCoord({tx, ty, tz});
      auto& tlBrickPtr  = grid.GetTopLevelBrickPtr(tlBrickIndex);
      Serialize<Save>(ar, entt::forward_as_meta(tlBrickPtr));
      if (tlBrickPtr.voxelsDoBeAllSame)
      {
        continue;
      }

      if constexpr (!Save)
      {
        tlBrickPtr.topLevelBrick = grid.AllocateTopLevelBrick(voxel_t::Air);
      }
      auto& tlBrick = grid.GetTopLevelBrick(tlBrickPtr.topLevelBrick);
      Serialize2(ar, tlBrick);

      // Bottom-level bricks are handled essentially the same as top-level bricks.
      for (auto& blBrickPtr : tlBrick.bricks)
      {
        Serialize2(ar, blBrickPtr);
        if (blBrickPtr.voxelsDoBeAllSame)
        {
          continue;
        }

        if constexpr (!Save)
        {
          blBrickPtr.bottomLevelBrick = grid.AllocateBottomLevelBrick(voxel_t::Air);
        }
        auto& blBrick = grid.GetBottomLevelBrick(blBrickPtr.bottomLevelBrick);
        Serialize2(ar, blBrick);
      }
    }
  }

  void Initialize()
  {
#define MAKE_SERIALIZERS(T)                                                     \
  entt::meta_factory<T>()                                                       \
    .func<Serialize2<cereal::BinaryInputArchive, T>>("BinaryInputArchive"_hs)   \
    .func<Serialize2<cereal::BinaryOutputArchive, const T>>("BinaryOutputArchive"_hs) 
    //.func<Serialize2<cereal::XMLInputArchive, T>>("XMLInputArchive"_hs)         \
    //.func<Serialize2<cereal::XMLOutputArchive, const T>>("XMLOutputArchive"_hs)
    //.func<Serialize2<cereal::JSONInputArchive, float>>("JSONInputArchive"_hs)
    //.func<Serialize2<cereal::JSONOutputArchive, float>>("JSONOutputArchive"_hs)

    MAKE_SERIALIZERS(bool);
    MAKE_SERIALIZERS(char);
    MAKE_SERIALIZERS(signed char);
    MAKE_SERIALIZERS(unsigned char);
    MAKE_SERIALIZERS(int8_t);
    MAKE_SERIALIZERS(int16_t);
    MAKE_SERIALIZERS(int32_t);
    MAKE_SERIALIZERS(int64_t);
    MAKE_SERIALIZERS(uint8_t);
    MAKE_SERIALIZERS(uint16_t);
    MAKE_SERIALIZERS(uint32_t);
    MAKE_SERIALIZERS(uint64_t);
    MAKE_SERIALIZERS(float);
    MAKE_SERIALIZERS(double);
    MAKE_SERIALIZERS(entt::id_type);
    MAKE_SERIALIZERS(std::string);
    entt::meta_factory<entt::entity>()
      .func<Serialize3<cereal::BinaryInputArchive>>("BinaryInputArchive"_hs)
      .func<Serialize2<cereal::BinaryOutputArchive, const entt::entity>>("BinaryOutputArchive"_hs);

    entt::meta_factory<TwoLevelGrid>()
      .func<SerializeGrid<false, cereal::BinaryInputArchive>>("BinaryInputArchive"_hs)
      .func<SerializeGrid<true, cereal::BinaryOutputArchive>>("BinaryOutputArchive"_hs);
      //.func<SerializeGrid<false, cereal::XMLInputArchive>>("XMLInputArchive"_hs)
      //.func<SerializeGrid<true, cereal::XMLOutputArchive>>("XMLOutputArchive"_hs);
    //entt::meta_factory<char*>().func<[](cereal::BinaryOutputArchive& ar, char* str) { ar(std::string(str)); }>("BinaryOutputArchive"_hs);
    //entt::meta_factory<const char*>().func<[](cereal::BinaryOutputArchive& ar, const char* str) { ar(std::string(str)); }>("BinaryOutputArchive"_hs);
    //entt::meta_factory<std::string_view>().func<[](cereal::BinaryOutputArchive& ar, std::string_view str) { ar(std::string(str)); }>("BinaryOutputArchive"_hs);
  }

  void SaveRegistryToFile(const World& world, const std::filesystem::path& path)
  {
    ZoneScoped;
    spdlog::info("Saving world {}", path);
    const auto& registry = world.GetRegistryRaw();
    auto file            = std::ofstream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    auto outputArchive   = cereal::BinaryOutputArchive(file);

    // Save relevant context variables.
    auto& grid = registry.ctx().get<TwoLevelGrid>();
    SerializeGrid<true>(outputArchive, grid);

    const auto numSets = (uint32_t)std::ranges::count_if(registry.storage(), [](const auto& p) { return entt::resolve(p.first).traits<Traits>() & Traits::COMPONENT; });
    Serialize<true>(outputArchive, numSets);
    for (auto [id, set] : registry.storage())
    {
      if (auto meta = entt::resolve(id))
      {
        const auto traits = meta.traits<Traits>();
        if (traits & Traits::COMPONENT)
        {
          ZoneScopedN("Component");
          ZoneText(meta.info().name().data(), meta.info().name().size());
          Serialize<true>(outputArchive, id);
          Serialize<true>(outputArchive, (uint32_t)set.size());
          for (auto entity : set)
          {
            Serialize<true>(outputArchive, entity);
            if (!(traits & Traits::TRANSIENT))
            {
              Serialize<true>(outputArchive, meta.from_void(set.value(entity)));
            }
          }
        }
      }
    }
    spdlog::info("Saving complete");
  }

  // TODO: ctx.PCG
  // TODO: ctx.NpcSpawnDirector
  void LoadRegistryFromFile(World& world, const std::filesystem::path& path)
  {
    ZoneScoped;
    spdlog::info("Loading world {}", path);
    auto& registry     = world.GetRegistryRaw();
    auto remoteToLocalTemp = std::unordered_map<entt::entity, entt::entity>();
    auto localToRemoteTemp = std::unordered_map<entt::entity, entt::entity>();
    {
      ZoneScopedN("registry.clear()");
      registry.clear(); // Required to invoke on_destroy observers. In particular, for cleaning up physics objects.
    }
    registry = {};
    CreateContextVariablesAndObservers(world);
    registry.ctx().get<GameState>() = GameState::PAUSED;

    auto file = std::ifstream(path, std::ios::binary | std::ios::in);

    {
      auto inputArchive = cereal::BinaryInputArchive(file);

      // Load relevant context variables.
      auto grid = TwoLevelGrid();
      SerializeGrid<false>(inputArchive, grid);
      grid.CoalesceBricksSLOW();
      grid.MarkAllBricksDirty();
      registry.ctx().emplace<TwoLevelGrid>(std::move(grid));
    }

    DeserializeComponentStream(world, file, remoteToLocalTemp, localToRemoteTemp, false, false);

    for (auto entity : registry.view<LocalTransform>())
    {
      world.UpdateLocalTransform(entity);
    }
    spdlog::info("Loading complete");
  }

  void SerializeAllEntitiesForNetwork(const World& world, std::ostream& stream)
  {
    ZoneScoped;
    const auto& registry = world.GetRegistryRaw();
    auto outputArchive   = cereal::BinaryOutputArchive(stream);

    const auto numSets = (uint32_t)std::ranges::count_if(registry.storage(),
      [](const auto& p)
      {
        const auto traits = entt::resolve(p.first).traits<Traits>();
        return traits & Traits::COMPONENT && traits & Traits::REPLICATED;
      });
    Serialize<true>(outputArchive, numSets);
    for (auto [id, set] : registry.storage())
    {
      if (auto meta = entt::resolve(id))
      {
        const auto traits = meta.traits<Traits>();
        if (traits & Traits::COMPONENT && traits & Traits::REPLICATED)
        {
          ZoneScopedN("Component");
          ZoneText(meta.info().name().data(), meta.info().name().size());
          Serialize<true>(outputArchive, id);
          Serialize<true>(outputArchive, (uint32_t)set.size());
          for (auto entity : set)
          {
            Serialize<true>(outputArchive, entity);
            if (!(traits & Traits::TRANSIENT))
            {
              Serialize<true>(outputArchive, meta.from_void(set.value(entity)));
            }
          }
        }
      }
    }
  }

  void SerializeModifiedComponents(const World& world,
    std::ostream& ostream,
    const std::unordered_map<entt::id_type, std::unordered_map<entt::entity, ActionType>>& modifiedComponents)
  {
    ZoneScoped;
    const auto& registry = world.GetRegistryRaw();
    auto outputArchive   = cereal::BinaryOutputArchive(ostream);

    // Only send non-empty sets.
    const auto numSets = (uint32_t)std::ranges::count_if(modifiedComponents,
      [](const auto& p)
      {
        if (p.second.empty())
        {
          return false;
        }
        const auto traits = entt::resolve(p.first).traits<Traits>();
        return traits & Traits::COMPONENT && traits & Traits::REPLICATED;
      });
    Serialize<true>(outputArchive, numSets);
    ZoneTextF("Serializing %u sets", numSets);
    for (auto [id, map] : modifiedComponents)
    {
      if (map.empty()) 
      {
        continue;
      }
      auto& set = *registry.storage(id);
      if (auto meta = entt::resolve(id))
      {
        const auto traits = meta.traits<Traits>();
        if (traits & Traits::COMPONENT && traits & Traits::REPLICATED)
        {
          ZoneScopedN("Component");
          ZoneText(meta.info().name().data(), meta.info().name().size());
          Serialize<true>(outputArchive, id);
          const auto mapSize = (uint32_t)std::ranges::count_if(map,
            [&](const auto& p) { return !(!bool(p.second & ActionType::Remove) && !set.contains(p.first)); });
          Serialize<true>(outputArchive, mapSize);
          SPDLOG_TRACE("Serializing {}x {}", mapSize, meta.info().name());
          ZoneTextF("Set elements: %u", mapSize);
          for (const auto& [entity, action] : map)
          {
            if (!bool(action & ActionType::Remove) && !set.contains(entity))
            {
              continue;
            }
            Serialize<true>(outputArchive, entity);
            Serialize<true>(outputArchive, action);
            SPDLOG_TRACE("Entity {}, action: {}", entt::to_integral(entity), (uint32_t)action);
            if (!bool(action & ActionType::Remove) && !(traits & Traits::TRANSIENT))
            {
              Serialize<true>(outputArchive, meta.from_void(set.value(entity)));
            }
          }
        }
      }
    }
  }

  void DeserializeComponentStream(World& world,
    std::istream& stream,
    std::unordered_map<entt::entity, entt::entity>& remoteToLocal,
    std::unordered_map<entt::entity, entt::entity>& localToRemote,
    bool readActionType,
    bool doRemap)
  {
    ZoneScoped;
    auto& registry    = world.GetRegistryRaw();
    auto inputArchive = cereal::BinaryInputArchive(stream);

    auto context = SerializationContext{.remapRemoteEntities = doRemap, .remoteToLocalEntity = &remoteToLocal};

    auto numSets = uint32_t();
    Serialize<false>(inputArchive, entt::forward_as_meta(numSets));
    ZoneTextF("Deserializing %u sets", numSets);
    for (uint32_t i = 0; i < numSets; i++)
    {
      ZoneScopedN("Component");
      auto typeId = entt::id_type();
      auto size   = uint32_t();
      Serialize<false>(inputArchive, entt::forward_as_meta(typeId));
      Serialize<false>(inputArchive, entt::forward_as_meta(size));
      auto meta = entt::resolve(typeId);
      ASSERT(meta);
      SPDLOG_TRACE("Deserializing {}x {}", size, meta.info().name());
      const auto traits = meta.traits<Traits>();
      ASSERT(traits & Traits::COMPONENT);
      ZoneText(meta.info().name().data(), meta.info().name().size());
      ZoneTextF("Elements: %u", size);
      for (uint32_t j = 0; j < size; j++)
      {
        auto remoteEntity = entt::entity();
        Serialize<false>(inputArchive, entt::forward_as_meta(remoteEntity));
        auto action       = ActionType{};
        ASSERT(remoteEntity != entt::null);
        if (readActionType)
        {
          Serialize<false>(inputArchive, entt::forward_as_meta(action));
        }
        // Get or create the mapped remote->local entity.
        auto localEntity = entt::entity();
        if (auto it = remoteToLocal.find(remoteEntity); it != remoteToLocal.end())
        {
          localEntity = it->second;
        }
        else
        {
          localEntity = registry.create();
          remoteToLocal.emplace(remoteEntity, localEntity);
          localToRemote.emplace(localEntity, remoteEntity);
          SPDLOG_TRACE("Created new entity mapping: {} (local) to {} (remote)", entt::to_integral(localEntity), entt::to_integral(remoteEntity));
        }

        auto* storage = registry.storage(typeId);
        if (readActionType && bool(action & ActionType::Remove))
        {
          if (storage)
          {
            storage->remove(localEntity);
          }
        }
        else
        {
          if (traits & Traits::TRANSIENT)
          {
            if (!storage || !storage->contains(localEntity))
            {
              auto emplaceFunc = meta.func("EmplaceDefault"_hs);
              ASSERT(emplaceFunc);
              emplaceFunc.invoke({}, &registry, localEntity);
            }
          }
          else
          {
            auto value = entt::meta_any{};
            const bool storageContainsEntity = storage && storage->contains(localEntity);
            const bool clientOwnsThisComponent = (meta.id() == entt::type_hash<LocalTransform>().value() || meta.id() == entt::type_hash<GlobalTransform>().value()) &&
                                           world.AncestorHasComponent<LocalAuthoritative>(localEntity);
            if (storageContainsEntity && !clientOwnsThisComponent)
            {
              // Overwrite existing component if it exists.
              value = meta.from_void(storage->value(localEntity));
            }
            else
            {
              value = meta.construct();
              ASSERT(value, "Type does not have a default constructor");
            }
            
            Serialize<false>(inputArchive, value.as_ref(), context);

            //if (storageContainsEntity && meta.id() == entt::type_hash<LocalTransform>().value() && world.AncestorHasComponent<LocalAuthoritative>(localEntity))
            //{
            //  auto realValue   = meta.from_void(storage->value(localEntity));
            //  const auto& copy = value.cast<LocalTransform&>();
            //  auto& xform      = realValue.cast<LocalTransform&>();
            //  if (glm::distance(copy.position, xform.position) > 5.0f)
            //  {
            //    xform = copy;
            //  }
            //}

            if (!storageContainsEntity)
            {
              auto emplaceFunc = meta.func("EmplaceMove"_hs);
              ASSERT(emplaceFunc);
              emplaceFunc.invoke({}, &registry, localEntity, value.as_ref());
            }
          }
        }
      }
    }
  }

  void SerializeEntity(std::stringstream& stream, const World& world, entt::entity entity)
  {
    ZoneScoped;
    
    auto outputArchive = cereal::BinaryOutputArchive(stream);
    auto& registry = world.GetRegistryRaw();

    Serialize<true>(outputArchive, entity);
    const auto numComponents = (uint32_t)std::ranges::count_if(registry.storage(),
      [&](const auto& p)
      {
        const auto traits = entt::resolve(p.first).template traits<Traits>();
        return p.second.contains(entity) && traits & Traits::COMPONENT && traits & Traits::REPLICATED;
      });
    Serialize<true>(outputArchive, numComponents);
    for (auto [id, set] : registry.storage())
    {
      if (!set.contains(entity))
      {
        continue;
      }

      if (auto meta = entt::resolve(id))
      {
        const auto traits = meta.traits<Traits>();
        if (traits & Traits::COMPONENT && traits & Traits::REPLICATED)
        {
          ZoneScopedN("Component");
          ZoneText(meta.info().name().data(), meta.info().name().size());
          Serialize<true>(outputArchive, id);
          if (!(traits & Traits::TRANSIENT))
          {
            Serialize<true>(outputArchive, meta.from_void(set.value(entity)));
          }
        }
      }
    }
  }

  void DeserializeEntity(std::stringstream& stream, World& world, std::unordered_map<entt::entity, entt::entity>& remoteToLocal)
  {
    ZoneScoped;
    auto& registry = world.GetRegistry();
    
    auto inputArchive = cereal::BinaryInputArchive(stream);

    auto remoteEntity = entt::entity();
    Serialize<false>(inputArchive, entt::forward_as_meta(remoteEntity));
    auto it = remoteToLocal.find(remoteEntity);
    ASSERT(it != remoteToLocal.end(), "Entity and its mapping must have already been created.");
    auto localEntity = it->second;

    auto context = SerializationContext{.remapRemoteEntities = true, .remoteToLocalEntity = &remoteToLocal};

    auto numComponents = uint32_t();
    Serialize<false>(inputArchive, entt::forward_as_meta(numComponents));
    for (uint32_t i = 0; i < numComponents; i++)
    {
      ZoneScopedN("Component");
      auto typeId = entt::id_type();
      Serialize<false>(inputArchive, entt::forward_as_meta(typeId));
      auto meta = entt::resolve(typeId);
      ASSERT(meta);
      const auto traits = meta.traits<Traits>();
      ASSERT(traits & Traits::COMPONENT && traits & Traits::REPLICATED);
      ZoneText(meta.info().name().data(), meta.info().name().size());
      ASSERT(remoteEntity != entt::null);

      auto* storage = world.GetRegistryRaw().storage(typeId);
      if (traits & Traits::TRANSIENT)
      {
        // We don't want transient components to be overwritten by the default value if they already exist.
        if (!storage || !storage->contains(localEntity))
        {
          auto emplaceFunc = meta.func("EmplaceDefault"_hs);
          ASSERT(emplaceFunc);
          emplaceFunc.invoke({}, &registry, localEntity);
        }
      }
      else
      {
        auto value = entt::meta_any{};
        const bool storageContainsEntity = storage && storage->contains(localEntity);
        if (storageContainsEntity)
        {
          // Overwrite existing component if it exists.
          value = meta.from_void(storage->value(localEntity));
        }
        else
        {
          value = meta.construct();
          ASSERT(value, "Type does not have a default constructor");
        }

        Serialize<false>(inputArchive, value.as_ref(), context);

        if (!storageContainsEntity)
        {
          auto emplaceFunc = meta.func("EmplaceMove"_hs);
          ASSERT(emplaceFunc);
          emplaceFunc.invoke({}, &registry, localEntity, value.as_ref());
        }
      }
    }
  }

  void SerializeObjectStream(std::stringstream& stream, entt::meta_any object)
  {
    ZoneScoped;
    auto outputArchive = cereal::BinaryOutputArchive(stream);
    //Serialize<true>(outputArchive, object.type().info().hash());
    Serialize<true>(outputArchive, object.as_ref());
  }

  entt::meta_any DeserializeObjectStream(std::stringstream& stream, const entt::meta_type& type)
  {
    ZoneScoped;
    auto inputArchive = cereal::BinaryInputArchive(stream);
    //auto typeHash     = uint32_t{};
    //Serialize<false>(inputArchive, entt::forward_as_meta(typeHash));
    ASSERT(type);
    auto object = type.construct();
    Serialize<false>(inputArchive, object.as_ref());
    return object;
  }

  std::vector<char> SerializeObject(entt::meta_any object)
  {
    ZoneScoped;
    auto stream = std::stringstream();
    
    {
      auto outputArchive = cereal::BinaryOutputArchive(stream);
      Serialize<true>(outputArchive, object.as_ref());
    }

    return {std::istreambuf_iterator{stream}, std::istreambuf_iterator<char>{}};
  }

  entt::meta_any DeserializeObject(std::span<const char> objectBytes, const entt::meta_type& type)
  {
    ZoneScoped;
    auto stream       = std::stringstream(std::string(objectBytes.data(), objectBytes.size()));
    auto inputArchive = cereal::BinaryInputArchive(stream);
    auto typeHash     = uint32_t{};
    Serialize<false>(inputArchive, entt::forward_as_meta(typeHash));
    ASSERT(type);
    auto object = type.construct();
    Serialize<false>(inputArchive, object.as_ref());
    return object;
  }
} // namespace Core::Serialization
