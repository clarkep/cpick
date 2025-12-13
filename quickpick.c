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
// #include <raylib.h>
// #include <rlgl.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>
// #include <GL/glew.h>

#include "util.h"
#include "draw.h"

#include "font/noto_sans_mono.h"
#include "quickpick_icon.h"
// #include "shapes.h"

#define WRITE_INTERVAL 1.0

/**************/

typedef struct { unsigned char r, g, b, a; } Color;
typedef struct { float x, y, width, height; } Rectangle;

#define WHITE ((Color){255, 255, 255, 255})
#define BLACK ((Color){0, 0, 0, 255})

// Key codes matching SDL scancodes we care about
#define KEY_ZERO SDL_SCANCODE_0
#define KEY_NINE SDL_SCANCODE_9
#define KEY_KP_0 SDL_SCANCODE_KP_0
#define KEY_KP_9 SDL_SCANCODE_KP_9
#define KEY_BACKSPACE SDL_SCANCODE_BACKSPACE
#define KEY_ESCAPE SDL_SCANCODE_ESCAPE
#define KEY_ENTER SDL_SCANCODE_RETURN

/*****************/

enum cursor_state { CURSOR_UP, CURSOR_START, CURSOR_DOWN, CURSOR_STOP };

typedef struct state {
	int screenWidth;
	int screenHeight;
	float dpi;
	int mode; // rgb(0) or hsv(1)
	int which_fixed; // red(0), green(1) or blue(2); or hue(0), saturation(1), or value(2)
	int saved_fixed; // in rgb mode, a fixed hsv to go back to; in hsv mode, an rgb
	enum cursor_state cursor_state;
	bool square_dragging;
	bool val_slider_dragging;
	// The coordinate that that the slider controls, 0-1
	float fixed_value;
	// the other two dims, usually x and y but can be theta/r when fixed=value.
	float x_value;
	float y_value;
	/* In hsv mode, you can manipulate rgb values and vice versa. In those cases we treat the alternate
	system as exact and get (slider, x, y) from it, or issues come up with the double conversion
	back and forth. For example, in hsv mode, changing r will often change g and b because of
	rounding. When the user manipulates the main axes again, we go back to treating that system
	as exact. from_alternate_value signifies that alternate_value is treated as the exact
	current color. */
	bool from_alternate_value;
	Vector3 alternate_value;
	Color text_color;
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
		char *shortened_path;
		int shortened_path_len;
		unsigned long long offset;
		int format;
		Color last_write_color;
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
	Color active_colors[3];
	Color inactive_colors[3];
	char labels[3];
	float hover_brightness;
	Color active_text_color;
	Color inactive_text_color;
	Color border_color;
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
	int drag_start_value;
	int drag_start_y;
	float shade_v;
	bool input_active;
	int input_n;
} Number_Select;

char *color_strings[2][3] = { "R", "G", "B", "H", "S", "V" };

/*************************************** CC stuff *************************************************/

// Color utility functions
Color hex2color(unsigned int hex) {
	return (Color){
		(hex >> 24) & 0xFF,
		(hex >> 16) & 0xFF,
		(hex >> 8) & 0xFF,
		hex & 0xFF
	};
}

Color color_brightness(Color c, float factor) {
	float r = (float) c.r + ((255-c.r) * factor);
	float g = (float) c.g + ((255-c.g) * factor);
	float b = (float) c.b + ((255-c.b) * factor);
	return (Color){
		(unsigned char)CLAMP(r, 0, 255),
		(unsigned char)CLAMP(g, 0, 255),
		(unsigned char)CLAMP(b, 0, 255),
		c.a
	};
}

Vector3 color_to_hsv(Color c) {
	float r = c.r / 255.0f;
	float g = c.g / 255.0f;
	float b = c.b / 255.0f;

	float max = MAX(MAX(r, g), b);
	float min = MIN(MIN(r, g), b);
	float delta = max - min;

	Vector3 hsv;
	hsv.z = max; // Value

	if (delta < 0.00001f) {
		hsv.x = 0;
		hsv.y = 0;
		return hsv;
	}

	hsv.y = (max > 0) ? (delta / max) : 0; // Saturation

	if (r >= max) {
		hsv.x = (g - b) / delta;
	} else if (g >= max) {
		hsv.x = 2.0f + (b - r) / delta;
	} else {
		hsv.x = 4.0f + (r - g) / delta;
	}

	hsv.x *= 60.0f;
	if (hsv.x < 0) hsv.x += 360.0f;

	return hsv;
}

Color color_from_hsv(float h, float s, float v) {
	float c = v * s;
	float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
	float m = v - c;

	float r, g, b;
	if (h < 60) { r = c; g = x; b = 0; }
	else if (h < 120) { r = x; g = c; b = 0; }
	else if (h < 180) { r = 0; g = c; b = x; }
	else if (h < 240) { r = 0; g = x; b = c; }
	else if (h < 300) { r = x; g = 0; b = c; }
	else { r = c; g = 0; b = x; }

	return (Color){
		(unsigned char)((r + m) * 255),
		(unsigned char)((g + m) * 255),
		(unsigned char)((b + m) * 255),
		255
	};
}

Vector4 gl_color(Color c)
{
	return (Vector4) { c.r / 255.0f, c.g / 255.0f, c.b / 255.0, c.a / 255.0f };
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

/**************************************************************************************************/

void myassert(bool p, char *fmt, ...) {
	if (!p) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		exit(1);
	}
}

bool read_color_from_outfile_and_maybe_update_offset(struct state *st, Color *c)
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
		*c = (Color) { r, g, b, 255 };
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

bool colors_equal(Color c1, Color c2) {
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b && c1.a == c2.a;
}

struct color_info {
	Color rgb;
	Vector3 hsv;
};

