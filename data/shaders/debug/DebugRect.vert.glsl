#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../GlobalUniforms.h.glsl"
#include "../Utility.h.glsl"
#include "DebugCommon.h.glsl"

layout(location = 0) out vec4 v_color;

FVOG_DECLARE_ARGUMENTS(DebugRectArguments)
{
  DebugDrawData debug;
};

void main()
{
  DebugRect rect = debug.rects[gl_InstanceIndex + gl_BaseInstance].rect;
  v_color = rect.color;

  vec2 uvPos;
  if (gl_VertexIndex == 0)
    uvPos = rect.minOffset;
  else if (gl_VertexIndex == 1)
    uvPos = vec2(rect.maxOffset.x, rect.minOffset.y);
  else if (gl_VertexIndex == 2)
    uvPos = rect.maxOffset;
  else
    uvPos = vec2(rect.minOffset.x, rect.maxOffset.y);
  
  vec2 clip = uvPos * 2.0 - 1.0;

  gl_Position = vec4(clip, rect.depth, 1.0);
}