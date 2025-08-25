#include "Reflection.h"

#include "Assert2.h"
#include "Serialization.h"
#include "Game/TwoLevelGrid.h"
#include "Game/World.h"
#include "Game/Game.h"
#include "Game/Physics/Physics.h"
#include "Game/Networking/Client.h"
#include "Game/Item.h"
#include "Game/Block.h"
#include "shaders/Light.h.glsl" // "TEMP"
#include "Game/Pathfinding.h"
#include "Game/Scripting.h"

#include "angelscript.h"
#include "imgui.h"
#include "entt/meta/container.hpp"
#include "entt/meta/meta.hpp"
#include "entt/meta/factory.hpp"
#include "entt/meta/template.hpp"
#include "entt/meta/pointer.hpp"
#include "entt/core/hashed_string.hpp"
#include "spdlog/spdlog.h"
#include "IconsFontAwesome6.h"

#include "Jolt/Physics/Body/AllowedDOFs.h"
#include "Jolt/Physics/Body/MotionQuality.h"
#include "Jolt/Physics/Body/MotionType.h"

#include "tracy/Tracy.hpp"

#include <type_traits>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <string>

// ADL and specializations
namespace entt
{
  template<>
  struct entt::type_name<std::string> final
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

namespace // type traits 2
{
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

namespace
{
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
  decltype(auto) AppendToPropertiesMap(entt::id_type id, U any)
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
}

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
}

namespace Core::Reflection
{
  using namespace entt::literals;

  template<typename Scalar>
  consteval ImGuiDataType ScalarToImGuiDataType()
  {
    if constexpr (std::is_same_v<Scalar, int8_t>)
    {
      return ImGuiDataType_S8;
    }
    if constexpr (std::is_same_v<Scalar, uint8_t>)
    {
      return ImGuiDataType_U8;
    }
    if constexpr (std::is_same_v<Scalar, int16_t>)
    {
      return ImGuiDataType_S16;
    }
    if constexpr (std::is_same_v<Scalar, uint16_t>)
    {
      return ImGuiDataType_U16;
    }
    if constexpr (std::is_same_v<Scalar, int32_t>)
    {
      return ImGuiDataType_S32;
    }
    if constexpr (std::is_same_v<Scalar, uint32_t>)
    {
      return ImGuiDataType_U32;
    }
    if constexpr (std::is_same_v<Scalar, int64_t>)
    {
      return ImGuiDataType_S64;
    }
    if constexpr (std::is_same_v<Scalar, uint64_t>)
    {
      return ImGuiDataType_U64;
    }
    if constexpr (std::is_same_v<Scalar, float>)
    {
      return ImGuiDataType_Float;
    }
    if constexpr (std::is_same_v<Scalar, double>)
    {
      return ImGuiDataType_Double;
    }

    throw "Error: unsupported type";
  }

  template<typename Scalar>
  consteval const char* ScalarToFormatString()
  {
    if constexpr (std::is_same_v<Scalar, bool>)
    {
      return "%d";
    }
    if constexpr (std::is_same_v<Scalar, int8_t>)
    {
      return "%d";
    }
    if constexpr (std::is_same_v<Scalar, uint8_t>)
    {
      return "%u";
    }
    if constexpr (std::is_same_v<Scalar, int16_t>)
    {
      return "%d";
    }
    if constexpr (std::is_same_v<Scalar, uint16_t>)
    {
      return "%u";
    }
    if constexpr (std::is_same_v<Scalar, int32_t>)
    {
      return "%d";
    }
    if constexpr (std::is_same_v<Scalar, uint32_t>)
    {
      return "%u";
    }
    if constexpr (std::is_same_v<Scalar, int64_t>)
    {
      return "%lld";
    }
    if constexpr (std::is_same_v<Scalar, uint64_t>)
    {
      return "%llu";
    }
    if constexpr (std::is_same_v<Scalar, float>)
    {
      return "%.3f";
    }
    if constexpr (std::is_same_v<Scalar, double>)
    {
      return "%.3f";
    }

    throw "Error: unsupported type";
  }

  static void GetEditorName(const char*& label, const PropertiesMap& properties)
  {
    if (auto it = properties.find("name"_hs); it != properties.end())
    {
      label = *it->second.try_cast<const char*>();
    }
  }

  template<typename Scalar>
  static void InitEditorScalarParams(const PropertiesMap& properties, const char*& label, Scalar& min, Scalar& max, Scalar& speed)
  {
    GetEditorName(label, properties);
    if (auto it = properties.find("min"_hs); it != properties.end())
    {
      min = *it->second.try_cast<Scalar>();
    }
    if (auto it = properties.find("max"_hs); it != properties.end())
    {
      max = *it->second.try_cast<Scalar>();
    }
    if (auto it = properties.find("speed"_hs); it != properties.end())
    {
      speed = *it->second.try_cast<Scalar>();
    }
  }

  template<typename Scalar>
  static bool EditorWriteScalar(Scalar& f, const PropertiesMap& properties)
  {
    const char* label = "float";
    Scalar min        = 0;
    Scalar max        = 1;
    Scalar speed      = 0;

    InitEditorScalarParams(properties, label, min, max, speed);

    if constexpr (std::is_same_v<Scalar, bool>)
    {
      return ImGui::Checkbox(label, &f);
    }
    else
    {
      if (speed <= 0)
      {
        return ImGui::SliderScalar(label, ScalarToImGuiDataType<Scalar>(), &f, &min, &max, ScalarToFormatString<Scalar>(), 0);
      }
      else
      {
        return ImGui::DragScalar(label, ScalarToImGuiDataType<Scalar>(), &f, static_cast<float>(speed), &min, &max, ScalarToFormatString<Scalar>(), 0);
      }
    }
  }

  static bool EditorWriteVec3(glm::vec3& v, const PropertiesMap& properties)
  {
    const char* label = "vec3";
    float min         = 0;
    float max         = 1;
    float speed       = 0;

    InitEditorScalarParams(properties, label, min, max, speed);

    if (speed <= 0)
    {
      return ImGui::SliderFloat3(label, &v[0], min, max);
    }
    else
    {
      return ImGui::DragFloat3(label, &v[0], speed);
    }
  }

  static bool EditorWriteQuat(glm::quat& q, const PropertiesMap& properties)
  {
    const char* label = "quat";
    float min         = -180;
    float max         = 180;
    float speed       = 0;

    InitEditorScalarParams(properties, label, min, max, speed);

    auto euler = glm::degrees(glm::eulerAngles(q));

    bool changed = false;
    if (speed <= 0)
    {
      changed = ImGui::SliderFloat3(label, &euler[0], min, max);
    }
    else
    {
      changed = ImGui::DragFloat3(label, &euler[0], speed);
    }

    if (changed)
    {
      q = glm::quat(glm::radians(euler));
    }

    return changed;
  }

  template<typename Scalar>
  static void EditorReadScalar(Scalar s, const PropertiesMap& properties)
  {
    const char* label = "scalar";
    GetEditorName(label, properties);
    ImGui::Text((std::string("%s: ") + ScalarToFormatString<Scalar>()).c_str(), label, s);
  }

  static void EditorReadVec3(glm::vec3 v, const PropertiesMap& properties)
  {
    const char* label = "vec3";
    GetEditorName(label, properties);
    ImGui::Text("%s: %f, %f, %f", label, v.x, v.y, v.z);
  }

  static void EditorReadQuat(glm::quat q, const PropertiesMap& properties)
  {
    const char* label = "quat";
    GetEditorName(label, properties);
    ImGui::Text("%s: %f, %f, %f, %f", label, q.w, q.x, q.y, q.z);
  }

