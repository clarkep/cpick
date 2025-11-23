/*
cpick: a color picker

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
#include <string.h> // strcmp, strchr, memcpy
#include <stdarg.h>
#include <errno.h>
#include <math.h> // round
#include <raylib.h>
#include <rlgl.h>

#include "font/noto_sans_mono_mini.h"

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define WRITE_INTERVAL 1.0

enum cursor_state {
	CURSOR_UP,
	CURSOR_START,
	CURSOR_DOWN,
	CURSOR_STOP
};

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
	// these two represent the same thing...
	float fixed_value; // 0-1
	// the other two dims, usually x and y but can be theta/r when fixed=value.
	float x_value;
	float y_value;
	Color text_color;
	Font text_font_small;
	// xx variable width fonts...
	int small_char_width;
	Font text_font_medium;
	int medium_label_width;
	Font text_font_large;
	Shader hsv_grad_shader;
	struct {
		char *path;
		unsigned long offset;
		int format;
		Color last_write_color;
		double last_write_time;
	} outfile;
	bool debug;
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
	int sel_i;
	float hover_v[3];
	float active_v[3];
} Tab_Select;

char *color_strings[2][3] = { "R", "G", "B", "H", "S", "V" };

void errexit(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(1);
}

void myassert(bool p, char *fmt, ...) {
	if (!p) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		exit(1);
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

struct color_info current_color(struct state *st)
{
	struct color_info res;
	if (st->mode == 0) {
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
		res.hsv = ColorToHSV(res.rgb);
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
		res.rgb = ColorFromHSV(res.hsv.x, res.hsv.y, res.hsv.z);
	}
	return res;
}

void write_color_to_file(struct state *st, Color color)
{
	char color_text[10];
	sprintf(color_text, "#%02x%02x%02x", color.r, color.g, color.b);

	FILE *f = fopen(st->outfile.path, "r+b");
	myassert(f, "Failed to open file: %s.\n", st->outfile.path);
	int res = fseek(f, st->outfile.offset, SEEK_SET);
	myassert(!res, "Failed to write offset %lu in file %s.\n", st->outfile.offset, st->outfile.path);
	fwrite(color_text, 1, 7, f);
	if (st->debug)
		printf("Wrote %s to %s:%lu.\n", color_text, st->outfile.path,
			st->outfile.offset);
	fclose(f);
}

void draw_gradient_square_rgb(int x, int y, int size, int which_fixed, float fixed_val)
{
	Color corner_cols[4];
	for (int i=0; i<2; i++) {
		for (int j=0; j<2; j++) {
			int c1 = j ? 255 : 0;
			int c2 = i ? 255 : 0;
			int f = roundf(255.0 * fixed_val);
			if (which_fixed == 0) {
				corner_cols[i*2+j] =  (Color) { f, c1, c2, 255 };
			} else if (which_fixed == 1) { // green
				corner_cols[i*2+j] = (Color) { c2, f, c1, 255 };
			} else if (which_fixed == 2) { // blue
				corner_cols[i*2+j] = (Color) { c1, c2, f, 255 };
			}
		}
	}
	Rectangle rec = { x, y, size, size };
	DrawRectangleGradientEx(rec, corner_cols[2], corner_cols[0], corner_cols[1], corner_cols[3]);
}

char *hsv_grad_fragshader =
"#version 330\n"
"in vec2 fragTexCoord;\n"
"in vec4 fragColor;\n"
"vec3 hsv2rgb(vec3 c)\n"
"{\n"
"    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
"    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
"    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
"}"
"void main()\n"
"{\n"
"	gl_FragColor = vec4(hsv2rgb(fragColor.xyz), 1.0);\n"
"}\n";

void draw_gradient_square_hsv(struct state *st, int x, int y, int size, int which_fixed,
	float fixed_val)
{
	Color corner_cols[4];
	for (int i=0; i<2; i++) {
		for (int j=0; j<2; j++) {
			int c1 = j ? 255 : 0;
			int c2 = i ? 255 : 0;
			if (which_fixed == 0) { // hue
				// The easiest way to write the fragment shader for raylib is to store the hsv
				// values as if they are RGB colors, which means they have to be clipped to 0-255.
				// But there are no visible changes compared to the more precise cpu only version.
				corner_cols[i*2+j] =  (Color) { fixed_val*255.0, j*255.0, i*255.0, 255.0 };
			} else if (which_fixed == 1) { // saturation
				corner_cols[i*2+j] = (Color) { j*255.0, fixed_val*255.0, i*255.0, 255.0 };
			}
		}
	}
	Rectangle rec = { x, y, size, size };
	BeginShaderMode(st->hsv_grad_shader);
	DrawRectangleGradientEx(rec, corner_cols[2], corner_cols[0], corner_cols[1], corner_cols[3]);
	EndShaderMode();
}

void draw_gradient_circle_and_axes(int x, int y, int r, float fixed_val, struct state *st)
{
	BeginShaderMode(st->hsv_grad_shader);
    rlBegin(RL_TRIANGLES);
        for (int i = 0; i < 255; i += 1)
        {
			float ang = (i / 255.0f) * 2 * PI;
			float ang2 = ((i+1) / 255.0f) * 2 * PI;
            rlColor4ub(i, 0, fixed_val*255.0f, 255);
            rlVertex2f(x, y);
            rlColor4ub(i, 255, fixed_val*255.0f, 255);
            rlVertex2f(x + cosf(ang)*r, y - sinf(ang)*r);
            rlColor4ub(i+1, 255, fixed_val*255.0f, 255);
            rlVertex2f(x + cosf(ang2)*r, y - sinf(ang2)*r);
        }
    rlEnd();
    EndShaderMode();
	// tick marks
	float dpi = st->dpi;
	for (float ang=0.0f; ang<360.0f; ang+=30.0) {
		float dx = cosf(ang*2*PI/360.0f);
		float dy = sinf(ang*2*PI/360.0f);
		int length = 5*dpi;
		Vector2 start = { x + r * dx, y + r * dy };
		Vector2 end = { start.x + length * dx, start.y + length * dy };
		DrawLineEx(start, end, 2.0*dpi, st->text_color);
	}
	// s arrow
	int arrow_len = 60*dpi;
	float arrow_w = 2.0*dpi;
	// int  = 4*dpi;
	// arrowhead
	float ah_len = 13*dpi;
	float ah_ang = (180-28)*2*PI/360.0f;
	float ah_w = 2.0*dpi;
	Vector2 arrow_end = (Vector2) { x+r+arrow_len, y};
	DrawLineEx((Vector2) {x+r+12*dpi, y}, arrow_end, arrow_w, st->text_color);
	Vector2 ah_left = { arrow_end.x + ah_len*cosf(ah_ang), arrow_end.y + ah_len*sinf(ah_ang)};
	Vector2 ah_right = { arrow_end.x + ah_len*cosf(-ah_ang), arrow_end.y + ah_len*sinf(-ah_ang)};
	DrawLineEx(arrow_end, ah_left, ah_w, st->text_color);
	DrawLineEx(arrow_end, ah_right, ah_w, st->text_color);
	DrawTextEx(st->text_font_small, "S",
			   (Vector2) {arrow_end.x-16.0*dpi, arrow_end.y-31.0*dpi}, 22*dpi, 2.*dpi,
			   st->text_color);
	// h label
	float harr_d = 30*dpi;
	float harr_w = 2*dpi;
	float harr_ang1 = 12;
	float harr_ang2 = 28;
	Vector2 harr_end = { x + (r+harr_d+harr_w/2)*cosf(2*PI*harr_ang2/360.0f),
		y - (r+harr_d+harr_w/2)*sinf(2*PI*harr_ang2/360.0f) };
	float harr_dir_ang = (harr_ang2*2*PI / 360.0f) + PI / 2;
	// arrowhead
	float adj = 2*PI/120;
	// xx angles are set up so that left arrowhead goes straight down, but still looks a bit janky
		Vector2 h_ah_left = { harr_end.x /*+ah_len*cosf(harr_dir_ang+ah_ang+adj)*/,
		harr_end.y-ah_len*sinf(harr_dir_ang+ah_ang+adj)};
	Vector2 h_ah_right = { harr_end.x+ah_len*cosf(harr_dir_ang-ah_ang+adj),
		harr_end.y-ah_len*sinf(harr_dir_ang-ah_ang+adj)};
	Color c3 = st->text_color;
	DrawLineEx(harr_end, h_ah_left, ah_w, c3);
	DrawLineEx(harr_end, h_ah_right, ah_w, c3);
	DrawTextEx(st->text_font_small, "H",
		(Vector2) {harr_end.x+18*dpi, harr_end.y-2*dpi}, 22*dpi, 2.*dpi, st->text_color);
	DrawRing((Vector2){x,y}, r+harr_d, r+harr_d+harr_w, -harr_ang1, -harr_ang2, 30, st->text_color);
	// Color c2 = { 255, 0, 0, 255 };
	// DrawPixelV(harr_end, c2);
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
	int label_size = 22*dpi;
	Color label_color = st->text_color;

	// x axis label
	char *x_label;
	char *y_label;
	if (!(st->mode == 1 && st->which_fixed == 1)) {
		x_label = color_strings[st->mode][(st->which_fixed+1)%3];
		y_label = color_strings[st->mode][(st->which_fixed+2)%3];
	} else {
		x_label = color_strings[1][0];
		y_label = color_strings[1][2];
	}
	DrawTextEx(st->text_font_small, x_label,
			   (Vector2) {x + 512*dpi/2 - label_size, y + 512*dpi + h - label_size}, label_size,
			   2.*dpi, label_color);
	// y axis label
	DrawTextEx(st->text_font_small, y_label,
			   (Vector2) {x - h, y + 512*dpi/2 - label_size}, label_size, 2.*dpi, label_color);
	// x axis
	for (int ix = x; ix < (x+512*dpi); ix += tick_sep) {
		DrawRectangle(ix, y+512*dpi, tick_width, x_tick_len, tick_color);
	}
	// y axis
	for (int yi = 0; yi < (512*dpi); yi += tick_sep) {
		DrawRectangle(x-y_tick_len, y + 512*dpi - yi - tick_width, y_tick_len, tick_width, tick_color);
	}
}

