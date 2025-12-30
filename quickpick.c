/*
quickpick: a color picker

By: Paul Clarke
Created: 4/10/2024
License: GPL 3(see LICENSE)
*/

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h> // malloc, exit
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <math.h> // round
#include <stdbool.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <glad/glad.h>

// include only the declarations for stb_image because the implementation is included by draw.c
#include "stb_image.h"

#include "util.h"
#include "draw.h"

#include "font/noto_sans_mono.h"
#include "quickpick_icon.h"

#define WRITE_INTERVAL 0.5f
#define FONT_SMALL_PX 17*dpi
#define FONT_MEDIUM_PX 23*dpi
#define FONT_LARGE_PX 35*dpi
// For vertically centering text, fraction of the em box to place at the center of the region
#define CENTER_EM 0.35f

/**************/

typedef struct { float x, y, width, height; } Rectangle;

#define WHITE ((Vector4) { 1.0f, 1.0f, 1.0f, 1.0f })
#define BLACK ((Vector4){0.0f, 0.0f, 0.0f, 1.0f})

/*****************/

enum cursor_state { CURSOR_UP, CURSOR_START, CURSOR_DOWN, CURSOR_STOP };

typedef struct state {
	int screenWidth;
	int screenHeight;
	float dpi;
	int mode; // rgb(0) or hsv(1)
	int which_fixed; // red(0), green(1) or blue(2); or hue(0), saturation(1), or value(2)
	enum cursor_state cursor_state;
	bool square_dragging;
	bool val_slider_dragging;
	// The coordinate that that the slider controls, 0-1
	float fixed_value;
	// the other two dims, usually x and y but can be theta/r when fixed=value.
	float x_value;
	float y_value;
	// In hsv mode, you can manipulate rgb values and vice versa. In those cases we must treat the
	// alternate system as exact, or a double conversion occurs that can cause the manipulation of
	// one value, say r while in hsv mode, to affect others in the same system, say g and b.
	// alternate_value is the current color represented in the alternate color system, and
	// from_alternate_value signals that alternate_value is taken as the official current color.
	bool from_alternate_value;
	Vector3 alternate_value;
	Vector4 text_color;
	// Font text_font_small;
	// xx variable width fonts...
	// Font text_font_medium;
	// Font text_font_large;
	i32 text_font_small;
	i32 text_font_medium;
	i32 text_font_large;
	int small_char_width;
	int medium_char_width;
	int large_char_width;
	i32 small_font_max_ascent;
	i32 medium_font_max_ascent;
	i32 large_font_max_ascent;
	int medium_label_width;
	// Shader hsv_grad_shader;
	GL_Scene *main_scene;
	GL_Scene *hsv_grad_scene;
	SDL_Window *window;
	SDL_GLContext *gl_context;
	struct {
		char *path;
		u32 *shortened_path_utf32;
		i32 shortened_path_len;
		unsigned long long offset;
		int format;
		Vector4 last_write_color;
		double last_write_time;
	} outfile;
	bool debug;
	// XX Input state
	int mouse_x, mouse_y;
	bool mouse_down;
	bool mouse_was_down;
	int key_pressed;
	Uint32 start_ticks;
} State;

typedef struct tab_select
{
	Vector4 active_colors[3];
	Vector4 inactive_colors[3];
	char labels[3];
	float hover_brightness;
	Vector4 active_text_color;
	Vector4 inactive_text_color;
	Vector4 border_color;
	State *st;
	float anim_vdt;
	int x;
	int y;
	int w;
	int h;
	bool top;
	// internal state:
	int sel_i;
	float hover_v[3];
	float active_v[3];
} Tab_Select;

typedef struct number_select {
	char *fmt;
	int min;
	int max;
	bool wrap_around;
	State *st;
	float anim_vdt;
	int x;
	int y;
	float drag_pixels_per_value;
	// "internal" state:
	int w;
	int h;
	int value;
	bool selected;
	bool dragging;
	bool clicking;
	int drag_start_value;
	int drag_start_y;
	float shade_v;
	bool input_active;
	int input_n;
} Number_Select;

void myassert(bool p, char *fmt, ...) {
	if (!p) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		exit(1);
	}
}

char *color_strings[2][3] = { "R", "G", "B", "H", "S", "V" };

Vector4 hex2color(unsigned int hex) {
	return (Vector4){
		((hex >> 24) & 0xFF) / 255.0f,
		((hex >> 16) & 0xFF) / 255.0f,
		((hex >> 8) & 0xFF) / 255.0f,
		(hex & 0xFF) / 255.0f
	};
}

Vector4 color_brightness(Vector4 c, float factor) {
	float r = (float) c.x + ((1.0f - c.x) * factor);
	float g = (float) c.y + ((1.0f - c.y) * factor);
	float b = (float) c.z + ((1.0f - c.z) * factor);
	return (Vector4) {
		CLAMP(r, 0.0f, 1.0f),
		CLAMP(g, 0.0f, 1.0f),
		CLAMP(b, 0.0f, 1.0f),
		c.w
	};
}

Vector4 rgb_to_hsv(Vector4 c) {
	float r = c.x;
	float g = c.y;
	float b = c.z;

	float max = MAX(MAX(r, g), b);
	float min = MIN(MIN(r, g), b);
	float delta = max - min;

	Vector4 hsv;
	hsv.w = c.w;
	hsv.z = max; // value

	if (delta < 0.00001f) {
		hsv.x = 0;
		hsv.y = 0;
		return hsv;
	}

	hsv.y = (max > 0) ? (delta / max) : 0; // saturation

	if (r >= max) {
		hsv.x = (g - b) / delta;
	} else if (g >= max) {
		hsv.x = 2.0f + (b - r) / delta;
	} else {
		hsv.x = 4.0f + (r - g) / delta;
	}

	hsv.x /= 6.0f;
	if (hsv.x < 0) hsv.x += 1.0f;

	return hsv;
}

Vector4 hsv_to_rgb(Vector4 hsv)
{
	float h = hsv.x;
	float s = hsv.y;
	float v = hsv.z;
	float c = v * s;
	float x = c * (1 - fabsf(fmodf(h * 6.0f, 2) - 1));
	float m = v - c;

	float r, g, b;
	if (h < 1.0f/6.0f) { r = c; g = x; b = 0; }
	else if (h < 2.0f/6.0f) { r = x; g = c; b = 0; }
	else if (h < 3.0f/6.0f) { r = 0; g = c; b = x; }
	else if (h < 4.0f/6.0f) { r = 0; g = x; b = c; }
	else if (h < 5.0f/6.0f) { r = x; g = 0; b = c; }
	else { r = c; g = 0; b = x; }

	return (Vector4){ r + m, g + m, b + m, hsv.w };
}

bool CheckCollisionPointRec(Vector2 point, Rectangle rec) {
	return point.x >= rec.x && point.x <= rec.x + rec.width &&
	       point.y >= rec.y && point.y <= rec.y + rec.height;
}

bool CheckCollisionPointCircle(Vector2 point, Vector2 center, float radius) {
	float dx = point.x - center.x;
	float dy = point.y - center.y;
	return (dx*dx + dy*dy) <= (radius * radius);
}

double GetTime(State *st) {
	return (SDL_GetTicks() - st->start_ticks) / 1000.0;
}

Vector2 GetMousePosition(State *st) {
	return (Vector2){st->mouse_x, st->mouse_y};
}

bool read_color_from_outfile_and_maybe_update_offset(struct state *st, Vector4 *c)
{
	char *format = "%02x%02x%02x";
	FILE *f = fopen(st->outfile.path, "rb");
	fseek(f, st->outfile.offset, SEEK_SET);
	char str[8];
	fread(str, 1, 7, f);
	char *strptr = str;
	if (*strptr == '#') {
		st->outfile.offset++;
		strptr++;
	}
	str[7] = '\0';
	int r, b, g;
	int n_parsed = sscanf(strptr, format, &r, &g, &b);
	fclose(f);
	if (n_parsed == 3 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
		*c = (Vector4) { r / 255.0f, g / 255.0f, b / 255.0f, 1.0f };
		return true;
	} else {
		return false;
	}
}

void value_creep_towards(float *val, float target, float amount)
{
	if (*val < target) {
		*val = MIN(*val + amount, target);
	} else if (*val > target) {
		*val = MAX(*val - amount, target);
	}
}

bool vector4_equal(Vector4 c1, Vector4 c2) {
	return c1.x == c2.x && c1.y == c2.y && c1.z == c2.z && c1.w == c2.w;
}