void update_color_or_mode(struct state *st, int mode, int fixed, struct color_info ci)
{
	Color cur_color = ci.rgb;
	Vector3 cur_hsv = ci.hsv;
/*
	int tmp = st->which_fixed;
	st->which_fixed = st->saved_fixed;
	st->saved_fixed = tmp;
*/
	if (st->mode) {
		if (fixed == 0) {
			st->fixed_value = cur_hsv.x / 360;
			st->x_value = cur_hsv.y;
			st->y_value = cur_hsv.z;
		} else if (fixed == 1) {
			st->fixed_value = cur_hsv.y;
			st->x_value = cur_hsv.x / 360;
			st->y_value = cur_hsv.z;
		} else if (fixed == 2) {
			st->fixed_value = cur_hsv.z;
			st->x_value = cur_hsv.x / 360;
			st->y_value = cur_hsv.y;
		}
	} else {
		if (fixed == 0) {
			st->fixed_value = cur_color.r / 255.0f;
			st->x_value = cur_color.g / 255.0f;
			st->y_value = cur_color.b / 255.0f;
		} else if (fixed == 1) {
			st->fixed_value = cur_color.g / 255.0f;
			st->x_value = cur_color.b / 255.0f;
			st->y_value = cur_color.r / 255.0f;
		} else if (fixed == 2) {
			st->fixed_value = cur_color.b / 255.0f;
			st->x_value = cur_color.r / 255.0f;
			st->y_value = cur_color.g / 255.0f;
		}
	}
}

struct color_info current_color(struct state *st)
{
	struct color_info res;
	/*
	if (st->from_alternate_value) {
		printf("from alternate value ");
		if (st->mode == 0) {
			printf("(h: %d, s:%d%%, v:%d%%)\n", (int) roundf(st->alternate_value.x*360.0f),
				(int) roundf(st->alternate_value.y*100.0f), (int) roundf(st->alternate_value.z*100.0f));
		} else if (st->mode == 1) {
			printf("(r: %d, g: %d, b: %d)\n", (int) roundf(st->alternate_value.x * 255.0f),
				(int) roundf(st->alternate_value.y * 255.0f), (int) roundf(st->alternate_value.z*255.0f));
		}
	} else {
		printf("from regular value\n");
	}
	*/
	if (st->mode == 0 && !st->from_alternate_value || st->mode == 1 && st->from_alternate_value) {
		if (st->from_alternate_value) {
			res.rgb = (Color) { roundf(255.0f*st->alternate_value.x),
				roundf(255.0f*st->alternate_value.y),
				roundf(255.0f*st->alternate_value.z), 255 };
		} else {
			int v1 = roundf(255.0f * st->fixed_value);
			int v2 = roundf(255.0f * st->x_value);
			int v3 = roundf(255.0f * st->y_value);
			switch (st->which_fixed) {
			case 0:
				res.rgb = (Color) { v1, v2, v3, 255 };
				break;
			case 1:
				res.rgb = (Color) { v3, v1, v2, 255 };
				break;
			case 2:
				res.rgb = (Color) { v2, v3, v1, 255 };
				break;
			}
		}
		res.hsv = color_to_hsv(res.rgb);
	} else {
		if (st->from_alternate_value) {
			res.hsv = st->alternate_value;
			res.hsv.x *= 360.0f;
		} else {
			switch (st->which_fixed) {
			case 0:
				res.hsv = (Vector3) { st->fixed_value * 360, st->x_value, st->y_value };
			break;
			case 1:
				res.hsv = (Vector3) { st->x_value * 360, st->fixed_value, st->y_value };
			break;
			case 2:
				// x=theta, y=r
				res.hsv = (Vector3) { st->x_value * 360, st->y_value, st->fixed_value };
			break;
			}
		}
		res.rgb = color_from_hsv(res.hsv.x, res.hsv.y, res.hsv.z);
	}
	if (st->from_alternate_value) {
		update_color_or_mode(st, st->mode, st->which_fixed, res);
	}
	return res;
}

void write_color_to_file(struct state *st, Color color)
{
	char color_text[10];
	sprintf(color_text, "%02x%02x%02x", color.r, color.g, color.b);

	FILE *f = fopen(st->outfile.path, "r+b");
	myassert(f, "Failed to open file: %s.\n", st->outfile.path);
	int res = fseek(f, st->outfile.offset, SEEK_SET);
	myassert(!res, "Failed to write byte %lu in file %s.\n", st->outfile.offset, st->outfile.path);
	fwrite(color_text, 1, 6, f);
	if (st->debug)
		printf("Wrote %s to %s byte %llu.\n", color_text, st->outfile.path,
			st->outfile.offset);
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
	    	float angle1 = 2*M_PI*i/360.0f;
	    	float angle2 = 2*M_PI*(i+1)/360.0f;
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
		float dx = cosf(ang*2*M_PI/360.0f);
		float dy = sinf(ang*2*M_PI/360.0f);
		int length = 5*dpi;
		Vector2 start = { x + r * dx, y + r * dy };
		Vector2 end = { start.x + length * dx, start.y + length * dy };
		add_line(st->main_scene, start.x, start.y, end.x, end.y, 2.0*dpi, gl_color(st->text_color));
	}
	// s arrow
	int arrow_len = 60*dpi;
	float arrow_w = 2.0*dpi;
	// int  = 4*dpi;
	// arrowhead
	float ah_len = 13*dpi;
	float ah_ang = (180-28)*2*M_PI/360.0f;
	float ah_w = 2.0*dpi;
	Vector2 arrow_end = (Vector2) { x+r+arrow_len, y};
	add_line(st->main_scene, x+r+12*dpi, y, arrow_end.x, arrow_end.y, arrow_w, gl_color(st->text_color));
	Vector2 ah_left = { arrow_end.x + ah_len*cosf(ah_ang), arrow_end.y + ah_len*sinf(ah_ang)};
	Vector2 ah_right = { arrow_end.x + ah_len*cosf(-ah_ang), arrow_end.y + ah_len*sinf(-ah_ang)};
	add_line(st->main_scene, arrow_end.x, arrow_end.y, ah_left.x, ah_left.y, ah_w, gl_color(st->text_color));
	add_line(st->main_scene, arrow_end.x, arrow_end.y, ah_right.x, ah_right.y, ah_w, gl_color(st->text_color));
	add_text(st->main_scene, st->text_font_medium, "S", arrow_end.x-16.0*dpi, arrow_end.y-20.0*dpi,
		gl_color(st->text_color));
	// h arrow
	float harr_d = 30*dpi;
	float harr_w = 2*dpi;
	float harr_ang1 = 12;
	float harr_ang2 = 28;
	Vector2 harr_end = { x + (r+harr_d+harr_w/2)*cosf(2*M_PI*harr_ang2/360.0f),
		y - (r+harr_d+harr_w/2)*sinf(2*M_PI*harr_ang2/360.0f) };
	float harr_dir_ang = (harr_ang2*2*M_PI / 360.0f) + M_PI / 2;
	// arrowhead
	float adj = 2*M_PI/120;
	// Angles are set up so that left arrowhead goes straight down
		Vector2 h_ah_left = { harr_end.x /*+ah_len*cosf(harr_dir_ang+ah_ang+adj)*/,
		harr_end.y-ah_len*sinf(harr_dir_ang+ah_ang+adj)};
	Vector2 h_ah_right = { harr_end.x+ah_len*cosf(harr_dir_ang-ah_ang+adj),
		harr_end.y-ah_len*sinf(harr_dir_ang-ah_ang+adj)};
	Color c3 = st->text_color;
	// Todo: the arrowhead might still be a little off of the arrow body. Renderdoc it.
	add_line(st->main_scene, harr_end.x, harr_end.y, h_ah_left.x, h_ah_left.y, ah_w, gl_color(c3));
	add_line(st->main_scene, harr_end.x, harr_end.y, h_ah_right.x, h_ah_right.y, ah_w,
		gl_color(c3));
	add_text(st->main_scene, st->text_font_medium, "H", harr_end.x+18*dpi, harr_end.y-2*dpi,
		gl_color(st->text_color));
	add_circle_arc(st->main_scene, x, y, r+harr_d+harr_w/2, 2*M_PI*harr_ang1/360.0f, 2*M_PI*harr_ang2/360.0f,
		30, 2.0f*dpi, gl_color(st->text_color));
}

