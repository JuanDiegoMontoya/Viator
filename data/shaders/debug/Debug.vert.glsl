#version 460 core

#include "../Resources.h.glsl"
#include "../GlobalUniforms.h.glsl"
#include "../BasicTypes.h.glsl"

struct Vertex
{
  vec3 position;
  vec4 color;
};

FVOG_DECLARE_STORAGE_BUFFERS_2(VertexBuffers)
{
  Vertex vertices[];
}vertexBuffers[];

FVOG_DECLARE_ARGUMENTS(DebugLinesPushConstants)
{
  FVOG_UINT32 vertexBufferIndex;
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 instanced;
};

layout(location = 0) out vec4 v_color;

void main()
{
  Vertex vertex;
  if (instanced != 0)
  {
    // Hardcoded for line instances (two vertices each)
    vertex = vertexBuffers[vertexBufferIndex].vertices[gl_VertexIndex + 2 * gl_InstanceIndex];
  }
  else
  {
    vertex = vertexBuffers[vertexBufferIndex].vertices[gl_VertexIndex];
  }
  v_color = vertex.color;
  gl_Position = perFrameUniformsBuffers[globalUniformsIndex].viewProj * vec4(vertex.position, 1.0);
}