struct color_info {
	Vector4 rgb;
	Vector4 hsv;
};

void update_color_or_mode(struct state *st, int mode, int fixed, struct color_info ci)
{
	Vector4 cur_rgb = ci.rgb;
	Vector4 cur_hsv = ci.hsv;

	if (st->mode) {
		if (fixed == 0) {
			st->fixed_value = cur_hsv.x;
			st->x_value = cur_hsv.y;
			st->y_value = cur_hsv.z;
		} else if (fixed == 1) {
			st->fixed_value = cur_hsv.y;
			st->x_value = cur_hsv.x;
			st->y_value = cur_hsv.z;
		} else if (fixed == 2) {
			st->fixed_value = cur_hsv.z;
			st->x_value = cur_hsv.x;
			st->y_value = cur_hsv.y;
		}
	} else {
		if (fixed == 0) {
			st->fixed_value = cur_rgb.x;
			st->x_value = cur_rgb.y;
			st->y_value = cur_rgb.z;
		} else if (fixed == 1) {
			st->fixed_value = cur_rgb.y;
			st->x_value = cur_rgb.z;
			st->y_value = cur_rgb.x;
		} else if (fixed == 2) {
			st->fixed_value = cur_rgb.z;
			st->x_value = cur_rgb.x;
			st->y_value = cur_rgb.y;
		}
	}
}

struct color_info current_color(struct state *st)
{
	struct color_info res;
	if (st->mode == 0 && !st->from_alternate_value || st->mode == 1 && st->from_alternate_value) {
		if (st->from_alternate_value) {
			res.rgb = (Vector4) { st->alternate_value.x, st->alternate_value.y,
				st->alternate_value.z, 1.0f };
		} else {
			float v1 = st->fixed_value;
			float v2 = st->x_value;
			float v3 = st->y_value;
			switch (st->which_fixed) {
			case 0:
				res.rgb = (Vector4) { v1, v2, v3, 1.0f };
				break;
			case 1:
				res.rgb = (Vector4) { v3, v1, v2, 1.0f };
				break;
			case 2:
				res.rgb = (Vector4) { v2, v3, v1, 1.0f };
				break;
			}
		}
		res.hsv = rgb_to_hsv(res.rgb);
	} else {
		if (st->from_alternate_value) {
			res.hsv = (Vector4) { st->alternate_value.x, st->alternate_value.y,
				st->alternate_value.z, 1.0f };
		} else {
			float v1 = st->fixed_value;
			float v2 = st->x_value;
			float v3 = st->y_value;
			switch (st->which_fixed) {
			case 0:
				res.hsv = (Vector4) { v1, v2, v3, 1.0f };
			break;
			case 1:
				res.hsv = (Vector4) { v2, v1, v3, 1.0f };
			break;
			case 2:
				// x=theta, y=r
				res.hsv = (Vector4) { v2, v3, v1, 1.0f };
			break;
			}
		}
		res.rgb = hsv_to_rgb(res.hsv);
	}
	if (st->from_alternate_value) {
		update_color_or_mode(st, st->mode, st->which_fixed, res);
	}
	return res;
}

void write_color_to_file(struct state *st, Vector4 color)
{
	char color_text[10];
	sprintf(color_text, "%02x%02x%02x", (int)(color.x*255.0f), (int)(color.y*255.0f),
		(int) (color.z*255.0f));

	FILE *f = fopen(st->outfile.path, "r+b");
	assertf(f, "Failed to open file: %s.\n", st->outfile.path);
	int res = fseek(f, st->outfile.offset, SEEK_SET);
	assertf(!res, "Failed to write byte %lu in file %s.\n", st->outfile.offset, st->outfile.path);
	fwrite(color_text, 1, 6, f);
	printf("Wrote %s to %s byte %llu.\n", color_text, st->outfile.path, st->outfile.offset);
	fclose(f);
}

// x, y = bottom left of gradient(for convenience with our visualization)
void add_gradient_square(GL_Scene *scene, float x, float y, float s, Vector4 *corner_colors)
{
	i32 stride = scene->vertex_size;
	float *data = scene->vertices + scene->n*scene->vertex_size;
	scene->n += 6;
	assert_not_overflowing(scene);
	float w = s;
	float h = s;
	if (scene->use_screen_coords) {
        x = x * (2.0f / scene->viewport_w) - 1.0f;
        y = y * (-2.0f / scene->viewport_w) + scene->y_scale;
        w = w * (2.0f / scene->viewport_w);
        h = h * (2.0f / scene->viewport_w);
	}
	Vector2 positions[4] = {
		{ x, y },
		{ x + w, y },
		{ x, y + h },
		{ x + w, y + h }
	};
	i32 corners[6] = { 0, 2, 1, 1, 3, 2};
	for (i32 i=0; i<6; i++) {
		i32 corner = corners[i];
		Vector2 pos = positions[corner];
		Vector4 color = corner_colors[corner];
		data[i*stride] = pos.x;
		data[i*stride+1] = pos.y;
		data[i*stride+2] = 0.0f;
		data[i*stride+3] = color.x;
		data[i*stride+4] = color.y;
		data[i*stride+5] = color.z;
		data[i*stride+6] = color.w;
		data[i*stride+7] = 0.0f;
		data[i*stride+8] = 0.0f;
		data[i*stride+9] = -1.0f;
	}
}

void draw_gradient_square_rgb(State *st, int x, int y, int size, int which_fixed, float fixed_val)
{
	Vector4 corner_cols[4];
	for (int i=0; i<2; i++) {
		for (int j=0; j<2; j++) {
			float c1 = j ? 1.0f : 0.0f;
			float c2 = i ? 1.0f : 0.0f;
			float f = fixed_val;
			if (which_fixed == 0) {
				corner_cols[i*2+j] =  (Vector4) { f, c1, c2, 1.0f };
			} else if (which_fixed == 1) { // green
				corner_cols[i*2+j] = (Vector4) { c2, f, c1, 1.0f };
			} else if (which_fixed == 2) { // blue
				corner_cols[i*2+j] = (Vector4) { c1, c2, f, 1.0f };
			}
		}
	}
	add_gradient_square(st->main_scene, x, y+size, size, corner_cols);
}

void draw_gradient_square_hsv(struct state *st, int x, int y, int size, int which_fixed,
	float fixed_val)
{
	Vector4 corner_cols[4];
	for (int i=0; i<2; i++) {
		for (int j=0; j<2; j++) {
			int c1 = j ? 1.0f : 0;
			int c2 = i ? 1.0f : 0;
			if (which_fixed == 0) { // hue
				// The easiest way to write the fragment shader for raylib is to store the hsv
				// values as if they are RGB colors, which means they have to be clipped to 0-255.
				// But there are no visible changes compared to the more precise cpu only version.
				corner_cols[i*2+j] =  (Vector4) { fixed_val, j, i, 1.0f };
			} else if (which_fixed == 1) { // saturation
				corner_cols[i*2+j] = (Vector4) { j, fixed_val, i, 1.0f };
			}
		}
	}
	Rectangle rec = { x, y, size, size };
	add_gradient_square(st->hsv_grad_scene, x, y+size, size, corner_cols);
	glUseProgram(0);
}

