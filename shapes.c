#include <stdlib.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include "shapes.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

Vector2 normalize_v2(Vector2 v)
{
    float d = sqrtf(v.x*v.x + v.y*v.y);
    return (Vector2) { v.x / d, v.y / d };
}

Vector2 add_v2(Vector2 v, Vector2 w)
{
    return (Vector2) { v.x + w.x, v.y + w.y };
}

Vector2 mult_cv2(float c, Vector2 v)
{
    return (Vector2) { c*v.x, c*v.y };
}

static float signf(float x) {
    return (x >= 0.0f) ? 1.0f : -1.0f;
}

void draw_rounded_quadrilateral(Vector2 *vertices, bool *rounded, float radius, int segments_per_vertex,
	Color color)
{
    static int frame_i = 0;
	Matrix proj = rlGetMatrixProjection();
	radius = radius * proj.m0;
    rlDrawRenderBatchActive(); // Force flush of cached drawing commands
	rlSetMatrixProjection(MatrixIdentity());
	rlDisableBackfaceCulling();
	Vector2 *transformed_vertices = malloc(4 * sizeof(Vector2));
    float centerX = 0.0f, centerY = 0.0f;
    // Project vertices into the (-1, 1) NDC system, so that I can use right-handed trig functions.
    for (int i = 0; i < 4; i++) {
    	transformed_vertices[i] = Vector2Transform(vertices[i], proj);
    	/*
    	if (frame_i == 0) {

	    	printf("vertex %d: %f %f\n", i, transformed_vertices[i].x, transformed_vertices[i].y);
	    	Matrix m = proj;
			printf("[%f %f %f %f]\n[%f %f %f %f]\n[%f %f %f %f]\n[%f %f %f %f]\n",
				m.m0, m.m4, m.m8, m.m12,
				m.m1, m.m5, m.m9, m.m13,
				m.m2, m.m6, m.m10, m.m14,
				m.m3, m.m7, m.m11, m.m15);
    	}
    	*/
        centerX += transformed_vertices[i].x;
        centerY += transformed_vertices[i].y;
    }
    centerX /= 4.0f;
    centerY /= 4.0f;
    int data_i = 6;

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);

    Vector2 prev_point;
    Vector2 prev_corner_last_point;
    Vector2 very_first_point;
    float very_first_phi;
    // Process each corner: the goal is to draw a circle of radius 'radius', with the sides of the
    // quadrilateral tangent so that the entire shape is smooth.
    for (int corner = 0; corner < 4; corner++) {
        Vector2 cur = transformed_vertices[corner];
        Vector2 prev = transformed_vertices[(corner + 3) % 4];
        Vector2 next = transformed_vertices[(corner + 1) % 4];

        if (!rounded[corner]) {
        	if (corner == 0) {
	            very_first_point = cur;
        	} else {
        		rlVertex2f(centerX, centerY);
	    		rlVertex2f(prev_corner_last_point.x, prev_corner_last_point.y);
        		rlVertex2f(cur.x, cur.y);
        	}
        	prev_point = cur;
	        prev_corner_last_point = cur;
	        continue;
        }

        Vector2 vprev = { prev.x - cur.x, prev.y - cur.y };
        Vector2 vnext = { next.x - cur.x, next.y - cur.y };
        vprev = normalize_v2(vprev);
        vnext = normalize_v2(vnext);

        float theta_prev = atan2f(vprev.y, vprev.x);
        if (theta_prev < 0) theta_prev += 2*M_PI;
        float theta_next = atan2f(vnext.y, vnext.x);
        if (theta_next < 0) theta_next += 2*M_PI;

        float theta_less = MIN(theta_prev, theta_next);
        float theta_greater = MAX(theta_prev, theta_next);
        float dtheta = theta_greater - theta_less;
        float theta_mid;
        float dtheta_mid;
        if (dtheta <= M_PI) {
            dtheta_mid = dtheta / 2.0f;
            theta_mid = theta_less + dtheta_mid;
        } else {
            dtheta_mid = (2*M_PI - dtheta) / 2.0f;
            theta_mid = fmod(theta_greater + dtheta_mid, 2*M_PI);
        }

        // xx diagram...
        // The radii of the circle form a right angle with the sides of the quad, so drawing a right
        // triangle, we see:
        //     radius = d_to_circle_center * sin(theta_mid),
        // where d_to_circle_center is the distance to the circle center along the bisector of the
        // corner angle.
        float d_to_circle_center = radius / sinf(dtheta_mid);
        Vector2 circle_center = { cur.x + d_to_circle_center*cosf(theta_mid),
                                  cur.y + d_to_circle_center*sinf(theta_mid)};

        // The distance to the first and last points is the projection onto the quad sides
        float d_to_first_and_last_points = d_to_circle_center * cosf(dtheta_mid);
        Vector2 first_point = add_v2(cur, mult_cv2(d_to_first_and_last_points, vprev));
        Vector2 last_point = add_v2(cur, mult_cv2(d_to_first_and_last_points, vnext));

        // Use phi to denote angles around the circle_center, which also define the normals.
        // xx I think we may be able to avoid these atan2's?
        float phi_first = atan2f(first_point.y-circle_center.y, first_point.x-circle_center.x);
        float phi_last = atan2f(last_point.y-circle_center.y, last_point.x-circle_center.x);
        if (phi_last < phi_first) {
            phi_last += 2*M_PI;
        }

        if (corner == 0) {
            very_first_point = first_point;
            very_first_phi = phi_first;
        } else {
        	rlVertex2f(centerX, centerY);
        	rlVertex2f(prev_corner_last_point.x, prev_corner_last_point.y);
        	rlVertex2f(first_point.x, first_point.y);
        }

        prev_point = first_point;
        float phi = phi_first;
        for (int seg_i = 0; seg_i < segments_per_vertex; seg_i++) {
            phi = phi_first + (phi_last-phi_first)*((seg_i+1)/(float)segments_per_vertex);
            Vector2 next_point = { circle_center.x + radius*cosf(phi),
                                   circle_center.y + radius*sinf(phi)};
            rlVertex2f(centerX, centerY);
            rlVertex2f(prev_point.x, prev_point.y);
            rlVertex2f(next_point.x, next_point.y);
            prev_point = next_point;
       }
       prev_corner_last_point = prev_point;
    }
    frame_i++;
    rlVertex2f(centerX, centerY);
    rlVertex2f(prev_point.x, prev_point.y);
    rlVertex2f(very_first_point.x, very_first_point.y);
    rlEnd();
    rlDrawRenderBatchActive(); // Force flush before projection is restored
	rlSetMatrixProjection(proj);
    free(transformed_vertices);
}

