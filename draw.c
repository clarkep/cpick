/*
Last updated 2025-12-29.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

/// #include <GL/glew.h>
#include <glad/glad.h>
#include "util.h"
#include "draw.h"

typedef struct bitmap {
    i32 texture_i;
    float tex_x;
    float tex_y;
    float w;
    float h;
} Bitmap;

typedef struct bitmap_dynarray {
    Bitmap *d;
    u64 length;
    u64 capacity;
    u64 item_size;
} Bitmap_Dynarray;

static FT_Library g_ft_library;

static float signf(float x) {
    return (x >= 0.0f) ? 1.0f : -1.0f;
}

typedef struct atlas_glyph_info {
    float tex_x;
    float tex_y;
    float tex_w;
    float tex_h;
    i32 bitmap_left;
    i32 bitmap_top;
    i32 advance_x;
    i32 advance_y; // always 0?
} Atlas_Glyph_Info;

static i32 create_and_add_opengl_texture(GL_Scene *scene, i32 w, i32 h, i32 channels, void *data)
{
    i32 i = scene->n_textures++;
    assertf(i < SCENE_MAX_TEXTURES, NULL);
    u32 tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLenum format;
    GLenum type;
    switch (channels) {
    case 1:
        format = GL_RED;
        type = GL_UNSIGNED_BYTE;
    break;
    case 4:
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    break;
    default:
        errexit("Programmer error, have to stop: invalid # of texture channels.\n");
    break;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, type, data);
    scene->textures[i].id = tex;
    scene->textures[i].channels = channels;
    scene->textures[i].data = data;
    scene->textures[i].w = w;
    scene->textures[i].h = h;
    return i;
}

static bool resize_opengl_texture(GL_Scene *scene, i32 i, i32 new_w, i32 new_h, i32 channels)
{
    i32 old_w = scene->textures[i].w;
    i32 old_h = scene->textures[i].h;
    u8 *old_data = (u8 *) scene->textures[i].data;
    u8 *new_data = (u8 *) malloc((u64) new_w * (u64) new_h * (u64) channels);
    if (!new_data) return false;

    // Copy existing rows with proper stride
    i32 copy_w = old_w < new_w ? old_w : new_w;
    i32 copy_h = old_h < new_h ? old_h : new_h;
    for (i32 row = 0; row < copy_h; row++) {
        memcpy(new_data + (u64) row * new_w * channels,
               old_data + (u64) row * old_w * channels,
               (u64) copy_w * channels);
    }

    free(old_data);
    scene->textures[i].data = new_data;
    scene->textures[i].w = new_w;
    scene->textures[i].h = new_h;
    u32 tex = scene->textures[i].id;
    glBindTexture(GL_TEXTURE_2D, tex);
    GLenum format;
    GLenum type;
    switch (channels) {
    case 1:
        format = GL_RED;
        type = GL_UNSIGNED_BYTE;
    break;
    case 4:
        format = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
    break;
    default:
        errexit("Programmer error, have to stop: invalid # of texture channels.\n");
    break;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format, new_w, new_h, 0, format, type, new_data);
    return true;
}

Font_Atlas *create_font_atlas(FT_Face ft_face,  u32 *charset, u32 charset_n, u32 font_size_px)
{
    Font_Atlas *res = malloc(sizeof(Font_Atlas));
    res->ft_face = ft_face;
    res->charset = charset;
    res->charset_n = charset_n;
    res->font_size_px = font_size_px;
    res->char_locations = create_hash_table(charset_n * 2);
    Atlas_Glyph_Info *info_arr = malloc(charset_n * sizeof(Atlas_Glyph_Info));

    FT_Error error = FT_Set_Pixel_Sizes(ft_face, font_size_px, font_size_px);
    if (error) {
        fprintf(stderr, "Failed to set font size for font face.\n");
        exit(1);
    }
    FT_BBox bbox = ft_face->bbox; // xx was this calculated at Load_Face time?
    double max_width_em = ((double) bbox.xMax - (double) bbox.xMin) / (double) ft_face->units_per_EM;
    double max_height_em = ((double) bbox.yMax - (double) bbox.yMin) / (double) ft_face->units_per_EM;
    u32 max_width_pix = (u32) ((double) font_size_px * max_width_em);
    u32 max_height_pix = (u32) ((double) font_size_px * max_height_em);
    u64 total_width = (u64) max_width_pix * charset_n + 1;
    total_width += 4 - (total_width % 4); // Round up to the standard OpenGL texture alignment of 4 bytes
    i32 max_texture_size; //1d 2d textures
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    res->data_w = MIN(total_width, max_texture_size);
    i32 rows = total_width / max_texture_size + 1;
    // If rows * max_height_pix > max_texture_size, we will *probably* overflow, but let's try it
    // anyway and catch overflow later.
    res->data_h = MIN((u64) rows * (u64) max_height_pix, (u64) max_texture_size);
    res->data = malloc((u64) res->data_w * (u64) res->data_h);
    // debug("Packing %u characters into a font atlas of size (%u, %u)(%u row%s). Character bbox: "
    //  "(%u, %u).\n", charset_n, res->data_w, res->data_h, rows, rows > 1 ? "s" : "", max_width_pix,
    //  max_height_pix);

    res->min_descent = INT_MAX;
    res->max_ascent = INT_MIN;
    res->max_height = INT_MIN;
    i32 x = 1;
    i32 y = 0;
    i32 row_height = 0;
    for (u32 i=0; i<charset_n; i++) {
        FT_UInt glyph_index = FT_Get_Char_Index( ft_face, charset[i] );
        if (!glyph_index) {
            fprintf(stderr, "Failed to find character in font face: %u.\n", charset[i]);
        }
        error = FT_Load_Glyph(
                  ft_face,          /* handle to face object */
                  glyph_index,   /* glyph index           */
                  FT_LOAD_DEFAULT );  /* load flags, see below */

        error = FT_Render_Glyph( ft_face->glyph,   /* glyph slot  */
                                 FT_RENDER_MODE_NORMAL ); /* render mode */
        FT_GlyphSlotRec *slot = ft_face->glyph;
        FT_Bitmap bitmap = ft_face->glyph->bitmap;

        if (x + (i32) bitmap.width >= res->data_w) {
            // xx I heard there was an even better algorithm than this...
            x = 1;
            y += row_height;
            assertf(y < res->data_h, "Programmer error: Cannot fit character set in a texture"
                "(Tried writing %u characters at size %u, could not fit in %ux%u).\n", charset_n,
                font_size_px, max_texture_size, max_texture_size);
            row_height = 0;
        }

        for (i32 r=bitmap.rows-1, res_r=0; r>=0; r--, res_r++) {
            for (i32 c=0; c<(i32)bitmap.width; c++) {
                res->data[(y+res_r)*(u64)res->data_w + (x+c)] = bitmap.buffer[r*bitmap.pitch+c];
            }
        }

        if ((i32) bitmap.rows > res->max_height) {
            res->max_height = bitmap.rows;
        }
        i32 ascent = slot->bitmap_top;
        if (ascent > res->max_ascent)
            res->max_ascent = ascent;
        i32 descent = ascent - (i32) bitmap.rows;
        if (descent < res->min_descent)
            res->min_descent = descent;

        info_arr[i] = (Atlas_Glyph_Info) { x/(float)res->data_w, y/(float)res->data_h,
            bitmap.width/(float)res->data_w, bitmap.rows/(float)res->data_h,
            slot->bitmap_left, slot->bitmap_top, slot->advance.x >> 6, slot->advance.y >> 6};
        hash_table_set(&res->char_locations, &charset[i], sizeof(u32), &info_arr[i]);

        if ((i32) bitmap.rows > row_height)
            row_height = bitmap.rows;
        x += bitmap.width;
    }
    res->info_table = info_arr;
    static int frame = 0;
    return res;
}

