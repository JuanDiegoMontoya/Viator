#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include "Resources.h.glsl"

FVOG_DECLARE_BUFFER_REFERENCE_3(IntPtr, 4)
{
  FVOG_INT32 data;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(IntList)
{
  FVOG_INT32 size;
  IntPtr values;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(IntVector)
{
  FVOG_INT32 size;
  FVOG_INT32 capacity;
  IntPtr values;
};

#endif // COMMON_TYPES_H