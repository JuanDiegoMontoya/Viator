#include "DebugProbesCommon.h.glsl"

layout(location = 0) out vec3 v_normal;
layout(location = 1) flat out int v_probeIndex;
layout(location = 2) flat out int v_cascade;

void main()
{
  v_cascade = args.cascade;
  v_probeIndex = gl_InstanceIndex;
  const ivec3 probeCoord = ProbeIndexToCoord(v_probeIndex, args.ddgi.gridInfo[args.cascade].gridResolution) + args.ddgi.gridInfo[args.cascade].gridOffset;
  Vertex vertex = args.vertexBuffer.vertices[gl_VertexIndex];
  const vec3 vertexPosWorld = vertex.position * args.probeSize + probeCoord * args.ddgi.gridInfo[args.cascade].baseGridScale + 0.5;

  v_normal = vertex.normal;
  gl_Position = perFrameUniforms.viewProj * vec4(vertexPosWorld, 1);
}