void destroy_font_atlas(Font_Atlas *atlas)
{
    destroy_hash_table(&atlas->char_locations);
    free(atlas->info_table);
    free(atlas);
}

i32 generate_rectangle(float *data, i32 stride, float x, float y, float w, float h, Vector4 color)
{
    data[0*stride + 0] = x;
    data[0*stride + 1] = y;
    data[1*stride + 0] = x;
    data[1*stride + 1] = y + h;
    data[2*stride + 0] = x + w;
    data[2*stride + 1] = y + h;
    data[3*stride + 0] = x + w;
    data[3*stride + 1] = y;
    return 4;
}

i32 generate_quad(float *data, i32 stride, Vector2 *corners)
{
    for (i32 i=0; i<4; i++) {
        data[i*stride + 0] = corners[i].x;
        data[i*stride + 1] = corners[i].y;
    }
    return 4;
}

i32 generate_circle(float *data, i32 stride, float x, float y, float r, i32 segments)
{
    // generate_circle is separate from generate_circle_arc because a circle is a connected shape,
    // so we don't generate the last vertex at stop_angle==2PI.
    for (i32 i=0; i<segments; i++) {
        float angle = 2*M_PI*i/segments;
        data[i*stride] = x + r*cosf(angle);
        data[i*stride+1] = y + r*sinf(angle);
    }
    return segments;
}

i32 generate_circle_arc(float *data, i32 stride, float x, float y, float r, float start_angle,
    float stop_angle, i32 segments)
{
    float d_angle = stop_angle - start_angle;
    for (i32 i=0; i<=segments; i++) {
        float angle = start_angle + d_angle*i/(segments);
        data[i*stride] = x + r*cosf(angle);
        data[i*stride+1] = y + r*sinf(angle);
    }
    return segments+1;
}

// (x/a)^n + (y/b)^n = 1.0
i32 generate_superellipse(float *data, i32 stride, float x, float y, float a, float b, float n, i32 segments)
{
    for (i32 i = 0; i < segments; i++) {
        float angle = 2.0f * M_PI * i / segments;
        float cx = cosf(angle);
        float cy = sinf(angle);
        data[i*stride] = x + a * signf(cx) * powf(fabsf(cx), 2.0f / n);
        data[i*stride + 1] = y + b * signf(cy) * powf(fabsf(cy), 2.0f / n);
    }
    return segments;
}

double signed_area(Vector2 *p, i32 n) {
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        sum += p[i].x * p[j].y - p[j].x * p[i].y;
    }
    return 0.5 * sum;
}

i32 positive_modulo(i32 x, i32 m)
{
    return ((x % m) + m) % m;
}

i32 generate_rounded_quad(float *data, i32 stride, Vector2 *corners, bool *rounded, float radius,
    i32 segments_per_corner)
{
    i32 n = 0;
    i32 start_i = 0;
    i32 stop_i = 4;
    i32 dir = 1;
    // "shoelace formula" to compute signed area and therefore winding direction; if negative, corners
    // wind clockwise, and so we traverse them in reverse order.
    if (signed_area(corners, 4) < 0) {
        start_i = 3;
        stop_i = -1;
        dir = -1;
    }

    // Process each corner: the goal is to draw a circle of radius 'radius', with the sides of the
    // quadrilateral tangent so that the entire shape is smooth.
    for (int corner = start_i; corner != stop_i; corner+=dir) {
        Vector2 cur = corners[corner];

        if (rounded && !rounded[corner]) {
            data[n*stride] = cur.x;
            data[n*stride+1] = cur.y;
            n++;
            continue;
        }

        Vector2 prev = corners[positive_modulo(corner - dir, 4)];
        Vector2 next = corners[positive_modulo(corner + dir, 4)];

        Vector2 vprev = { prev.x - cur.x, prev.y - cur.y };
        Vector2 vnext = { next.x - cur.x, next.y - cur.y };
        Vector2 vprev_n = normalize_v2(vprev);
        Vector2 vnext_n = normalize_v2(vnext);

        Vector2 bisector = { (vprev_n.x + vnext_n.x)*0.5, (vprev_n.y + vnext_n.y)*0.5 };
        bisector = normalize_v2(bisector);
        float theta = acosf(vprev_n.x * vnext_n.x + vprev_n.y * vnext_n.y);
        float theta_mid = theta / 2;

/*
        float theta_prev = atan2f(vprev_n.y, vprev_n.x);
        if (theta_prev < 0) theta_prev += 2*M_PI;
        float theta_next = atan2f(vnext_n.y, vnext_n.x);
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
*/

        // xx diagram...
        // The radii of the circle form a right angle with the sides of the quad, so
        //     radius = d_to_circle_center * sin(theta_mid),
        // where d_to_circle_center is the distance to the circle center along the bisector of the
        // corner angle.
        float d_to_circle_center = radius / sinf(theta_mid);
        Vector2 circle_center = { cur.x + d_to_circle_center*bisector.x,
                                  cur.y + d_to_circle_center*bisector.y};

        // The distance from the corner to the first and last points of the circle(the tangent points)
        // is the projection of d_to_circle_center onto the quad sides.
        float d_to_first_and_last_points = d_to_circle_center * cosf(theta_mid);
        Vector2 first_point = add_v2(cur, mult_cv2(d_to_first_and_last_points, vprev_n));
        Vector2 last_point = add_v2(cur, mult_cv2(d_to_first_and_last_points, vnext_n));

        // Use phi to denote angles around the circle_center, which also define the normals.
        float phi_first = atan2f(first_point.y-circle_center.y, first_point.x-circle_center.x);
        float phi_last = atan2f(last_point.y-circle_center.y, last_point.x-circle_center.x);
        if (phi_last < phi_first) {
            phi_last += 2*F_PI;
        }

        Vector2 prev_point = first_point;
        float phi = phi_first;
        for (int seg_i = 0; seg_i < segments_per_corner; seg_i++) {
            data[n*stride] = prev_point.x;
            data[n*stride+1] = prev_point.y;
            phi = phi_first + (phi_last-phi_first)*((seg_i+1)/(float)segments_per_corner);
            Vector2 next_point = { circle_center.x + radius*cosf(phi),
                                   circle_center.y + radius*sinf(phi)};
            prev_point = next_point;
            n++;
        }
        data[n*stride] = last_point.x; // xx should have prev_point == last_point
        data[n*stride+1] = last_point.y;
        n++;
    }
    return n;
}