  static bool EditorWriteString(std::string& s, const PropertiesMap& properties)
  {
    const char* label = "string";
    GetEditorName(label, properties);
    constexpr size_t bufferSize = 256;
    char buffer[bufferSize]{};
    s.copy(buffer, bufferSize);
    if (ImGui::InputText(label, buffer, bufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
    {
      s.assign(buffer, std::strlen(buffer));
      return true;
    }
    return false;
  }

  static void EditorReadString(const std::string& s, const PropertiesMap& properties)
  {
    const char* label = "string";
    GetEditorName(label, properties);
    ImGui::Text("%.*s", static_cast<int>(s.size()), s.c_str());
  }

  static bool EditorWriteEntity(entt::entity& entity, const PropertiesMap& properties)
  {
    const char* label = "entity";
    GetEditorName(label, properties);
    using T = std::underlying_type_t<entt::entity>;
    auto temp = entity;
    if (ImGui::InputScalar(label, ScalarToImGuiDataType<T>(), &temp, nullptr, nullptr, ScalarToFormatString<T>(), ImGuiInputTextFlags_EnterReturnsTrue))
    {
      // TODO: Validate new entity ID with registry.
      entity = temp;
      return true;
    }
    return false;
  }

  static void EditorReadEntity(entt::entity entity, const PropertiesMap& properties)
  {
    const char* label = "entity";
    GetEditorName(label, properties);
    using T = std::underlying_type_t<entt::entity>;
    if (entity == entt::null)
    {
      ImGui::Text("%s: null", label);
    }
    else
    {
      ImGui::Text("%s: %u, v%u", label, (uint32_t)entt::to_entity(entity), (uint32_t)entt::to_version(entity));
    }
  }

  // Tentative, unsure if something like this is necessary.
  static void EditorUpdateTransform(World& world, entt::entity entity)
  {
    world.UpdateLocalTransform(entity);
  }

  static void EditorUpdateLinearPath(World& world, entt::entity entity)
  {
    // Path component treats 0 as the "reset transform value", and we don't want to trigger that.
    auto& path = world.GetRegistry().get<LinearPath>(entity);
    if (path.secondsElapsed == 0)
    {
      path.secondsElapsed = 1e-5f;
    }
  }
} // namespace Core::Reflection

const char* Core::Reflection::EnumToString(entt::meta_any value)
{
  ASSERT(value.type());
  ASSERT(value.type().is_enum());

  for (auto [id, data] : value.type().data())
  {
    PropertiesMap dataProps = {};
    if (auto* mp = static_cast<const PropertiesMap*>(data.custom()))
    {
      dataProps = *mp;
    }

    if (auto it = dataProps.find("display_name"_hs); it != dataProps.end())
    {
      auto name = it->second.cast<const char*>();
      if (value == data.get({}))
      {
        return name;
      }
    }

    if (auto it = dataProps.find("name"_hs); it != dataProps.end())
    {
      auto name = it->second.cast<const char*>();
      if (value == data.get({}))
      {
        return name;
      }
    }
  }

  return "Unknown enumerator";
}

const char* Core::Reflection::EnumToRawName(entt::meta_any value)
{
  ASSERT(value.type());
  ASSERT(value.type().is_enum());

  for (auto [id, data] : value.type().data())
  {
    PropertiesMap dataProps = {};
    if (auto* mp = static_cast<const PropertiesMap*>(data.custom()))
    {
      dataProps = *mp;
    }

    if (auto it = dataProps.find("name"_hs); it != dataProps.end())
    {
      auto name = it->second.cast<const char*>();
      if (value == data.get({}))
      {
        return name;
      }
    }
  }

  return "Unknown enumerator";
}

const char* Core::Reflection::EnumToIcon(entt::meta_any value)
{
  ASSERT(value.type());
  ASSERT(value.type().is_enum());

  for (auto [id, data] : value.type().data())
  {
    PropertiesMap dataProps = {};
    if (auto* mp = static_cast<const PropertiesMap*>(data.custom()))
    {
      dataProps = *mp;
    }

    if (auto it = dataProps.find("icon"_hs); it != dataProps.end())
    {
      auto name = it->second.cast<const char*>();
      if (value == data.get({}))
      {
        return name;
      }
    }
  }

  return "Unknown enumerator";
}

template<typename T>
static std::string GetName()
{
  if (auto meta = entt::resolve<T>())
  {
    if (const auto* props = static_cast<const Core::Reflection::PropertiesMap*>(meta.custom()))
    {
      if (auto it = props->find("name"_hs); it != props->end())
      {
        return it->second.cast<const char*>();
      }
    }
  }

  return std::string(entt::type_name<T>::value());
}

void Core::Reflection::Initialize(Scripting& scripting)
{
  ZoneScoped;
  spdlog::info("Initializing type reflection.");
  entt::meta_reset();
  
  auto& asEngine  = scripting.GetEngine();

  ASSERT(asEngine.RegisterObjectType("World", 0, asOBJ_REF | asOBJ_NOCOUNT) >= 0);
  entt::meta_factory<World>{}.func<[](void* ctx, int argIdx, World& v) { ((asIScriptContext*)ctx)->SetArgObject(argIdx, &v); }>("ASSetArg"_hs);
  entt::meta_factory<World*>{}.func<[](void* ctx, int argIdx, World* v) { ((asIScriptContext*)ctx)->SetArgObject(argIdx, v); }>("ASSetArg"_hs);
  ASSERT(asEngine.RegisterObjectType("entity", sizeof(entt::entity), asOBJ_VALUE | asOBJ_POD | asOBJ_APP_PRIMITIVE) >= 0);
  entt::meta_factory<entt::entity>{}.func<[](void* ctx, int argIdx, entt::entity v) { ((asIScriptContext*)ctx)->SetArgObject(argIdx, &v); }>("ASSetArg"_hs);
  ASSERT(asEngine.RegisterObjectType("vec3", sizeof(glm::vec3), asOBJ_VALUE | asOBJ_POD) >= 0);
  entt::meta_factory<glm::vec3>{}.func<[](void* ctx, int argIdx, glm::vec3 v) { ((asIScriptContext*)ctx)->SetArgObject(argIdx, &v); }>("ASSetArg"_hs);
  ASSERT(asEngine.RegisterObjectType("ivec3", sizeof(glm::ivec3), asOBJ_VALUE | asOBJ_POD) >= 0);
  entt::meta_factory<glm::ivec3>{}.func<[](void* ctx, int argIdx, glm::ivec3 v) { ((asIScriptContext*)ctx)->SetArgObject(argIdx, &v); }>("ASSetArg"_hs);
  ASSERT(asEngine.RegisterObjectType("ivec2", sizeof(glm::ivec3), asOBJ_VALUE | asOBJ_POD) >= 0);
  entt::meta_factory<glm::ivec2>{}.func<[](void* ctx, int argIdx, glm::ivec2 v) { ((asIScriptContext*)ctx)->SetArgObject(argIdx, &v); }>("ASSetArg"_hs);
  ASSERT(asEngine.RegisterObjectType("quat", sizeof(glm::quat), asOBJ_VALUE | asOBJ_POD) >= 0);
  entt::meta_factory<glm::quat>{}.func<[](void* ctx, int argIdx, glm::quat v) { ((asIScriptContext*)ctx)->SetArgObject(argIdx, &v); }>("ASSetArg"_hs);
  ASSERT(asEngine.RegisterTypedef("short", "uint16") >= 0);

#define MAKE_IDENTIFIER() CONCAT(factory_, __LINE__)
#define MAKE_IDENTIFIER2(name) CONCAT(name, __LINE__)
#define CONCAT(x, y) CONCAT_INDIRECT(x, y)
#define CONCAT_INDIRECT(x, y) x ## y

#define REFLECT_TYPE(T)    \
  REGISTER_OBJECT_TYPE(T); \
  auto MAKE_IDENTIFIER() = entt::meta_factory<T>{}.custom<PropertiesMap>(PropertiesMap{{"name"_hs, #T}})

#define REFLECT_COMPONENT_NO_DEFAULT(T, ...)                                                        \
  REGISTER_OBJECT_TYPE(T);                                                                          \
  __VA_OPT__(static_assert(!((__VA_ARGS__) & Traits::TRIVIAL) || std::is_trivially_copyable_v<T>);) \
  [[maybe_unused]] auto MAKE_IDENTIFIER() = entt::meta_factory<T>{}.traits(COMPONENT __VA_OPT__(| __VA_ARGS__))

#define REFLECT_COMPONENT(T, ...)       \
  REGISTER_OBJECT_TYPE(T);              \
  REGISTER_COMPONENT_REGISTRY_FUNCS(T); \
  REFLECT_COMPONENT_BASE(T __VA_OPT__(, __VA_ARGS__))

#define REFLECT_COMPONENT_BASE(T, ...)                                                                                                                     \
  __VA_OPT__(static_assert(!((__VA_ARGS__) & Traits::TRIVIAL) || std::is_trivially_copyable_v<T>);)                                                        \
  [[maybe_unused]] auto MAKE_IDENTIFIER() =                                                                                                                \
    entt::meta_factory<T>{}                                                                                                                                \
      .traits(COMPONENT | (std::is_empty_v<T> ? EMPTY : Traits(0)) | (is_optional_v<T> ? OPTIONAL : Traits(0)) |                                           \
              (is_variant_v<T> ? VARIANT : Traits(0))__VA_OPT__(| __VA_ARGS__))                                                                            \
      .func<[](entt::registry* registry, entt::entity entity) { registry->emplace<T>(entity); }>("EmplaceDefault"_hs)                                      \
      .func<[](entt::registry* registry, entt::entity entity, T& value) { registry->emplace_or_replace<T>(entity, std::move(value)); }>("EmplaceMove"_hs); \
  MAKE_IDENTIFIER()

#define TRAITS(TraitsV) .traits(TraitsV)

#define REGISTER_ENUM(Enum) ASSERT(asEngine.RegisterEnum(RemoveNamespaces(#Enum).c_str()) >= 0)
#define REGISTER_ENUM_VALUE(Enum, Value) ASSERT(asEngine.RegisterEnumValue(RemoveNamespaces(#Enum).c_str(), RemoveNamespaces(#Value).c_str(), static_cast<int>(Enum::Value)) >= 0)

#define REGISTER_OBJECT_TYPE(Type)                                                                                                                                   \
  /*if constexpr (!is_variant_v<Type>)*/                                                                                                                             \
  {                                                                                                                                                                  \
    entt::meta_factory<Type>{}.func<[](void* ctx, int argIdx, Type v) { ((asIScriptContext*)ctx)->SetArgObject(argIdx, &v); }>("ASSetArg"_hs);                                \
    const auto MAKE_IDENTIFIER2(name) = RemoveNamespaces(#Type);                                                                                                     \
    ASSERT(asEngine.RegisterObjectType(MAKE_IDENTIFIER2(name).c_str(),                                                                                               \
             sizeof(Type),                                                                                                                                           \
             asOBJ_VALUE | asGetTypeTraits<Type>() | (alignof(Type) == 8 ? asOBJ_APP_CLASS_ALIGN8 : 0)) >= 0);                                                         \
    ASSERT(                                                                                                                                                          \
      asEngine.RegisterObjectBehaviour(MAKE_IDENTIFIER2(name).c_str(), asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(std::construct_at<Type>), asCALL_CDECL_OBJLAST) >= \
      0);                                                                                                                                                            \
    ASSERT(                                                                                                                                                          \
      asEngine.RegisterObjectBehaviour(MAKE_IDENTIFIER2(name).c_str(), asBEHAVE_DESTRUCT, "void f()", asFUNCTION(std::destroy_at<Type>), asCALL_CDECL_OBJLAST) >=    \
      0);                                                                                                                                                            \
    ASSERT(asEngine.RegisterObjectMethod(MAKE_IDENTIFIER2(name).c_str(),                                                                                             \
             (MAKE_IDENTIFIER2(name) + "& opAssign(" + MAKE_IDENTIFIER2(name) + "& in)").c_str(),                                                                    \
             asMETHODPR(Type, operator=, (const Type&), Type&),                                                                                                      \
             asCALL_THISCALL) >= 0);                                                                                                                                 \
  }

#define REGISTER_OBJECT_PROPERTIES(Type, Member)                                        \
  ASSERT(asEngine.RegisterObjectProperty(RemoveNamespaces(#Type).c_str(),               \
           (RemoveNamespaces(GetName<decltype(Type::Member)>()) + " " #Member).c_str(), \
           asOFFSET(Type, Member)) >= 0)

  // Registers the following functions:
  // registry::has (alias for single-component registry::all)
  // registry::get (wrapper for get() that throws if not exists)
  // registry::set (wrapper for emplace_or_replace())
#define REGISTER_COMPONENT_REGISTRY_FUNCS(Type)                                                                                                                   \
  {                                                                                                                                                               \
    [&asEngine]<typename T>                                                                                                                                       \
    {                                                                                                                                                             \
      const auto MAKE_IDENTIFIER2(name2) = RemoveNamespaces(#Type);                                                                                               \
      /* e.g. Velocity GetVelocity(World& in, entity)*/                                                                                                           \
      if constexpr (!std::is_empty_v<Type>)                                                                                                                       \
      {                                                                                                                                                           \
        ASSERT(asEngine.RegisterObjectMethod("World",                                                                                                             \
                 (MAKE_IDENTIFIER2(name2) + " Get" + MAKE_IDENTIFIER2(name2) + "(entity)").c_str(),                                                               \
                 asFUNCTION(WorldGet<Type>),                                                                                                                      \
                 asCALL_CDECL_OBJFIRST) >= 0);                                                                                                                    \
        ASSERT(asEngine.RegisterObjectMethod("World",                                                                                                             \
                 ("void Set" + MAKE_IDENTIFIER2(name2) + "(entity, " + MAKE_IDENTIFIER2(name2) + ")").c_str(),                                                    \
                 asFUNCTION(WorldSet<Type>),                                                                                                                      \
                 asCALL_CDECL_OBJFIRST) >= 0);                                                                                                                    \
      }                                                                                                                                                           \
      ASSERT(                                                                                                                                                     \
        asEngine.RegisterObjectMethod("World", ("bool Has" + MAKE_IDENTIFIER2(name2) + "(entity)").c_str(), asFUNCTION(WorldHas<Type>), asCALL_CDECL_OBJFIRST) >= \
        0);                                                                                                                                                       \
    }.operator()<Type>();                                                                                                                                         \
  }

#define DATA(Type, Member, ...)                                                                                                                              \
  ;                                                                                                                                                          \
  REGISTER_OBJECT_PROPERTIES(Type, Member); \
  DATA_BASE(Type, Member __VA_OPT__(, __VA_ARGS__))

#define DATA_BASE(Type, Member, ...);                                                                                                                          \
  auto MAKE_IDENTIFIER() = entt::meta_factory<Type> {};                                                                                                      \
  MAKE_IDENTIFIER() \
  .data<&Type ::Member, entt::as_ref_t>(#Member##_hs) \
  .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Member} __VA_OPT__(, __VA_ARGS__)}); \
  entt::meta_factory<decltype(Type::Member)>{}.traits(                                                                                                       \
    (is_optional_v<decltype(Type::Member)> ? OPTIONAL : Traits(0)) | (is_variant_v<decltype(Type::Member)> ? VARIANT : Traits(0)));                          \
  []<typename U>()                                                                                                                                           \
  {                                                                                                                                                          \
    if constexpr (is_variant_v<U>)                                                                                                                           \
    {                                                                                                                                                        \
      /*entt::meta_factory<U>{} VARIANT_FUNCS(U);*/                                                                                                          \
    }                                                                                                                                                        \
  }.operator()<decltype(Type::Member)>();                                                                                                                    \
  []<typename U>()                                                                                                                                           \
  {                                                                                                                                                          \
    if constexpr (is_optional_v<U>)                                                                                                                          \
    {                                                                                                                                                        \
      entt::meta_factory<U>{}.template ctor<typename U::value_type>() OPTIONAL_FUNCS(U);                                                                     \
    }                                                                                                                                                        \
  }.operator()<decltype(Type::Member)>();                                                                                                                    \
  MAKE_IDENTIFIER()

#define PROP_SPEED(Scalar) {"speed"_hs, Scalar}
#define PROP_MIN(Scalar) {"min"_hs, Scalar}
#define PROP_MAX(Scalar) {"max"_hs, Scalar}
#define PROP_DISPLAY_NAME(Name) {"display_name"_hs, Name}
#define PROP_ICON(Icon) {"icon"_hs, Icon}
#define REFLECT_ENUM(T) REGISTER_ENUM(T);\
  entt::meta_factory<T>{}\
  .func<[](T value) { return static_cast<std::underlying_type_t<T>>(value); }>("to_underlying"_hs)

#define ENUMERATOR(E, Member, ...) \
  ;                                \
  REGISTER_ENUM_VALUE(E, Member); \
  entt::meta_factory<E>{}.data<E :: Member>(#Member##_hs) \
  .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Member} __VA_OPT__(, __VA_ARGS__)})

#define VARIANT_FUNCS(T)                                                               \
  ;                                                                                    \
  ForEachVariantAlternative<T>(                                                        \
    []<typename V>                                                                     \
    {                                                                                  \
      entt::meta_factory<T>{}.ctor<V>();                                               \
      auto& vec = AppendToPropertiesMap<T>("alternatives"_hs, std::vector<entt::id_type>());  \
      vec.push_back(entt::type_hash<V>::value());                                      \
    });                                                                                \
  entt::meta_factory<T>{}                                                              \
    .traits<Traits>(VARIANT)                                                           \
    .template func<[](const T& ps)                                                     \
      {                                                                                \
        auto info = entt::id_type();                                                   \
        std::visit([&](auto&& x) { info = entt::type_id<decltype(x)>().hash(); }, ps); \
        return info;                                                                   \
      }>("type_hash"_hs)                                                               \
    .template func<[](const T& ps)                                                     \
      {                                                                                \
        auto value = entt::meta_any();                                                 \
        std::visit([&](auto&& x) { value = entt::forward_as_meta(x); }, ps);           \
        return value;                                                                  \
      }>("const_value"_hs)                                                             \
    .template func<[](T& ps)                                                           \
      {                                                                                \
        auto value = entt::meta_any();                                                 \
        std::visit([&](auto&& x) { value = entt::forward_as_meta(x); }, ps);           \
        return value;                                                                  \
      }>("value"_hs)

#define PTR_FUNCS(T)                                                            \
  .template func<[]() { return new T(); }>("make_raw_ptr"_hs)                            \
  .template func<[]() { return std::make_unique<T>(); }>("make_unique_ptr"_hs)           \
  .template func<[]() { return std::make_shared<T>(); }>("make_shared_ptr"_hs)           \
  .template func<[](T*& p, T* v) { p = v; }>("reset_raw_ptr"_hs)                         \
  .template func<[](std::unique_ptr<T>& p, T* v) { p.reset(v); }>("reset_unique_ptr"_hs) \
  .template func<[](std::shared_ptr<T>& p, T* v) { p.reset(v); }>("reset_shared_ptr"_hs) \

#define OPTIONAL_FUNCS(T)                                                   \
  .template func<[](const T& option) { return option.has_value(); }>("has_value"_hs) \
  .template func<[](T& option) { return option.emplace(); }>("emplace"_hs)           \
  .template func<[](T& option) { option.reset(); }>("reset"_hs)                      \
  .template func<[](T& option) -> decltype(auto) { return option.value(); }>("value"_hs)

  #define REGISTER_RPC(Function, Traits) \
    entt::meta_factory<RpcTraits>().func<Function>(#Function##_hs) \
    .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Function}}) \
    .traits<RpcTraits>(Traits)

  entt::meta_factory<int>()
    .func<&EditorWriteScalar<int>>("EditorWrite"_hs)
    .func<&EditorReadScalar<int>>("EditorRead"_hs)
    .traits(TRIVIAL)
    .func<[](void* ctx, int argIdx, int v) { ((asIScriptContext*)ctx)->SetArgDWord(argIdx, std::bit_cast<asDWORD>(v)); }>("ASSetArg"_hs);
  entt::meta_factory<uint32_t>()
    .func<&EditorWriteScalar<uint32_t>>("EditorWrite"_hs)
    .func<&EditorReadScalar<uint32_t>>("EditorRead"_hs)
    .traits(TRIVIAL)
    .func<[](void* ctx, int argIdx, uint32_t v) { ((asIScriptContext*)ctx)->SetArgDWord(argIdx, std::bit_cast<asDWORD>(v)); }>("ASSetArg"_hs);
  entt::meta_factory<uint16_t>()
    .func<&EditorWriteScalar<uint16_t>>("EditorWrite"_hs)
    .func<&EditorReadScalar<uint16_t>>("EditorRead"_hs)
    .traits(TRIVIAL)
    .func<[](void* ctx, int argIdx, uint16_t v) { ((asIScriptContext*)ctx)->SetArgWord(argIdx, std::bit_cast<asWORD>(v)); }>("ASSetArg"_hs);
  entt::meta_factory<uint8_t>()
    .func<&EditorWriteScalar<uint8_t>>("EditorWrite"_hs)
    .func<&EditorReadScalar<uint8_t>>("EditorRead"_hs)
    .traits(TRIVIAL)
    .func<[](void* ctx, int argIdx, uint8_t v) { ((asIScriptContext*)ctx)->SetArgByte(argIdx, std::bit_cast<asBYTE>(v)); }>("ASSetArg"_hs);
  entt::meta_factory<float>()
    .func<&EditorWriteScalar<float>>("EditorWrite"_hs)
    .func<&EditorReadScalar<float>>("EditorRead"_hs)
    .traits(TRIVIAL)
    .func<[](void* ctx, int argIdx, float v) { ((asIScriptContext*)ctx)->SetArgFloat(argIdx, v); }>("ASSetArg"_hs);
  entt::meta_factory<glm::vec3>()
    .func<&EditorWriteVec3>("EditorWrite"_hs)
    .func<&EditorReadVec3>("EditorRead"_hs)
    TRAITS(TRIVIAL)
    DATA(glm::vec3, x)
    DATA(glm::vec3, y)
    DATA(glm::vec3, z);
  entt::meta_factory<glm::ivec3>()
    TRAITS(TRIVIAL)
    DATA(glm::ivec3, x)
    DATA(glm::ivec3, y)
    DATA(glm::ivec3, z);
  entt::meta_factory<glm::ivec2>()
    TRAITS(TRIVIAL)
    DATA(glm::ivec2, x)
    DATA(glm::ivec2, y);
  entt::meta_factory<glm::quat>()
    .func<&EditorWriteQuat>("EditorWrite"_hs)
    .func<&EditorReadQuat>("EditorRead"_hs)
    TRAITS(TRIVIAL)
    DATA(glm::quat, w)
    DATA(glm::quat, x)
    DATA(glm::quat, y)
    DATA(glm::quat, z);
  AppendToPropertiesMap<glm::vec3>("name"_hs, "glm::vec3");
  AppendToPropertiesMap<glm::ivec3>("name"_hs, "glm::ivec3");
  AppendToPropertiesMap<glm::ivec2>("name"_hs, "glm::ivec2");
  AppendToPropertiesMap<glm::quat>("name"_hs, "glm::quat");
  AppendToPropertiesMap<std::string>("name"_hs, "std::string");
  entt::meta_factory<std::string>().func<&EditorWriteString>("EditorWrite"_hs).func<&EditorReadString>("EditorRead"_hs);
  entt::meta_factory<bool>().func<&EditorWriteScalar<bool>>("EditorWrite"_hs).func<&EditorReadScalar<bool>>("EditorRead"_hs).traits(TRIVIAL);
  entt::meta_factory<entt::entity>().func<&EditorWriteEntity>("EditorWrite"_hs).func<&EditorReadEntity>("EditorRead"_hs); // NOT trivial, because they need to be remapped for networking.

  REFLECT_COMPONENT_NO_DEFAULT(LocalTransform, REPLICATED | TRIVIAL)
    .func<&EditorUpdateTransform>("OnUpdate"_hs)
    .func<[](entt::registry* registry, entt::entity entity) { registry->emplace<LocalTransform>(entity); }>("EmplaceDefault"_hs)
    .func<[](entt::registry* registry, entt::entity entity, LocalTransform& transform)
      {
        registry->emplace_or_replace<LocalTransform>(entity, std::move(transform));
        registry->emplace_or_replace<NetworkNeedUpdateLocalTransform>(entity);
      }>("EmplaceMove"_hs)
    DATA(LocalTransform, position, PROP_SPEED(0.20f))
    DATA(LocalTransform, rotation)
    DATA(LocalTransform, scale, PROP_SPEED(0.0125f));

  REFLECT_COMPONENT(NetworkNeedUpdateLocalTransform, NO_EDITOR);
  
  REFLECT_COMPONENT(GlobalTransform, EDITOR_READ_ONLY | REPLICATED | TRIVIAL)
    //.func<[](entt::registry* registry, entt::entity entity) { registry->emplace<GlobalTransform>(entity); }>("EmplaceDefault"_hs)
    //.func<[](entt::registry* registry, entt::entity entity, GlobalTransform& transform)
    //  {
    //    if (auto* prevTransform = registry->try_get<const GlobalTransform>(entity))
    //    {
    //      registry->emplace_or_replace<PreviousGlobalTransform>(entity, prevTransform->position, prevTransform->rotation, prevTransform->scale, false);
    //    }
    //    registry->emplace_or_replace<GlobalTransform>(entity, std::move(transform));
    //  }>("EmplaceMove"_hs)
    DATA(GlobalTransform, position)
    DATA(GlobalTransform, rotation)
    DATA(GlobalTransform, scale);

  REFLECT_COMPONENT(PreviousGlobalTransform, EDITOR_READ_ONLY | REPLICATED | TRIVIAL | TRANSIENT)
    DATA(PreviousGlobalTransform, position)
    DATA(PreviousGlobalTransform, rotation)
    DATA(PreviousGlobalTransform, scale);

  REFLECT_COMPONENT(RenderTransform, EDITOR_READ_ONLY | REPLICATED | TRIVIAL | TRANSIENT)
    DATA(RenderTransform, transform);

  REFLECT_COMPONENT(Health, REPLICATED | TRIVIAL)
    DATA(Health, hp, PROP_MIN(0.0f), PROP_MAX(100.0f))
    DATA(Health, maxHp, PROP_MIN(0.0f), PROP_MAX(100.0f));

  REFLECT_COMPONENT(ContactDamage)
    DATA(ContactDamage, damage, PROP_MIN(0.125f), PROP_MAX(100.0f))
    DATA(ContactDamage, knockback, PROP_MIN(0.125f), PROP_MAX(100.0f));

  REFLECT_COMPONENT(LinearVelocity, TRIVIAL)
    DATA(LinearVelocity, v, PROP_SPEED(0.0125f));

  REFLECT_COMPONENT(TeamFlags)
    DATA(TeamFlags, flags);

  REFLECT_COMPONENT(Friction)
    DATA(Friction, axes, PROP_MAX(5.0f));

  REFLECT_COMPONENT(Player, EDITOR_READ_ONLY | REPLICATED)
    DATA(Player, id)
    DATA(Player, inventoryIsOpen)
    TRAITS(TRANSIENT)
    DATA(Player, openContainerId)
    TRAITS(TRANSIENT)
    DATA(Player, showInteractPrompt)
    TRAITS(TRANSIENT);

  REFLECT_COMPONENT(InputState, EDITOR_READ_ONLY)
    DATA(InputState, strafe)
    DATA(InputState, forward)
    DATA(InputState, elevate)
    DATA(InputState, jump)
    DATA(InputState, sprint)
    DATA(InputState, walk)
    DATA(InputState, usePrimary)
    DATA(InputState, useSecondary)
    DATA(InputState, interact);

  REFLECT_COMPONENT(InputLookState, EDITOR_READ_ONLY)
    DATA(InputLookState, pitch)
    DATA(InputLookState, yaw);

  REFLECT_COMPONENT(Mesh, REPLICATED)
    DATA(Mesh, name);

  REFLECT_COMPONENT(NoclipCharacterController);

  REFLECT_COMPONENT(FlyingCharacterController)
    .func<[](World* w, entt::entity e) { w->GivePlayerFlyingCharacterController(e); }>("add"_hs)
    DATA(FlyingCharacterController, maxSpeed, PROP_MAX(50.0f))
    DATA(FlyingCharacterController, acceleration, PROP_MAX(50.0f));

  using namespace Physics;

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

  REFLECT_TYPE(ItemState)
    DATA(ItemState, id)
    DATA(ItemState, count)
    DATA(ItemState, useAccum);

  REFLECT_COMPONENT(DroppedItem)
    DATA(DroppedItem, item);

  REFLECT_ENUM(JPH::CharacterBase::EGroundState)
    ENUMERATOR(JPH::CharacterBase::EGroundState, OnGround)
    ENUMERATOR(JPH::CharacterBase::EGroundState, OnSteepGround)
    ENUMERATOR(JPH::CharacterBase::EGroundState, NotSupported)
    ENUMERATOR(JPH::CharacterBase::EGroundState, InAir);

  REFLECT_TYPE(ShapeSettings)
    TRAITS(EDITOR_READ_ONLY)
    DATA(ShapeSettings, shape)
    DATA(ShapeSettings, density)
    DATA(ShapeSettings, translation)
    DATA(ShapeSettings, rotation);

  REFLECT_ENUM(JPH::EMotionType)
    ENUMERATOR(JPH::EMotionType, Static)
    ENUMERATOR(JPH::EMotionType, Kinematic)
    ENUMERATOR(JPH::EMotionType, Dynamic);

  REFLECT_ENUM(JPH::EMotionQuality)
    ENUMERATOR(JPH::EMotionQuality, Discrete)
    ENUMERATOR(JPH::EMotionQuality, LinearCast);

  REFLECT_ENUM(JPH::EAllowedDOFs)
    ENUMERATOR(JPH::EAllowedDOFs, None)
    ENUMERATOR(JPH::EAllowedDOFs, All)
    ENUMERATOR(JPH::EAllowedDOFs, TranslationX)
    ENUMERATOR(JPH::EAllowedDOFs, TranslationY)
    ENUMERATOR(JPH::EAllowedDOFs, TranslationZ)
    ENUMERATOR(JPH::EAllowedDOFs, RotationX)
    ENUMERATOR(JPH::EAllowedDOFs, RotationY)
    ENUMERATOR(JPH::EAllowedDOFs, RotationZ)
    ENUMERATOR(JPH::EAllowedDOFs, Plane2D);

  REFLECT_COMPONENT(CharacterController)
    .func<[](World* w, entt::entity e) { w->GivePlayerCharacterController(e); }>("add"_hs)
    DATA(CharacterController, previousGroundState)
    TRAITS(EDITOR_READ_ONLY)
    DATA(CharacterController, previousPosition)
    TRAITS(EDITOR_READ_ONLY);
  
  REFLECT_COMPONENT(CharacterControllerShrimple)
    .func<[](World* w, entt::entity e) { w->GivePlayerCharacterControllerShrimple(e); }>("add"_hs);

  REFLECT_COMPONENT(Name, REPLICATED)
    DATA(Name, name);

  //REFLECT_COMPONENT(RigidBody);
  REFLECT_COMPONENT(CharacterControllerSettings, EDITOR_READ_ONLY | REPLICATED)
    DATA(CharacterControllerSettings, shape);
  
  REFLECT_COMPONENT(CharacterControllerShrimpleSettings, EDITOR_READ_ONLY)
    DATA(CharacterControllerShrimpleSettings, shape);

  REFLECT_COMPONENT(RigidBodySettings, EDITOR_READ_ONLY | TRIVIAL | REPLICATED)
    DATA(RigidBodySettings, shape)
    DATA(RigidBodySettings, activate)
    DATA(RigidBodySettings, isSensor)
    DATA(RigidBodySettings, gravityFactor)
    DATA(RigidBodySettings, motionType)
    DATA(RigidBodySettings, motionQuality)
    DATA(RigidBodySettings, layer)
    DATA(RigidBodySettings, degreesOfFreedom);

  REFLECT_TYPE(TwoLevelGrid::Material)
    DATA(TwoLevelGrid::Material, isVisible)
    DATA(TwoLevelGrid::Material, isSolid);

  REFLECT_COMPONENT(DeferredDelete);

  REFLECT_COMPONENT(ForwardCollisionsToParent);

  REFLECT_COMPONENT(SimpleEnemyBehavior);

  REFLECT_ENUM(PredatoryBirdBehavior::State)
    ENUMERATOR(PredatoryBirdBehavior::State, IDLE)
    ENUMERATOR(PredatoryBirdBehavior::State, CIRCLING)
    ENUMERATOR(PredatoryBirdBehavior::State, SWOOPING);

  REFLECT_COMPONENT(PredatoryBirdBehavior)
    DATA(PredatoryBirdBehavior, state)
    DATA(PredatoryBirdBehavior, accum)
    DATA(PredatoryBirdBehavior, target)
    TRAITS(EDITOR_READ_ONLY)
    DATA(PredatoryBirdBehavior, idlePosition)
    DATA(PredatoryBirdBehavior, lineOfSightDuration);

  REFLECT_COMPONENT(SimplePathfindingEnemyBehavior);

  REFLECT_COMPONENT(NoHashGrid, REPLICATED);

  REFLECT_COMPONENT(WormEnemyBehavior)
    DATA(WormEnemyBehavior, maxTurnSpeedDegPerSec);

  REFLECT_COMPONENT(LinearPath, REPLICATED)
    .func<&EditorUpdateLinearPath>("OnUpdate"_hs)
    DATA_BASE(LinearPath, frames)
    DATA(LinearPath, secondsElapsed)
    DATA(LinearPath, originalLocalTransform)
    TRAITS(NO_EDITOR);

  REFLECT_ENUM(Math::Easing)
    ENUMERATOR(Math::Easing, LINEAR)
    ENUMERATOR(Math::Easing, EASE_IN_SINE)
    ENUMERATOR(Math::Easing, EASE_OUT_SINE)
    ENUMERATOR(Math::Easing, EASE_IN_OUT_BACK)
    ENUMERATOR(Math::Easing, EASE_IN_CUBIC)
    ENUMERATOR(Math::Easing, EASE_OUT_CUBIC);

  //using LinearPath::KeyFrame;
  REFLECT_TYPE(LinearPath::KeyFrame)
    DATA(LinearPath::KeyFrame, position)
    DATA(LinearPath::KeyFrame, rotation)
    DATA(LinearPath::KeyFrame, scale)
    DATA(LinearPath::KeyFrame, offsetSeconds)
    DATA(LinearPath::KeyFrame, easing);

  REFLECT_COMPONENT(BlockHealth)
    DATA(BlockHealth, health, PROP_MIN(0.0f), PROP_MAX(100.0f));
    
  REFLECT_COMPONENT(Hierarchy, EDITOR_READ_ONLY | REPLICATED)
    DATA(Hierarchy, parent)
    DATA_BASE(Hierarchy, children)
    DATA(Hierarchy, useLocalPositionAsGlobal)
    DATA(Hierarchy, useLocalRotationAsGlobal);

  REFLECT_COMPONENT(Lifetime)
    DATA(Lifetime, remainingSeconds);

  REFLECT_COMPONENT(GhostPlayer, REPLICATED)
    DATA(GhostPlayer, remainingSeconds);

  REFLECT_COMPONENT(Invulnerability)
    DATA(Invulnerability, remainingSeconds, PROP_MAX(1000.0f));

  REFLECT_COMPONENT(CannotDamageEntities)
    DATA_BASE(CannotDamageEntities, entities);

  REFLECT_COMPONENT(Projectile)
    DATA(Projectile, initialSpeed, PROP_MAX(500.0f))
    DATA(Projectile, drag)
    DATA(Projectile, restitution);

  REFLECT_COMPONENT(Inventory, EDITOR_READ_ONLY | REPLICATED)
    DATA(Inventory, activeSlotCoord)
    DATA(Inventory, canHaveActiveItem)
    DATA(Inventory, activeSlotEntity)
    DATA_BASE(Inventory, slots)
    TRAITS(TRIVIAL);

  REFLECT_COMPONENT(Billboard, REPLICATED)
    DATA(Billboard, name);

  REFLECT_COMPONENT(GpuLight, REPLICATED | TRIVIAL)
    DATA(GpuLight, color)
    DATA(GpuLight, type)
    DATA(GpuLight, direction, PROP_MIN(-1.0f))
    DATA(GpuLight, intensity, PROP_MAX(50.0f))
    DATA(GpuLight, position)
    TRAITS(EDITOR_READ_ONLY)
    DATA(GpuLight, range, PROP_MAX(200.0f))
    DATA(GpuLight, innerConeAngle, PROP_MAX(6.28f))
    DATA(GpuLight, outerConeAngle, PROP_MAX(6.28f))
    DATA(GpuLight, colorSpace);

  REFLECT_COMPONENT(BlockEntity, REPLICATED | TRANSIENT);

  REFLECT_COMPONENT(DespawnWhenFarFromPlayer)
    DATA(DespawnWhenFarFromPlayer, maxDistance)
    DATA(DespawnWhenFarFromPlayer, gracePeriod);

  REFLECT_COMPONENT(Loot)
    DATA(Loot, name);

  REFLECT_COMPONENT(Enemy);

  REFLECT_COMPONENT(AiWanderBehavior)
    DATA(AiWanderBehavior, minWanderDistance, PROP_MAX(10.0f))
    DATA(AiWanderBehavior, maxWanderDistance, PROP_MAX(10.0f))
    DATA(AiWanderBehavior, timeBetweenMoves, PROP_MAX(10.0f))
    DATA(AiWanderBehavior, accumulator)
    DATA(AiWanderBehavior, targetCanBeFloating);

  REFLECT_COMPONENT(AiTarget)
    DATA(AiTarget, currentTarget)
    TRAITS(EDITOR_READ_ONLY);

  REFLECT_COMPONENT(AiVision)
    DATA(AiVision, coneAngleRad, PROP_MAX(glm::two_pi<float>()))
    DATA(AiVision, distance, PROP_MAX(50.0f))
    DATA(AiVision, invAcuity, PROP_MAX(5.0f))
    DATA(AiVision, accumulator);

  REFLECT_COMPONENT(AiHearing)
    DATA(AiHearing, distance, PROP_MAX(50.0f));

  REFLECT_COMPONENT(KnockbackMultiplier)
    DATA(KnockbackMultiplier, factor, PROP_MAX(10.0f));

  REFLECT_COMPONENT(Tint, REPLICATED | TRIVIAL)
    DATA(Tint, color);

  REFLECT_COMPONENT(WalkingMovementAttributes, REPLICATED | TRIVIAL)
    DATA(WalkingMovementAttributes, walkModifier)
    DATA(WalkingMovementAttributes, runMaxSpeed, PROP_MAX(20.0f))
    DATA(WalkingMovementAttributes, terminalVelocity, PROP_MIN(-100.0f), PROP_MAX(0.0f))
    DATA(WalkingMovementAttributes, gravity, PROP_MIN(-50.0f), PROP_MAX(0.0f))
    DATA(WalkingMovementAttributes, jumpInitialImpulse, PROP_MAX(25.0f))
    DATA(WalkingMovementAttributes, jumpAcceleration, PROP_MAX(100.0f))
    DATA(WalkingMovementAttributes, timeSinceJumped)
    TRAITS(EDITOR_READ_ONLY)
    DATA(WalkingMovementAttributes, jumpControlTime, PROP_MAX(1.0f))
    DATA(WalkingMovementAttributes, acceleration, PROP_MAX(100.0f))
    DATA(WalkingMovementAttributes, deceleration, PROP_MAX(100.0f))
    DATA(WalkingMovementAttributes, airAcceleration, PROP_MAX(100.0f))
    DATA(WalkingMovementAttributes, airDeceleration, PROP_MAX(100.0f));
  
  REFLECT_COMPONENT(VoxelsComponent, REPLICATED);

  // TODO: TwoLevelGrid reflection should be removed
  REFLECT_TYPE(TwoLevelGrid::TopLevelBrickPtr)
    DATA(TwoLevelGrid::TopLevelBrickPtr, voxelsDoBeAllSame)
    DATA_BASE(TwoLevelGrid::TopLevelBrickPtr, voxelIfAllSame);

  REFLECT_TYPE(TwoLevelGrid::TopLevelBrick)
    DATA_BASE(TwoLevelGrid::TopLevelBrick, bricks);
  
  REFLECT_TYPE(TwoLevelGrid::BottomLevelBrickPtr)
    DATA(TwoLevelGrid::BottomLevelBrickPtr, voxelsDoBeAllSame)
    DATA_BASE(TwoLevelGrid::BottomLevelBrickPtr, voxelIfAllSame);
  
  REFLECT_TYPE(TwoLevelGrid::BottomLevelBrick)
    DATA_BASE(TwoLevelGrid::BottomLevelBrick, occupancy)
    DATA_BASE(TwoLevelGrid::BottomLevelBrick, voxels);

  REFLECT_TYPE(TwoLevelGrid::OccupancyBitmask)
    DATA_BASE(TwoLevelGrid::OccupancyBitmask, bitmask);

  REFLECT_ENUM(voxel_t)
    ENUMERATOR(voxel_t, Air)
    ENUMERATOR(voxel_t, Null);
  
  REFLECT_COMPONENT(LocalPlayer);

  REFLECT_ENUM(Networking::ClientStatus)
    ENUMERATOR(Networking::ClientStatus, Resolving)
    ENUMERATOR(Networking::ClientStatus, Joining)
    ENUMERATOR(Networking::ClientStatus, Connected)
    ENUMERATOR(Networking::ClientStatus, Disconnected);

  entt::meta_factory<const char*>().conv<std::string>().conv<std::string_view>();
  entt::meta_factory<char*>().conv<std::string>().conv<std::string_view>();

  REFLECT_ENUM(Networking::PacketType);

  REFLECT_COMPONENT(LocalAuthoritative, NO_EDITOR);
  
  REGISTER_RPC(UpdatePlayerInput, RpcTraits::Server);
  
  REGISTER_RPC(GiveLocalPlayerRPC, RpcTraits::Client);
  entt::meta_factory<RpcTraits>().func<[]() {}>("peni"_hs);
  
  REGISTER_RPC(UpdateTransformRPC, RpcTraits::Server);

  REFLECT_TYPE(ItemIdAndCount)
    DATA(ItemIdAndCount, item)
    DATA(ItemIdAndCount, count);

  REFLECT_TYPE(Crafting::Recipe)
    DATA_BASE(Crafting::Recipe, ingredients)
    DATA_BASE(Crafting::Recipe, output)
    DATA(Crafting::Recipe, craftingStation)
    DATA(Crafting::Recipe, name)
    DATA(Crafting::Recipe, description);

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

  REFLECT_ENUM(ActionType);

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

  REFLECT_ENUM(Item::EffectType)
    ENUMERATOR(Item::EffectType, MovementSpeedModifier, PROP_DISPLAY_NAME("Speed"))
    ENUMERATOR(Item::EffectType, JumpImpulseModifier, PROP_DISPLAY_NAME("Jump Impulse"))
    ENUMERATOR(Item::EffectType, ArmorModifier, PROP_DISPLAY_NAME("Armor"))
    ENUMERATOR(Item::EffectType, BaseDamage, PROP_DISPLAY_NAME("Damage"))
    ENUMERATOR(Item::EffectType, Knockback, PROP_DISPLAY_NAME("Knockback"))
    ENUMERATOR(Item::EffectType, HealthRegeneration, PROP_DISPLAY_NAME("Health Regeneration"))
    ENUMERATOR(Item::EffectType, Shine, PROP_DISPLAY_NAME("Shine"))
    ENUMERATOR(Item::EffectType, Spelunker, PROP_DISPLAY_NAME("Spelunker"));

  REFLECT_ENUM(ArmorAndAccessories::Slot)
    ENUMERATOR(ArmorAndAccessories::Slot, SLOT_HEAD, PROP_DISPLAY_NAME("Head"), PROP_ICON(ICON_FA_HAT_COWBOY))
    ENUMERATOR(ArmorAndAccessories::Slot, SLOT_BODY, PROP_DISPLAY_NAME("Body"), PROP_ICON(ICON_FA_SHIRT))
    ENUMERATOR(ArmorAndAccessories::Slot, SLOT_LEGS, PROP_DISPLAY_NAME("Legs"), PROP_ICON(ICON_FA_SHOE_PRINTS))
    ENUMERATOR(ArmorAndAccessories::Slot, SLOT_ACCESSORY0, PROP_DISPLAY_NAME("Accessory"), PROP_ICON(ICON_FA_GEAR))
    ENUMERATOR(ArmorAndAccessories::Slot, SLOT_ACCESSORY1, PROP_DISPLAY_NAME("Accessory"), PROP_ICON(ICON_FA_GEAR))
    ENUMERATOR(ArmorAndAccessories::Slot, SLOT_ACCESSORY2, PROP_DISPLAY_NAME("Accessory"), PROP_ICON(ICON_FA_GEAR))
    ENUMERATOR(ArmorAndAccessories::Slot, SLOT_ACCESSORY3, PROP_DISPLAY_NAME("Accessory"), PROP_ICON(ICON_FA_GEAR))
    ENUMERATOR(ArmorAndAccessories::Slot, SLOT_ACCESSORY4, PROP_DISPLAY_NAME("Accessory"), PROP_ICON(ICON_FA_GEAR));

  REFLECT_COMPONENT(TemporaryEffects, REPLICATED)
    DATA_BASE(TemporaryEffects, effects);

  REFLECT_COMPONENT(ArmorAndAccessories, REPLICATED)
    DATA_BASE(ArmorAndAccessories, slots);

  REFLECT_COMPONENT(Pathfinding::CachedPath)
    DATA_BASE(Pathfinding::CachedPath, path)
    TRAITS(TRANSIENT)
    DATA(Pathfinding::CachedPath, progress)
    DATA(Pathfinding::CachedPath, updateAccum)
    DATA(Pathfinding::CachedPath, timeBetweenUpdates);

  REFLECT_COMPONENT(DoNotRenderIfAncestorIsLocalPlayer, REPLICATED);

  REFLECT_ENUM(Item::EffectCondition)
    ENUMERATOR(Item::EffectCondition, OnUse)
    ENUMERATOR(Item::EffectCondition, OnHeld)
    ENUMERATOR(Item::EffectCondition, OnWorn);

  REFLECT_ENUM(Item::EffectQuantityType)
    ENUMERATOR(Item::EffectQuantityType, Additive)
    ENUMERATOR(Item::EffectQuantityType, Multiplicative);

  REFLECT_COMPONENT(Item::Component::MaterializeAsMeshEntity, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::MaterializeAsMeshEntity, mesh)
    DATA(Item::Component::MaterializeAsMeshEntity, tint)
    DATA(Item::Component::MaterializeAsMeshEntity, position)
    DATA(Item::Component::MaterializeAsMeshEntity, scale)
    DATA(Item::Component::MaterializeAsMeshEntity, rotation);

  REFLECT_COMPONENT(Item::Component::Usable, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::Usable, timeBetweenUses);

  REFLECT_COMPONENT(Item::Component::Stackable, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::Stackable, maxStackSize);
  
  REFLECT_COMPONENT(Item::Component::ColliderWhenDropped, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::ColliderWhenDropped, shape)
    DATA(Item::Component::ColliderWhenDropped, translation)
    DATA(Item::Component::ColliderWhenDropped, rotation)
    DATA(Item::Component::ColliderWhenDropped, friction);
  
  REFLECT_COMPONENT_BASE(Item::Component::AllowedSlots, ITEM_COMPONENT | REPLICATED);

  REFLECT_ENUM(Item::Component::AllowedSlots)
    ENUMERATOR(Item::Component::AllowedSlots, Normal)
    ENUMERATOR(Item::Component::AllowedSlots, Head)
    ENUMERATOR(Item::Component::AllowedSlots, Body)
    ENUMERATOR(Item::Component::AllowedSlots, Legs)
    ENUMERATOR(Item::Component::AllowedSlots, Accessory)
    ENUMERATOR(Item::Component::AllowedSlots, Hidden);

  REFLECT_COMPONENT(Item::Component::StaticEffect, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::StaticEffect, condition)
    DATA(Item::Component::StaticEffect, quantityType)
    DATA(Item::Component::StaticEffect, type)
    DATA(Item::Component::StaticEffect, amount);

  REFLECT_COMPONENT(Item::Component::StaticEffects, ITEM_COMPONENT | REPLICATED)
    DATA_BASE(Item::Component::StaticEffects, effects);
  
  REFLECT_COMPONENT(Item::Component::Gun, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::Gun, model)
    DATA(Item::Component::Gun, tint)
    DATA(Item::Component::Gun, scale)
    DATA(Item::Component::Gun, damage)
    DATA(Item::Component::Gun, knockback)
    DATA(Item::Component::Gun, bullets)
    DATA(Item::Component::Gun, velocity)
    DATA(Item::Component::Gun, accuracyMoa)
    DATA(Item::Component::Gun, vrecoil)
    DATA(Item::Component::Gun, vrecoilDev)
    DATA(Item::Component::Gun, hrecoil)
    DATA(Item::Component::Gun, hrecoilDev)
    DATA_BASE(Item::Component::Gun, light)
    DATA(Item::Component::Gun, sticky)
    DATA(Item::Component::Gun, stickyDist);

  REFLECT_COMPONENT(Item::Component::MaterializeAsSprite, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::MaterializeAsSprite, tag)
    DATA(Item::Component::MaterializeAsSprite, tint);

  REFLECT_COMPONENT(Item::Component::Rainbow, ITEM_COMPONENT | REPLICATED);

  REFLECT_COMPONENT(Item::Component::Tool, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::Tool, blockDamage)
    DATA(Item::Component::Tool, blockDamageTier)
    DATA_BASE(Item::Component::Tool, blockDamageFlags);

  REFLECT_COMPONENT(Item::Component::SpawnEntityPrefabOnUse, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::SpawnEntityPrefabOnUse, tag);
  
  REFLECT_COMPONENT(Item::Component::HealUserOnUse, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::HealUserOnUse, amount);
  
  REFLECT_COMPONENT(Item::Component::Block, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::Block, voxel);
  
  REFLECT_COMPONENT(Item::Component::SpawnTempHurtboxOnUse, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::SpawnTempHurtboxOnUse, shape)
    DATA(Item::Component::SpawnTempHurtboxOnUse, position)
    DATA(Item::Component::SpawnTempHurtboxOnUse, damage)
    DATA(Item::Component::SpawnTempHurtboxOnUse, knockback)
    DATA_BASE(Item::Component::SpawnTempHurtboxOnUse, duration);

  // TODO: reflect AnimatePathOnUse

  REFLECT_COMPONENT(Item::Component::GiveEffectOnUse, ITEM_COMPONENT | REPLICATED)
    DATA(Item::Component::GiveEffectOnUse, effectId)
    DATA(Item::Component::GiveEffectOnUse, duration);

  REFLECT_TYPE(Block::DropSelf);

  REFLECT_ENUM(BlockDamageFlagBit)
    ENUMERATOR(BlockDamageFlagBit, NONE)
    ENUMERATOR(BlockDamageFlagBit, PICKAXE)
    ENUMERATOR(BlockDamageFlagBit, AXE)
    ENUMERATOR(BlockDamageFlagBit, ALL_TOOLS)
    ENUMERATOR(BlockDamageFlagBit, NO_LOOT)
    ENUMERATOR(BlockDamageFlagBit, NO_LOOT_95_PERCENT);

  REFLECT_TYPE(BlockDamageFlags)
    DATA_BASE(BlockDamageFlags, flags);

  using LootType = decltype(Block::Component::Breakable::dropWhenBroken);
  REFLECT_TYPE(LootType)
    VARIANT_FUNCS(LootType);

  REFLECT_COMPONENT(Block::Component::Breakable, BLOCK_COMPONENT | REPLICATED)
    DATA(Block::Component::Breakable, initialHealth)
    DATA(Block::Component::Breakable, damageTier)
    DATA_BASE(Block::Component::Breakable, damageFlags)
    DATA_BASE(Block::Component::Breakable, dropWhenBroken);

  REFLECT_COMPONENT(Block::Component::Valuable, BLOCK_COMPONENT | REPLICATED);

  REFLECT_COMPONENT(Block::Component::RenderAsSubGrid, BLOCK_COMPONENT | REPLICATED);

  REFLECT_TYPE(Block::CubeFaceMaterial);
    DATA(Block::CubeFaceMaterial, randomizeTexcoordRotation)
    DATA_BASE(Block::CubeFaceMaterial, baseColorTexture)
    DATA(Block::CubeFaceMaterial, baseColorFactor)
    DATA_BASE(Block::CubeFaceMaterial, emissionTexture)
    DATA(Block::CubeFaceMaterial, emissionFactor);

  REFLECT_COMPONENT(Block::Component::RenderAsTexturedCube, BLOCK_COMPONENT | REPLICATED)
    DATA(Block::Component::RenderAsTexturedCube, material);

  REFLECT_COMPONENT(Block::Component::RenderAsTexturedCube2, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::RenderAsTexturedCube2, faces);

  REFLECT_COMPONENT(Block::Component::PhysicalProperties, BLOCK_COMPONENT | REPLICATED)
    DATA(Block::Component::PhysicalProperties, isSolid);
  
  REFLECT_COMPONENT(Block::Component::ExplodeWhenBroken, BLOCK_COMPONENT | REPLICATED)
    DATA(Block::Component::ExplodeWhenBroken, radius)
    DATA(Block::Component::ExplodeWhenBroken, damage)
    DATA(Block::Component::ExplodeWhenBroken, damageTier)
    DATA(Block::Component::ExplodeWhenBroken, pushForce)
    DATA(Block::Component::ExplodeWhenBroken, damageFlags);

  REFLECT_COMPONENT(Block::Component::SpawnDependentEntityPrefabWhenPlaced, BLOCK_COMPONENT | REPLICATED)
    DATA(Block::Component::SpawnDependentEntityPrefabWhenPlaced, id);

  REFLECT_COMPONENT(Block::Component::CorrespondingItem, BLOCK_COMPONENT | REPLICATED)
    DATA(Block::Component::CorrespondingItem, item);
  
  REFLECT_COMPONENT(Block::Component::Script, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::Script, path);

  REFLECT_ENUM(Block::Direction)
    ENUMERATOR(Block::Direction, North)
    ENUMERATOR(Block::Direction, South)
    ENUMERATOR(Block::Direction, East)
    ENUMERATOR(Block::Direction, West)
    ENUMERATOR(Block::Direction, Up)
    ENUMERATOR(Block::Direction, Down);

  REFLECT_COMPONENT(Block::Component::RequiresSupport, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::RequiresSupport, supportingSide);
  
  REFLECT_COMPONENT(Block::Component::RequiresSupportByBlock, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::RequiresSupportByBlock, block);
  
  REFLECT_COMPONENT(Block::Component::RequiresSupportByBlocks, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::RequiresSupportByBlocks, blocks);
  
  REFLECT_COMPONENT(Block::Component::BaseVariant, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::BaseVariant, block);
  
  REFLECT_COMPONENT(Block::Component::StandardRotatedVariants, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::StandardRotatedVariants, east)
    DATA_BASE(Block::Component::StandardRotatedVariants, south)
    DATA_BASE(Block::Component::StandardRotatedVariants, west);
  
  REFLECT_COMPONENT(Block::Component::SpawnExtraBlockOnPlace, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::SpawnExtraBlockOnPlace, block)
    DATA_BASE(Block::Component::SpawnExtraBlockOnPlace, direction);
  
  REFLECT_COMPONENT(Block::Component::InterlinkedBlock, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::InterlinkedBlock, direction);
  
  REFLECT_COMPONENT(Block::Component::TransformWhenUsed, BLOCK_COMPONENT | REPLICATED)
    DATA_BASE(Block::Component::TransformWhenUsed, block);
}
