//? #version 450
#ifndef DEBUG_COMMON_H
#define DEBUG_COMMON_H

#include "../Resources.h.glsl"
#include "../BasicTypes.h.glsl"

struct DebugAabb
{
  FVOG_VEC3 center;
  FVOG_VEC3 extent;
  FVOG_VEC4 color;
};

struct DebugRect
{
  FVOG_VEC2 minOffset;
  FVOG_VEC2 maxOffset;
  FVOG_VEC4 color;
  FVOG_FLOAT depth;
};

struct DebugLine
{
  FVOG_VEC3 aPosition;
  FVOG_VEC4 aColor;
  FVOG_VEC3 bPosition;
  FVOG_VEC4 bColor;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(DebugAabbBuffer)
{
  DebugAabb aabb;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(DebugRectBuffer)
{
  DebugRect rect;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(DebugLineBuffer)
{
  DebugLine line;
};

FVOG_DECLARE_BUFFER_REFERENCE_2(DebugDrawData)
{
  DrawIndirectCommand aabbDrawCommand;
  DrawIndirectCommand rectDrawCommand;
  DrawIndirectCommand lineDrawCommand;
  FVOG_UINT32 maxAabbCount;
  FVOG_UINT32 maxRectCount;
  FVOG_UINT32 maxLineCount;
  DebugAabbBuffer aabbs;
  DebugRectBuffer rects;
  DebugLineBuffer lines;
};

#ifndef __cplusplus
// World-space box
bool TryPushDebugAabb(DebugDrawData debug, DebugAabb box)
{
  uint index = atomicAdd(debug.aabbDrawCommand.instanceCount, 1);

  // Check if buffer is full
  if (index >= debug.maxAabbCount)
  {
    atomicAdd(debug.aabbDrawCommand.instanceCount, -1);
    return false;
  }

  debug.aabbs[index].aabb = box;
  return true;
}

// UV-space rect
bool TryPushDebugRect(DebugDrawData debug, DebugRect rect)
{
  uint index = atomicAdd(debug.rectDrawCommand.instanceCount, 1);

  // Check if buffer is full
  if (index >= debug.maxRectCount)
  {
    atomicAdd(debug.rectDrawCommand.instanceCount, -1);
    return false;
  }

  debug.rects[index].rect = rect;
  return true;
}

// World-space line
bool TryPushDebugLine(DebugDrawData debug, DebugLine line)
{
  uint index = atomicAdd(debug.lineDrawCommand.instanceCount, 1);

  // Check if buffer is full
  if (index >= debug.maxLineCount)
  {
    atomicAdd(debug.lineDrawCommand.instanceCount, -1);
    return false;
  }

  debug.lines[index].line = line;
  return true;
}

bool TryPushDebugLine(DebugDrawData debug, vec3 aPosition, vec4 aColor, vec3 bPosition, vec4 bColor)
{
  DebugLine line;
  line.aPosition = aPosition;
  line.bPosition = bPosition;
  line.aColor = aColor;
  line.bColor = bColor;
  return TryPushDebugLine(debug, line);
}

bool DrawDebugLineBasis(DebugDrawData debug, vec3 position, mat3 basis)
{
  uint success = 1;
  success |= uint(TryPushDebugLine(debug, position, vec4(1, 0, 0, 1), position + basis[0], vec4(1, 0, 0, 1)));
  success |= uint(TryPushDebugLine(debug, position, vec4(0, 1, 0, 1), position + basis[1], vec4(0, 1, 0, 1)));
  success |= uint(TryPushDebugLine(debug, position, vec4(0, 0, 1, 1), position + basis[2], vec4(0, 0, 1, 1)));
  return bool(success);
}

#endif // __cplusplus

#endif // DEBUG_COMMON_H