#include "DrawSingleVoxelToGBuffer.shared.h"

layout(location = 0) out vec3 o_posWS;
layout(location = 1) out flat int o_instanceIndex;
layout(location = 2) out vec3 o_posOS;

void main()
{
  DrawSingleVoxelInstance instance = pc.instances[gl_InstanceIndex];
  o_posOS = CreateCube(gl_VertexIndex) - 0.5;
  o_posWS = (instance.world_from_object * vec4(o_posOS, 1)).xyz;
  o_instanceIndex = gl_InstanceIndex;
  gl_Position = pc.uniforms.viewProj * vec4(o_posWS, 1);
}