void triangleize(float *data, i32 stride, i32 n, Vector2 center, Vector4 color, bool connected)
{
    i32 vstride = stride;
    i32 tstride = 3 * vstride;
    i32 limit = n - (i32) !connected;
    for (i32 i=0; i<limit; i++) {
        data[i*tstride + 2] = 0;
        data[i*tstride + 3] = color.x;
        data[i*tstride + 4] = color.y;
        data[i*tstride + 5] = color.z;
        data[i*tstride + 6] = color.w;
        i32 next_offset = ((i + 1) % n)*tstride;
        data[i*tstride + vstride] = data[next_offset];
        data[i*tstride + vstride + 1] = data[next_offset+1];
        data[i*tstride + vstride + 2] = 0;
        data[i*tstride + vstride + 3] = color.x;
        data[i*tstride + vstride + 4] = color.y;
        data[i*tstride + vstride + 5] = color.z;
        data[i*tstride + vstride + 6] = color.w;
        data[i*tstride + 2*vstride] = center.x;
        data[i*tstride + 2*vstride + 1] = center.y;
        data[i*tstride + 2*vstride + 2] = 0;
        data[i*tstride + 2*vstride + 3] = color.x;
        data[i*tstride + 2*vstride + 4] = color.y;
        data[i*tstride + 2*vstride + 5] = color.z;
        data[i*tstride + 2*vstride + 6] = color.w;
        for (int j=0; j<3; j++) {
            i32 base = i*tstride + j*vstride;
            data[base + 7] = 0.0f;
            data[base + 8] = 0.0f;
            data[base + 9] = -1.0f;
        }
    }
}

void outlineize(float *data, i32 stride, i32 n, float thickness, Vector4 color, bool connected)
{
    i32 vstride = stride;
    i32 sstride = 6*vstride; // segment stride
    i32 limit = n - (i32) !connected;
    float first_x, first_y;
    for (i32 i=0; i<limit; i++) {
        i32 offset = i*sstride;
        float x1 = data[offset];
        float y1 = data[offset+1];
        float x2 = i==n-1 ? first_x : data[offset+sstride];
        float y2 = i==n-1 ? first_y : data[offset+sstride+1];
        if (i==0) {
            first_x = x1;
            first_y = y1;
        }
        float dx = (x2 - x1);
        float dy = (y2 - y1);
        float d = sqrt(dx*dx + dy*dy);
        // (xadj, yadj) is a vector pointing to the right of the direction of travel.
        float adj = thickness / 2.0f;
        float xadj = adj*(dy / d);
        float yadj = adj*(-dx / d);
        data[offset] = x1+xadj;
        data[offset+1] = y1+yadj;
        data[offset+vstride] = x2+xadj;
        data[offset+vstride+1] = y2+yadj;
        data[offset+2*vstride] = x1-xadj;
        data[offset+2*vstride+1] = y1-yadj;
        data[offset+3*vstride] = x1-xadj;
        data[offset+3*vstride+1] = y1-yadj;
        data[offset+4*vstride] = x2+xadj;
        data[offset+4*vstride+1] = y2+yadj;
        data[offset+5*vstride] = x2-xadj;
        data[offset+5*vstride+1] = y2-yadj;
        for (int j=0; j<6; j++) {
            data[offset+j*vstride+2] = 0.0f;
            data[offset+j*vstride+3] = color.x;
            data[offset+j*vstride+4] = color.y;
            data[offset+j*vstride+5] = color.z;
            data[offset+j*vstride+6] = color.w;
            data[offset+j*vstride+7] = 0.0f;
            data[offset+j*vstride+8] = 0.0f;
            data[offset+j*vstride+9] = -1.0f;
        }
    }
}

void assert_not_overflowing(GL_Scene *scene)
{
    assertf(scene->n < scene->capacity, "Overflow of scene buffer capacity(too much drawing).\n");
}