void switch_into_mode(struct state *st, int mode, int fixed, struct color_info ci)
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
	Color color1 = i == 0 ? active[0] : ColorBrightness(inactive[0], self->hover_brightness*hover_v[0]);
	Color color2 = i == 1 ? active[1] : ColorBrightness(inactive[1], self->hover_brightness*hover_v[1]);
	Color color3 = i == 2 ? active[2] : ColorBrightness(inactive[2], self->hover_brightness*hover_v[2]);
	Color text_color1 = i == 0 ? active_text : ColorBrightness(inactive_text, self->hover_brightness*hover_v[0]);
	Color text_color2 = i == 1 ? active_text : ColorBrightness(inactive_text, self->hover_brightness*hover_v[1]);
	Color text_color3 = i == 2 ? active_text : ColorBrightness(inactive_text, self->hover_brightness*hover_v[2]);
	char text[2] = "X";
	float x = self->x;
	float tw = self->w / 3.0;
	DrawRectangle(x, self->y, tw, self->h, color1);
	DrawRectangleLines(x, self->y, tw, self->h, self->border_color);
	text[0] = self->labels[0];
	DrawTextEx(st->text_font_small, text, (Vector2) {x + (tw - st->small_char_width)/2.0,
		self->y + (self->h-22*dpi)/2.0 }, 22*dpi, 2*dpi, text_color1);
	x += tw;
	DrawRectangle(x, self->y, tw, self->h, color2);
	DrawRectangleLines(x, self->y, tw, self->h, self->border_color);
	text[0] = self->labels[1];
	DrawTextEx(st->text_font_small, text, (Vector2) {x + (tw - st->small_char_width)/2.0,
		self->y + (self->h-22*dpi)/2.0 }, 22*dpi, 2*dpi, text_color2);
	int last_x = x + tw;
	int end_x = self->x + self->w;
	int last_w = end_x - last_x;
	DrawRectangle(last_x, self->y, last_w, self->h, color3);
	DrawRectangleLines(last_x, self->y, last_w, self->h, self->border_color);
	text[0] = self->labels[2];
	DrawTextEx(st->text_font_small, text, (Vector2) {last_x + (last_w - st->small_char_width)/2.0,
		self->y + (self->h-22*dpi)/2.0 }, 22*dpi, 2*dpi, text_color3);

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

