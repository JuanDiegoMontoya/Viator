#include "Forward.h.glsl"

layout(location = 0) out vec3 o_color;
layout(location = 1) out vec3 o_normal;
layout(location = 2) out vec4 o_posCS;
layout(location = 3) out vec4 o_posCS_unjittered;
layout(location = 4) out vec4 o_posCS_old;

void main()
{
  ObjectUniforms object = pc.objects.uniforms[gl_InstanceIndex];
  const Vertex vertex   = object.vertexBuffer.vertices[gl_VertexIndex];

  const mat4 worldFromObject    = pc.objects.uniforms[gl_InstanceIndex].worldFromObject;
  o_color                       = vertex.color * object.tint;
  o_normal                      = inverse(transpose(mat3(worldFromObject))) * vertex.normal;
  const vec4 worldPos           = worldFromObject * vec4(vertex.position, 1.0);
  const mat4 worldFromObjectOld = pc.objects.uniforms[gl_InstanceIndex].worldFromObjectOld;
  const vec4 worldPosOld        = worldFromObjectOld * vec4(vertex.position, 1.0);

  o_posCS     = pc.frame.clipFromWorld * worldPos;
  o_posCS_unjittered = pc.frame.viewProjUnjittered * worldPos;
  o_posCS_old = pc.frame.oldViewProjUnjittered * worldPosOld;
  gl_Position = o_posCS;
}