void add_rectangle(GL_Scene *scene, float x, float y, float w, float h, Vector4 color)
{
    if (scene->use_screen_coords) {
        /* Here and in all of the functions below, we translate the geometry into a coordinate
        system with x from -1 to 1, and y from -y_scale(bottom) to y_scale(top), where y_scale is
        the window's h/w ratio. This is a uniform scaling, so that shapes are preserved(and we
        can do things like generate circles in this system that will be circles on screen) but
        is also right handed and easy to translate to OpenGL's NDC system in the vertex shader(by
        dividing y by y_scale).*/
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f  / scene->viewport_w) + scene->y_scale;
        w = w * (2.0f / scene->viewport_w);
        h = h * (-2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_rectangle(data, 3*scene->vertex_size, x, y, w, h, color);
    Vector2 center = { x + 0.5*w, y + 0.5*h };
    triangleize(data, scene->vertex_size, n, center, color, true);
    scene->n += n*3;
    assert_not_overflowing(scene);
}

void add_rectangle_outline(GL_Scene *scene, float x, float y, float w, float h, float thickness,
    Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        w = w * (2.0f / scene->viewport_w);
        h = h * (-2.0f / scene->viewport_w);
        // xx
        thickness = thickness * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + (u64)scene->vertex_size*scene->n;
    i32 n = generate_rectangle(data, 6*scene->vertex_size, x, y, w, h, color);
    outlineize(data, scene->vertex_size, n, thickness, color, true);
    scene->n += n*6;
    assert_not_overflowing(scene);
}

void transform_points_to_screen_coords(GL_Scene *scene, Vector2 *result, Vector2 *points, i32 n)
{
    for (i32 i=0; i<n; i++) {
        result[i].x = points[i].x * (2.0f / scene->viewport_w) - 1.0f;
        result[i].y = points[i].y * (-2.0f / scene->viewport_w) + scene->y_scale;
    }
}

void add_quad(GL_Scene *scene, Vector2 *corners, Vector4 color)
{
    Vector2 new_corners[4];
    if (scene->use_screen_coords) {
        transform_points_to_screen_coords(scene, new_corners, corners, 4);
        corners = new_corners;
    }
    Vector2 com = { 0, 0 };
    for (i32 i=0; i<4; i++) {
        com.x += corners[i].x;
        com.y += corners[i].y;
    }
    com.x /= 4.0f;
    com.y /= 4.0f;
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_quad(data, 3*scene->vertex_size, corners);
    triangleize(data, scene->vertex_size, n, com, color, true);
    scene->n += n*3;
    assert_not_overflowing(scene);
}

void add_quad_outline(GL_Scene *scene, Vector2 *corners, float thickness, Vector4 color)
{
    Vector2 new_corners[4];
    if (scene->use_screen_coords) {
        transform_points_to_screen_coords(scene, new_corners, corners, 4);
        corners = new_corners;
        // xx
        thickness = thickness * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_quad(data, 6*scene->vertex_size, corners);
    outlineize(data, scene->vertex_size, n, thickness, color, true);
    scene->n += n*6;
    assert_not_overflowing(scene);
}

/* If using screen coords, angle1 and angle2 are still specified according to normal, right-handed,
counterclockwise=positive angles. We don't translate from a left-handed angle system that matches
the left-handed coordinate system of the screen. */
void add_circle_slice(GL_Scene *scene, float x, float y, float r, float angle1, float angle2,
    i32 segments, Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        r = r * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_circle_arc(data, 3*scene->vertex_size, x, y, r, angle1, angle2, segments);
    triangleize(data, scene->vertex_size, n, (Vector2) { x, y }, color, false);
    scene->n += (n-1)*3;
    assert_not_overflowing(scene);
}

void add_circle_arc (GL_Scene *scene, float x, float y, float r, float angle1, float angle2,
    i32 segments, float thickness, Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        r = r * (2.0f / scene->viewport_w);
        thickness = thickness * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_circle_arc(data, 6*scene->vertex_size, x, y, r, angle1, angle2, segments);
    outlineize(data, scene->vertex_size, n, thickness, color, false);
    scene->n += (n-1)*6;
    assert_not_overflowing(scene);
}

void add_circle(GL_Scene *scene, float x, float y, float r, float segments, Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        r = r * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_circle(data, 3*scene->vertex_size, x, y, r, segments);
    triangleize(data, scene->vertex_size, n, (Vector2) { x, y }, color, true);
    scene->n += n*3;
    assert_not_overflowing(scene);
}

void add_circle_outline(GL_Scene *scene, float x, float y, float r, float segments, float thickness,
    Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        r = r * (2.0f / scene->viewport_w);
        thickness = thickness * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_circle(data, 6*scene->vertex_size, x, y, r, segments);
    outlineize(data, scene->vertex_size, n, thickness, color, true);
    scene->n += n*6;
    assert_not_overflowing(scene);
}


void add_superellipse(GL_Scene *scene, float x, float y, float a, float b, i32 n,
    float segments, Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        a = a * (2.0f / scene->viewport_w);
        b = b * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n_shape = generate_superellipse(data, 3*scene->vertex_size, x, y, a, b, n, segments);
    triangleize(data, scene->vertex_size, n_shape, (Vector2) { x, y }, color, true);
    scene->n += n_shape*3;
    assert_not_overflowing(scene);
}

void add_superellipse_outline(GL_Scene *scene, float x, float y, float a, float b, float n,
    float segments, float thickness, Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        a = a * (2.0f / scene->viewport_w);
        b = b * (2.0f / scene->viewport_w);
        thickness = thickness * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n_shape = generate_superellipse(data, 6*scene->vertex_size, x, y, a, b, n, segments);
    outlineize(data, scene->vertex_size, n_shape, thickness, color, true);
    scene->n += n_shape*6;
    assert_not_overflowing(scene);
}

void add_rounded_quad(GL_Scene *scene, Vector2 *corners, bool *rounded, float radius,
    i32 segments_per_corner, Vector4 color)
{
    Vector2 new_corners[4];
    if (scene->use_screen_coords) {
        transform_points_to_screen_coords(scene, new_corners, corners, 4);
        corners = new_corners;
        radius = radius * (2.0f / scene->viewport_w);
    }
    Vector2 center = { 0.0f, 0.0f };
    for (i32 i=0; i<4; i++) {
        center.x += corners[i].x;
        center.y += corners[i].y;
    }
    center.x /= 4.0f;
    center.y /= 4.0f;
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_rounded_quad(data, 3*scene->vertex_size, corners, rounded, radius,
        segments_per_corner);
    triangleize(data, scene->vertex_size, n, center, color, true);
    scene->n += n*3;
    assert_not_overflowing(scene);
}

void add_rounded_quad_outline(GL_Scene *scene, Vector2 *corners, bool *rounded, float radius,
    i32 segments_per_corner, float thickness, Vector4 color)
{
    Vector2 new_corners[4];
    if (scene->use_screen_coords) {
        transform_points_to_screen_coords(scene, new_corners, corners, 4);
        corners = new_corners;
        // xx
        radius = radius * (2.0f / scene->viewport_w);
        thickness = thickness * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_rounded_quad(data, 6*scene->vertex_size, corners, rounded, radius,
        segments_per_corner);
    outlineize(data, scene->vertex_size, n, thickness, color, true);
    scene->n += n*6;
    assert_not_overflowing(scene);
}

void add_rounded_rectangle(GL_Scene *scene, float x, float y, float w, float h, float radius,
    float segments_per_corner, Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        w = w * (2.0f / scene->viewport_w);
        h = h * (-2.0f / scene->viewport_w);
        radius = radius * (2.0f / scene->viewport_w);
    }
    Vector2 rect_corners[4] = { { x, y }, { x, y+h }, { x+w, y+h }, { x+w, y } };
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_rounded_quad(data, 3*scene->vertex_size, rect_corners, NULL, radius,
        segments_per_corner);
    Vector2 center = { x + 0.5f*w, y + 0.5f*h };
    triangleize(data, scene->vertex_size, n, center, color, true);
    scene->n += n*3;
    assert_not_overflowing(scene);
}

void add_rounded_rectangle_outline(GL_Scene *scene, float x, float y, float w, float h,
    float radius, float segments_per_corner, float thickness, Vector4 color)
{
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        w = w * (2.0f / scene->viewport_w);
        h = h * (-2.0f / scene->viewport_w);
        radius = radius * (2.0f / scene->viewport_w);
        thickness = thickness * (2.0f / scene->viewport_w);
    }
    Vector2 rect_corners[4] = { { x, y }, { x, y+h }, { x+w, y+h }, { x+w, y } };
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 n = generate_rounded_quad(data, 6*scene->vertex_size, rect_corners, NULL, radius,
        segments_per_corner);
    outlineize(data, scene->vertex_size, n, thickness, color, true);
    scene->n += n*6;
    assert_not_overflowing(scene);
}

void add_line(GL_Scene *scene, float x1, float y1, float x2, float y2, float thickness,
    Vector4 color)
{
    if (scene->use_screen_coords) {
        x1 = x1 * (2.0f / scene->viewport_w) - 1.0f;
        y1 = y1 * (-2.0f / scene->viewport_w) + scene->y_scale;
        x2 = x2 * (2.0f / scene->viewport_w) - 1.0f;
        y2 = y2 * (-2.0f / scene->viewport_w) + scene->y_scale;
        thickness = thickness * (2.0f / scene->viewport_w);
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 stride = scene->vertex_size;
    // copied from outlinize
    float dx = (x2 - x1);
    float dy = (y2 - y1);
    float d = sqrt(dx*dx + dy*dy);
    // (xadj, yadj) is a vector pointing to the right of the direction of travel.
    float adj = thickness / 2.0f;
    float xadj = adj*(dy / d);
    float yadj = adj*(-dx / d);
    data[0] = x1+xadj;
    data[1] = y1+yadj;
    data[stride] = x2+xadj;
    data[stride+1] = y2+yadj;
    data[2*stride] = x1-xadj;
    data[2*stride+1] = y1-yadj;
    data[3*stride] = x1-xadj;
    data[3*stride+1] = y1-yadj;
    data[4*stride] = x2+xadj;
    data[4*stride+1] = y2+yadj;
    data[5*stride] = x2-xadj;
    data[5*stride+1] = y2-yadj;
    for (int j=0; j<6; j++) {
        data[j*stride+2] = 0.0f;
        data[j*stride+3] = color.x;
        data[j*stride+4] = color.y;
        data[j*stride+5] = color.z;
        data[j*stride+6] = color.w;
        data[j*stride+7] = 0.0f;
        data[j*stride+8] = 0.0f;
        data[j*stride+9] = -1.0f;
    }
    scene->n += 6;
    assert_not_overflowing(scene);
}

// xx stride
void add_character(GL_Scene *scene, int font_i, float x, float y, u32 c, Vector4 color, float *advance_x)
{
    if (font_i < 0 || font_i >= scene->n_fonts) {
        fprintf(stderr, "Invalid font index %d\n", font_i);
        return;
    }
    Font_Atlas *atlas = scene->fonts[font_i];
    Atlas_Glyph_Info *info = hash_table_get(&atlas->char_locations, &c, sizeof(u32));
    if (!info) {
        *advance_x = 0;
        return;
    }
    float dc_w = (info->tex_w * atlas->data_w) * (2.0f / scene->viewport_w);
    float dc_h = (info->tex_h * atlas->data_h) * (2.0f / scene->viewport_w);
    float bitmap_left = info->bitmap_left * (2.0f / scene->viewport_w);
    float bitmap_top = info->bitmap_top * (2.0f / scene->viewport_w);
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
    }
    x += bitmap_left;
    // The -dc_h is because (x, y) is at the bottom of the bitmap. Freetype would normally expect you to
    // start drawing at the top of the character(row 0) and work down, which is why you see
    // y = baseline_y + bitmap_top in example code, but we flipped the bitmap during atlas generation.
    y += (bitmap_top  - dc_h);
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    i32 stride = scene->vertex_size;
    float positions[6][2] = {
        { x, y },
        { x + dc_w, y },
        { x + dc_w, y + dc_h },
        { x + dc_w, y + dc_h },
        { x, y + dc_h },
        { x, y }
    };
    float texcoords[6][2] = {
        { info->tex_x, info->tex_y },
        { info->tex_x + info->tex_w, info->tex_y },
        { info->tex_x + info->tex_w, info->tex_y + info->tex_h },
        { info->tex_x + info->tex_w, info->tex_y + info->tex_h },
        { info->tex_x, info->tex_y + info->tex_h },
        { info->tex_x, info->tex_y },
    };
    for (i32 i=0; i<6; i++) {
        i32 base = i*stride;
        data[base + 0] = positions[i][0];
        data[base + 1] = positions[i][1];
        data[base + 2] = 0;
        data[base + 3] = color.x;
        data[base + 4] = color.y;
        data[base + 5] = color.z;
        data[base + 6] = color.w;
        data[base + 7] = texcoords[i][0];
        data[base + 8] = texcoords[i][1];
        data[base + 9] = (float) font_i;
    }
    scene->n += 6;
    assert_not_overflowing(scene);
    float adv_x = info->advance_x;
    // info->advance_x is in pixels, so translate to NDC if *not* using pixels.
    if (!scene->use_screen_coords) {
        adv_x = adv_x * (2.0f / scene->viewport_w);
    }
    *advance_x = adv_x;
}

// TODO: decode utf-8
void add_text(GL_Scene *scene, int font_i, const char *text, float x, float y, Vector4 color)
{
    if (font_i < 0 || font_i >= scene->n_fonts) {
        fprintf(stderr, "Invalid font index %d\n", font_i);
        return;
    }
    Font_Atlas *atlas = scene->fonts[font_i];
    float pen_x = x;
    float pen_y = y;
    float line_advance_ratio = 1.2f;
    float line_advance = atlas->font_size_px * line_advance_ratio;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            pen_x = x;
            pen_y += line_advance;
            continue;
        }
        u32 code = (unsigned char)*p;
        float adv = 0;
        add_character(scene, font_i, pen_x, pen_y, code, color, &adv);
        if (adv == 0) {
            // maybe a character wasn't found; skip it using a best guess size
            float default_advance = atlas->font_size_px;
            if (!scene->use_screen_coords) {
                default_advance = default_advance * (2.0f / scene->viewport_w);
            }
            pen_x += default_advance;
        } else {
            pen_x += adv;
        }
    }
}

