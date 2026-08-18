#ifndef MESH_SHARED_H
#define MESH_SHARED_H
#include "../Resources.h.glsl"

struct MeshColorVertex
{
  FVOG_VEC3 position;
  FVOG_VEC3 normal;
  FVOG_VEC3 color;
};

FVOG_DECLARE_BUFFER_REFERENCE_3(MeshColorVertexPtr, 4)
{
  MeshColorVertex data;
};

#endif // MESH_SHARED_H