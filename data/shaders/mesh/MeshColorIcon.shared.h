#ifndef MESH_COLOR_ICON_SHARED_H
#define MESH_COLOR_ICON_SHARED_H

#include "../Resources.h.glsl"
#include "Mesh.shared.h"

FVOG_DECLARE_BUFFER_REFERENCE_2(DrawMeshColorIconParams)
{
  FVOG_MAT4 clip_from_world;
  FVOG_MAT4 world_from_object;
  MeshColorVertexPtr vertexBuffer;
  FVOG_VEC3 tint;
  FVOG_VEC3 cameraPos;
};

FVOG_DECLARE_ARGUMENTS(DrawMeshColorIconArgs)
{
  DrawMeshColorIconParams pc;
};

#endif // MESH_COLOR_ICON_SHARED_H