#pragma once

#ifndef R_IMPLEMENTATION

#define R_STRUCT(T, ...) struct T {
#define R_MEMBER(Name, Type, Default, ...) Type Name = Default
#define R_MEMBER_2(Name, Type,  ...) Type Name
#define R_END(...) }
#define R_DECLARE_COMPONENT(T, ...)

#else

struct Assignable
{
  Core::Reflection::Traits traits;

  template<typename T>
  operator T()
  {
    entt::meta_factory<T>{}.traits(traits);
    return {};
  }
};

#define R_STRUCT(T) struct T { using T2 = T;

#define R_MEMBER(Name, Type, Default, ...)                                                          \
  Type Name                              = Default;                                                 \
  const static inline int reflect_##Name = []                                                       \
  {                                                                                                 \
    s_reflectionRegistrationFuncs.push_back([] { DATA_BASE(T2, Name __VA_OPT__(, __VA_ARGS__)); }); \
    return 0;                                                                                       \
  }()

#define R_MEMBER_2(Name, Type, ...)                                                                 \
  Type Name;                                                                                        \
  const static inline int reflect_##Name = []                                                       \
  {                                                                                                 \
    s_reflectionRegistrationFuncs.push_back([] { DATA_BASE(T2, Name __VA_OPT__(, __VA_ARGS__)); }); \
    return 0;                                                                                       \
  }()

#define R_END(...)                                            \
  }                                                           \
  static const inline* CONCAT(end_, __COUNTER__) = Assignable \
  {                                                           \
    __VA_OPT__(, __VA_ARGS__)                                 \
  }

#define R_DECLARE_COMPONENT(T, ...)                                                                  \
  const static inline int reflect_##T = []                                                           \
  {                                                                                                  \
    s_reflectionRegistrationFuncs.push_back([] { REFLECT_COMPONENT(T __VA_OPT__(, __VA_ARGS__)); }); \
    return 0;                                                                                        \
  }()

#endif
