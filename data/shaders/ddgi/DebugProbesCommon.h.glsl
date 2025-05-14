#ifndef DEBUG_PROBES_COMMON_H
#define DEBUG_PROBES_COMMON_H

#include "../Resources.h.glsl"
#define DDGI_NO_PUSH_CONSTANTS
#include "ProbeCommon.shared.h"

#ifndef __cplusplus
#include "../GlobalUniforms.h.glsl"
#define perFrameUniforms perFrameUniformsBuffers[args.globalUniformsIndex]

struct Vertex
{
  vec3 position;
  vec3 normal;
  vec3 color;
};

FVOG_DECLARE_BUFFER_REFERENCE(VertexBuffer)
{
  Vertex vertices[];
};

#endif // !__cplusplus

FVOG_DECLARE_ARGUMENTS(DebugProbesArguments)
{
#ifndef __cplusplus
  VertexBuffer vertexBuffer;
  DDGIArgs ddgi;
#else
  VkDeviceAddress vertexBuffer;
  VkDeviceAddress ddgi;
#endif
  FVOG_UINT32 globalUniformsIndex;
  FVOG_SHARED Sampler samplerr;
  FVOG_UINT32 debugMode; // 1 == radiance. 2 == irradiance.
}
#ifndef __cplusplus
args
#endif
;

#endif // DEBUG_PROBES_COMMON_H