void add_text_utf32(GL_Scene *scene, int font_i, const u32 *text, float x, float y, Vector4 color)
{
    if (font_i < 0 || font_i >= scene->n_fonts) {
        fprintf(stderr, "Invalid font index %d\n", font_i);
        return;
    }
    Font_Atlas *atlas = scene->fonts[font_i];
    float pen_x = x;
    float pen_y = y;
    float line_advance_ratio = 1.2f;
    float line_advance = atlas->font_size_px * line_advance_ratio;
    for (const u32 *p = text; *p; p++) {
        if (*p == '\n') {
            pen_x = x;
            pen_y += line_advance;
            continue;
        }
        u32 code = *p;
        float adv = 0;
        add_character(scene, font_i, pen_x, pen_y, code, color, &adv);
        if (adv == 0) {
            // maybe a character wasn't found; skip it using a best guess size
            float default_advance = atlas->font_size_px;
            if (!scene->use_screen_coords) {
                default_advance = default_advance * (2.0f / scene->viewport_w);
            }
            pen_x += default_advance;
        } else {
            pen_x += adv;
        }
    }
}

float measure_text_width(GL_Scene *scene, int font_i, const char *text)
{
    if (font_i < 0 || font_i >= scene->n_fonts) {
        fprintf(stderr, "Invalid font index %d\n", font_i);
        return 0;
    }
    Font_Atlas *atlas = scene->fonts[font_i];
    float pen_x = 0;
    float line_advance_ratio = 1.2f;
    float line_advance = atlas->font_size_px * line_advance_ratio;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            pen_x = 0;
            continue;
        }
        float adv = 0;
        u32 code = (unsigned char)*p;
        Atlas_Glyph_Info *info = hash_table_get(&atlas->char_locations, &code, sizeof(u32));
        if (!info) {
            adv = 0;
        } else {
            adv = info->advance_x;
        }
        float default_advance = atlas->font_size_px;
        if (adv == 0) adv = default_advance;
        if (!scene->use_screen_coords) {
            adv = adv * (2.0f / scene->viewport_w);
        }
        pen_x += adv;
    }
    return pen_x;
}