void draw_gradient_circle_and_axes(int x, int y, int r, float fixed_val, struct state *st)
{
    {
	    GL_Scene *scene = st->hsv_grad_scene;
	    float *data = scene->vertices + scene->vertex_size*scene->n;
	    scene->n += 360*3;
	    assert_not_overflowing(scene);
    	float x_ndc = x * (2.0f / scene->viewport_w) - 1.0f;
    	float y_ndc = y * (-2.0f / scene->viewport_w) + scene->y_scale;
    	float r_ndc = r *(2.0f / scene->viewport_w);
	    i32 stride = scene->vertex_size;
	    for (int i=0; i<360; i++) {
	    	float angle1 = 2*F_PI*i/360.0f;
	    	float angle2 = 2*F_PI*(i+1)/360.0f;
	    	i32 offset = 3*i*stride;
	    	data[offset] = x_ndc + r_ndc*cos(angle1);
	    	data[offset+1] = y_ndc + r_ndc*sin(angle1);
	    	data[offset+2] = 0.0f;
	    	data[offset+3] = i / 360.0f;
	    	data[offset+4] = 1.0f;
	    	data[offset+5] = fixed_val;
	    	data[offset+6] = 1.0f;
	    	data[offset+7] = 0.0f;
	    	data[offset+8] = 0.0f;
	    	data[offset+9] = 0.0f;
	    	data[offset+stride] = x_ndc + r_ndc*cos(angle2);
	    	data[offset+stride+1] = y_ndc + r_ndc*sin(angle2);
	    	data[offset+stride+2] = 0.0f;
	    	data[offset+stride+3] = (i+1) / 360.0f;
	    	data[offset+stride+4] = 1.0f;
	    	data[offset+stride+5] = fixed_val;
	    	data[offset+stride+6] = 1.0f;
	    	data[offset+stride+7] = 0.0f;
	    	data[offset+stride+8] = 0.0f;
	    	data[offset+stride+9] = -1.0f;
	    	data[offset+2*stride] = x_ndc;
	    	data[offset+2*stride+1] = y_ndc;
	    	data[offset+2*stride+2] = 0.0f;
	    	data[offset+2*stride+3] = i / 360.0f;
	    	data[offset+2*stride+4] = 0.0f;
	    	data[offset+2*stride+5] = fixed_val;
	    	data[offset+2*stride+6] = 1.0f;
	    	data[offset+2*stride+7] = 0.0f;
	    	data[offset+2*stride+8] = 0.0f;
	    	data[offset+2*stride+9] = -1.0f;
	    }
	}
	// tick marks
	float dpi = st->dpi;
	for (float ang=0.0f; ang<360.0f; ang+=30.0) {
		float dx = cosf(ang*2*F_PI/360.0f);
		float dy = sinf(ang*2*F_PI/360.0f);
		int length = 5*dpi;
		Vector2 start = { x + r * dx, y + r * dy };
		Vector2 end = { start.x + length * dx, start.y + length * dy };
		add_line(st->main_scene, start.x, start.y, end.x, end.y, 2.0*dpi, st->text_color);
	}
	// s arrow
	int arrow_len = 60*dpi;
	float arrow_w = 2.0*dpi;
	// int  = 4*dpi;
	// arrowhead
	float ah_len = 13*dpi;
	float ah_ang = (180-28)*2*F_PI/360.0f;
	float ah_w = 2.0*dpi;
	Vector2 arrow_end = (Vector2) { x+r+arrow_len, y};
	add_line(st->main_scene, x+r+12*dpi, y, arrow_end.x, arrow_end.y, arrow_w, st->text_color);
	Vector2 ah_left = { arrow_end.x + ah_len*cosf(ah_ang), arrow_end.y + ah_len*sinf(ah_ang)};
	Vector2 ah_right = { arrow_end.x + ah_len*cosf(-ah_ang), arrow_end.y + ah_len*sinf(-ah_ang)};
	add_line(st->main_scene, arrow_end.x, arrow_end.y, ah_left.x, ah_left.y, ah_w, st->text_color);
	add_line(st->main_scene, arrow_end.x, arrow_end.y, ah_right.x, ah_right.y, ah_w, st->text_color);
	add_text(st->main_scene, st->text_font_medium, "S", arrow_end.x-16.0*dpi, arrow_end.y-20.0*dpi,
		st->text_color);
	// h arrow
	float harr_d = 30*dpi;
	float harr_w = 2*dpi;
	float harr_ang1 = 12;
	float harr_ang2 = 28;
	Vector2 harr_end = { x + (r+harr_d+harr_w/2)*cosf(2*F_PI*harr_ang2/360.0f),
		y - (r+harr_d+harr_w/2)*sinf(2*F_PI*harr_ang2/360.0f) };
	float harr_dir_ang = (harr_ang2*2*F_PI / 360.0f) + F_PI / 2;
	// arrowhead
	float adj = 2*F_PI/120;
	// Angles are set up so that left arrowhead goes straight down
		Vector2 h_ah_left = { harr_end.x /*+ah_len*cosf(harr_dir_ang+ah_ang+adj)*/,
		harr_end.y-ah_len*sinf(harr_dir_ang+ah_ang+adj)};
	Vector2 h_ah_right = { harr_end.x+ah_len*cosf(harr_dir_ang-ah_ang+adj),
		harr_end.y-ah_len*sinf(harr_dir_ang-ah_ang+adj)};
	Vector4 c3 = st->text_color;
	// Todo: the arrowhead might still be a little off of the arrow body. Renderdoc it.
	add_line(st->main_scene, harr_end.x, harr_end.y, h_ah_left.x, h_ah_left.y, ah_w, c3);
	add_line(st->main_scene, harr_end.x, harr_end.y, h_ah_right.x, h_ah_right.y, ah_w,
		c3);
	add_text(st->main_scene, st->text_font_medium, "H", harr_end.x+18*dpi, harr_end.y-2*dpi,
		st->text_color);
	add_circle_arc(st->main_scene, x, y, r+harr_d+harr_w/2, 2*F_PI*harr_ang1/360.0f, 2*F_PI*harr_ang2/360.0f,
		30, 2.0f*dpi, st->text_color);
}

// TODO: parameter for 512 vs etc ?
void draw_axes(int x, int y, int w, int h, struct state *st)
{
	float dpi = st->dpi;
	int tick_sep = 64*dpi;
	int tick_width = 2*dpi;
	int y_tick_len = w/4;
	int x_tick_len = h/4;
	Vector4 tick_color = st->text_color;
	int label_size = 30*dpi;
	Vector4 label_color = st->text_color;

	char *x_label;
	char *y_label;
	if (!(st->mode == 1 && st->which_fixed == 1)) {
		x_label = color_strings[st->mode][(st->which_fixed+1)%3];
		y_label = color_strings[st->mode][(st->which_fixed+2)%3];
	} else {
		x_label = color_strings[1][0];
		y_label = color_strings[1][2];
	}
	// x axis label
	add_text(st->main_scene, st->text_font_medium, x_label,
			   x + 512*dpi/2 - label_size, y + 512*dpi + h, label_color);
	// y axis label
	add_text(st->main_scene, st->text_font_medium, y_label,
			   x - h, y + 512*dpi/2, label_color);
	// x axis
	for (int ix = x; ix < (x+512*dpi); ix += tick_sep) {
		add_rectangle(st->main_scene, ix, y+512*dpi, tick_width, x_tick_len, tick_color);
	}
	// y axis
	for (int yi = 0; yi < (512*dpi); yi += tick_sep) {
		add_rectangle(st->main_scene, x-y_tick_len, y + 512*dpi - yi - tick_width, y_tick_len,
			tick_width, tick_color);
	}
}

