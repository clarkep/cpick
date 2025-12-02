#ifndef CPICK_SHAPES_H
#define CPICK_SHAPES_H

#include <raylib.h>

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

void draw_rounded_quadrilateral(Vector2 *vertices, bool *rounded, float radius, int segments_per_vertex,
	Color color);

#endif