void add_image(GL_Scene *scene, i32 image_i, float x, float y)
{
    Bitmap_Dynarray *bitmaps = (Bitmap_Dynarray *) scene->bitmaps;
    assertf(image_i >= 0 && image_i < bitmaps->length, NULL);
    Bitmap *bitmap = &bitmaps->d[image_i];
    float dc_w = (bitmap->w) * (2.0f / scene->viewport_w);
    float dc_h = (bitmap->h) * (2.0f / scene->viewport_w);
    if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
    }
    float *data = scene->vertices + scene->vertex_size*(u64)scene->n;
    scene->n += 6;
    assert_not_overflowing(scene);
    i32 stride = scene->vertex_size;
    float positions[6][2] = {
        { x, y },
        { x + dc_w, y },
        { x + dc_w, y + dc_h },
        { x + dc_w, y + dc_h },
        { x, y + dc_h },
        { x, y }
    };
    i32 tex_id = bitmap->texture_i;
    float bitmap_tex_x = bitmap->tex_x / (float) scene->textures[tex_id].w;
    float bitmap_tex_y = bitmap->tex_y / (float) scene->textures[tex_id].h;
    float bitmap_tex_w = bitmap->w / (float) scene->textures[tex_id].w;
    float bitmap_tex_h = bitmap->h / (float) scene->textures[tex_id].h;
    float texcoords[6][2] = {
        { bitmap_tex_x, bitmap_tex_y },
        { bitmap_tex_x + bitmap_tex_w, bitmap_tex_y },
        { bitmap_tex_x + bitmap_tex_w, bitmap_tex_y + bitmap_tex_h },
        { bitmap_tex_x + bitmap_tex_w, bitmap_tex_y + bitmap_tex_h },
        { bitmap_tex_x, bitmap_tex_y + bitmap_tex_h },
        { bitmap_tex_x, bitmap_tex_y },
    };
    for (i32 i=0; i<6; i++) {
        i32 base = i*stride;
        data[base + 0] = positions[i][0];
        data[base + 1] = positions[i][1];
        data[base + 2] = 0;
        data[base + 3] = 0;
        data[base + 4] = 0;
        data[base + 5] = 0;
        data[base + 6] = 0;
        data[base + 7] = texcoords[i][0];
        data[base + 8] = texcoords[i][1];
        data[base + 9] = (float) tex_id;
    }
}

const char* default_vertex_shader =
"#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec4 aColor;\n"
"layout (location = 2) in vec2 aTexCoord;\n"
"layout (location = 3) in float aFontIndex;\n"
"out vec4 fColor;\n"
"out vec2 TexCoord;\n"
"flat out float fFontIndex;\n"
"uniform float uYScale;\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(aPos.x, aPos.y / uYScale, aPos.z, 1.0);\n"
"   fColor = aColor;\n"
"   TexCoord = aTexCoord;\n"
"   fFontIndex = aFontIndex;\n"
"}";

const char* default_fragment_shader =
"#version 330 core\n"
"#define MAX_TEXTURES 8\n"
"out vec4 FragColor;\n"
"in vec4 fColor;\n"
"in vec2 TexCoord;\n"
"flat in float fFontIndex;\n"
"uniform sampler2D uTextures[MAX_TEXTURES];\n"
"uniform int uTextureChannels[MAX_TEXTURES];\n"
"void main()\n"
"{\n"
"    vec4 base = fColor;\n"
"    if (fFontIndex >= 0.0) {\n"
"        int idx = int(fFontIndex + 0.5);\n"
"        float alpha = 0.0;\n"
"        vec4 tex;\n"
"        if (idx == 0) tex = texture(uTextures[0], TexCoord);\n"
"        else if (idx == 1) tex = texture(uTextures[1], TexCoord);\n"
"        else if (idx == 2) tex = texture(uTextures[2], TexCoord);\n"
"        else if (idx == 3) tex = texture(uTextures[3], TexCoord);\n"
"        else if (idx == 4) tex = texture(uTextures[4], TexCoord);\n"
"        else if (idx == 5) tex = texture(uTextures[5], TexCoord);\n"
"        else if (idx == 6) tex = texture(uTextures[6], TexCoord);\n"
"        else if (idx == 7) tex = texture(uTextures[7], TexCoord);\n"
"        int channels = uTextureChannels[idx];\n"
"        if (channels == 1) base.a *= tex.r;\n"
"        else if (channels == 4) base = tex;\n"
"    }\n"
"    FragColor = base;\n"
"}";

// XX error handling
i32 compile_shader_program(const char *vertex_source, const char *fragment_source)
{
    unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_source, NULL);
    glCompileShader(vertex_shader);
    int  success;
    char info[512];
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertex_shader, 512, NULL, info);
        printf("Failed to compile vertex shader: %s", info);
        return -1;
    }

    unsigned int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_source, NULL);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragment_shader, 512, NULL, info);
        printf("Failed to compile fragment shader: %s", info);
        return -1;
    }

    unsigned int shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader_program, 512, NULL, info);
        printf("Failed to link shader program: %s", info);
        return -1;
    }
    return shader_program;
}

