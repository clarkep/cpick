/*
cpick: a color picker

By: Paul Clarke
Created: 4/10/2024
License: GPL-3.0 (see LICENSE)
*/

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h> // malloc, exit
#include <string.h> // strcmp, strchr, memcpy
#include <stdarg.h>
#include <errno.h>
#include <math.h> // round
#include <raylib.h>
#include "font/noto_sans_mono_small.h"
#include "font/noto_sans_mono_medium.h"
#include "font/noto_sans_mono_large.h"

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define WRITE_INTERVAL 1.0

struct state {
	int screenWidth;
	int screenHeight;
	int which_fixed; // red(0), green(1) or blue(2)
	bool val_slider_dragging;
	// these two represent the same thing...
	int fixed_value; // 0-255
	// last click in the square, in pixels:
	int x_value;
	int y_value;
	Color text_color;
	Font text_font_small;
	Font text_font_medium;
	Font text_font_large;
	struct {
		char *path;
		unsigned long offset;
		int format;
		Color last_write_color;
		double last_write_time;
	} outfile;
	bool debug;
};

char *color_strings[3] = { "R", "G", "B" };

void myassert(bool p, char *fmt, ...) {
	if (!p) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		exit(1);
	}
}

bool colors_equal(Color c1, Color c2) {
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b && c1.a == c2.a;
}

