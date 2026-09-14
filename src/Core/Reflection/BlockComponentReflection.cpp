#include "BlockComponentReflection.h"

#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ReflectionUtilitiesInternal.h"
#include "Core/Reflection/ReflectionMacrosInternal.h"
#include "Game/Block.h"
#include "Game/Scripting.h"
#include "Core/Assert2.h"

#include "angelscript.h"
#include "entt/meta/meta.hpp"
#include "entt/meta/factory.hpp"
#include "entt/core/hashed_string.hpp"
#include "entt/entity/registry.hpp"

#include <type_traits>

static asIScriptEngine* asEngine{};

namespace Core::Reflection
{
  using namespace entt::literals;

  void InitializeBlockComponentReflection(Scripting& scripting)
  {
    asEngine = &scripting.GetEngine();

    REFLECT_COMPONENT(Block::Component::Valuable, BLOCK_COMPONENT | REPLICATED);

    REFLECT_COMPONENT(Block::Component::RenderAsSubGrid, BLOCK_COMPONENT | REPLICATED);

    REFLECT_COMPONENT(Block::Component::RenderAsTexturedCube, BLOCK_COMPONENT | REPLICATED)
      DATA_BASE(Block::Component::RenderAsTexturedCube, material);

    REFLECT_COMPONENT(Block::Component::RenderAsTexturedCube2, BLOCK_COMPONENT | REPLICATED)
      DATA_BASE(Block::Component::RenderAsTexturedCube2, faces);

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

    BEGIN_REFLECT_COMPONENT(Block::Component::RequiresSupportAdvanced, BLOCK_COMPONENT | REPLICATED)
      MEMBER(supports);
    END_REFLECT
  }
}