static void build_default_charset(u32 **charset_out, u32 *charset_n_out)
{
    u32 charset_n = 0x7f - 0x20;
    u32 *charset = malloc(charset_n*sizeof(u32));
    for (i32 i=0x20; i<0x7f; i++) {
        charset[i-0x20] = i;
    }
    *charset_out = charset;
    *charset_n_out = charset_n;
}

static int ensure_freetype_initialized(void)
{
    if (g_ft_library) return 0;
    FT_Error error = FT_Init_FreeType(&g_ft_library);
    if (error) {
        fprintf(stderr, "Failed to init freetype.\n");
        return -1;
    }
    return 0;
}

int load_font_internal(GL_Scene *scene, FT_Face face, u32 font_size_px, u32 *charset, u32 charset_n)
{
    Font_Atlas *atlas = create_font_atlas(face,  charset, charset_n, font_size_px);

    u32 tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlas->data_w, atlas->data_h, 0, GL_RED, GL_UNSIGNED_BYTE,
        atlas->data);
    scene->textures[scene->n_textures].id = tex;
    scene->textures[scene->n_textures].channels = 1;
    scene->textures[scene->n_textures].data = atlas->data;
    scene->textures[scene->n_textures].w = atlas->data_w;
    scene->textures[scene->n_textures].h = atlas->data_h;
    scene->n_textures++;

    i32 slot = scene->n_fonts;
    scene->fonts[slot] = atlas;
    scene->n_fonts++;

    return slot;
}

int load_font_from_memory(GL_Scene *scene, const void *font_data, u64 data_size, u32 font_size_px,
    u32 *charset, u32 charset_n)
{
    if (scene->n_textures >= SCENE_MAX_TEXTURES) {
        fprintf(stderr, "Max textures (%d) reached\n", SCENE_MAX_TEXTURES);
        return -1;
    }
    if (ensure_freetype_initialized() != 0) {
        return -1;
    }
    bool default_charset = false;
    if (!charset) {
        build_default_charset(&charset, &charset_n);
        default_charset = true;

    }
    FT_Open_Args open_args = { FT_OPEN_MEMORY, font_data, data_size, NULL, NULL, NULL, 0, NULL };
    FT_Face face;
    FT_Error error = FT_Open_Face(g_ft_library, &open_args, 0, &face);
    if (error) {
        fprintf(stderr, "Failed to create font face(from memory).\n");
        return -1;
    }
    int ret = load_font_internal(scene, face, font_size_px, charset, charset_n);
    if (default_charset)
        free(charset);
    return ret;
}

int load_font(GL_Scene *scene, const char *font_file, u32 font_size_px, u32 *charset, u32 charset_n)
{
    if (scene->n_textures >= SCENE_MAX_TEXTURES) {
        fprintf(stderr, "Max fonts (%d) reached\n", SCENE_MAX_TEXTURES);
        return -1;
    }
    if (ensure_freetype_initialized() != 0) {
        return -1;
    }
    bool default_charset = false;
    if (!charset) {
        build_default_charset(&charset, &charset_n);
        default_charset = true;
    }
    FT_Face face;
    FT_Error error = FT_New_Face(g_ft_library, font_file, 0, &face);
    if (error) {
        fprintf(stderr, "Failed to create font face for %s.\n", font_file);
        return -1;
    }
    int ret = load_font_internal(scene, face, font_size_px, charset, charset_n);
    if (default_charset)
        free(charset);
    return ret;
}

i32 load_image(GL_Scene *scene, const char *path, const char *type)
{
    FILE *in = fopen(path, "rb");
    assertf(in, "could not open %s.\n", path);
    fseek(in, 0, SEEK_END);
    u64 in_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    u8 *in_data = (u8 *) malloc(in_size);

    fread(in_data, 1, in_size, in);

    i32 res = load_image_from_memory(scene, in_data, in_size, type);
    free(in_data);
    return res;
}

static void create_new_opengl_texture_for_images(GL_Scene *scene, i32 w, i32 h) {
    void *new_texture_data = malloc((u64) w * (u64) h * 4ULL);
    scene->cur_image_texture.texture_i = create_and_add_opengl_texture(scene, w, h,
        4, new_texture_data);
    scene->cur_image_texture.pen_x = 0;
    scene->cur_image_texture.pen_y = 0;
}