Color current_color(struct state *st)
{
	int v1 = st->fixed_value;
	int v2 = st->x_value;
	int v3 = st->y_value;
	switch (st->which_fixed) {
		case 0:
			return (Color) { v1, v2, v3, 255 };
			break;
		case 1:
			return (Color) { v3, v1, v2, 255 };
			break;
		case 2:
			return (Color) { v2, v3, v1, 255 };
			break;
	}
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

void draw_gradient_n(int x, int y, int n, int which_fixed, int fixed_val)
{
	int cur_x = 0;
	int cur_y = 0;
	for (int v2 = 0; v2 < 256; v2++) {
		for (int v1 = 0; v1 < 256; v1++) {
			struct Color col;
			if (which_fixed == 0) { // red
				col =  (Color) { fixed_val, v1, v2, 255 };
			} else if (which_fixed == 1) { // green
				col = (Color) { v2, fixed_val, v1, 255 };
			} else if (which_fixed == 2) { // blue
				col = (Color) { v1, v2, fixed_val, 255 };
			}
			for (int iy = 0; iy < n; iy++) {
				for (int ix = 0; ix < n; ix++) {
					DrawPixel(x + cur_x + ix, y + 511 - (cur_y + iy), col);
				}
			}
			cur_x += n;
		}
		cur_x = 0;
		cur_y += n;
	}
}

// TODO: parameter for 512 vs etc ?
void draw_axes(int x, int y, int w, int h, struct state *st)
{
	int tick_sep = 64;
	int tick_width = 2; 
	int y_tick_len = w/4;
	int x_tick_len = h/4;
	Color tick_color = st->text_color;
	int label_size = 22;
	Color label_color = st->text_color;

	// x axis label
	DrawTextEx(st->text_font_small, color_strings[(st->which_fixed+1)%3],
			   (Vector2) {x + 512/2 - label_size, y + 512 + h - label_size}, label_size, 2.,
			   label_color);
	DrawTextEx(st->text_font_small, color_strings[(st->which_fixed+2)%3],
			   (Vector2) {x - h, y + 512/2 - label_size}, label_size, 2., label_color);
	// x axis
	for (int ix = x; ix < (x+512); ix += tick_sep) {
		DrawRectangle(ix, y+512, tick_width, x_tick_len, tick_color);
	}
	// perfectionist last tick
	DrawRectangle(x+512-tick_width, y+512, tick_width, x_tick_len, tick_color);
	// y axis
	for (int iy = y; iy < (y+512); iy += tick_sep) {
		DrawRectangle(x-y_tick_len, iy, y_tick_len, tick_width, tick_color);
	}
	DrawRectangle(x-y_tick_len, y+512-tick_width, y_tick_len, tick_width, tick_color);
}

void draw_ui_and_respond_input(struct state *st)
{
	ClearBackground( current_color(st) );
	Color cur_color = current_color(st);
	if (cur_color.r*cur_color.r + cur_color.g*cur_color.g + cur_color.b*cur_color.b > 110000) {
		st->text_color = BLACK;
	} else {
		st->text_color = WHITE;
	}

	// gradient square
	int y_axis_w = 30;
	int x_axis_h = 30;
	int grad_square_w = 512 + y_axis_w;
	int grad_square_h = 512 + x_axis_h;
	int grad_square_x = (st->screenWidth - 512)/2;
	int grad_square_y = 30;
	int grad_square_y_end = grad_square_y + 512;
	int grad_square_x_end = grad_square_x + 512;
	draw_axes(grad_square_x, grad_square_y, x_axis_h, y_axis_w, st);
	draw_gradient_n(grad_square_x, grad_square_y, 512/256, st->which_fixed, st->fixed_value);
	int cur_loc_sq_sz = 4;
	// depends on 512...
	int square_x_offset = st->x_value * 2;
	int square_y_offset = st->y_value * 2;
	DrawRectangle(grad_square_x + square_x_offset - cur_loc_sq_sz/2,
			grad_square_y + 511 - square_y_offset - cur_loc_sq_sz/2,
			cur_loc_sq_sz, cur_loc_sq_sz, st->text_color);
	if (IsMouseButtonDown(0)) {
		Vector2 pos = GetMousePosition();
		if (CheckCollisionPointRec(pos, (Rectangle) { grad_square_x, grad_square_y, 512, 512 })) {
			// depends on 512...
			st->x_value = (pos.x - grad_square_x) / 2;
			st->y_value = (grad_square_y + 511 - pos.y) / 2;
		}
	}

	// indicator button 
	int ind_button_x = grad_square_x;
	int ind_button_y = grad_square_y_end + x_axis_h + 10;
	int ind_button_h = 60;
	DrawRectangleLines(ind_button_x, ind_button_y, ind_button_h, ind_button_h, st->text_color);
	DrawTextEx(st->text_font_large, color_strings[st->which_fixed], (Vector2) {ind_button_x+18, ind_button_y+10}, 40., 2, st->text_color);
	if (IsMouseButtonPressed(0)) {
		Vector2 pos = GetMousePosition();
		if (CheckCollisionPointRec(pos, (Rectangle) { ind_button_x, ind_button_y, ind_button_h, ind_button_h})) {
			st->which_fixed = (st->which_fixed + 1) % 3;
			// Preserve color: x becomes the new fixed, y the new x, fixed the new y
			int tmp = st->fixed_value;
			st->fixed_value = st->x_value;
			st->x_value = st->y_value;
			st->y_value = tmp;
		}
	}

	// fixed value slider 
	int val_slider_x = ind_button_x + ind_button_h + 20;
	int val_slider_y = ind_button_y;
	int val_slider_w = grad_square_x_end - val_slider_x;
	int val_slider_h = 60;
	DrawRectangle(val_slider_x, val_slider_y+26, val_slider_w, 6, st->text_color);
	int wf = st->which_fixed;
	int val_slider_offset = roundf(val_slider_w * ( (float) st->fixed_value / 255 ));
	Vector2 circle_center = { val_slider_x + val_slider_offset, val_slider_y+30 };
	DrawCircleV(circle_center, 15,
			   (Color) { wf == 0 ? 218 : 0, wf == 1 ? 216 : 0,  wf == 2 ? 216 : 0, 255 });
	if (IsMouseButtonDown(0)) {
		TraceLog(LOG_DEBUG, "Received click. dragging: %d", st->val_slider_dragging);
		Vector2 pos = GetMousePosition();
		if (CheckCollisionPointCircle(pos, circle_center, 30) || st->val_slider_dragging) {
			st->val_slider_dragging = true;
			val_slider_offset = MIN(val_slider_w, MAX(0, pos.x - val_slider_x));
		} else if (CheckCollisionPointRec(pos, (Rectangle) { val_slider_x, val_slider_y, val_slider_w, val_slider_w } )) {
			val_slider_offset = pos.x - val_slider_x;
		}
		st->fixed_value = roundf((float) 255*val_slider_offset / val_slider_w);
	} else {
		st->val_slider_dragging = false;
	}

	// color read out
	char value[30];
	sprintf(value, "r:%-3d g:%-3d b:%-3d hex:#%02x%02x%02x", cur_color.r, cur_color.g, cur_color.b, cur_color.r, cur_color.g, cur_color.b);

	DrawTextEx(st->text_font_medium, value, (Vector2) {grad_square_x, val_slider_y + 70}, 30., 1.5, st->text_color);

	double now = GetTime();
	if (st->outfile.path && now - st->outfile.last_write_time > WRITE_INTERVAL &&
		!colors_equal(cur_color, st->outfile.last_write_color)) {
		write_color_to_file(st, cur_color);
		st->outfile.last_write_color = cur_color;
		st->outfile.last_write_time = now;
	}
}

void assert_usage(bool p)
{
	myassert(p, "usage: cpick [file.txt:offset]\n");
}

int small_large_codepoints[] = { 'R', 'G', 'B' };
int medium_codepoints[] = { 'r', 'g', 'b', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
	'a', 'c', 'd', 'e', 'f', 'h', 'x', ':', ' ', ':', '#'};

int main(int argc, char *argv[])
{
	struct state *st = (struct state *) calloc(1, sizeof(struct state));
	st->screenWidth = 620;
	st->screenHeight = 700;
	st->which_fixed = 0;
	st->fixed_value = 0;
	st->x_value = 0;
	st->y_value = 0;
	st->text_color = WHITE;
	st->outfile.path = NULL;
	st->outfile.offset = 0;
	st->outfile.format = 0;
	st->outfile.last_write_time = GetTime();
	st->outfile.last_write_color = (Color) { 0, 0, 0, 255 };
	st->debug = true;

	for (int i=1; i<argc; i++) {
		char *arg = argv[i];
		if (argv[i][0] == '-') {
			if (argv[i][1] == '-') {

			} else {

			}
		} else {
			assert_usage(!st->outfile.path);
			char *sep = strchr(arg, ':');
			assert_usage(sep);
			int path_len = sep - arg;
			st->outfile.path = malloc(path_len+1);
			memcpy(st->outfile.path, arg, path_len);
			st->outfile.path[path_len] = '\0';
			errno = 0;
			st->outfile.offset = strtoul(sep+1, NULL, 10);
			assert_usage(!errno);
			i++;
		}
	}

	if (st->debug && st->outfile.path) {
		printf("outfile: %s\n", st->outfile.path);
	}

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	SetTraceLogLevel(LOG_WARNING);
	InitWindow(st->screenWidth, st->screenHeight, "CPick");

	/*
	// Load directly from ttf file(one time)
	st->text_font_small = LoadFontEx("font/NotoSansMono-Regular.ttf", 22, small_large_codepoints,
		sizeof(small_large_codepoints)/sizeof(int));
	st->text_font_medium = LoadFontEx("font/NotoSansMono-Regular.ttf", 30, medium_codepoints,
		sizeof(medium_codepoints)/sizeof(int));
	st->text_font_large = LoadFontEx("font/NotoSansMono-Regular.ttf", 40, small_large_codepoints,
 		sizeof(small_large_codepoints)/sizeof(int));
	*/
	// Now that fonts have been exported, we can load them from embedded data.
	// TODO: to support hi-dpi, we will have to load at arbitrary sizes, which will require
	// embedding the actual ttf file. I'm putting that off because it will require some way
	// to shrink the file first(compress and remove unnecessary chars).
	st->text_font_small = LoadFont_NotoSansMonoSmall();
	st->text_font_medium = LoadFont_NotoSansMonoMedium();
	st->text_font_large = LoadFont_NotoSansMonoLarge();

	SetTargetFPS(60);
	while (!WindowShouldClose())
	{
		st->screenWidth = GetScreenWidth();
		st->screenHeight = GetScreenHeight();
		BeginDrawing();
		draw_ui_and_respond_input(st);
		EndDrawing();
    }

	/*
    // Export fonts(one time)
	ExportFontAsCode(st->text_font_small, "font/noto_sans_mono_small.h");
	ExportFontAsCode(st->text_font_medium, "font/noto_sans_mono_medium.h");
	ExportFontAsCode(st->text_font_large, "font/noto_sans_mono_large.h");
	*/
	CloseWindow();
	return 0;
}
