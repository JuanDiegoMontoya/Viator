#include "ReflectionUtilitiesInternal.h"

#include "HashUtilities.h"
#include "entt/meta/resolve.hpp"

#include <unordered_map>

namespace Core::Reflection
{
  using namespace entt::literals;

  std::string RemoveNamespaces(std::string name)
  {
    if (auto pos = name.find_last_of(':'); pos != std::string::npos)
    {
      name.erase(name.begin(), name.begin() + pos + 1);
    }

    if (auto pos = name.find_last_of(' '); pos != std::string_view::npos)
    {
      name.erase(name.begin(), name.begin() + pos + 1);
    }

    return name;
  }

  static std::unordered_map<uint32_t, entt::meta_type>* gIdToTypeMap;

  void GenerateHashedNamesForReflection()
  {
    DEBUG_ASSERT(!gIdToTypeMap);
    gIdToTypeMap = new std::unordered_map<uint32_t, entt::meta_type>();
    gIdToTypeMap->clear();

    for (auto [id, type] : entt::resolve())
    {
      // Hash the name of the type.
      if (auto* map = static_cast<PropertiesMap*>(type.custom()))
      {
        if (auto it = map->find("name"_hs); it != map->end())
        {
          const char* name = it->second.cast<const char*>();
          ASSERT(name);
          const uint32_t hash = HashFnv1a(name, std::strlen(name));
          map->emplace("name_hashed"_hs, hash);
          auto [_, success] = gIdToTypeMap->try_emplace(hash, type);
          ASSERT(success, "A type with that name was already registered!");
        }
      }

      // Hash the names of data members of the type.
      for (auto [dataId, data] : type.data())
      {
        if (auto* map = static_cast<PropertiesMap*>(data.custom()))
        {
          if (auto it = map->find("name"_hs); it != map->end())
          {
            const char* name = it->second.cast<const char*>();
            ASSERT(name);
            const uint32_t hash = HashFnv1a(name, std::strlen(name));
            map->emplace("name_hashed"_hs, hash);
          }
        }
      }
    }
  }

  const std::unordered_map<uint32_t, entt::meta_type>& GetIdToTypeMap()
  {
    ASSERT(gIdToTypeMap);
    return *gIdToTypeMap;
  }

  std::expected<uint32_t, GetHashedTypeNameError> GetHashedTypeName(const entt::meta_type& type)
  {
    using enum GetHashedTypeNameError;

    const auto* properties = static_cast<const Core::Reflection::PropertiesMap*>(type.custom());
    if (!properties)
    {
      return std::unexpected(PropertiesMapDoesNotExist);
    }

    auto it = properties->find("name_hashed"_hs);
    if (it == properties->end())
    {
      return std::unexpected(HashedNamePropertyDoesNotExist);
    }

    const auto* hash = it->second.try_cast<uint32_t>();
    if (!hash)
    {
      return std::unexpected(HashedNamePropertyHasWrongType);
    }

    return *hash;
  }

  // Initialize is in Reflection.cpp :)
  void Cleanup()
  {
    ASSERT(gIdToTypeMap);
    delete gIdToTypeMap;
  }
} // namespace Core::Reflection