i32 load_image_from_memory(GL_Scene *scene, const void *data, u64 data_size, const char *type)
{
    if (scene->n_textures >= SCENE_MAX_TEXTURES) {
        fprintf(stderr, "Max textures (%d) reached\n", SCENE_MAX_TEXTURES);
        return -1;
    }

    i32 img_w;
    i32 img_h;
    u32 *img_data;
    if (strcmp(type, "png")==0) {
        i32 channels_in_file = 0;
        img_data = (u32 *) stbi_load_from_memory(data, data_size, &img_w, &img_h,
            &channels_in_file, 4);
    } else if (strcmp(type, "svg")==0) {
        NSVGimage* image;
        image = nsvgParse((char *) data, "px", 96);
        img_w = image->width;
        img_h = image->height;

        // Create rasterizer (can be used to render multiple images).
        struct NSVGrasterizer* rast = nsvgCreateRasterizer();
        // Allocate memory for image
        img_data = malloc(img_w*img_h*4);
        // Rasterize
        nsvgRasterize(rast, image, 0,0,1, (u8 *) img_data, img_w, img_h, img_w*4);
    } else {
        return -1;
    }

    i32 max_texture_size; //1d 2d textures
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    if (scene->cur_image_texture.texture_i < 0) {
        create_new_opengl_texture_for_images(scene, img_w, img_h);
    } else {
        bool found_a_spot = false;
        while (!found_a_spot) {
            // Todo: overflow checks.
            i32 i = scene->cur_image_texture.texture_i;
            if (scene->cur_image_texture.pen_x + img_w > scene->textures[i].w) {
                i32 new_w = scene->cur_image_texture.pen_x + img_w;
                if (new_w < max_texture_size) {
                    assertf(resize_opengl_texture(scene, i, new_w, scene->textures[i].h, 4),
                        NULL);
                } else {
                    scene->cur_image_texture.pen_x = 0;
                    // next row starts at the bottom of the current texture: guarantees a resize.
                    scene->cur_image_texture.pen_y = scene->textures[i].h;
                }
            }
            if (scene->cur_image_texture.pen_y + img_h > scene->textures[i].h) {
                i32 new_h = scene->textures[i].h + img_h;
                if (new_h < max_texture_size) {
                    assertf(resize_opengl_texture(scene, i, scene->textures[i].w, new_h, 4),
                        NULL);
                } else {
                    if (img_w < max_texture_size && img_h < max_texture_size) {
                        create_new_opengl_texture_for_images(scene, img_w, img_h);
                        continue;
                    } else {
                        break;
                    }
                }
            }
            found_a_spot = true;
        }
        if (!found_a_spot) {
            return -1;
        }
    }
    i32 i = scene->cur_image_texture.texture_i;
    i32 tex_w = scene->textures[i].w;
    i32 tex_h = scene->textures[i].h;
    i32 pen_x = scene->cur_image_texture.pen_x;
    i32 pen_y = scene->cur_image_texture.pen_y;
    u32 *tex_data = (u32 *) scene->textures[i].data;
    for (i32 r=0; r<img_h; r++) {
        i32 tex_row = pen_y + img_h - r - 1;
        for (i32 c=0; c<img_w; c++) {
            i32 tex_col = pen_x + c;
            tex_data[tex_row*tex_w + tex_col] = img_data[r*img_w + c];
        }
    }

    glBindTexture(GL_TEXTURE_2D, scene->textures[i].id);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, tex_w);
    glTexSubImage2D(GL_TEXTURE_2D, 0, pen_x, pen_y, img_w, img_h, GL_RGBA, GL_UNSIGNED_BYTE,
        &tex_data[(pen_y * tex_w) + pen_x]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    free(img_data);

    Bitmap_Dynarray *bitmaps = (Bitmap_Dynarray *) scene->bitmaps;
    i32 res = bitmaps->length;
    Bitmap b = { scene->cur_image_texture.texture_i, pen_x, pen_y, img_w, img_h };
    dynarray_add(bitmaps, &b);
    scene->cur_image_texture.pen_x += img_w;
    return res;
}

GL_Scene *create_scene(const char *vertex_shader, const char *fragment_shader, i32 vertex_size,
    i32 max_vertices, bool use_screen_coords)
{
    GL_Scene *scene = malloc(sizeof(GL_Scene));
    scene->vertices = calloc((u64) max_vertices*vertex_size, sizeof(float));
    scene->vertex_size = vertex_size;
    scene->capacity = max_vertices;
    scene->n = 0;
    if (vertex_size < 10) {
        fprintf(stderr, "vertex_size must be at least 10 (pos3+color4+tex2+font1)\n");
        goto fail;
    }
    scene->n_textures = 0;
    scene->n_fonts = 0;
    for (int i=0; i<SCENE_MAX_TEXTURES; i++) {
        scene->textures[i].id = 0;
        scene->textures[i].data = NULL;
        scene->fonts[i] = NULL;
    }
    scene->cur_image_texture.texture_i = -1;
    scene->bitmaps = new_dynarray(sizeof(Bitmap));

    if (!vertex_shader) vertex_shader = default_vertex_shader;
    if (!fragment_shader) fragment_shader = default_fragment_shader;
    scene->shader_program = compile_shader_program(vertex_shader, fragment_shader);
    if (scene->shader_program < 0) {
        goto fail;
    }
    scene->uYScale_location = glGetUniformLocation(scene->shader_program, "uYScale");
    scene->uTextures_location = glGetUniformLocation(scene->shader_program, "uTextures");
    scene->uTextureChannels_location = glGetUniformLocation(scene->shader_program, "uTextureChannels");

    glGenVertexArrays(1, &scene->vao);
    glBindVertexArray(scene->vao);
    glGenBuffers(1, &scene->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
    glBufferData(GL_ARRAY_BUFFER, (u64) max_vertices * vertex_size * sizeof(float), scene->vertices,
        GL_DYNAMIC_DRAW);
    // position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_size*sizeof(float), (void *) 0);
    glEnableVertexAttribArray(0);
    // color
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, vertex_size*sizeof(float), (void *) (3*sizeof(float)));
    glEnableVertexAttribArray(1);
    // texture coordinates
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vertex_size*sizeof(float), (void *) (7*sizeof(float)));
    glEnableVertexAttribArray(2);
    // font index
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, vertex_size*sizeof(float), (void *) (9*sizeof(float)));
    glEnableVertexAttribArray(3);
    // If the caller wants any more vertex attributes, they have to set them up themselves as above.

    i32 viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    scene->viewport_w = viewport[2];
    scene->viewport_h = viewport[3];
    scene->y_scale = (float) scene->viewport_h / scene->viewport_w;

    scene->use_screen_coords = use_screen_coords;

    return scene;
    fail:
    if (scene) {
        free(scene->vertices);
        free(scene);
    }
    return NULL;
}

void destroy_scene(GL_Scene *scene)
{
    for (i32 i=0; i<scene->n_fonts; i++) {
        destroy_font_atlas(scene->fonts[i]);
    }
    for (i32 i=0; i<scene->n_textures; i++) {
        glDeleteTextures(1, &scene->textures[i].id);
        if (scene->textures[i].data)
            free(scene->textures[i].data);
    }
    Bitmap_Dynarray *bitmaps = (Bitmap_Dynarray *) scene->bitmaps;
    free(bitmaps->d);
    free(bitmaps);
    free(scene->vertices);
    free(scene);
}

void reset_scene(GL_Scene *scene)
{
    i32 viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    scene->viewport_w = viewport[2];
    scene->viewport_h = viewport[3];
    scene->y_scale = (float) scene->viewport_h / scene->viewport_w;

    scene->n = 0;
}

void draw_scene(GL_Scene *scene)
{
    glUseProgram(scene->shader_program);
    glUniform1f(scene->uYScale_location, scene->y_scale);
    if (scene->uTextures_location >= 0) {
        GLint units[SCENE_MAX_TEXTURES];
        GLint channels[SCENE_MAX_TEXTURES];
        for (int i=0; i<scene->n_textures; i++) {
            units[i] = i;
            channels[i] = scene->textures[i].channels;
        }
        glUniform1iv(scene->uTextures_location, scene->n_textures, units);
        glUniform1iv(scene->uTextureChannels_location, scene->n_textures, channels);
    }
    for (int i=0; i<scene->n_textures; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, scene->textures[i].id);
    }
    glBindVertexArray(scene->vao);
    glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (u64) scene->n * scene->vertex_size * sizeof(float),
        scene->vertices);
    glDrawArrays(GL_TRIANGLES, 0, scene->n);
}

