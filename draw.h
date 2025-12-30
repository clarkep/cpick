#ifndef UTIL_DRAW_H
#define UTIL_DRAW_H

#include "util.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SCENE_MAX_TEXTURES 8

struct atlas_glyph_info;

typedef struct font_atlas {
    FT_Face ft_face;
    u32 *charset;
    u32 charset_n;
    u32 font_size_px;
    i32 max_ascent;
    i32 min_descent;
    i32 max_height;
    // u32 -> Vector2
    Hash_Table char_locations;
    struct atlas_glyph_info *info_table;
    u8 *data;
    i32 data_w;
    i32 data_h;
} Font_Atlas;

typedef struct texture_info {
    u32 id;
    void *data;
    i8 channels;
    i32 w;
    i32 h;
} Texture_Info;

typedef struct gl_scene {
    float *vertices;
    u32 vertex_size;
    u32 n;
    u32 capacity; // maximum number of vertices
    struct texture_info textures[SCENE_MAX_TEXTURES];
    i32 n_textures;
    struct {
        i32 texture_i;
        i32 pen_x;
        i32 pen_y;
    } cur_image_texture;
    struct font_atlas *fonts[SCENE_MAX_TEXTURES];
    i32 n_fonts;
    // internal Bitmap_Dynarray *
    void *bitmaps;
    i32 viewport_w;
    i32 viewport_h;
    bool use_screen_coords;
    float y_scale;
    u32 vao;
    u32 vbo;
    u32 shader_program;
    i32 uYScale_location;
    i32 uTextures_location;
    i32 uTextureChannels_location;
} GL_Scene;

i32 generate_rectangle(float *data, i32 stride, float x, float y, float w, float h, Vector4 color);
i32 generate_quad(float *data, i32 stride, Vector2 *corners);
i32 generate_circle(float *data, i32 stride, float x, float y, float r, i32 segments);
i32 generate_circle_arc(float *data, i32 stride, float x, float y, float r, float start_angle,
    float stop_angle, i32 segments);
i32 generate_superellipse(float *data, i32 stride, float x, float y, float a, float b, float n, i32 segments);
i32 generate_rounded_quad(float *data, i32 stride, Vector2 *corners, bool *rounded, float radius,
    i32 segments_per_corner);
void triangleize(float *data, i32 stride, i32 n, Vector2 center, Vector4 color, bool connected);
void outlineize(float *data, i32 stride, i32 n, float thickness, Vector4 color, bool connected);
void assert_not_overflowing(GL_Scene *scene);
void add_rectangle(GL_Scene *scene, float x, float y, float w, float h, Vector4 color);
void add_rectangle_outline(GL_Scene *scene, float x, float y, float w, float h, float thickness,
    Vector4 color);
void add_quad(GL_Scene *scene, Vector2 *corners, Vector4 color);
void add_quad_outline(GL_Scene *scene, Vector2 *corners, float thickness, Vector4 color);
void add_circle_slice(GL_Scene *scene, float x, float y, float r, float angle1, float angle2,
    i32 segments, Vector4 color);
void add_circle_arc(GL_Scene *scene, float x, float y, float r, float angle1, float angle2,
    i32 segments, float thickness, Vector4 color);
void add_circle(GL_Scene *scene, float x, float y, float r, float segments, Vector4 color);
void add_circle_outline(GL_Scene *scene, float x, float y, float r, float segments, float thickness,
    Vector4 color);
void add_superellipse(GL_Scene *scene, float x, float y, float a, float b, i32 n,
    float segments, Vector4 color);
void add_superellipse_outline(GL_Scene *scene, float x, float y, float a, float b, float n,
    float segments, float thickness, Vector4 color);
// if rounded is NULL, round all corners
void add_rounded_quad(GL_Scene *scene, Vector2 *corners, bool *rounded, float radius, i32 segments_per_corner,
    Vector4 color);
void add_rounded_quad_outline(GL_Scene *scene, Vector2 *corners, bool *rounded, float radius,
    i32 segments_per_corner, float thickness, Vector4 color);
void add_rounded_rectangle(GL_Scene *scene, float x, float y, float w, float h, float radius,
    float segments_per_corner, Vector4 color);
void add_rounded_rectangle_outline(GL_Scene *scene, float x, float y, float w, float h,
    float radius, float segments_per_corner, float thickness, Vector4 color);
void add_line(GL_Scene *scene, float x1, float y1, float x2, float y2, float thickness,
    Vector4 color);
void add_character(GL_Scene *scene, int font_i, float x, float y, u32 c, Vector4 color, float *advance_x);
void add_text(GL_Scene *scene, int font_i, const char *text, float x, float y, Vector4 color);
void add_text_utf32(GL_Scene *scene, int font_i, const u32 *text, float x, float y, Vector4 color);
float measure_text_width(GL_Scene *scene, int font_i, const char *text);
void add_image(GL_Scene *scene, i32 image_i, float x, float y);
GL_Scene *create_scene(const char *vertex_shader, const char *fragment_shader,
    i32 vertex_size, i32 max_vertices, bool use_screen_coords);
void destroy_scene(GL_Scene *scene);
// Unlike the other add_ functions, this is to be called once at initialization, not every frame
int load_font(GL_Scene *scene, const char *font_file, u32 font_size_px, u32 *charset, u32 charset_n);
int load_font_from_memory(GL_Scene *scene, const void *font_data, u64 data_size, u32 font_size_px,
    u32 *charset, u32 charset_n);
// supported types: "svg"
i32 load_image(GL_Scene *scene, const char *file, const char *type);
i32 load_image_from_memory(GL_Scene *scene, const void *data, u64 data_size, const char *type);
void reset_scene(GL_Scene *scene);
void draw_scene(GL_Scene *scene);

#ifdef __cplusplus
}
#endif

#endif