// TODO: parameter for 512 vs etc ?
void draw_axes(int x, int y, int w, int h, struct state *st)
{
	float dpi = st->dpi;
	int tick_sep = 64*dpi;
	int tick_width = 2*dpi;
	int y_tick_len = w/4;
	int x_tick_len = h/4;
	Color tick_color = st->text_color;
	int label_size = 30*dpi;
	Color label_color = st->text_color;

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
			   x + 512*dpi/2 - label_size, y + 512*dpi + h, gl_color(label_color));
	// y axis label
	add_text(st->main_scene, st->text_font_medium, y_label,
			   x - h, y + 512*dpi/2, gl_color(label_color));
	// x axis
	for (int ix = x; ix < (x+512*dpi); ix += tick_sep) {
		add_rectangle(st->main_scene, ix, y+512*dpi, tick_width, x_tick_len, gl_color(tick_color));
	}
	// y axis
	for (int yi = 0; yi < (512*dpi); yi += tick_sep) {
		add_rectangle(st->main_scene, x-y_tick_len, y + 512*dpi - yi - tick_width, y_tick_len,
			tick_width, gl_color(tick_color));
	}
}

bool tab_select(Tab_Select *self, Vector2 pos, enum cursor_state cs)
{
	State *st = self->st;
	float dpi = st->dpi;
	int i = self->sel_i;
	Color *active = self->active_colors;
	Color *inactive = self->inactive_colors;
	Color active_text = self->active_text_color;
	Color inactive_text = self->inactive_text_color;
	float *hover_v = self->hover_v;
	Color color1 = i == 0 ? active[0] : color_brightness(inactive[0], self->hover_brightness*hover_v[0]);
	Color color2 = i == 1 ? active[1] : color_brightness(inactive[1], self->hover_brightness*hover_v[1]);
	Color color3 = i == 2 ? active[2] : color_brightness(inactive[2], self->hover_brightness*hover_v[2]);
	Color text_color1 = i == 0 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[0]);
	Color text_color2 = i == 1 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[1]);
	Color text_color3 = i == 2 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[2]);
	char text[2] = "X";
	float x = self->x;
	float tw = self->w / 3.0;
	float rnd = 7.0f; // rounded rectangle roundness
	float segs = 20; // rounded rectangle segments
	float off = 0 * dpi;
	float y = self->top ? self->y : self->y - off;
	// xx
	float h = self->h + off;
	float text_y = y + h/2.0f + st->small_font_max_ascent/2.0f;
	Vector2 left_corners[4] = { { x, y }, { x, y+h }, { x+tw, y+h }, { x+tw, y }};
	bool left_rounded[4] = { self->top, !self->top, false, false };
	add_rounded_quad(st->main_scene, left_corners, left_rounded, rnd, segs, gl_color(color1));
	add_rounded_quad_outline(st->main_scene, left_corners, left_rounded, rnd, segs, 1.0f,
		gl_color(self->border_color));
	text[0] = self->labels[0];
	add_text(st->main_scene, st->text_font_small, text, x + (tw - st->small_char_width)/2.0,
		text_y, gl_color(text_color1));
	float x_mid = x + tw;
	int last_x = x_mid + tw;
	int end_x = self->x + self->w;
	int last_w = end_x - last_x;
	Vector2 right_corners[4] = { { last_x, y }, { last_x, y+h }, { last_x+last_w, y+h },
		{ last_x+last_w, y } };
	bool right_rounded[4] = { false, false, !self->top, self->top };
	add_rounded_quad(st->main_scene, right_corners, right_rounded, rnd, segs, gl_color(color3));
	add_rounded_quad_outline(st->main_scene, right_corners, right_rounded, rnd, segs, 1.0f,
		gl_color(self->border_color));
	text[0] = self->labels[2];
	add_text(st->main_scene, st->text_font_small, text, last_x + (last_w - st->small_char_width)/2.0,
		text_y, gl_color(text_color3));

	add_rectangle(st->main_scene, x_mid, self->y, tw, self->h, gl_color(color2));
	add_rectangle_outline(st->main_scene, x_mid, self->y, tw, self->h, 1*dpi,
		gl_color(self->border_color));
	text[0] = self->labels[1];
	add_text(st->main_scene, st->text_font_small, text, x_mid + (tw - st->small_char_width)/2.0,
		text_y, gl_color(text_color2));

	bool updated = false;
	float hover_targets[3] = { 0.0f, 0.0f, 0.0f };
	if (CheckCollisionPointRec(pos, (Rectangle) { self->x, self->y, self->w, self->h})) {
		int tab_i = (pos.x - self->x) / (self->w / 3.0f);
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
	int a = 64 + self->shade_v;
	Color hl_color = { a, a, a, self->shade_v };

	i32 text_y = self->y + self->h/2.0f + st->medium_font_max_ascent/2.0f;

	bool hovered = false;
	float rnd = 7.0f;
	i32 segs = 15;
	add_rounded_rectangle(st->main_scene, self->x - 10*dpi, self->y, self->w, self->h,
		rnd, segs, gl_color(hl_color));
	i32 font_h = 30*dpi;
	char text[21];
	memset(text, 0, 21);
	if (self->input_active) {
		// Color only the number(%d) in the string fmt(there must be one and only one %d)
		int text_i = 0;
		int fmt_i = 0;
		int d_i;
		int d_chars;
		int len = strlen(self->fmt);
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
		add_text(st->main_scene, st->text_font_medium, text, x, text_y, gl_color(st->text_color));
		text[d_i] = c;
		x += d_i*(st->medium_char_width + 1.5*dpi);
		c = text[d_i + d_chars];
		// xx GetGlColor at least
		add_text(st->main_scene, st->text_font_medium, &text[d_i], x, text_y,
			gl_color(st->text_color.r < 128 ? hex2color(0x303030ff) : hex2color(0xd8d8d8ff)));
		text[d_i + d_chars] = c;
		x += d_chars * (st->medium_char_width + 1.5*dpi);
		add_text(st->main_scene, st->text_font_medium, &text[d_i+d_chars], x, text_y,
			gl_color(st->text_color));
	} else {
		int n_chars = snprintf(text, 20, self->fmt, self->input_active ? self->input_n : self->value);
		add_text(st->main_scene, st->text_font_medium, text, self->x, text_y,
			gl_color(st->text_color));
	}
	bool hit = CheckCollisionPointRec(pos, (Rectangle) { self->x, self->y, self->w, self->h });
	// xx can this logic be simplified?
	if ((hit && cs != CURSOR_DOWN) || self->dragging) {
		hovered = true;
	}
	// xx self->dragging here ensures that the click that's ending now started on the widget, but not that
	// it never left.
	if (hit && cs == CURSOR_STOP && self->dragging) {
		self->selected = true;
	}
	if (!hit && cs == CURSOR_START) {
		self->selected = false;
		self->input_active = false;
	}
	if (self->selected && key) {
		int key_num = (key >= KEY_ZERO && key <= KEY_NINE) ? (key - KEY_ZERO)
			: (key >= KEY_KP_0 && key <= KEY_KP_9 ? (key - KEY_KP_0) : -1);
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
		if (self->input_active && key == KEY_BACKSPACE) {
			if (self->input_n >= 10) {
				self->input_n /= 10;
			} else {
				self->input_active = false;
			}
		}
		if (self->input_active && key == KEY_ESCAPE) {
			self->input_active = false;
		}
		if (self->input_active && key == KEY_ENTER) {
			new_value = self->input_n;
			self->input_active = false;
		}
	}
	if (hit && cs == CURSOR_START) {
		self->dragging = true;
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
	int shade_v_target = 0;
	if (self->selected) {
		shade_v_target = 128;
	} else if (hovered) {
		shade_v_target = 102;
	}
	value_creep_towards(&self->shade_v, shade_v_target, self->anim_vdt * 100);
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

float luminance(unsigned int r, unsigned int g, unsigned int b)
{
  float rf = r / 255.0f;
  float gf = g / 255.0f;
  float bf = b / 255.0f;
  float rs = rf <= 0.3928f ? rf / 12.92f : powf((rf+0.055f)/1.055f, 2.4f);
  float gs = gf <= 0.3928f ? gf / 12.92f : powf((gf+0.055f)/1.055f, 2.4f);
  float bs = bf <= 0.3928f ? bf / 12.92f : powf((bf+0.055f)/1.055f, 2.4f);
  return 0.2126f * rs + 0.7152f * gs + 0.0722f * bs;
}

void draw_ui_and_respond_input(struct state *st)
{
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
	float anim_vdt = .3;

	struct color_info ci = current_color(st);
	Color cur_color = ci.rgb;
	Vector3 cur_hsv = ci.hsv;
	// ClearBackground( cur_color );
	glClearColor(cur_color.r/255.0f, cur_color.g/255.0f, cur_color.b/255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	float dpi = st->dpi;
	if (luminance(cur_color.r, cur_color.g, cur_color.b) >= 0.179) {
		st->text_color = BLACK;
	} else {
		st->text_color = WHITE;
	}

	int dark_text_bright_grey_bg = 208;
	int dark_text_dim_grey_bg = 144;
	int light_text_bright_grey_bg = 160;
	int light_text_dim_grey_bg = 80;
	Color fixed_indication_color;
	Color light_text_indication_color;
	if (st->mode == 0) {
		int wf = st->which_fixed;
		int rgb_fixed_ind[] = { 0xc00000ff, 0x00c000ff, 0x0080ffff };
		fixed_indication_color = hex2color(rgb_fixed_ind[wf]);
		light_text_indication_color = fixed_indication_color;
	} else {
		int a = st->text_color.r < 128 ? dark_text_bright_grey_bg : light_text_bright_grey_bg;
		int b = light_text_bright_grey_bg;
		fixed_indication_color = (Color) { a, a, a, 255 };
		light_text_indication_color = (Color) { b, b, b, 255 };
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
	Color out_ind_bgcolor = { 48, 48, 48, 192 };
	if (st->outfile.path) {
		i32 text_x = out_ind_bottom_x + (out_ind_bottom_w - st->outfile.shortened_path_len*(st->small_char_width+1.0*dpi))/2.0f;
		i32 text_y = out_ind_top_y + out_ind_h/2.0f + st->small_font_max_ascent/2.0f;
		add_rounded_quad(st->main_scene, out_ind_verts, out_ind_rounded, 12*dpi, 12,
			gl_color(out_ind_bgcolor));
		add_text(st->main_scene, st->text_font_small, st->outfile.shortened_path,
			text_x, text_y, gl_color(WHITE));
	}

	// gradient
	int y_axis_w = 30*dpi;
	int x_axis_h = 30*dpi;
	int grad_square_w = 512*dpi + y_axis_w;
	int grad_square_h = 512*dpi + x_axis_h;
	int grad_square_x = (st->screenWidth - 512*dpi)/2;
	int grad_square_y = out_ind_bottom_y+(st->outfile.path ? 10*dpi : 0)+20*dpi;
	int grad_square_y_end = grad_square_y + 512*dpi;
	int grad_square_x_end = grad_square_x + 512*dpi;
	bool square = true;
	int cx = grad_square_x + 512*dpi/2;
	int cy = grad_square_y + 512*dpi/2;
	int cr = 512*dpi/2;
	if (st->mode == 0) {
		draw_gradient_square_rgb(st, grad_square_x, grad_square_y, 512*dpi, st->which_fixed, st->fixed_value);
		draw_axes(grad_square_x, grad_square_y, x_axis_h, y_axis_w, st);
	} else {
		if (st->which_fixed == 2) {
			square = false;
			draw_gradient_circle_and_axes(cx, cy, cr, st->fixed_value, st);
		} else {
			draw_gradient_square_hsv(st, grad_square_x, grad_square_y, 512*dpi, st->which_fixed,
				st->fixed_value);
			draw_axes(grad_square_x, grad_square_y, x_axis_h, y_axis_w, st);
		}
	}
	int cur_loc_sq_sz = 4*dpi;
	// indicator circle
	int ind_x, ind_y;
	if (square) {
		ind_x = grad_square_x + st->x_value * (512*dpi);
		ind_y = grad_square_y + 512*dpi - st->y_value * (512*dpi);
	} else {
		ind_x = cx + cr*st->y_value*cosf(st->x_value * 2*M_PI);
		ind_y = cy - cr*st->y_value*sinf(st->x_value * 2*M_PI);
	}
	add_circle_outline(st->main_scene, ind_x, ind_y, 6*dpi, 20, 1*dpi, gl_color(st->text_color));
	int r2 = 4*dpi;
	int r3 = 8*dpi;
	add_line(st->main_scene, ind_x - r3, ind_y, ind_x - r2, ind_y, 1*dpi, gl_color(st->text_color));
	add_line(st->main_scene, ind_x + r2, ind_y, ind_x + r3, ind_y, 1*dpi, gl_color(st->text_color));
	add_line(st->main_scene, ind_x, ind_y - r3, ind_x, ind_y - r2, 1*dpi, gl_color(st->text_color));
	add_line(st->main_scene, ind_x, ind_y + r2, ind_x, ind_y + r3, 1*dpi, gl_color(st->text_color));
	if (st->cursor_state == CURSOR_START || st->square_dragging) {
		if (!st->square_dragging) {
			Rectangle rec = {grad_square_x, grad_square_y, 512*dpi, 512*dpi};
			Vector2 c = { grad_square_x + 512*dpi/2, grad_square_y + 512*dpi/2 };
			if ((square && CheckCollisionPointRec(pos, rec))
				|| (!square && CheckCollisionPointCircle(pos, c, 512*dpi/2))) {
				st->square_dragging = true;
			}
		}
		if (st->square_dragging) {
			int y_adj = 3*dpi;
			int x_adj = 2*dpi;
			if (square) {
				st->x_value = MIN(MAX((pos.x - x_adj - grad_square_x) / (512*dpi), 0.0f), 1.0f);
				// xx off by one?
				st->y_value = MIN(MAX((grad_square_y + 512*dpi - pos.y + y_adj) / (512*dpi), 0.0f), 1.0f);
			} else {
				int x_res = pos.x -x_adj - (grad_square_x + 512*dpi/2);
				int y_res = pos.y - y_adj - (grad_square_y + 512*dpi/2);
				y_res = -y_res;
				// theta
				st->x_value = atan2(y_res, x_res) / (2*M_PI);
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
	int top_tabs_y = grad_square_y_end + x_axis_h;
	int top_tabs_h = 25*dpi;
	int top_tabs_w = 90*dpi;
	int ind_button_x = top_tabs_x;
	int ind_button_y = top_tabs_y + top_tabs_h;
	int ind_button_w = top_tabs_w;
	int ind_button_h = 70*dpi;
	// int ind_tabs_y = ind_button_y + ind_button_h - ind_tabs_h - 1;
	int ind_tabs_y = ind_button_y + ind_button_h;
	Color ind_border_color = hex2color(0xb0b0b0ff);
	// tabs
	static Tab_Select rgb_select;
	static Tab_Select hsv_select;
	static bool first_frame_setup_done = false;
	if (!first_frame_setup_done) {
		rgb_select.active_colors[0] = hex2color(0xc00000ff);
		rgb_select.active_colors[1] = hex2color(0x00c000ff);
		rgb_select.active_colors[2] = hex2color(0x0080ffff);
		rgb_select.inactive_colors[0] = hex2color(0x700000ff);
		rgb_select.inactive_colors[1] = hex2color(0x007000ff);
		rgb_select.inactive_colors[2] = hex2color(0x0000c0ff);
		rgb_select.active_text_color = hex2color(0xffffffff);
		rgb_select.inactive_text_color = hex2color(0xa0a0a0ff);
		rgb_select.labels[0] = 'R';
		rgb_select.labels[1] = 'G';
		rgb_select.labels[2] = 'B';
		rgb_select.top = true;
		int a = light_text_bright_grey_bg;
		int b = light_text_dim_grey_bg;
		Color bright = { a, a, a, 255 };
		Color dim = { b, b, b, 255 };
		for (int i=0; i<3; i++) {
			hsv_select.active_colors[i] = bright;
			hsv_select.inactive_colors[i] = dim;
		}
		float sel_hov_brightness = 0.4;
		hsv_select.active_text_color = rgb_select.active_text_color;
		hsv_select.inactive_text_color = rgb_select.inactive_text_color;
		rgb_select.hover_brightness = sel_hov_brightness;
		rgb_select.anim_vdt = anim_vdt;
		rgb_select.st = st;
		hsv_select.labels[0] = 'H';
		hsv_select.labels[1] = 'S';
		hsv_select.labels[2] = 'V';
		hsv_select.hover_brightness = sel_hov_brightness;
		hsv_select.anim_vdt = anim_vdt;
		hsv_select.st = st;
		hsv_select.top = false;
		first_frame_setup_done = true;
	}
	rgb_select.border_color = ind_border_color;
	hsv_select.border_color = rgb_select.border_color;
	if (!st->mode) {
		rgb_select.sel_i = st->which_fixed;
		hsv_select.sel_i = -1;
	} else {
		rgb_select.sel_i = -1;
		hsv_select.sel_i = st->which_fixed;
	}
	rgb_select.x = top_tabs_x;
	rgb_select.y = top_tabs_y;
	rgb_select.w = top_tabs_w;
	rgb_select.h = top_tabs_h;
	if (tab_select(&rgb_select, pos, st->cursor_state)) {
		st->mode = 0;
		st->which_fixed = rgb_select.sel_i;
		update_color_or_mode(st, st->mode, st->which_fixed, ci);
	}
	hsv_select.x = ind_button_x;
	hsv_select.y = ind_button_y + ind_button_h;
	hsv_select.w = ind_button_w;
	hsv_select.h = top_tabs_h;
	if (tab_select(&hsv_select, pos, st->cursor_state)) {
		st->mode = 1;
		st->which_fixed = hsv_select.sel_i;
		update_color_or_mode(st, st->mode, st->which_fixed, ci);
	}
	// main button
	static float ind_button_hover_v = 0;
	float hov_bright = .4;
	Color fixed_button_color = color_brightness(light_text_indication_color, ind_button_hover_v * hov_bright);
	add_rectangle(st->main_scene, ind_button_x, ind_button_y, ind_button_w, ind_button_h,
		gl_color(fixed_button_color));
	add_rectangle_outline(st->main_scene, ind_button_x, ind_button_y, ind_button_w, ind_button_h,
		1*dpi, gl_color(ind_border_color));
	Color white = { 255, 255, 255, 255 };
	i32 ind_text_x = ind_button_x+ind_button_w/2.0f-st->large_char_width/2.0f;
	i32 ind_text_y = ind_button_y + ind_button_h/2.0f+st->large_font_max_ascent/2.0f;
	add_text(st->main_scene, st->text_font_large, color_strings[st->mode][st->which_fixed],
		ind_text_x, ind_text_y, gl_color(white));
	if (CheckCollisionPointRec(pos, (Rectangle) { ind_button_x, ind_button_y, ind_button_w,
		ind_tabs_y-ind_button_y})) {
		if (st->cursor_state == CURSOR_START) {
			st->which_fixed = (st->which_fixed + 1) % 3;
			update_color_or_mode(st, st->mode, st->which_fixed, ci);
			ind_button_hover_v = 0;
		}
		if (st->cursor_state != CURSOR_DOWN) {
			ind_button_hover_v = MIN(ind_button_hover_v + anim_vdt, 1.0);
		}
	} else {
		ind_button_hover_v = MAX(ind_button_hover_v - anim_vdt, 0.0);
	}

	// hsv-rgb toggle
	int toggle_button_x = ind_button_x;
	int toggle_button_y = hsv_select.y + hsv_select.h;
	int toggle_button_w = ind_button_w;
	int toggle_button_h = 20*dpi;

	// fixed value slider
	int val_slider_x = ind_button_x + ind_button_w + 30*dpi;
	int val_slider_y = ind_button_y + ind_button_h / 2.0f;
	int val_slider_w = grad_square_x_end - val_slider_x;
	int val_slider_h = 60*dpi;
	// center vertically relative to two adjacent buttons
	int toggle_button_y_end = toggle_button_y + toggle_button_h;
	int val_slider_offset = roundf(val_slider_w * ( (float) st->fixed_value ));
	{
		int bar_h = 8*dpi;
		int circle_r = 15*dpi;
		add_rectangle(st->main_scene, val_slider_x, val_slider_y-bar_h/2.0f, val_slider_w, bar_h,
			gl_color(st->text_color));
		Vector2 circle_center = {  };
		add_circle(st->main_scene, val_slider_x + val_slider_offset, val_slider_y, 15*dpi,
			30, gl_color(fixed_indication_color));
		if (st->cursor_state == CURSOR_START || st->val_slider_dragging) {
			if (!st->val_slider_dragging && CheckCollisionPointRec(pos,
				(Rectangle) { val_slider_x - circle_r, val_slider_y-val_slider_h/2.0f,
					val_slider_w+2*circle_r, val_slider_h } )) {
				st->val_slider_dragging = true;
			}
			if (st->val_slider_dragging) {
				val_slider_offset = MIN(val_slider_w, MAX(0, pos.x - val_slider_x));
			}
			st->fixed_value = MIN(MAX((float) val_slider_offset / val_slider_w, 0), 1.0);
			// xx check if we actually changed the color?
			st->from_alternate_value = false;
		}
	}

	bool rgb_num_select_changed = false;
	int rgb_select_w = 6*(st->medium_char_width + 1.5*dpi);
	int r_select_x = (st->screenWidth - st->medium_label_width)/2.0f;
	int r_select_y = val_slider_y + 85*dpi;
	static Number_Select r_num_select;
	r_num_select.value = cur_color.r;
	if (number_select_immargs(&r_num_select, "r:%d ", 0, 255, false, st, anim_vdt,
		r_select_x, r_select_y, rgb_select_w, 30*dpi, 800.0f / 256.0f, pos, st->cursor_state, key)) {
		rgb_num_select_changed = true;
	}
	static Number_Select g_num_select;
	g_num_select.value = cur_color.g;
	if (number_select_immargs(&g_num_select, "g:%d ", 0, 255, false, st, anim_vdt,
		r_num_select.x+r_num_select.w, r_num_select.y, rgb_select_w, 30*dpi, 800.0f / 256.0f,
		pos, st->cursor_state, key)) {
		rgb_num_select_changed = true;
	}
	static Number_Select b_num_select;
	b_num_select.value = cur_color.b;
	if (number_select_immargs(&b_num_select, "b:%d ", 0, 255, false, st, anim_vdt,
		g_num_select.x+g_num_select.w, g_num_select.y, rgb_select_w, 30*dpi, 800.0f / 256.0f,
		pos, st->cursor_state, key)) {
		rgb_num_select_changed = true;
	}
	if (rgb_num_select_changed) {
		Color new_rgb = { r_num_select.value,  g_num_select.value, b_num_select.value };
		if (st->mode == 1) {
			st->from_alternate_value = true;
			st->alternate_value = (Vector3) { new_rgb.r / 255.0f, new_rgb.g / 255.0f, new_rgb.b / 255.0f };
		}
		Vector3 new_hsv = color_to_hsv(new_rgb);
		struct color_info new_ci = { new_rgb, new_hsv };
		update_color_or_mode(st, st->mode, st->which_fixed, new_ci);
	}

	char value[40];
	sprintf(value, "hex:#%02x%02x%02x", cur_color.r, cur_color.g, cur_color.b);
	int hex_label_x = b_num_select.x + b_num_select.w;
	int hex_label_y = r_num_select.y + r_num_select.h/2.0f + st->medium_font_max_ascent/2.0f;
	i32 font_h = 30*dpi;
	add_text(st->main_scene, st->text_font_medium, value, hex_label_x, hex_label_y,
		gl_color(st->text_color));

	bool hsv_num_select_changed = false;
	int hsv_select_w = 7*(st->medium_char_width + 1.5*dpi);
	static Number_Select h_num_select;
	h_num_select.value = cur_hsv.x;
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
		Vector3 new_hsv = { h_num_select.value, s_num_select.value / 100.0f, v_num_select.value / 100.0f };
		if (st->mode == 0) {
			st->from_alternate_value = true;
			st->alternate_value = new_hsv;
			st->alternate_value.x /= 360.0f;
		}
		Color new_rgb = color_from_hsv(new_hsv.x, new_hsv.y, new_hsv.z);
		struct color_info new_ci = { new_rgb, new_hsv };
		update_color_or_mode(st, st->mode, st->which_fixed, new_ci);
	}
	if (rgb_num_select_changed && hsv_num_select_changed) {
		printf("rgb and hsv selects changed\n");
	}

	double now = GetTime(st);
	if (st->outfile.path && now - st->outfile.last_write_time > WRITE_INTERVAL &&
		!colors_equal(cur_color, st->outfile.last_write_color)) {
		write_color_to_file(st, cur_color);
		st->outfile.last_write_color = cur_color;
		st->outfile.last_write_time = now;
	}
}

/*
static const char *hsv_grad_vertex_shader =
"#version 330 core\n"
"uniform mat4 transform;\n"
"void main() {\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"    gl_FrontColor = gl_Color;\n"
"}\n";

static const char *hsv_grad_fragment_shader =
"#version 330 core\n"
"uniform mat4 transform;\n"
"vec3 hsv2rgb(vec3 c) {\n"
"    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
"    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
"    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
"}\n"
"void main() {\n"
"    gl_FragColor = vec4(hsv2rgb(gl_Color.xyz), 1.0);\n"
"}\n";
*/

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
"    if (fFontIndex >= 0.0) {\n"
"        int idx = int(fFontIndex + 0.5);\n"
"        float alpha = 0.0;\n"
"        if (idx == 0) alpha = texture(uFonts[0], TexCoord).r;\n"
"        else if (idx == 1) alpha = texture(uFonts[1], TexCoord).r;\n"
"        else if (idx == 2) alpha = texture(uFonts[2], TexCoord).r;\n"
"        else if (idx == 3) alpha = texture(uFonts[3], TexCoord).r;\n"
"        else if (idx == 4) alpha = texture(uFonts[4], TexCoord).r;\n"
"        else if (idx == 5) alpha = texture(uFonts[5], TexCoord).r;\n"
"        else if (idx == 6) alpha = texture(uFonts[6], TexCoord).r;\n"
"        else if (idx == 7) alpha = texture(uFonts[7], TexCoord).r;\n"
"        base.a *= alpha;\n"
"    }\n"
"    FragColor = vec4(hsv2rgb(base.xyz), base.w);\n"
"}";

char *usage_str =
"quickpick [file@offset]\n"
"Options:\n"
"  --file FILE     choose a file to output to; alternative to file@offset\n"
"  --offset N      choose an offset in FILE; alternative to file@offset\n";

void init_for_dpi(struct state *st, float dpi, float old_dpi)
{
	st->dpi = dpi;
	float ratio = dpi / old_dpi;
	int new_target_w = st->screenWidth * ratio;
	int new_target_h = st->screenHeight * ratio;
	if (new_target_w  != st->screenWidth) {
		SDL_SetWindowSize(st->window, new_target_w, new_target_h);
		st->screenWidth = new_target_w; // GetScreenWidth();
		st->screenHeight = new_target_h; // GetScreenHeight();
	}

	char *font_file = "font/NotoSansMono-Regular.ttf";
	st->text_font_small = load_font(st->main_scene, font_file, 17*dpi, NULL, 0);
	st->text_font_medium = load_font(st->main_scene, font_file, 23*dpi, NULL, 0);
	st->text_font_large = load_font(st->main_scene, font_file, 35*dpi, NULL, 0);

	// Measure the label width once; since it's a monospace font, it will be the same for all colors.
	/*
	st->medium_label_width = MeasureTextEx(st->text_font_medium, "r:255 g:255 b:255 hex:#ffffff",
		30*dpi, 1.5*dpi).x;
	st->small_char_width = MeasureTextEx(st->text_font_small, "R",
		22*dpi, 0*dpi).x;
	st->medium_char_width = MeasureTextEx(st->text_font_medium, "R",
		30*dpi, 0*dpi).x;
	st->large_char_width = MeasureTextEx(st->text_font_large, "R",
		40*dpi, 0*dpi).x;
	*/
	st->medium_label_width = measure_text_width(st->main_scene, st->text_font_medium, "r:255 g:255 b:255 hex:#ffffff");
	st->small_char_width = measure_text_width(st->main_scene, st->text_font_small, "R");
	st->medium_char_width = measure_text_width(st->main_scene, st->text_font_medium, "R");
	st->large_char_width = measure_text_width(st->main_scene, st->text_font_large, "R");
	st->small_font_max_ascent = st->main_scene->fonts[st->text_font_small]->max_ascent;
	st->medium_font_max_ascent = st->main_scene->fonts[st->text_font_medium]->max_ascent;
	st->large_font_max_ascent = st->main_scene->fonts[st->text_font_large]->max_ascent;
}

int main(int argc, char *argv[])
{
	struct state *st = (struct state *) calloc(1, sizeof(struct state));
	st->screenWidth = 680;
	st->screenHeight = 860;
	st->mode = 1;
	st->which_fixed = 2;
	st->saved_fixed = 0;
	st->fixed_value = 1.0;
	st->x_value = 0;
	st->y_value = 0;
	st->square_dragging = false;
	st->val_slider_dragging = false;
	st->text_color = WHITE;
	st->outfile.path = NULL;
	st->outfile.offset = 0;
	st->outfile.format = 0;
	st->outfile.last_write_color = (Color) { 0, 0, 0, 255 };
	st->debug = false;

	for (int i=1; i<argc; i++) {
		char *arg = argv[i];
		if (argv[i][0] == '-' && argv[i][1] == '-') {
			char *longarg = &argv[i][2];
			if (strcmp(longarg, "file")==0) {
				myassert(i+1<argc && !st->outfile.path, usage_str);
				st->outfile.path = argv[i+1];
				i++;
			} else if (strcmp(longarg, "offset")==0) {
				myassert(i+1<argc && !st->outfile.offset, usage_str);
				errno = 0;
				st->outfile.offset = strtoull(argv[i+1], NULL, 10);
				myassert(!errno, usage_str);
				i++;
			}
		} else if (argv[i][0] == '-') {
			errexit(usage_str);
		} else {
			myassert(!st->outfile.path, usage_str);
			char *sep = strchr(arg, '@');
			myassert(sep, usage_str);
			int path_len = sep - arg;
			st->outfile.path = malloc(path_len+1);
			memcpy(st->outfile.path, arg, path_len);
			st->outfile.path[path_len] = '\0';
			errno = 0;
			st->outfile.offset = strtoul(sep+1, NULL, 10);
			myassert(!errno, usage_str);
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

	st->window = SDL_CreateWindow("QuickPick",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		st->screenWidth, st->screenHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!st->window) {
		errexit("SDL_CreateWindow failed: %s\n", SDL_GetError());
	}

	// set window icon
	SDL_RWops *icon_rw = SDL_RWFromMem(quickpick_icon_png, quickpick_icon_png_len);
	SDL_Surface *icon_surface = SDL_LoadBMP_RW(icon_rw, 1);
	// Note: PNG loading would require SDL_image, so icon may not work without it
	// For now we skip icon setting if it fails
	if (icon_surface) {
		SDL_SetWindowIcon(st->window, icon_surface);
		SDL_FreeSurface(icon_surface);
	}

	st->gl_context = SDL_GL_CreateContext(st->window);
	if (!st->gl_context) {
		errexit("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
	}

/*
	glewExperimental = GL_TRUE;
	GLenum glew_err = glewInit();
	if (glew_err != GLEW_OK) {
		errexit("glewInit failed: %s\n", glewGetErrorString(glew_err));
	}
*/
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
		st->outfile.shortened_path = spath;
		st->outfile.shortened_path_len = str_n - remove;
		assert(st->outfile.shortened_path_len == strlen(st->outfile.shortened_path));

		Color start_color;
		bool success = read_color_from_outfile_and_maybe_update_offset(st, &start_color);
		if (success) {
			struct color_info ci;
			ci.rgb = start_color;
			ci.hsv = color_to_hsv(start_color);
			update_color_or_mode(st, st->mode, st->which_fixed, ci);
		} else {
			// since we failed to read, we probably shouldn't write
			fprintf(stderr, "[QUICKPICK WARNING] Failed to find a valid rrggbb(or #rrggbb) color at %s byte offset %llu, so not writing "
				"to the file.\n", st->outfile.path, st->outfile.offset);
			st->outfile.path = NULL;
		}
	}

	st->main_scene = create_scene(NULL, NULL, 10, 10000, true);
	st->hsv_grad_scene = create_scene(hsv_grad_vertex_shader, hsv_grad_fragment_shader,
		10, 361*3, true);
	// st->hsv_grad_scene = create_scene(NULL, NULL,
	// 	10, 361*3, true);

	int drawable_w, drawable_h;
	SDL_GL_GetDrawableSize(st->window, &drawable_w, &drawable_h);
	int window_w, window_h;
	SDL_GetWindowSize(st->window, &window_w, &window_h);
	st->dpi = (float)drawable_w / window_w;

	float dpi = st->dpi;
	init_for_dpi(st, st->dpi, 1.0f);

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
			init_for_dpi(st, new_dpi, st->dpi);
		}

		// Setup viewport and projection
		glViewport(0, 0, drawable_w, drawable_h);

		/*
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, st->screenWidth, st->screenHeight, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		*/

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
		if (ticks_now >= ticks_start + 1000) {
			printf("FPS: %llu\n", frames);
			ticks_start = ticks_now;
			frames = 0;
		}
*/

/*
		st->screenWidth = GetScreenWidth();
		st->screenHeight = GetScreenHeight();
		BeginDrawing();
		draw_ui_and_respond_input(st);
		EndDrawing();
*/
    }

    exit:
	SDL_GL_DeleteContext(st->gl_context);
	SDL_DestroyWindow(st->window);
	SDL_Quit();
	free(st);

	return 0;
}
