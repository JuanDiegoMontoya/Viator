#include "DebugProbesCommon.h.glsl"

layout(location = 0) out vec3 v_normal;
layout(location = 1) flat out int v_probeIndex;

void main()
{
  v_probeIndex = gl_InstanceIndex;
  const ivec3 probeCoord = ProbeIndexToCoord(v_probeIndex, args.ddgi.gridInfo.gridResolution) + args.ddgi.gridInfo.gridOffset;
  Vertex vertex = args.vertexBuffer.vertices[gl_VertexIndex];
  const vec3 vertexPosWorld = vertex.position * args.probeSize + probeCoord * args.ddgi.gridInfo.baseGridScale + 0.5;

  v_normal = vertex.normal;
  gl_Position = perFrameUniforms.viewProj * vec4(vertexPosWorld, 1);
}