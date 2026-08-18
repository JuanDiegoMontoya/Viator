#include "Mesh.shared.h"

layout(location = 0) in vec3 i_color;
layout(location = 1) in vec3 i_normal;

layout(location = 0) out vec4 o_radiance;

void main()
{
  o_radiance = vec4(i_color, 1);
}