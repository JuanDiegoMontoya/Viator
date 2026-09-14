#pragma once
#include "Reflection.h"
#include "Game/World.h"

#include "entt/meta/resolve.hpp"

namespace Core::Reflection
{
  std::string RemoveNamespaces(std::string name);

  template<typename T>
  T WorldGet(World& world, entt::entity entity)
  {
    if (!world.GetRegistry().valid(entity))
    {
      throw std::runtime_error("Invalid entity");
    }
    if (const auto* ptr = world.GetRegistry().try_get<const T>(entity))
    {
      return *ptr;
    }
    throw std::runtime_error("Component did not exist on entity");
  }

  template<typename T>
  void WorldSet(World& world, entt::entity entity, T val)
  {
    if (!world.GetRegistry().valid(entity))
    {
      throw std::runtime_error("Invalid entity");
    }
    world.GetRegistry().emplace_or_replace<T>(entity, val);
  }

  template<typename T>
  bool WorldHas(World& world, entt::entity entity)
  {
    if (!world.GetRegistry().valid(entity))
    {
      throw std::runtime_error("Invalid entity");
    }
    return world.GetRegistry().all_of<T>(entity);
  }

  template<typename T>
  void EmplaceOrReplaceWrapper_HACK(entt::registry* registry, entt::entity entity, T& value)
  {
    registry->emplace_or_replace<T>(entity, std::move(value));
  }

  template<typename T>
  static std::string GetName()
  {
    if (auto meta = entt::resolve<T>())
    {
      if (const auto* props = static_cast<const Core::Reflection::PropertiesMap*>(meta.custom()))
      {
        using namespace entt::literals;
        if (auto it = props->find("name"_hs); it != props->end())
        {
          return it->second.cast<const char*>();
        }
      }
    }

    return std::string(entt::type_name<T>::value());
  }

  template<typename Variant, std::size_t I = 0>
  void ForEachVariantAlternative(auto&& func)
  {
    if constexpr (I < std::variant_size_v<Variant>)
    {
      using T = std::variant_alternative_t<I, Variant>;
      func.template operator()<T>();
      ForEachVariantAlternative<Variant, I + 1>(std::forward<decltype(func)>(func));
    }
  }

  template<typename T, typename U>
  decltype(auto) AppendToPropertiesMap(entt::hashed_string id, U any)
  {
    auto meta = entt::resolve<T>();
    auto* map = static_cast<Core::Reflection::PropertiesMap*>(meta.custom());
    if (!map)
    {
      entt::meta_factory<T>{}.template custom<Core::Reflection::PropertiesMap>();
      map = static_cast<Core::Reflection::PropertiesMap*>(entt::resolve<T>().custom());
      DEBUG_ASSERT(map);
    }
    return map->emplace(id, std::move(any)).first->second.template cast<U&>();
  }

  ///////////////// Type traits 2.0
  template<typename T>
  struct is_optional : std::false_type
  {
  };

  template<typename T>
  struct is_optional<std::optional<T>> : std::true_type
  {
  };

  template<typename T>
  constexpr bool is_optional_v = is_optional<T>::value;

  template<typename T>
  struct is_unique_ptr : std::false_type
  {
  };

  template<typename T, typename D>
  struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type
  {
  };

  template<typename T>
  constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

  template<typename T>
  struct is_variant : std::false_type
  {
  };

  template<typename... Ts>
  struct is_variant<std::variant<Ts...>> : std::true_type
  {
  };

  template<typename T>
  constexpr bool is_variant_v = is_variant<T>::value;
}

// ADL and specializations
namespace entt
{
  template<>
  struct type_name<std::string> final
  {
    [[nodiscard]] static constexpr std::string_view value() noexcept
    {
      return std::string_view{"std::string"};
    }
  };
#if 0
#if 1
  template<typename T>
  struct adl_meta_pointer_like<std::optional<T>>
  {
    static decltype(auto) dereference(const std::optional<T>& option)
    {
      return option.value();
    }

    static decltype(auto) dereference(std::optional<T>& option)
    {
      return option.value();
    }
  };

  template<typename T>
  struct is_meta_pointer_like<std::optional<T>> : std::true_type
  {
  };
#else
  template<typename T>
  struct meta_sequence_container_traits<std::optional<T>>
  {
    using size_type = typename meta_sequence_container::size_type;
    //using iterator = T*;
    using value_type = T;
    using iterator = typename meta_sequence_container::iterator;
    using reference = T&;
    using const_reference = const T&;

    static constexpr bool fixed_size = false;
    
    [[nodiscard]] static size_type size(const void* container)
    {
      const auto* cont = static_cast<const std::optional<T>*>(container);
      return cont->has_value() ? 1 : 0;
    }

    [[nodiscard]] static bool clear(void* container)
    {
      auto* cont = static_cast<std::optional<T>*>(container);
      cont->reset();
      return true;
    }

    [[nodiscard]] static bool reserve([[maybe_unused]] void* container, [[maybe_unused]] size_type size)
    {
      return false;
    }

    [[nodiscard]] static bool resize(void* container, size_type size)
    {
      if constexpr (!std::is_default_constructible_v<T>)
      {
        return false;
      }

      if (size == 0)
      {
        return clear(container);
      }

      if (size == 1)
      {
        auto* cont = static_cast<std::optional<T>*>(container);
        cont->emplace();
        return true;
      }

      return false;
    }
    static iterator begin(const meta_ctx& area, void* container, const void* as_const)
    {
      if (container)
      {
        auto* cont = static_cast<std::optional<T>*>(container);
        return cont->has_value() ? iterator{area, &*cont} : iterator{area, (T*)nullptr};
      }

      const auto* cont = static_cast<const std::optional<T>*>(as_const);
      return cont->has_value() ? iterator{area, &*cont} : iterator{area, (const T*)nullptr};
    }
    
    static iterator end(const meta_ctx& area, void* container, const void* as_const)
    {
      if (container)
      {
        auto* cont = static_cast<std::optional<T>*>(container);
        return cont->has_value() ? iterator{area, &*cont + 1} : iterator{area, (T*)nullptr};
      }

      const auto* cont = static_cast<const std::optional<T>*>(as_const);
      return cont->has_value() ? iterator{area, &*cont + 1} : iterator{area, (const T*)nullptr};
    }
    
    [[nodiscard]] static iterator insert([[maybe_unused]] const meta_ctx& area,
      [[maybe_unused]] void* container,
      [[maybe_unused]] const void* value,
      [[maybe_unused]] const void* cref,
      [[maybe_unused]] const iterator& it)
    {
      //auto* cont = static_cast<std::optional<T>*>(container);
      return iterator{area, (T*)nullptr};
    }
    
    [[nodiscard]] static iterator erase([[maybe_unused]] const meta_ctx& area, [[maybe_unused]] void* container, [[maybe_unused]] const iterator& it)
    {
      auto* cont = static_cast<std::optional<T>*>(container);
      if (cont->has_value() && it == &*cont)
      {
        cont->reset();
      }
      return iterator{area, (T*)nullptr};
    }
  };
#endif
#endif
} // namespace entt