bool tab_select(Tab_Select *self, Vector2 pos, enum cursor_state cs)
{
	State *st = self->st;
	float dpi = st->dpi;
	int i = self->sel_i;
	Vector4 *active = self->active_colors;
	Vector4 *inactive = self->inactive_colors;
	Vector4 active_text = self->active_text_color;
	Vector4 inactive_text = self->inactive_text_color;
	float *hover_v = self->hover_v;
	Vector4 color1 = i == 0 ? active[0] : color_brightness(inactive[0], self->hover_brightness*hover_v[0]);
	Vector4 color2 = i == 1 ? active[1] : color_brightness(inactive[1], self->hover_brightness*hover_v[1]);
	Vector4 color3 = i == 2 ? active[2] : color_brightness(inactive[2], self->hover_brightness*hover_v[2]);
	Vector4 text_color1 = i == 0 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[0]);
	Vector4 text_color2 = i == 1 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[1]);
	Vector4 text_color3 = i == 2 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[2]);
	char text[2] = "X";
	float x = self->x;
	float tw = self->w / 3.0;
	float rnd = 7.0f; // rounded rectangle roundness
	float segs = 20; // rounded rectangle segments
	float off = 0 * dpi;
	float y = self->top ? self->y : self->y - off;
	// xx
	float h = self->h + off;
	float text_y = y + h/2.0f + FONT_SMALL_PX*CENTER_EM;
	Vector2 left_corners[4] = { { x, y }, { x, y+h }, { x+tw, y+h }, { x+tw, y }};
	bool left_rounded[4] = { self->top, !self->top, false, false };
	add_rounded_quad(st->main_scene, left_corners, left_rounded, rnd, segs, color1);
	add_rounded_quad_outline(st->main_scene, left_corners, left_rounded, rnd, segs, 1.0f,
		self->border_color);
	text[0] = self->labels[0];
	add_text(st->main_scene, st->text_font_small, text, x + (tw - st->small_char_width)/2.0,
		text_y, text_color1);
	float x_mid = x + tw;
	int last_x = x_mid + tw;
	int end_x = self->x + self->w;
	int last_w = end_x - last_x;
	Vector2 right_corners[4] = { { last_x, y }, { last_x, y+h }, { last_x+last_w, y+h },
		{ last_x+last_w, y } };
	bool right_rounded[4] = { false, false, !self->top, self->top };
	add_rounded_quad(st->main_scene, right_corners, right_rounded, rnd, segs, color3);
	add_rounded_quad_outline(st->main_scene, right_corners, right_rounded, rnd, segs, 1.0f,
		self->border_color);
	text[0] = self->labels[2];
	add_text(st->main_scene, st->text_font_small, text, last_x + (last_w - st->small_char_width)/2.0,
		text_y, text_color3);

	add_rectangle(st->main_scene, x_mid, self->y, tw, self->h, color2);
	add_rectangle_outline(st->main_scene, x_mid, self->y, tw, self->h, 1*dpi,
		self->border_color);
	text[0] = self->labels[1];
	add_text(st->main_scene, st->text_font_small, text, x_mid + (tw - st->small_char_width)/2.0,
		text_y, text_color2);

	bool updated = false;
	float hover_targets[3] = { 0.0f, 0.0f, 0.0f };
	if (CheckCollisionPointRec(pos, (Rectangle) { self->x, self->y, self->w, self->h})) {
		int tab_i = CLAMP((pos.x - self->x) / (self->w / 3.0f), 0, 2);
		if (cs == CURSOR_START && tab_i != self->sel_i) {
			self->sel_i = tab_i;
			updated = true;
		}
		hover_targets[tab_i] = 1.0f;
	}
	float active_targets[3] = { 0.0f, 0.0f, 0.0f };
	if (self->sel_i > 0) {
		active_targets[self->sel_i] = 1.0f;
	}
	for (int tab_i=0; tab_i<3; tab_i++) {
		value_creep_towards(&self->hover_v[tab_i], hover_targets[tab_i], self->anim_vdt);
		value_creep_towards(&self->active_v[tab_i], active_targets[tab_i], self->anim_vdt);
	}

	return updated;
}

bool number_select(Number_Select *self, Vector2 pos, enum cursor_state cs, int key)
{
	State *st = self->st;
	float dpi = st->dpi;
	int new_value = self->value;
	/*
	// if changing font, may need to measure.
	self->w = (st->medium_char_width + 1.5*dpi) * n_chars;
	self->h = 30*dpi;
	*/
	float a = st->text_color.x < .5f ? .25f + .25f*self->shade_v : .75f - .25f*self->shade_v;
	Vector4 hl_color = { a, a, a, .5f*self->shade_v };

	i32 text_y = self->y + self->h/2.0f + FONT_MEDIUM_PX*CENTER_EM;

	bool hovered = false;
	float rnd = 7.0f;
	i32 segs = 15;
	add_rounded_rectangle(st->main_scene, self->x - 10*dpi, self->y, self->w, self->h,
		rnd, segs, hl_color);
	i32 font_h = 30*dpi;
	char text[21];
	memset(text, 0, 21);
	if (self->input_active) {
		// Color only the number(%d) in the string fmt(there must be one and only one %d)
		int text_i = 0;
		int fmt_i = 0;
		int d_i;
		int d_chars;
		int len = (int) strlen(self->fmt);
		while (fmt_i <= len-2) {
			if (self->fmt[fmt_i] == '%' && self->fmt[fmt_i+1] == 'd') {
				d_chars = snprintf(text+text_i, 20-fmt_i, "%d", self->input_n);
				d_i = text_i;
				fmt_i += 2;
				text_i += d_chars;
				continue;
			} else if (self->fmt[fmt_i] == '%' && self->fmt[fmt_i+1] == '%') {
				// manually convert %% to % by skipping the first %
				fmt_i++;
				continue;
			} else {
				text[text_i++] = self->fmt[fmt_i++];
			}
			assert(text_i < 20);
		}
		text[text_i++] = self->fmt[fmt_i++];
		int x = self->x;
		char c = text[d_i];
		text[d_i] = '\0';
		add_text(st->main_scene, st->text_font_medium, text, x, text_y, st->text_color);
		text[d_i] = c;
		x += d_i*(st->medium_char_width + 1.5*dpi);
		c = text[d_i + d_chars];
		add_text(st->main_scene, st->text_font_medium, &text[d_i], x, text_y,
			st->text_color.x < 0.5f ? hex2color(0x303030ff) : hex2color(0xd8d8d8ff));
		text[d_i + d_chars] = c;
		x += d_chars * (st->medium_char_width + 1.5*dpi);
		add_text(st->main_scene, st->text_font_medium, &text[d_i+d_chars], x, text_y,
			st->text_color);
	} else {
		int n_chars = snprintf(text, 20, self->fmt, self->input_active ? self->input_n : self->value);
		add_text(st->main_scene, st->text_font_medium, text, self->x, text_y,
			st->text_color);
	}
	bool hit = CheckCollisionPointRec(pos, (Rectangle) { self->x, self->y, self->w, self->h });
	// xx can this logic be simplified?
	if ((hit && cs != CURSOR_DOWN) || self->dragging) {
		hovered = true;
	}
	// xx self->dragging here ensures that the click that's ending now started on the widget, but not that
	// it never left.
	if (hit && cs == CURSOR_STOP && self->clicking) {
		self->selected = true;
	}
	if (!hit && cs == CURSOR_START) {
		self->selected = false;
		self->input_active = false;
	}
	if (!hit) {
		self->clicking = false;
	}
	if (self->selected && key) {
		int key_num = (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) ? ((key-SDL_SCANCODE_1+1)%10)
			: (key >= SDL_SCANCODE_KP_1 && key <= SDL_SCANCODE_KP_0 ? ((key-SDL_SCANCODE_KP_1+1)%10) : -1);
		if (!self->input_active && key_num >= 0) {
			// This breaks input if self->min > 9, but that doesn't apply to us and would require
			// some special logic.
			if (key_num >= self->min && key_num <= self->max) {
				self->input_active = true;
				self->input_n = key_num;
			}
		} else if (self->input_active && key_num >= 0) {
			int new_input_n = 10 * self->input_n + key_num;
			if (new_input_n >= self->min && new_input_n <= self->max) {
				self->input_n = new_input_n;
			}
		}
		if (self->input_active && key == SDL_SCANCODE_BACKSPACE) {
			if (self->input_n >= 10) {
				self->input_n /= 10;
			} else {
				self->input_active = false;
			}
		}
		if (self->input_active && key == SDL_SCANCODE_ESCAPE) {
			self->input_active = false;
		}
		if (self->input_active && key == SDL_SCANCODE_RETURN) {
			new_value = self->input_n;
			self->input_active = false;
		}
	}
	if (hit && cs == CURSOR_START) {
		self->dragging = true;
		self->clicking = true;
		self->drag_start_y = pos.y;
		self->drag_start_value = self->value;
	}
	if (cs == CURSOR_STOP) {
		self->dragging = false;
	}
	if (self->dragging) {
		new_value = self->drag_start_value + (-(pos.y - self->drag_start_y) / (self->drag_pixels_per_value));
		if (self->wrap_around) {
			if (new_value < self->min) {
				new_value = self->max + 1 - (self->min - new_value) % (self->max + 1 - self->min);
			} else if (new_value > self->max) {
				new_value = self->min + (new_value - self->min) % (self->max + 1 - self->min);
			}
		} else {
			new_value = CLAMP(new_value, self->min, self->max);
			if (new_value == self->max || new_value == self->min) {
				// so that you can go past the end, and get immediate changes coming back
				self->drag_start_value = new_value;
				self->drag_start_y = pos.y;
			}
		}
	}
	float shade_v_target = 0.0;
	if (self->selected) {
		shade_v_target = 1.0f;
	} else if (hovered) {
		shade_v_target = 0.5f;
	}
	value_creep_towards(&self->shade_v, shade_v_target, self->anim_vdt * .6);
	if (new_value != self->value) {
		self->value = new_value;
		return true;
	} else {
		return false;
	}
}

bool number_select_immargs(Number_Select *ns, char *fmt, int min, int max, bool wrap_around,
	State *st, float anim_vdt, int x, int y, int w, int h, float drag_pixels_per_value, Vector2 pos,
	enum cursor_state cs, int key)
{
	ns->fmt = fmt;
	ns->min = min;
	ns->max = max;
	ns->wrap_around = wrap_around;
	ns->st = st;
	ns->anim_vdt = anim_vdt;
	ns->x = x;
	ns->y = y;
	ns->w = w;
	ns->h = h;
	ns->drag_pixels_per_value = drag_pixels_per_value;
	return number_select(ns, pos, cs, key);
}

