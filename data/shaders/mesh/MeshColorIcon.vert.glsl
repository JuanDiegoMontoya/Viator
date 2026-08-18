
#include "MeshColorIcon.shared.h"

layout(location = 0) out vec3 o_color;
layout(location = 1) out vec3 o_normal;

void main()
{
  const MeshColorVertex vertex = pc.vertexBuffer[gl_VertexIndex].data;
  const vec4 worldPos = pc.world_from_object * vec4(vertex.position, 1.0);

  o_color     = vertex.color * pc.tint;
  o_normal    = inverse(transpose(mat3(pc.world_from_object))) * vertex.normal;
  gl_Position = pc.clip_from_world * worldPos;
}