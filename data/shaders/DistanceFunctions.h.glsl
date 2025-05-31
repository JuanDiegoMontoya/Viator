#ifndef DISTANCE_FUNCTIONS_H
#define DISTANCE_FUNCTIONS_H

// https://iquilezles.org/articles/distfunctions/
float sd_Box(vec3 p, vec3 b)
{
  vec3 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

#endif // DISTANCE_FUNCTIONS_H