float luminance(float r, float g, float b)
{
  float rs = r <= 0.3928f ? r / 12.92f : powf((r+0.055f)/1.055f, 2.4f);
  float gs = g <= 0.3928f ? g / 12.92f : powf((g+0.055f)/1.055f, 2.4f);
  float bs = b <= 0.3928f ? b / 12.92f : powf((b+0.055f)/1.055f, 2.4f);
  return 0.2126f * rs + 0.7152f * gs + 0.0722f * bs;
}

void draw_ui_and_respond_input(struct state *st)
{
	// frame counter for debugging
	static u64 frame_n = 0;
	// setup input
	if (st->mouse_down) {
		if (st->cursor_state == CURSOR_UP || st->cursor_state == CURSOR_STOP)
			st->cursor_state = CURSOR_START;
		else if (st->cursor_state == CURSOR_START)
			st->cursor_state = CURSOR_DOWN;
	} else {
		if (st->cursor_state == CURSOR_DOWN || st->cursor_state == CURSOR_START) {
			st->cursor_state = CURSOR_STOP;
			st->square_dragging = false;
			st->val_slider_dragging = false;
		} else if (st->cursor_state == CURSOR_STOP)
			st->cursor_state = CURSOR_UP;
	}
	Vector2 pos = GetMousePosition(st);
	// consume one keypress per frame
	int key = st->key_pressed;
	float anim_vdt = 0.2f;

	struct color_info ci = current_color(st);
	Vector4 cur_color = ci.rgb;
	Vector4 cur_hsv = ci.hsv;
	glClearColor(cur_color.x, cur_color.y, cur_color.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	float dpi = st->dpi;
	if (luminance(cur_color.x, cur_color.y, cur_color.z) >= 0.179) {
		st->text_color = BLACK;
	} else {
		st->text_color = WHITE;
	}

	// misc ui colors
	float dark_text_bright_grey_bg = 208.0f / 255.0f;
	float dark_text_dim_grey_bg = 144.0f / 255.0f;
	float light_text_bright_grey_bg = 160.0f / 255.0f;
	float light_text_dim_grey_bg = 80.0f / 255.0f;
	Vector4 fixed_indication_color;
	Vector4 light_text_indication_color;
	if (st->mode == 0) {
		int wf = st->which_fixed;
		int rgb_fixed_ind[] = { 0xc00000ff, 0x00c000ff, 0x0080ffff };
		fixed_indication_color = hex2color(rgb_fixed_ind[wf]);
		light_text_indication_color = fixed_indication_color;
	} else {
		float a = st->text_color.x < .05 ? dark_text_bright_grey_bg : light_text_bright_grey_bg;
		float b = light_text_bright_grey_bg;
		fixed_indication_color = (Vector4) { a, a, a, 1.0f };
		light_text_indication_color = (Vector4) { b, b, b, 1.0f };
	}

	// output file indicator
	int out_ind_top_w = 512*dpi;
	int out_ind_bottom_w = 462*dpi;
	int out_ind_h = 30*dpi;
	int out_ind_top_x = (st->screenWidth - out_ind_top_w) / 2.0f;
	int out_ind_bottom_x = (st->screenWidth - out_ind_bottom_w) / 2.0f;
	int out_ind_top_y = 0;
	int out_ind_bottom_y = out_ind_top_y + out_ind_h;
	Vector2 out_ind_verts[4] = { { out_ind_bottom_x, out_ind_bottom_y }, { out_ind_bottom_x + out_ind_bottom_w, out_ind_bottom_y},
	{ out_ind_top_x + out_ind_top_w, out_ind_top_y }, { out_ind_top_x, out_ind_top_y }};
	bool out_ind_rounded[4] = { true, true, false, false };
	Vector4 out_ind_bgcolor = hex2color(0x303030c0);
	if (st->outfile.path) {
		i32 text_x = out_ind_bottom_x + (out_ind_bottom_w - st->outfile.shortened_path_len*(st->small_char_width+1.0*dpi))/2.0f;
		i32 text_y = out_ind_top_y + out_ind_h/2.0f + FONT_SMALL_PX*CENTER_EM;
		add_rounded_quad(st->main_scene, out_ind_verts, out_ind_rounded, 12*dpi, 12,
			out_ind_bgcolor);
		add_text_utf32(st->main_scene, st->text_font_small, st->outfile.shortened_path_utf32,
			text_x, text_y, WHITE);
	}

	// gradient
	int grad_y_axis_w = 30*dpi;
	int grad_x_axis_h = 30*dpi;
	int grad_square_x = (st->screenWidth - 512*dpi)/2;
	int grad_square_y = out_ind_bottom_y+(st->outfile.path ? 10*dpi : 0)+20*dpi;
	int grad_square_y_end = grad_square_y + 512*dpi;
	int grad_square_x_end = grad_square_x + 512*dpi;
	bool grad_square = true;
	int grad_circle_x = grad_square_x + 512*dpi/2;
	int grad_circle_y = grad_square_y + 512*dpi/2;
	int grad_circle_r = 512*dpi/2;
	if (st->mode == 0) {
		draw_gradient_square_rgb(st, grad_square_x, grad_square_y, 512*dpi, st->which_fixed, st->fixed_value);
		draw_axes(grad_square_x, grad_square_y, grad_x_axis_h, grad_y_axis_w, st);
	} else {
		if (st->which_fixed == 2) {
			grad_square = false;
			draw_gradient_circle_and_axes(grad_circle_x, grad_circle_y, grad_circle_r, st->fixed_value, st);
		} else {
			draw_gradient_square_hsv(st, grad_square_x, grad_square_y, 512*dpi, st->which_fixed,
				st->fixed_value);
			draw_axes(grad_square_x, grad_square_y, grad_x_axis_h, grad_y_axis_w, st);
		}
	}
	int cur_loc_sq_sz = 4*dpi;
	// indicator circle
	int ind_x, ind_y;
	if (grad_square) {
		ind_x = grad_square_x + st->x_value * (512*dpi);
		ind_y = grad_square_y + 512*dpi - st->y_value * (512*dpi);
	} else {
		ind_x = grad_circle_x + grad_circle_r*st->y_value*cosf(st->x_value * 2*F_PI);
		ind_y = grad_circle_y - grad_circle_r*st->y_value*sinf(st->x_value * 2*F_PI);
	}
	add_circle_outline(st->main_scene, ind_x, ind_y, 6*dpi, 20, 1*dpi, st->text_color);
	int r2 = 4*dpi;
	int r3 = 8*dpi;
	add_line(st->main_scene, ind_x - r3, ind_y, ind_x - r2, ind_y, 1*dpi, st->text_color);
	add_line(st->main_scene, ind_x + r2, ind_y, ind_x + r3, ind_y, 1*dpi, st->text_color);
	add_line(st->main_scene, ind_x, ind_y - r3, ind_x, ind_y - r2, 1*dpi, st->text_color);
	add_line(st->main_scene, ind_x, ind_y + r2, ind_x, ind_y + r3, 1*dpi, st->text_color);
	if (st->cursor_state == CURSOR_START || st->square_dragging) {
		if (!st->square_dragging) {
			Rectangle rec = {grad_square_x, grad_square_y, 512*dpi, 512*dpi};
			Vector2 c = { grad_square_x + 512*dpi/2, grad_square_y + 512*dpi/2 };
			if ((grad_square && CheckCollisionPointRec(pos, rec))
				|| (!grad_square && CheckCollisionPointCircle(pos, c, 512*dpi/2))) {
				st->square_dragging = true;
			}
		}
		if (st->square_dragging) {
			int y_adj = 3*dpi;
			int x_adj = 2*dpi;
			if (grad_square) {
				st->x_value = MIN(MAX((pos.x - x_adj - grad_square_x) / (512*dpi), 0.0f), 1.0f);
				// xx off by one?
				st->y_value = MIN(MAX((grad_square_y + 512*dpi - pos.y + y_adj) / (512*dpi), 0.0f), 1.0f);
			} else {
				int x_res = pos.x -x_adj - (grad_square_x + 512*dpi/2);
				int y_res = pos.y - y_adj - (grad_square_y + 512*dpi/2);
				y_res = -y_res;
				// theta
				st->x_value = atan2(y_res, x_res) / (2*F_PI);
				st->x_value = st->x_value < 0 ? 1.0 + st->x_value : st->x_value;
				// r
				st->y_value = MIN(MAX(sqrtf(x_res*x_res+y_res*y_res)/(512*dpi/2), 0.0), 1.0);
			}
			// xx check if we actually changed the color?
			st->from_alternate_value = false;
		}
	}

	// fixed color buttons
	int top_tabs_x = grad_square_x;
	int top_tabs_y = grad_square_y_end + grad_x_axis_h + 10*dpi;
	int top_tabs_h = 30*dpi;
	int top_tabs_w = 95*dpi;
	int main_button_x = top_tabs_x;
	int main_button_y = top_tabs_y + top_tabs_h;
	int main_button_w = top_tabs_w;
	int main_button_h = 75*dpi;
	int ind_tabs_y = main_button_y + main_button_h;
	Vector4 buttons_border_color = hex2color(0xb0b0b0ff);
	// tabs
	static Tab_Select rgb_tabs;
	static Tab_Select hsv_tabs;
	static bool first_frame_setup_done = false;
	if (!first_frame_setup_done) {
		rgb_tabs.active_colors[0] = hex2color(0xc00000ff);
		rgb_tabs.active_colors[1] = hex2color(0x00c000ff);
		rgb_tabs.active_colors[2] = hex2color(0x0080ffff);
		rgb_tabs.inactive_colors[0] = hex2color(0x700000ff);
		rgb_tabs.inactive_colors[1] = hex2color(0x007000ff);
		rgb_tabs.inactive_colors[2] = hex2color(0x0000c0ff);
		rgb_tabs.active_text_color = hex2color(0xffffffff);
		rgb_tabs.inactive_text_color = hex2color(0xa0a0a0ff);
		rgb_tabs.labels[0] = 'R';
		rgb_tabs.labels[1] = 'G';
		rgb_tabs.labels[2] = 'B';
		rgb_tabs.top = true;
		float a = light_text_bright_grey_bg;
		float b = light_text_dim_grey_bg;
		Vector4 bright = { a, a, a, 1.0f };
		Vector4 dim = { b, b, b, 1.0f };
		for (int i=0; i<3; i++) {
			hsv_tabs.active_colors[i] = bright;
			hsv_tabs.inactive_colors[i] = dim;
		}
		float sel_hov_brightness = 0.4f;
		hsv_tabs.active_text_color = rgb_tabs.active_text_color;
		hsv_tabs.inactive_text_color = rgb_tabs.inactive_text_color;
		rgb_tabs.hover_brightness = sel_hov_brightness;
		rgb_tabs.anim_vdt = anim_vdt;
		rgb_tabs.st = st;
		hsv_tabs.labels[0] = 'H';
		hsv_tabs.labels[1] = 'S';
		hsv_tabs.labels[2] = 'V';
		hsv_tabs.hover_brightness = sel_hov_brightness;
		hsv_tabs.anim_vdt = anim_vdt;
		hsv_tabs.st = st;
		hsv_tabs.top = false;
		first_frame_setup_done = true;
	}
	rgb_tabs.border_color = buttons_border_color;
	hsv_tabs.border_color = rgb_tabs.border_color;
	if (!st->mode) {
		rgb_tabs.sel_i = st->which_fixed;
		hsv_tabs.sel_i = -1;
	} else {
		rgb_tabs.sel_i = -1;
		hsv_tabs.sel_i = st->which_fixed;
	}
	rgb_tabs.x = top_tabs_x;
	rgb_tabs.y = top_tabs_y;
	rgb_tabs.w = top_tabs_w;
	rgb_tabs.h = top_tabs_h;
	if (tab_select(&rgb_tabs, pos, st->cursor_state)) {
		st->mode = 0;
		st->which_fixed = rgb_tabs.sel_i;
		update_color_or_mode(st, st->mode, st->which_fixed, ci);
	}
	hsv_tabs.x = main_button_x;
	hsv_tabs.y = main_button_y + main_button_h;
	hsv_tabs.w = main_button_w;
	hsv_tabs.h = top_tabs_h;
	if (tab_select(&hsv_tabs, pos, st->cursor_state)) {
		st->mode = 1;
		st->which_fixed = hsv_tabs.sel_i;
		update_color_or_mode(st, st->mode, st->which_fixed, ci);
	}
	// main button
	static float main_button_hover_v = 0;
	float hov_bright = 0.4f;
	Vector4 fixed_button_color = color_brightness(light_text_indication_color, main_button_hover_v * hov_bright);
	add_rectangle(st->main_scene, main_button_x, main_button_y, main_button_w, main_button_h,
		fixed_button_color);
	add_rectangle_outline(st->main_scene, main_button_x, main_button_y, main_button_w, main_button_h,
		1*dpi, buttons_border_color);
	i32 main_button_text_x = main_button_x + main_button_w/2.0f-st->large_char_width/2.0f;
	i32 main_button_text_y = main_button_y + main_button_h/2.0f+FONT_LARGE_PX*CENTER_EM;
	add_text(st->main_scene, st->text_font_large, color_strings[st->mode][st->which_fixed],
		main_button_text_x, main_button_text_y, WHITE);
	if (CheckCollisionPointRec(pos, (Rectangle) { main_button_x, main_button_y, main_button_w,
		ind_tabs_y-main_button_y})) {
		if (st->cursor_state == CURSOR_START) {
			st->which_fixed = (st->which_fixed + 1) % 3;
			update_color_or_mode(st, st->mode, st->which_fixed, ci);
			main_button_hover_v = 0;
		}
		if (st->cursor_state != CURSOR_DOWN) {
			main_button_hover_v = MIN(main_button_hover_v + anim_vdt, 1.0);
		}
	} else {
		main_button_hover_v = MAX(main_button_hover_v - anim_vdt, 0.0);
	}

	// fixed value slider
	int val_slider_x = main_button_x + main_button_w + 30*dpi;
	// center vertically relative to two adjacent buttons
	int val_slider_y = main_button_y + main_button_h / 2.0f;
	int val_slider_w = grad_square_x_end - val_slider_x;
	int val_slider_h = 60*dpi;
	int val_slider_offset = roundf(val_slider_w * ( (float) st->fixed_value ));
	{
		int bar_h = 8*dpi;
		int circle_r = 15*dpi;
		add_rounded_rectangle(st->main_scene, val_slider_x, val_slider_y-bar_h/2.0f, val_slider_w, bar_h,
			3.0f, 10, st->text_color);
		Vector2 circle_center = {  };
		add_circle(st->main_scene, val_slider_x + val_slider_offset, val_slider_y, 15*dpi,
			30, fixed_indication_color);
		if (st->cursor_state == CURSOR_START || st->val_slider_dragging) {
			if (!st->val_slider_dragging && CheckCollisionPointRec(pos,
				(Rectangle) { val_slider_x - circle_r, val_slider_y-val_slider_h/2.0f,
					val_slider_w+2*circle_r, val_slider_h } )) {
				st->val_slider_dragging = true;
			}
			if (st->val_slider_dragging) {
				val_slider_offset = MIN(val_slider_w, MAX(0, pos.x - val_slider_x));
				st->fixed_value = MIN(MAX((float) val_slider_offset / val_slider_w, 0), 1.0);
				// xx check if we actually changed the color?
				st->from_alternate_value = false;
			}
		}
	}

	// rgb number selectors
	bool rgb_num_select_changed = false;
	int rgb_select_w = 6*(st->medium_char_width + 1.5*dpi);
	int r_select_x = (st->screenWidth - st->medium_label_width)/2.0f;
	int r_select_y = val_slider_y + 90*dpi;
	static Number_Select r_num_select;
	r_num_select.value = cur_color.x * 255.0f;
	if (number_select_immargs(&r_num_select, "r:%d ", 0, 255, false, st, anim_vdt,
		r_select_x, r_select_y, rgb_select_w, 30*dpi, 800.0f / 256.0f, pos, st->cursor_state, key)) {
		rgb_num_select_changed = true;
	}
	static Number_Select g_num_select;
	g_num_select.value = cur_color.y * 255.0f;
	if (number_select_immargs(&g_num_select, "g:%d ", 0, 255, false, st, anim_vdt,
		r_num_select.x+r_num_select.w, r_num_select.y, rgb_select_w, 30*dpi, 800.0f / 256.0f,
		pos, st->cursor_state, key)) {
		rgb_num_select_changed = true;
	}
	static Number_Select b_num_select;
	b_num_select.value = cur_color.z * 255.0f;
	if (number_select_immargs(&b_num_select, "b:%d ", 0, 255, false, st, anim_vdt,
		g_num_select.x+g_num_select.w, g_num_select.y, rgb_select_w, 30*dpi, 800.0f / 256.0f,
		pos, st->cursor_state, key)) {
		rgb_num_select_changed = true;
	}
	if (rgb_num_select_changed) {
		Vector4 new_rgb = { r_num_select.value / 255.0f,  g_num_select.value / 255.0f,
			b_num_select.value / 255.0f, 1.0f };
		if (st->mode == 1) {
			st->from_alternate_value = true;
			st->alternate_value = (Vector3) { new_rgb.x, new_rgb.y, new_rgb.z };
		} else if (st->from_alternate_value) {
			st->from_alternate_value = false;
		}
		Vector4 new_hsv = rgb_to_hsv(new_rgb);
		struct color_info new_ci = { new_rgb, new_hsv };
		update_color_or_mode(st, st->mode, st->which_fixed, new_ci);
	}
	// hex label
	char value[40];
	sprintf(value, "hex:#%02x%02x%02x", (int)(cur_color.x*255.0f), (int)(cur_color.y*255.0f),
		(int)(cur_color.z*255.0f));
	int hex_label_x = b_num_select.x + b_num_select.w;
	int hex_label_y = r_num_select.y + r_num_select.h/2.0f + FONT_MEDIUM_PX*CENTER_EM;
	i32 font_h = 30*dpi;
	add_text(st->main_scene, st->text_font_medium, value, hex_label_x, hex_label_y,
		st->text_color);
	// hsv number selectors
	bool hsv_num_select_changed = false;
	int hsv_select_w = 7*(st->medium_char_width + 1.5*dpi);
	static Number_Select h_num_select;
	h_num_select.value = cur_hsv.x * 360.0f;
	if (number_select_immargs(&h_num_select, "h:%d\xc2\xb0", 0, 359, true, st, anim_vdt,
		r_num_select.x, r_num_select.y + 35*dpi, hsv_select_w, 30*dpi, 800.0f/360.0f,
		pos, st->cursor_state, key)) {
		hsv_num_select_changed = true;
	}
	static Number_Select s_num_select;
	s_num_select.value = cur_hsv.y * 100.0f;
	if (number_select_immargs(&s_num_select, "s:%d%% ", 0, 100, false, st, anim_vdt,
		h_num_select.x+h_num_select.w, h_num_select.y, hsv_select_w, 30*dpi, 800.0f/100.0f,
		pos, st->cursor_state, key)) {
		hsv_num_select_changed = true;
	}
	static Number_Select v_num_select;
	v_num_select.value = cur_hsv.z * 100.0f;
	if (number_select_immargs(&v_num_select, "v:%d%% ", 0, 100, false, st, anim_vdt,
		s_num_select.x+s_num_select.w, h_num_select.y, hsv_select_w, 30*dpi, 800.0f/100.0f,
		pos, st->cursor_state, key)) {
		hsv_num_select_changed = true;
	}
	if (hsv_num_select_changed) {
		Vector4 new_hsv = { h_num_select.value / 360.0f, s_num_select.value / 100.0f,
			v_num_select.value / 100.0f, 1.0f };
		if (st->mode == 0) {
			st->from_alternate_value = true;
			st->alternate_value = (Vector3) { new_hsv.x, new_hsv.y, new_hsv.z };
		} else if (st->from_alternate_value) {
			st->from_alternate_value = false;
		}
		Vector4 new_rgb = hsv_to_rgb(new_hsv);
		struct color_info new_ci = { new_rgb, new_hsv };
		update_color_or_mode(st, st->mode, st->which_fixed, new_ci);
	}
	if (rgb_num_select_changed && hsv_num_select_changed) {
		printf("rgb and hsv selects changed\n");
	}

	// write to file
	double now = GetTime(st);
	if (st->outfile.path && now - st->outfile.last_write_time > WRITE_INTERVAL &&
		!vector4_equal(cur_color, st->outfile.last_write_color)) {
		write_color_to_file(st, cur_color);
		st->outfile.last_write_color = cur_color;
		st->outfile.last_write_time = now;
	}
	frame_n++;
}

const char* hsv_grad_vertex_shader =
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

const char* hsv_grad_fragment_shader =
"#version 330 core\n"
"#define MAX_FONTS 8\n"
"out vec4 FragColor;\n"
"in vec4 fColor;\n"
"in vec2 TexCoord;\n"
"flat in float fFontIndex;\n"
"uniform sampler2D uFonts[MAX_FONTS];\n"
"vec3 hsv2rgb(vec3 c) {\n"
"    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
"    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
"    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
"}\n"
"void main()\n"
"{\n"
"    vec4 base = fColor;\n"
"    FragColor = vec4(hsv2rgb(base.xyz), base.w);\n"
"}";

char *usage_str =
"quickpick [file@offset]\n"
"Options:\n"
"  --file FILE     choose a file to output to; alternative to file@offset\n"
"  --offset N      choose an offset in FILE; alternative to file@offset\n";

void init_for_dpi(struct state *st, float dpi, float old_dpi, u32 *small_charset,
	u32 small_charset_n)
{
	st->dpi = dpi;
	float ratio = dpi / old_dpi;
	int new_target_w = st->screenWidth * ratio;
	int new_target_h = st->screenHeight * ratio;
	if (new_target_w  != st->screenWidth) {
		SDL_SetWindowSize(st->window, new_target_w, new_target_h);
		st->screenWidth = new_target_w;
		st->screenHeight = new_target_h;
	}

	st->text_font_small = load_font_from_memory(st->main_scene, noto_sans_mono,
		noto_sans_mono_len, FONT_SMALL_PX, small_charset, small_charset_n);
	st->text_font_medium = load_font_from_memory(st->main_scene, noto_sans_mono,
		noto_sans_mono_len, FONT_MEDIUM_PX, NULL, 0);
	st->text_font_large = load_font_from_memory(st->main_scene, noto_sans_mono,
		noto_sans_mono_len, FONT_LARGE_PX, NULL, 0);

	st->medium_label_width = measure_text_width(st->main_scene, st->text_font_medium,
		"r:255 g:255 b:255 hex:#ffffff");
	st->small_char_width = measure_text_width(st->main_scene, st->text_font_small, "R");
	st->medium_char_width = measure_text_width(st->main_scene, st->text_font_medium, "R");
	st->large_char_width = measure_text_width(st->main_scene, st->text_font_large, "R");
	st->small_font_max_ascent = st->main_scene->fonts[st->text_font_small]->max_ascent;
	st->medium_font_max_ascent = st->main_scene->fonts[st->text_font_medium]->max_ascent;
	st->large_font_max_ascent = st->main_scene->fonts[st->text_font_large]->max_ascent;
}

SDL_Surface* surface_from_memory(const u8* buffer, int size, u8 **bmp) {
	int x, y, channels;
	*bmp = stbi_load_from_memory (buffer, size, &x, &y, &channels, 4);

    SDL_Surface* res = SDL_CreateRGBSurfaceFrom(*bmp, x, y, 32, x*4, 0xff, 0xff00, 0xff0000, 0xff000000);
    if (!res) {
        fprintf(stderr, "SDL_CreateRGBSurfaceFrom failed: %s\n", SDL_GetError());
        return NULL;
    }
//    stbi_image_free(bmp);
    return res;
}
int main(int argc, char *argv[])
{
	struct state *st = (struct state *) calloc(1, sizeof(struct state));
	st->screenWidth = 680;
	st->screenHeight = 860;
	st->mode = 0;
	st->which_fixed = 0;
	st->fixed_value = 0.0f;
	st->x_value = 0;
	st->y_value = 0;
	st->square_dragging = false;
	st->val_slider_dragging = false;
	st->text_color = WHITE;
	st->outfile.path = NULL;
	st->outfile.offset = 0;
	st->outfile.format = 0;
	st->outfile.last_write_color = (Vector4) { 0, 0, 0, 1.0f };
	st->debug = false;

	for (int i=1; i<argc; i++) {
		char *arg = argv[i];
		if (argv[i][0] == '-' && argv[i][1] == '-') {
			char *longarg = &argv[i][2];
			if (strcmp(longarg, "file")==0) {
				assertf(i+1<argc && !st->outfile.path, usage_str);
				st->outfile.path = argv[i+1];
				i++;
			} else if (strcmp(longarg, "offset")==0) {
				assertf(i+1<argc && !st->outfile.offset, usage_str);
				errno = 0;
				st->outfile.offset = strtoull(argv[i+1], NULL, 10);
				assertf(!errno, usage_str);
				i++;
			}
		} else if (argv[i][0] == '-') {
			errexit(usage_str);
		} else {
			assertf(!st->outfile.path, usage_str);
			char *sep = strchr(arg, '@');
			assertf(sep, usage_str);
			int path_len = sep - arg;
			st->outfile.path = malloc(path_len+1);
			memcpy(st->outfile.path, arg, path_len);
			st->outfile.path[path_len] = '\0';
			errno = 0;
			st->outfile.offset = strtoul(sep+1, NULL, 10);
			assertf(!errno, usage_str);
			i++;
		}
	}

	if (st->debug && st->outfile.path) {
		printf("Outfile: %s\n", st->outfile.path);
	}

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		errexit("SDL_Init failed: %s\n", SDL_GetError());
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	u8 *bmp;
	SDL_Surface *icon_surface = surface_from_memory(quickpick_icon_png, quickpick_icon_png_len, &bmp);

	st->window = SDL_CreateWindow("QuickPick",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		st->screenWidth, st->screenHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!st->window) {
		errexit("SDL_CreateWindow failed: %s\n", SDL_GetError());
	}

	if (icon_surface) {
		SDL_SetWindowIcon(st->window, icon_surface);
		SDL_FreeSurface(icon_surface);
		stbi_image_free(bmp);
	}

	st->gl_context = SDL_GL_CreateContext(st->window);
	if (!st->gl_context) {
		errexit("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
	}

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD");
        goto exit;
    }

	// xxx this stuff...
	// enable vsync
	// SDL_GL_SetSwapInterval(1);
	// glEnable(GL_BLEND);
	// glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// st->hsv_grad_shader = create_shader_program(hsv_grad_vertex_shader, hsv_grad_fragment_shader);

    // The charset for our small font includes the default ASCII characters, and anything in the outfile
    // name which will be displayed at the top of the window.
	u32 *small_charset = malloc((128 + st->outfile.shortened_path_len)*sizeof(u32));
	u32 small_charset_n = 0;
    for (i32 i=0x20; i<0x7f; i++) {
        small_charset[small_charset_n++] = i;
    }
    char *spath;
    size_t spath_len;
    // If we have an outfile, shorten its name for the outfile indicator convert it to utf-32, and
    // add its codepoints to the small charset.
	if (st->outfile.path) {
		int maxlen = 5 + (int) strlen(st->outfile.path) + 3 + 20 + 1;
		char *spath = (char *) malloc(maxlen);
		int n = snprintf(spath, maxlen, "out: %s @ %llu", st->outfile.path, st->outfile.offset);

		int str_n = MIN(maxlen-1, n);
		// Todo: expand based on window size
		int max_chars = 46;
		int remove = MAX(str_n - max_chars, 0);
		if (remove) {
			memmove(spath + 5, "...", 3);
			memmove(spath + 8, spath+8+remove, str_n-(8+remove)+1);
		}
		spath_len = str_n - remove;
		assertf(spath_len == strlen(spath), NULL);

		Vector4 start_color;
		bool success = read_color_from_outfile_and_maybe_update_offset(st, &start_color);
		if (success) {
			struct color_info ci;
			ci.rgb = start_color;
			ci.hsv = rgb_to_hsv(start_color);
			update_color_or_mode(st, st->mode, st->which_fixed, ci);
		} else {
			// since we failed to read, we probably shouldn't write
			fprintf(stderr, "[QUICKPICK WARNING] Failed to find a valid rrggbb(or #rrggbb) color at %s byte offset %llu, so not writing "
				"to the file.\n", st->outfile.path, st->outfile.offset);
			st->outfile.path = NULL;
		}
		u64 codepoints_len;
		// TODO: Windows uses UTF-16, not UTF-8, and may need special calls to retrieve unicode cli args.
		u32 *codepoints = decode_string(spath, &codepoints_len);
		if (codepoints) {
		    for (i32 i=0; i<codepoints_len; i++) {
		    	u32 c = codepoints[i];
		    	if (c >= 0x20 && c <= 0x7f)
		    		continue;
		    	bool found = false;
		    	for (i32 j=0; j<i; j++) {
		    		if (codepoints[j] == c) {
		    			found = true;
		    			break;
		    		}
		    	}
		    	if (found)
		    		continue;
		    	small_charset[small_charset_n++] = c;
		    }
		    st->outfile.shortened_path_utf32 = codepoints;
		    assertf(codepoints_len < INT32_MAX, NULL);
		    st->outfile.shortened_path_len = (i32) codepoints_len;
		} else {
			// It can't be decoded, so we won't be able to display the filename
			// Todo: decode from locale encoding to unicode, or at least add better escapes
			st->outfile.shortened_path_utf32[0] = '?';
			st->outfile.shortened_path_utf32[1] = '?';
			st->outfile.shortened_path_utf32[2] = '?';
			st->outfile.shortened_path_len = 3;
		}
	}

	st->main_scene = create_scene(NULL, NULL, 10, 10000, true);
	st->hsv_grad_scene = create_scene(hsv_grad_vertex_shader, hsv_grad_fragment_shader,
		10, 361*3, true);

	int drawable_w, drawable_h;
	SDL_GL_GetDrawableSize(st->window, &drawable_w, &drawable_h);
	int window_w, window_h;
	SDL_GetWindowSize(st->window, &window_w, &window_h);
	st->dpi = (float)drawable_w / window_w;

	float dpi = st->dpi;
	init_for_dpi(st, st->dpi, 1.0f, small_charset, small_charset_n);

    glEnable(GL_MULTISAMPLE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	bool running = true;
	unsigned long long ticks_start = SDL_GetTicks64();
	unsigned long long frames = 0;
	while (running)
	{
		st->key_pressed = 0;
		st->mouse_was_down = st->mouse_down;

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				running = false;
				break;
			case SDL_KEYDOWN:
				if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
					// Don't quit on escape, just pass it through
				}
				st->key_pressed = event.key.keysym.scancode;
				break;
			case SDL_MOUSEMOTION:
				st->mouse_x = event.motion.x;
				st->mouse_y = event.motion.y;
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (event.button.button == SDL_BUTTON_LEFT) {
					st->mouse_down = true;
				}
				break;
			case SDL_MOUSEBUTTONUP:
				if (event.button.button == SDL_BUTTON_LEFT) {
					st->mouse_down = false;
				}
				break;
			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					st->screenWidth = event.window.data1;
					st->screenHeight = event.window.data2;
				}
				break;
			}
		}

		// Check for DPI changes
		SDL_GL_GetDrawableSize(st->window, &drawable_w, &drawable_h);
		SDL_GetWindowSize(st->window, &window_w, &window_h);
		float new_dpi = (float)drawable_w / window_w;
		if (new_dpi != st->dpi) {
			init_for_dpi(st, new_dpi, st->dpi, small_charset, small_charset_n);
		}

		// Setup viewport and projection
		glViewport(0, 0, drawable_w, drawable_h);

		reset_scene(st->hsv_grad_scene);
		reset_scene(st->main_scene);

		draw_ui_and_respond_input(st);

		if (st->hsv_grad_scene->n)
			draw_scene(st->hsv_grad_scene);
		draw_scene(st->main_scene);

		SDL_GL_SwapWindow(st->window);
		// XX measure frame time and subtract
		SDL_Delay(16);

		/*
		frames++;
		unsigned long long ticks_now = SDL_GetTicks64();
		const i32 print_frame_interval_secs = 2;
		if (ticks_now >= ticks_start + print_frame_interval_secs*1000) {
			printf("FPS: %.1f\n", frames / (float)print_frame_interval_secs);
			ticks_start = ticks_now;
			frames = 0;
		}
		*/
    }

    exit:
	SDL_GL_DeleteContext(st->gl_context);
	SDL_DestroyWindow(st->window);
	SDL_Quit();
	if (st->outfile.path)
		free(st->outfile.path);
	if (st->outfile.shortened_path_utf32)
		free(st->outfile.shortened_path_utf32);
	destroy_scene(st->main_scene);
	destroy_scene(st->hsv_grad_scene);
	free(st);
	return 0;
}
