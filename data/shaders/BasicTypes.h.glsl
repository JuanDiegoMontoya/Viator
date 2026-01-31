#ifndef BASIC_TYPES_H
#define BASIC_TYPES_H

#include "Resources.h.glsl"

struct DrawElementsIndirectCommand
{
  FVOG_UINT32 indexCount;
  FVOG_UINT32 instanceCount;
  FVOG_UINT32 firstIndex;
  FVOG_INT32 baseVertex;
  FVOG_UINT32 baseInstance;
};

struct DrawIndirectCommand
{
  FVOG_UINT32 vertexCount;
  FVOG_UINT32 instanceCount;
  FVOG_UINT32 firstVertex;
  FVOG_UINT32 firstInstance;
};

#endif // BASIC_TYPES_H