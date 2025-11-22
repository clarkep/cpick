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

struct state {
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
};

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

void value_creep_towards(float *val, float target, float amount) {
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
	if (st->mode == 0) {
		int wf = st->which_fixed;
		int rgb_fixed_ind[] = { 0xc00000ff, 0x00c000ff, 0x0050ffff };
		fixed_indication_color = GetColor(rgb_fixed_ind[wf]);
	} else {
		int a = st->text_color.r < 128 ? dark_text_bright_grey_bg : light_text_bright_grey_bg;
		fixed_indication_color = (Color) { a, a, a, 255 };
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
	int ind_button_x = grad_square_x;
	int ind_button_y = grad_square_y_end + x_axis_h + 10*dpi;
	int ind_button_w = 70*dpi;
	int ind_button_h = 64*dpi;
	int ind_tabs_h = 9*dpi;
	// int ind_tabs_y = ind_button_y + ind_button_h - ind_tabs_h - 1;
	int ind_tabs_y = ind_button_y + ind_button_h;
	// main button
	static float ind_button_hover_v = 0;
	Color hl_color = { 255, 255, 0, 255 };
	float anim_vdt = .3;
	float hov_bright = .4;
	Color fixed_button_color = ColorBrightness(fixed_indication_color, ind_button_hover_v * hov_bright);
	DrawRectangle(ind_button_x, ind_button_y, ind_button_w, ind_button_h, fixed_button_color);
	DrawRectangleLines(ind_button_x, ind_button_y, ind_button_w, ind_button_h, st->text_color);
	DrawTextEx(st->text_font_large, color_strings[st->mode][st->which_fixed],
		(Vector2) {ind_button_x+23*dpi, ind_button_y+15*dpi}, 40.*dpi, 2*dpi, st->text_color);
	Vector2 pos = GetMousePosition();
	int new_fixed = -1;
	if (CheckCollisionPointRec(pos, (Rectangle) { ind_button_x, ind_button_y, ind_button_w, ind_tabs_y-ind_button_y})) {
		if (st->cursor_state == CURSOR_START) {
			new_fixed = (st->which_fixed + 1) % 3;
			ind_button_hover_v = 0;
		}
		if (st->cursor_state != CURSOR_DOWN) {
			ind_button_hover_v = MIN(ind_button_hover_v + anim_vdt, 1.0);
		}
	} else {
		ind_button_hover_v = MAX(ind_button_hover_v - anim_vdt, 0.0);
	}
	// tabs
	static float tabs_hover_v[3];
	static float tabs_active_v[3];
	float tabs_active_target[3] = { 0.0, 0.0, 0.0 };
	float tabs_hover_target[3] = { 0.0, 0.0, 0.0 };
	static bool tabs_active_v_initialized = false;
	tabs_active_target[st->which_fixed] = 1.0;
	/*
	if (!tabs_active_v_initialized) {
		tabs_active_v[st->which_fixed] = 1.0;
		tabs_active_v_initialized = true;
	}
	*/
	Color color1, color2, color3; // tab colors
	if (!st->mode) {
		color1 = st->which_fixed == 0 ? GetColor(0xf00000ff) : GetColor(0xc00000ff);
		color2 = st->which_fixed == 1 ? GetColor(0x00f000ff) : GetColor(0x00c000ff);
		color3 = st->which_fixed == 2 ? GetColor(0x00a0ffff) : GetColor(0x0050ffff);
	} else {
		int a = st->text_color.r < 128 ? dark_text_bright_grey_bg : light_text_bright_grey_bg;
		int b = st->text_color.r < 128 ? dark_text_dim_grey_bg : light_text_dim_grey_bg;
		Color bright = { a, a, a, 255 };
		Color dim = { b, b, b, 255 };
		color1 = st->which_fixed == 0 ? bright : dim;
		color2 = st->which_fixed == 1 ? bright : dim;
		color3 = st->which_fixed == 2 ? bright : dim;
	}
	color1 = ColorBrightness(color1, tabs_hover_v[0]*hov_bright);
	color2 = ColorBrightness(color2, tabs_hover_v[1]*hov_bright);
	color3 = ColorBrightness(color3, tabs_hover_v[2]*hov_bright);
	// XX I don't like this height boost effect, but it might look nice with some tweaking(maybe
	// animating the widths as well). So just zero it for now.
	int max_boost = 0.0 * dpi;;
	float hb[3]; // tab height boosts
	for (int tab_i=0; tab_i<3; tab_i++) {
		hb[tab_i] = max_boost * tabs_hover_v[tab_i];
	}
	// XX why is the +1 on the heights necessary?
	DrawRectangle(ind_button_x, ind_tabs_y-hb[0], ind_button_w/3, ind_tabs_h+hb[0], color1);
	DrawRectangleLines(ind_button_x, ind_tabs_y-hb[0], ind_button_w/3, ind_tabs_h+1+hb[0], st->text_color);
	DrawRectangle(ind_button_x+ind_button_w/3, ind_tabs_y-hb[1], ind_button_w/3, ind_tabs_h+hb[1], color2);
	DrawRectangleLines(ind_button_x+ind_button_w/3, ind_tabs_y-hb[1], ind_button_w/3, ind_tabs_h+1+hb[1], st->text_color);
	// XX and here, the +1 on the widths
	DrawRectangle(ind_button_x+2*ind_button_w/3, ind_tabs_y-hb[2], ind_button_w/3+1, ind_tabs_h+hb[2], color3);
	DrawRectangleLines(ind_button_x+2*ind_button_w/3, ind_tabs_y-hb[2], ind_button_w/3+1, ind_tabs_h+1+hb[2], st->text_color);
	// DrawRectangleLines(ind_button_x, ind_tabs_y, ind_button_w, ind_tabs_h, st->text_color);
	if (CheckCollisionPointRec(pos, (Rectangle) { ind_button_x, ind_tabs_y-max_boost, ind_button_w, ind_tabs_h+max_boost})) {
		int tab_i = (pos.x - ind_button_x) / (ind_button_w / 3.0);
		if (st->cursor_state == CURSOR_START && tab_i != st->which_fixed) {
			new_fixed = tab_i;
			// slam to zero instead of setting target
			tabs_hover_v[tab_i] = 0.0;
		}
		tabs_hover_target[tab_i] = 1.0;
		tabs_active_target[tab_i] = 1.0;
	}
	for (int tab_i=0; tab_i<3; tab_i++) {
		value_creep_towards(&tabs_hover_v[tab_i], tabs_hover_target[tab_i], anim_vdt);
		value_creep_towards(&tabs_active_v[tab_i], tabs_active_target[tab_i], anim_vdt);
	}
	if (new_fixed >= 0) {
		st->which_fixed = new_fixed;
		switch_into_mode(st, st->mode, st->which_fixed, ci);
	}

	// hsv-rgb toggle
	int toggle_button_x = ind_button_x;
	int toggle_button_y = ind_tabs_y + ind_tabs_h + 5*dpi;
	int toggle_button_w = ind_button_w;
	int toggle_button_h = 20*dpi;
	Color toggle_selected_bg;
	Color toggle_unselected_bg;
	if (st->text_color.r < 128) {
		int a = dark_text_bright_grey_bg;
		int b = dark_text_dim_grey_bg;
		toggle_selected_bg = (Color) { a, a, a, 255 };
		toggle_unselected_bg = (Color) { b, b, b, 255 };
	} else {
		int a = light_text_bright_grey_bg;
		int b = light_text_dim_grey_bg;
		toggle_selected_bg = (Color) { a, a, a, 255 };
		toggle_unselected_bg = (Color) { b, b, b, 255 };
	}
	static float toggle_unselected_hover_v = 0;
	Color toggle_unselected_bg_mod = ColorBrightness(toggle_unselected_bg,
		toggle_unselected_hover_v*0.15);
	DrawRectangle(toggle_button_x, toggle_button_y, toggle_button_w/2, toggle_button_h,
		st->mode ? toggle_unselected_bg_mod : toggle_selected_bg);
	DrawRectangle(toggle_button_x + toggle_button_w/2, toggle_button_y, toggle_button_w/2,
		toggle_button_h, st->mode ? toggle_selected_bg : toggle_unselected_bg_mod);
	DrawRectangleLines(toggle_button_x, toggle_button_y, toggle_button_w, toggle_button_h,
		st->text_color);
	DrawTextEx(st->text_font_small, "RGB", (Vector2) { toggle_button_x+4*dpi,
		toggle_button_y-1*dpi }, 22*dpi, 0*dpi, st->text_color);
	DrawTextEx(st->text_font_small, "HSV", (Vector2) { toggle_button_x+toggle_button_w/2+4*dpi,
		toggle_button_y-1*dpi }, 22*dpi, 0*dpi, st->text_color);
	Rectangle cur_rect;
	cur_rect = (Rectangle) { toggle_button_x + (st->mode ? 0 : toggle_button_w/2), toggle_button_y,
		toggle_button_w/2, toggle_button_h };
	if (CheckCollisionPointRec(pos, cur_rect)) {
		toggle_unselected_hover_v = MIN(toggle_unselected_hover_v + anim_vdt, 1.0);
		if (st->cursor_state == CURSOR_START) {
			int tmp = st->which_fixed;
			st->which_fixed = st->saved_fixed;
			st->saved_fixed = tmp;
			st->mode = (st->mode + 1) % 2;
			switch_into_mode(st, st->mode, st->which_fixed, ci);
		}
	} else {
		toggle_unselected_hover_v = MAX(toggle_unselected_hover_v - anim_vdt, 0.0);
	}

	// fixed value slider
	int val_slider_x = ind_button_x + ind_button_w + 20*dpi;
	int val_slider_w = grad_square_x_end - val_slider_x;
	int val_slider_h = 60*dpi;
	// center vertically relative to two adjacent buttons
	int toggle_button_y_end = toggle_button_y + toggle_button_h;
	int val_slider_y = ind_button_y + ((toggle_button_y_end-ind_button_y) - val_slider_h) / 2;
	int val_slider_offset = roundf(val_slider_w * ( (float) st->fixed_value ));
	{
		DrawRectangle(val_slider_x, val_slider_y+26*dpi, val_slider_w, 6*dpi, st->text_color);
		Vector2 circle_center = { val_slider_x + val_slider_offset, val_slider_y+30*dpi };
		DrawCircleV(circle_center, 15*dpi, fixed_indication_color);
	}
	if (st->cursor_state == CURSOR_START || st->val_slider_dragging) {
		TraceLog(LOG_DEBUG, "Received click. dragging: %d", st->val_slider_dragging);
		Vector2 pos = GetMousePosition();
		if (!st->val_slider_dragging && CheckCollisionPointRec(pos,
			(Rectangle) { val_slider_x, val_slider_y, val_slider_w, val_slider_h } )) {
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
