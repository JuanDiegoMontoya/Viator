#pragma once

#define MAKE_IDENTIFIER() CONCAT(factory_, __LINE__)
#define MAKE_IDENTIFIER2(name) CONCAT(name, __LINE__)
#define CONCAT(x, y) CONCAT_INDIRECT(x, y)
#define CONCAT_INDIRECT(x, y) x ## y

#define REFLECT_TYPE(T)    \
  REGISTER_OBJECT_TYPE(T); \
  [[maybe_unused]] auto MAKE_IDENTIFIER() = entt::meta_factory<T>{}.custom<PropertiesMap>(PropertiesMap{{"name"_hs, #T}})

#define REFLECT_NON_OBJECT_TYPE(T)    \
  [[maybe_unused]] auto MAKE_IDENTIFIER() = entt::meta_factory<T>{}.custom<PropertiesMap>(PropertiesMap{{"name"_hs, #T}})

#define REFLECT_COMPONENT_NO_DEFAULT(T, ...)                                                        \
  REGISTER_OBJECT_TYPE(T);                                                                          \
  __VA_OPT__(static_assert(!((__VA_ARGS__) & Traits::TRIVIAL) || std::is_trivially_copyable_v<T>);) \
  [[maybe_unused]] auto MAKE_IDENTIFIER() = entt::meta_factory<T>{}.traits(COMPONENT __VA_OPT__(| __VA_ARGS__))

#define REFLECT_COMPONENT(T, ...)       \
  REGISTER_OBJECT_TYPE(T);              \
  REGISTER_COMPONENT_REGISTRY_FUNCS(T); \
  REFLECT_COMPONENT_BASE(T __VA_OPT__(, __VA_ARGS__))

#define REFLECT_COMPONENT_BASE(T, ...)                                                                                                                        \
  __VA_OPT__(static_assert(!((__VA_ARGS__) & Traits::TRIVIAL) || std::is_trivially_copyable_v<T>);)                                                           \
  [[maybe_unused]] auto MAKE_IDENTIFIER() = entt::meta_factory<T>{}                                                                                           \
                                              .traits(COMPONENT | (std::is_empty_v<T> ? EMPTY : Traits(0)) | (is_optional_v<T> ? OPTIONAL : Traits(0)) |      \
                                                      (is_variant_v<T> ? VARIANT : Traits(0))__VA_OPT__(| __VA_ARGS__))                                       \
                                              .func<[](entt::registry* registry, entt::entity entity) { registry->emplace<T>(entity); }>("EmplaceDefault"_hs) \
                                              .func<[](entt::registry* registry, entt::entity entity, T& value)                                               \
                                                {                                                                                                             \
                                                  if constexpr (!std::is_empty_v<T>)                                                                          \
                                                    EmplaceOrReplaceWrapper_HACK(registry, entity, value);                                                    \
                                                  else                                                                                                        \
                                                    registry->emplace_or_replace<T>(entity);                                                                  \
                                                }>("EmplaceMove"_hs); \
  MAKE_IDENTIFIER()

#define TRAITS(TraitsV) .traits(TraitsV)

#define REGISTER_ENUM(Enum) ASSERT(asEngine->RegisterEnum(RemoveNamespaces(#Enum).c_str()) >= 0)
#define REGISTER_ENUM_VALUE(Enum, Value) ASSERT(asEngine->RegisterEnumValue(RemoveNamespaces(#Enum).c_str(), RemoveNamespaces(#Value).c_str(), static_cast<int>(Enum::Value)) >= 0)

#define REGISTER_OBJECT_TYPE(Type)                                                                                                                                   \
  /*if constexpr (!is_variant_v<Type>)*/                                                                                                                             \
  {                                                                                                                                                                  \
    entt::meta_factory<Type>{}.func<[](void* ctx, int argIdx, Type v) { ((asIScriptContext*)ctx)->SetArgObject((asUINT)argIdx, &v); }>("ASSetArg"_hs);                                \
    const auto MAKE_IDENTIFIER2(name) = RemoveNamespaces(#Type);                                                                                                     \
    ASSERT(asEngine->RegisterObjectType(MAKE_IDENTIFIER2(name).c_str(),                                                                                               \
             sizeof(Type),                                                                                                                                           \
             asOBJ_VALUE | asGetTypeTraits<Type>() | (alignof(Type) == 8 ? asOBJ_APP_CLASS_ALIGN8 : 0)) >= 0);                                                         \
    ASSERT(                                                                                                                                                          \
      asEngine->RegisterObjectBehaviour(MAKE_IDENTIFIER2(name).c_str(), asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(std::construct_at<Type>), asCALL_CDECL_OBJLAST) >= \
      0);                                                                                                                                                            \
    ASSERT(                                                                                                                                                          \
      asEngine->RegisterObjectBehaviour(MAKE_IDENTIFIER2(name).c_str(), asBEHAVE_DESTRUCT, "void f()", asFUNCTION(std::destroy_at<Type>), asCALL_CDECL_OBJLAST) >=    \
      0);                                                                                                                                                            \
    ASSERT(asEngine->RegisterObjectMethod(MAKE_IDENTIFIER2(name).c_str(),                                                                                             \
             (MAKE_IDENTIFIER2(name) + "& opAssign(" + MAKE_IDENTIFIER2(name) + "& in)").c_str(),                                                                    \
             asMETHODPR(Type, operator=, (const Type&), Type&),                                                                                                      \
             asCALL_THISCALL) >= 0);                                                                                                                                 \
  }

#define REGISTER_OBJECT_PROPERTIES(Type, Member)                                        \
  ASSERT(asEngine->RegisterObjectProperty(RemoveNamespaces(#Type).c_str(),               \
           (RemoveNamespaces(GetName<decltype(Type::Member)>()) + " " #Member).c_str(), \
           asOFFSET(Type, Member)) >= 0)

  // Registers the following functions:
  // registry::has (alias for single-component registry::all)
  // registry::get (wrapper for get() that throws if not exists)
  // registry::set (wrapper for emplace_or_replace())
#define REGISTER_COMPONENT_REGISTRY_FUNCS(Type)                                                                                                                   \
  {                                                                                                                                                               \
    []<typename T>                                                                                                                                       \
    {                                                                                                                                                             \
      const auto MAKE_IDENTIFIER2(name2) = RemoveNamespaces(#Type);                                                                                               \
      /* e.g. Velocity GetVelocity(World& in, entity)*/                                                                                                           \
      if constexpr (!std::is_empty_v<Type>)                                                                                                                       \
      {                                                                                                                                                           \
        ASSERT(asEngine->RegisterObjectMethod("World",                                                                                                             \
                 (MAKE_IDENTIFIER2(name2) + " Get" + MAKE_IDENTIFIER2(name2) + "(entity)").c_str(),                                                               \
                 asFUNCTION(WorldGet<Type>),                                                                                                                      \
                 asCALL_CDECL_OBJFIRST) >= 0);                                                                                                                    \
        ASSERT(asEngine->RegisterObjectMethod("World",                                                                                                             \
                 ("void Set" + MAKE_IDENTIFIER2(name2) + "(entity, " + MAKE_IDENTIFIER2(name2) + ")").c_str(),                                                    \
                 asFUNCTION(WorldSet<Type>),                                                                                                                      \
                 asCALL_CDECL_OBJFIRST) >= 0);                                                                                                                    \
      }                                                                                                                                                           \
      ASSERT(                                                                                                                                                     \
        asEngine->RegisterObjectMethod("World", ("bool Has" + MAKE_IDENTIFIER2(name2) + "(entity)").c_str(), asFUNCTION(WorldHas<Type>), asCALL_CDECL_OBJFIRST) >= \
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
  .data<nullptr, &Type ::Member, entt::as_ref_t>(#Member##_hs) \
  .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Member} __VA_OPT__(, __VA_ARGS__)}); \
  entt::meta_factory<decltype(Type::Member)>{}.traits(                                                                                                       \
    ((is_optional_v<decltype(Type::Member)> || is_unique_ptr_v<decltype(Type::Member)>) ? OPTIONAL : Traits(0)) | (is_variant_v<decltype(Type::Member)> ? VARIANT : Traits(0)));                          \
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
    else if constexpr (is_unique_ptr_v<U>)                                                                                                                   \
    {                                                                                                                                                        \
      entt::meta_factory<U> {}                                                                                                                               \
      UNIQUE_PTR_FUNCS(U);                                                                                                                                   \
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

#define OPTIONAL_FUNCS(T)                                                            \
  .template func<[](const T& option) { return option.has_value(); }>("has_value"_hs) \
  .template func<[](T& option) -> decltype(auto) { return option.emplace(); }, entt::as_ref_t>("emplace"_hs)           \
  .template func<[](T& option) { option.reset(); }>("reset"_hs)                      \
  .template func<[](T& option) -> decltype(auto) { return option.value(); }, entt::as_ref_t>("value"_hs)

#define UNIQUE_PTR_FUNCS(T)                                                                                                     \
  .template func<[](const T& ptr) { return ptr != nullptr; }>("has_value"_hs)                                                   \
  .template func<[](T& ptr) -> decltype(auto) { ptr.reset(new typename std::remove_cvref_t<T>::element_type()); return *ptr.get(); }, entt::as_ref_t>("emplace"_hs) \
  .template func<[](T& ptr) { ptr.reset(); }>("reset"_hs)                                                                       \
  .template func<[](T& ptr) -> decltype(auto) { return *ptr.get(); }, entt::as_ref_t>("value"_hs)

  #define REGISTER_RPC(Function, Traits) \
    entt::meta_factory<RpcTraits>().func<Function>(#Function##_hs) \
    .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Function}}) \
    .traits<RpcTraits>(Traits)

#define BEGIN_REFLECT_TYPE(T) \
  {                            \
    using T2 = T;              \
    REFLECT_TYPE(T)

#define BEGIN_REFLECT_NON_OBJECT_TYPE(T) \
  {                           \
    using T2 = T;             \
    REFLECT_NON_OBJECT_TYPE(T)

#define BEGIN_REFLECT_COMPONENT(T, ...) \
  {                            \
    using T2 = T;              \
    REFLECT_COMPONENT(T __VA_OPT__(, __VA_ARGS__))

#define MEMBER(Member, ...) DATA_BASE(T2, Member __VA_OPT__(, __VA_ARGS__))

#define END_REFLECT }
