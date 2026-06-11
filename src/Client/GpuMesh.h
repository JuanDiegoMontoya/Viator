#pragma once
#include "Client/Fvog/Buffer2.h"

#include "glm/vec3.hpp"

#include <cstdint>
#include <optional>

using index_t = uint32_t;

struct Vertex
{
  glm::vec3 position{};
  glm::vec3 normal{};
  glm::vec3 color{};
};

struct GpuMesh
{
  std::optional<Fvog::TypedBuffer<Vertex>> vertexBuffer;
  std::optional<Fvog::TypedBuffer<index_t>> indexBuffer;
  std::vector<Vertex> vertices;
  std::vector<index_t> indices;
};