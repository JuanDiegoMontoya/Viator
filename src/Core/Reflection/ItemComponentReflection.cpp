#include "ItemComponentReflection.h"
#include "Core/Reflection/ReflectionUtilitiesInternal.h"
#include "Game/Item.h"
#include "Game/Scripting.h"

#include "ReflectionMacrosInternal.h"

#include "entt/meta/factory.hpp"
#include "entt/meta/meta.hpp"
#include "angelscript.h"

static asIScriptEngine* asEngine{};

namespace Core::Reflection
{
  void InitializeItemComponentReflection(Scripting& scripting)
  {
    asEngine = &scripting.GetEngine();

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
      DATA(Item::Component::Gun, stickyDist)
      DATA_BASE(Item::Component::Gun, fogEmitter);

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

    REFLECT_COMPONENT(Item::Component::Rope, ITEM_COMPONENT | REPLICATED)
      DATA(Item::Component::Rope, length);

    REFLECT_COMPONENT(Item::Component::AbsorbFogOnUse, ITEM_COMPONENT | REPLICATED);

    BEGIN_REFLECT_COMPONENT(Item::Component::GrapplingHookLauncher, ITEM_COMPONENT | REPLICATED)
      MEMBER(maxDistance, PROP_MAX(100.0f));
      MEMBER(launchVelocity, PROP_MAX(100.0f));
      MEMBER(pullAcceleration, PROP_MAX(100.0f));
      MEMBER(pullMaxVelocity, PROP_MAX(100.0f));
      MEMBER(pullInitialVelocity, PROP_MAX(100.0f));
      MEMBER(initialSpringFrequency, PROP_MAX(20.0f));
      MEMBER(maxSpringFrequency, PROP_MAX(20.0f));
      MEMBER(springFrequencyVelocity, PROP_MAX(50.0f));
      MEMBER(springDamping);
    END_REFLECT

    BEGIN_REFLECT_COMPONENT(Item::Component::IconModifiers, ITEM_COMPONENT | REPLICATED)
      MEMBER(deltaCameraPosition);
      MEMBER(deltaBound, PROP_MIN(-0.5f), PROP_MAX(0.5f));
      MEMBER(deltaLookAtPosition);
      MEMBER(deltaPitch, PROP_MIN(-3.14f), PROP_MAX(3.14f));
      MEMBER(deltaYaw, PROP_MIN(-3.14f), PROP_MAX(3.14f));
      MEMBER(deltaRoll, PROP_MIN(-3.14f), PROP_MAX(3.14f));
    END_REFLECT
  }
}