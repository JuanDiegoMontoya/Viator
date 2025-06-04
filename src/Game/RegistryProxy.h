#pragma once
#include "entt/entity/entity.hpp"
#include "entt/entity/registry.hpp"

#include <cstdint>
#include <unordered_map>

enum class ActionType : uint8_t
{
  Add    = 1 << 0,
  Modify = 1 << 1,
  Remove = 1 << 2
};

constexpr ActionType operator|(ActionType a, ActionType b)
{
  return static_cast<ActionType>((uint32_t)a | (uint32_t)b);
}

constexpr ActionType operator|=(ActionType& a, ActionType b)
{
  return a = a | b;
}

constexpr ActionType operator&(ActionType a, ActionType b)
{
  return static_cast<ActionType>((uint32_t)a & (uint32_t)b);
}

// Proxy that wraps common entt::registry operations for the purpose of tracking component changes.
// This allows us to replicate component state with essentially no change to gameplay code.
// However, this tracking comes with some overhead when getting or viewing mutable components.
// Note: this tracking is currently only necessary in multiplayer sessions.
class RegistryProxy
{
public:
  explicit RegistryProxy(entt::registry& registry) : registry_(&registry) {}

  [[nodiscard]] const entt::registry& GetRaw() const
  {
    return *registry_;
  }

  [[nodiscard]] auto create()
  {
    return registry_->create();
  }

  [[nodiscard]] decltype(auto) ctx() noexcept
  {
    return registry_->ctx();
  }

  [[nodiscard]] decltype(auto) ctx() const noexcept
  {
    return ConstRegistry()->ctx();
  }

  template<typename... Type>
  [[nodiscard]] auto try_get(entt::entity entity)
  {
    if constexpr (sizeof...(Type) == 1)
    {
      MarkIfNotConst<Type...>(entity, ActionType::Modify);
      return registry_->try_get<Type...>(entity);
    }
    else
    {
      return std::make_tuple(try_get<Type>(entity)...);
    }
  }

  template<typename... Type>
  [[nodiscard]] auto try_get(entt::entity entity) const
  {
    if constexpr (sizeof...(Type) == 1)
    {
      return ConstRegistry()->try_get<Type...>(entity);
    }
    else
    {
      return std::make_tuple(try_get<Type>(entity)...);
    }
  }

  template<typename... Type>
  [[nodiscard]] decltype(auto) get(entt::entity entity) const
  {
    if constexpr (sizeof...(Type) == 1)
    {
      return ConstRegistry()->get<Type...>(entity);
    }
    else
    {
      return std::forward_as_tuple(get<Type>(entity)...);
    }
  }

  template<typename... Type>
  [[nodiscard]] decltype(auto) get(entt::entity entity)
  {
    if constexpr (sizeof...(Type) == 1)
    {
      MarkIfNotConst<Type...>(entity, ActionType::Modify);
      return registry_->get<Type...>(entity);
    }
    else
    {
      return std::forward_as_tuple(get<Type>(entity)...);
    }
  }

  template<typename... Types, typename... Exclude>
  [[nodiscard]] auto view(entt::exclude_t<Exclude...> exclude = entt::exclude_t{}) const
  {
    return ConstRegistry()->view<Types...>(exclude);
  }

  template<typename... Types, typename... Exclude>
  [[nodiscard]] auto view(entt::exclude_t<Exclude...> exclude = entt::exclude_t{})
  {
    auto v = registry_->view<Types...>(exclude);
    for (auto entity : v)
    {
      (MarkIfNotConst<Types>(entity, ActionType::Modify), ...);
    }
    return v;
  }

  template<typename... Type>
  [[nodiscard]] bool all_of(entt::entity entity) const
  {
    return registry_->all_of<Type...>(entity);
  }

  template<typename... Type>
  [[nodiscard]] bool any_of(entt::entity entity) const
  {
    return registry_->any_of<Type...>(entity);
  }

  template<typename Type, typename... Args>
  decltype(auto) emplace(entt::entity entity, Args&&... args)
  {
    modifiedComponents_[entt::type_id<Type>().hash()][entity] |= ActionType::Add;
    return registry_->emplace<Type>(entity, std::forward<Args>(args)...);
  }

  template<typename Type, typename... Args>
  decltype(auto) emplace_or_replace(entt::entity entity, Args&&... args)
  {
    modifiedComponents_[entt::type_id<Type>().hash()][entity] |= ActionType::Add | ActionType::Modify;
    return registry_->emplace_or_replace<Type, Args...>(entity, std::forward<Args>(args)...);
  }

  template<typename Type, typename... Args>
  [[nodiscard]] decltype(auto) get_or_emplace(const entt::entity entity, Args&&... args)
  {
    modifiedComponents_[entt::type_id<Type>().hash()][entity] |= ActionType::Add | ActionType::Modify;
    return registry_->get_or_emplace<Type>(entity, std::forward<Args>(args)...);
  }

  template<typename... Types>
  auto remove(const entt::entity entity)
  {
    ((modifiedComponents_[entt::type_id<Types>().hash()][entity] |= ActionType::Remove), ...);
    return registry_->remove<Types...>(entity);
  }

  [[nodiscard]] auto valid(entt::entity entity) const
  {
    return registry_->valid(entity);
  }

  auto destroy(entt::entity entity)
  {
    return registry_->destroy(entity);
  }

  const auto& GetModifiedComponents() const
  {
    return modifiedComponents_;
  }

  void ClearModifiedComponents()
  {
    for (auto& [id, map] : modifiedComponents_)
    {
      map.clear();
    }
  }

private:
  entt::registry* registry_{};
  class World* world_{};
  std::unordered_map<entt::id_type, std::unordered_map<entt::entity, ActionType>> modifiedComponents_;

  const entt::registry* ConstRegistry() const
  {
    return registry_;
  }

  template<typename T>
  void MarkIfNotConst(entt::entity entity, ActionType actionType)
  {
    if constexpr (!std::is_const_v<T>)
    {
      modifiedComponents_[entt::type_id<T>().hash()][entity] |= actionType;
    }
  }
};