void draw_ui_and_respond_input(struct state *st)
{
	if (IsMouseButtonDown(0)) {
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
	Vector2 pos = GetMousePosition();
	float anim_vdt = .3;

	struct color_info ci = current_color(st);
	Color cur_color = ci.rgb;
	Vector3 cur_hsv = ci.hsv;
	ClearBackground( cur_color );

	float dpi = st->dpi;
	// TODO: improve
	if (cur_color.r*cur_color.r + cur_color.g*cur_color.g*1.6 + cur_color.b*cur_color.b > 97500) {
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
		fixed_indication_color = GetColor(rgb_fixed_ind[wf]);
		light_text_indication_color = fixed_indication_color;
	} else {
		int a = st->text_color.r < 128 ? dark_text_bright_grey_bg : light_text_bright_grey_bg;
		int b = light_text_bright_grey_bg;
		fixed_indication_color = (Color) { a, a, a, 255 };
		light_text_indication_color = (Color) { b, b, b, 255 };
	}

	// gradient
	int y_axis_w = 30*dpi;
	int x_axis_h = 30*dpi;
	int grad_square_w = 512*dpi + y_axis_w;
	int grad_square_h = 512*dpi + x_axis_h;
	int grad_square_x = (st->screenWidth - 512*dpi)/2;
	int grad_square_y = 30*dpi;
	int grad_square_y_end = grad_square_y + 512*dpi;
	int grad_square_x_end = grad_square_x + 512*dpi;
	bool square = true;
	int cx = grad_square_x + 512*dpi/2;
	int cy = grad_square_y + 512*dpi/2;
	int cr = 512*dpi/2;
	if (st->mode == 0) {
		draw_gradient_square_rgb(grad_square_x, grad_square_y, 512*dpi, st->which_fixed, st->fixed_value);
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
		ind_x = cx + cr*st->y_value*cosf(st->x_value * 2*PI);
		ind_y = cy - cr*st->y_value*sinf(st->x_value * 2*PI);
	}
	DrawCircleLines(ind_x, ind_y, 6*dpi, st->text_color);
	int r2 = 4*dpi;
	int r3 = 8*dpi;
	DrawLine(ind_x - r3, ind_y, ind_x - r2, ind_y, st->text_color);
	DrawLine(ind_x + r2, ind_y, ind_x + r3, ind_y, st->text_color);
	DrawLine(ind_x, ind_y - r3, ind_x, ind_y - r2, st->text_color);
	DrawLine(ind_x, ind_y + r2, ind_x, ind_y + r3, st->text_color);
	if (st->cursor_state == CURSOR_START || st->square_dragging) {
		Vector2 pos = GetMousePosition();
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
				st->x_value = atan2(y_res, x_res) / (2*PI);
				st->x_value = st->x_value < 0 ? 1.0 + st->x_value : st->x_value;
				// r
				st->y_value = MIN(MAX(sqrtf(x_res*x_res+y_res*y_res)/(512*dpi/2), 0.0), 1.0);
			}
		}
	}

	// fixed color buttons
	int top_tabs_x = grad_square_x;
	int top_tabs_y = grad_square_y_end + x_axis_h + 10*dpi;
	int top_tabs_h = 20*dpi;
	int top_tabs_w = 70*dpi;
	int ind_button_x = top_tabs_x;
	int ind_button_y = top_tabs_y + top_tabs_h;
	int ind_button_w = top_tabs_w;
	int ind_button_h = 60*dpi;
	// int ind_tabs_y = ind_button_y + ind_button_h - ind_tabs_h - 1;
	int ind_tabs_y = ind_button_y + ind_button_h;
	Color ind_border_color = GetColor(0xa0a0a0ff);
	// main button
	static float ind_button_hover_v = 0;
	float hov_bright = .4;
	Color fixed_button_color = ColorBrightness(light_text_indication_color, ind_button_hover_v * hov_bright);
	DrawRectangle(ind_button_x, ind_button_y, ind_button_w, ind_button_h, fixed_button_color);
	DrawRectangleLines(ind_button_x, ind_button_y, ind_button_w, ind_button_h, ind_border_color);
	DrawTextEx(st->text_font_large, color_strings[st->mode][st->which_fixed],
		(Vector2) {ind_button_x+23*dpi, ind_button_y+(ind_button_h-40.0f*dpi)/2.0f}, 40.*dpi, 2*dpi, WHITE);
	if (CheckCollisionPointRec(pos, (Rectangle) { ind_button_x, ind_button_y, ind_button_w, ind_tabs_y-ind_button_y})) {
		if (st->cursor_state == CURSOR_START) {
			st->which_fixed = (st->which_fixed + 1) % 3;
			switch_into_mode(st, st->mode, st->which_fixed, ci);
			ind_button_hover_v = 0;
		}
		if (st->cursor_state != CURSOR_DOWN) {
			ind_button_hover_v = MIN(ind_button_hover_v + anim_vdt, 1.0);
		}
	} else {
		ind_button_hover_v = MAX(ind_button_hover_v - anim_vdt, 0.0);
	}
	// tabs
	Color color1, color2, color3; // tab colors
	static Tab_Select rgb_select;
	static Tab_Select hsv_select;
	static bool first_frame_setup_done = false;
	if (!first_frame_setup_done) {
		rgb_select.active_colors[0] = GetColor(0xc00000ff);
		rgb_select.active_colors[1] = GetColor(0x00c000ff);
		rgb_select.active_colors[2] = GetColor(0x0080ffff);
		rgb_select.inactive_colors[0] = GetColor(0x700000ff);
		rgb_select.inactive_colors[1] = GetColor(0x007000ff);
		rgb_select.inactive_colors[2] = GetColor(0x0000c0ff);
		rgb_select.active_text_color = GetColor(0xffffffff);
		rgb_select.inactive_text_color = GetColor(0xa0a0a0ff);
		rgb_select.labels[0] = 'R';
		rgb_select.labels[1] = 'G';
		rgb_select.labels[2] = 'B';
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
		switch_into_mode(st, st->mode, st->which_fixed, ci);
	}
	hsv_select.x = ind_button_x;
	hsv_select.y = ind_button_y + ind_button_h;
	hsv_select.w = ind_button_w;
	hsv_select.h = top_tabs_h;
	if (tab_select(&hsv_select, pos, st->cursor_state)) {
		st->mode = 1;
		st->which_fixed = hsv_select.sel_i;
		switch_into_mode(st, st->mode, st->which_fixed, ci);
	}

	// hsv-rgb toggle
	int toggle_button_x = ind_button_x;
	int toggle_button_y = hsv_select.y + hsv_select.h;
	int toggle_button_w = ind_button_w;
	int toggle_button_h = 20*dpi;

	// fixed value slider
	int val_slider_x = ind_button_x + ind_button_w + 20*dpi;
	int val_slider_y = ind_button_y + ind_button_h / 2.0f;
	int val_slider_w = grad_square_x_end - val_slider_x;
	int val_slider_h = 60*dpi;
	// center vertically relative to two adjacent buttons
	int toggle_button_y_end = toggle_button_y + toggle_button_h;
	int val_slider_offset = roundf(val_slider_w * ( (float) st->fixed_value ));
	{
		int bar_h = 8*dpi;
		int circle_r = 15*dpi;
		DrawRectangle(val_slider_x, val_slider_y-bar_h/2.0f, val_slider_w, 6*dpi, st->text_color);
		Vector2 circle_center = { val_slider_x + val_slider_offset, val_slider_y };
		DrawCircleV(circle_center, 15*dpi, fixed_indication_color);
	}
	if (st->cursor_state == CURSOR_START || st->val_slider_dragging) {
		TraceLog(LOG_DEBUG, "Received click. dragging: %d", st->val_slider_dragging);
		Vector2 pos = GetMousePosition();
		if (!st->val_slider_dragging && CheckCollisionPointRec(pos,
			(Rectangle) { val_slider_x, val_slider_y-val_slider_h/2.0f, val_slider_w, val_slider_h } )) {
			st->val_slider_dragging = true;
		}
		if (st->val_slider_dragging) {
			val_slider_offset = MIN(val_slider_w, MAX(0, pos.x - val_slider_x));
		}
		st->fixed_value = MIN(MAX((float) val_slider_offset / val_slider_w, 0), 1.0);
	}


	// color read out
	char value[40];
	sprintf(value, "r:%-3d g:%-3d b:%-3d hex:#%02x%02x%02x", cur_color.r, cur_color.g, cur_color.b,
		cur_color.r, cur_color.g, cur_color.b);

	int label_x = (st->screenWidth - st->medium_label_width) / 2;
	DrawTextEx(st->text_font_medium, value, (Vector2) {label_x, val_slider_y + 85*dpi}, 30.*dpi,
		1.5*dpi, st->text_color);
	char h_value[8];
	sprintf(h_value, "%d\xc2\xb0", (int) cur_hsv.x);
	char s_value[10];
	sprintf(s_value, "%d%%", (int)(cur_hsv.y*100));
	char v_value[10];
	sprintf(v_value, "%d%%", (int)(cur_hsv.z*100));
	char hsv_value[30];
	sprintf(hsv_value, "h:%-5s s:%-4s v:%-3s", h_value, s_value, v_value);
	DrawTextEx(st->text_font_medium, hsv_value, (Vector2) {label_x, val_slider_y + 125*dpi}, 30.*dpi,
		1.5*dpi, st->text_color);

	double now = GetTime();
	if (st->outfile.path && now - st->outfile.last_write_time > WRITE_INTERVAL &&
		!colors_equal(cur_color, st->outfile.last_write_color)) {
		write_color_to_file(st, cur_color);
		st->outfile.last_write_color = cur_color;
		st->outfile.last_write_time = now;
	}
}

char *usage_str =
"cpick [file@offset]\n"
"Options:\n"
"  --file FILE     choose a file to output to; alternative to file@offset\n"
"  --offset N      choose an offset in FILE; alternative to file@offset\n";

// A bug in raylib's font atlas generation code causes LoadFontEx to fail to load 'B' if the only
// chars are 'R', 'G', and 'B' when setting up text_font_large. A workaround for now is to add
// unnecessary extra characters.
int small_codepoints[] = { 'R', 'G', 'B', 'H', 'S', 'V', '/', 'r', 'g', 'b', 'h', 's', 'v', 'A', 'B',
	'C', 'D', 'E', 'F' };
int medium_codepoints[] = { 'r', 'g', 'b', 'h', 's', 'v', '0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '0', 'a', 'c', 'd', 'e', 'f', 'x', ':', ' ', ':', '#', '%', 0xb0 };
int large_codepoints[] = { 'R', 'G', 'H', 'S', 'V', 'A', 'B', 'C', 'D', 'E', 'F' };

void init_for_dpi(struct state *st, float dpi, float old_dpi)
{
	st->dpi = dpi;
	float ratio = dpi / old_dpi;
	int new_target_w = st->screenWidth * ratio;
	int new_target_h = st->screenHeight * ratio;
	if (new_target_w  != st->screenWidth) {
		SetWindowSize(new_target_w, new_target_h);
		st->screenWidth = GetScreenWidth();
		st->screenHeight = GetScreenHeight();
	}

	st->text_font_small = LoadFontFromMemory(".ttf", noto_sans_mono_mini_ttf, noto_sans_mono_mini_ttf_len,
		22*dpi, small_codepoints, sizeof(small_codepoints)/sizeof(int));
	st->text_font_medium = LoadFontFromMemory(".ttf", noto_sans_mono_mini_ttf, noto_sans_mono_mini_ttf_len,
		30*dpi, medium_codepoints, sizeof(medium_codepoints)/sizeof(int));
	st->text_font_large = LoadFontFromMemory(".ttf", noto_sans_mono_mini_ttf, noto_sans_mono_mini_ttf_len,
		40*dpi, large_codepoints, sizeof(large_codepoints)/sizeof(int));

	// Measure the label width once; since it's a monospace font, it will be the same for all colors.
	st->medium_label_width = MeasureTextEx(st->text_font_medium, "r:255 g:255 b:255 hex:#ffffff",
		30*dpi, 1.5*dpi).x;
	st->small_char_width = MeasureTextEx(st->text_font_small, "R",
		22*dpi, 0*dpi).x;
}

int main(int argc, char *argv[])
{
	struct state *st = (struct state *) calloc(1, sizeof(struct state));
	st->screenWidth = 680;
	st->screenHeight = 780;
	st->mode = 0;
	st->which_fixed = 2;
	st->saved_fixed = 0;
	st->fixed_value = 0.0;
	st->x_value = 0;
	st->y_value = 0;
	st->square_dragging = false;
	st->val_slider_dragging = false;
	st->text_color = WHITE;
	st->outfile.path = NULL;
	st->outfile.offset = 0;
	st->outfile.format = 0;
	st->outfile.last_write_time = GetTime();
	st->outfile.last_write_color = (Color) { 0, 0, 0, 255 };
	st->debug = true;

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
				st->outfile.offset = strtoul(argv[i+1], NULL, 10);
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
		printf("outfile: %s\n", st->outfile.path);
	}

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	SetTraceLogLevel(LOG_WARNING);
	InitWindow(st->screenWidth, st->screenHeight, "Cpick");
	st->hsv_grad_shader = LoadShaderFromMemory(NULL, hsv_grad_fragshader);

	st->dpi = GetWindowScaleDPI().x;
	init_for_dpi(st, st->dpi, 1);

	SetTargetFPS(60);
	while (!WindowShouldClose())
	{
		float new_dpi = GetWindowScaleDPI().x;
		if (new_dpi != st->dpi) {
			init_for_dpi(st, new_dpi, st->dpi);
		}
		st->screenWidth = GetScreenWidth();
		st->screenHeight = GetScreenHeight();
		BeginDrawing();
		draw_ui_and_respond_input(st);
		EndDrawing();
    }

	return 0;
}
