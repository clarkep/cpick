/*
cpick: a color picker

By: Paul Clarke
Created: 4/10/2024
*/

#include <stdio.h>
#include <stdlib.h> // malloc, exit
#include <string.h> // strcmp, strchr, memcpy
#include <stdarg.h>
#include <errno.h>
#include <sys/param.h> // MIN, MAX
#include <sys/time.h>
#include <math.h> // round
#include "raylib.h" // everything CamelCase except...
#include "noto_sans_mono_ttf.h" // LoadFont_NotoSansMonoTtf

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
	Font text_font;
	struct {
		char *path;
		unsigned long long offset;
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

double get_os_time()
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return (double) t.tv_sec + t.tv_usec / 1e6;
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
	myassert(!res, "Failed to write offset %llu in file %s.\n", st->outfile.offset, st->outfile.path);
	int written = fwrite(color_text, 1, 7, f);
	if (st->debug)
		printf("Wrote %s to %s:%llu.\n", color_text, st->outfile.path,
			st->outfile.offset);
	fclose(f);
}

void draw_gradient_n(int x, int y, int n, int which_fixed, int fixed_val)
{
	int cur_x = x;
	int cur_y = y;
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
					DrawPixel(cur_x + ix, cur_y + iy, col);
				}
			}
			cur_x += n;
		}
		cur_x = x;
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

	DrawTextEx(st->text_font, color_strings[(st->which_fixed+1)%3],
			   (Vector2) {x+w + 512/2 - label_size, y}, label_size, 2., label_color);
	DrawTextEx(st->text_font, color_strings[(st->which_fixed+2)%3],
			   (Vector2) {x, y+h + 512/2 - label_size}, label_size, 2., label_color);
	// x axis
	for (int ix = x+w; ix < (x+w+512); ix += tick_sep) {
		DrawRectangle(ix, y+h-x_tick_len, tick_width, x_tick_len, tick_color);
	}
	// perfectionist last tick
	DrawRectangle(x+w+512-tick_width, y+h-x_tick_len, tick_width, x_tick_len, tick_color);
	// y axis
	for (int iy = y+h; iy < (y+h+512); iy += tick_sep) {
		DrawRectangle(x+w-y_tick_len, iy, y_tick_len, tick_width, tick_color);
	}
	DrawRectangle(x+w-y_tick_len, y+h+512-tick_width, y_tick_len, tick_width, tick_color);
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
	int grad_square_y = 40;
	int grad_square_y_end = grad_square_y + 512;
	int grad_square_x_end = grad_square_x + 512;
	draw_axes(grad_square_x-y_axis_w, grad_square_y-x_axis_h, x_axis_h, y_axis_w, st);
	draw_gradient_n(grad_square_x, grad_square_y, 512/256, st->which_fixed, st->fixed_value);
	int cur_loc_sq_sz = 4;
	// depends on 512...
	int square_x_offset = st->x_value * 2;
	int square_y_offset = st->y_value * 2;
	DrawRectangle(grad_square_x + square_x_offset - cur_loc_sq_sz/2,
			grad_square_y + square_y_offset - cur_loc_sq_sz/2,
			cur_loc_sq_sz, cur_loc_sq_sz, st->text_color);
	if (IsMouseButtonDown(0)) {
		Vector2 pos = GetMousePosition();
		if (CheckCollisionPointRec(pos, (Rectangle) { grad_square_x, grad_square_y, 512, 512 })) {
			// depends on 512...
			st->x_value = (pos.x - grad_square_x) / 2;
			st->y_value = (pos.y - grad_square_y) / 2;
		}
	}

	// indicator button 
	int ind_button_x = grad_square_x;
	int ind_button_y = grad_square_y_end + 10;
	int ind_button_h = 60;
	DrawRectangleLines(ind_button_x, ind_button_y, ind_button_h, ind_button_h, st->text_color);
	DrawTextEx(st->text_font, color_strings[st->which_fixed], (Vector2) {ind_button_x+18, ind_button_y+10}, 40., 2, st->text_color);
	// DrawTextEx(st->text_font, value, (Vector2) {grad_square_x, val_slider_y + 70}, 30., 1.5, st->text_color);
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

	DrawTextEx(st->text_font, value, (Vector2) {grad_square_x, val_slider_y + 70}, 30., 1.5, st->text_color);

	double now = get_os_time();
	if (st->outfile.path && now - st->outfile.last_write_time > WRITE_INTERVAL &&
		!colors_equal(cur_color, st->outfile.last_write_color)) {
		write_color_to_file(st, cur_color);
		st->outfile.last_write_color = cur_color;
		st->outfile.last_write_time = now;
	}
}

void assert_usage(bool p)
{
	myassert(p, "usage: cpick [-o file.txt:offset]\n");
}

int main(int argc, char *argv[])
{
	struct state *st = (struct state *) calloc(1, sizeof(struct state));
	st->screenWidth = 620;
	st->screenHeight = 680;
	st->which_fixed = 0;
	st->fixed_value = 0;
	st->x_value = 0;
	st->y_value = 0;
	st->text_color = WHITE;
	st->outfile.path = NULL;
	st->outfile.offset = 0;
	st->outfile.format = 0;
	st->outfile.last_write_time = get_os_time();
	st->outfile.last_write_color = (Color) { 0, 0, 0, 255 };
	st->debug = true;

	for (int i=1; i<argc; i++) {
		char *arg = argv[i];
		if (strcmp(arg, "-o")==0 || strcmp(arg, "--outfile")==0) {
			assert_usage(i+1<argc);
			char *param = argv[i+1];
			char *sep = strchr(param, ':');
			assert_usage(sep);
			int path_len = sep - param;
			st->outfile.path = malloc(path_len+1);
			memcpy(st->outfile.path, param, path_len);
			st->outfile.path[path_len] = '\0';
			errno = 0;
			st->outfile.offset = strtoull(sep+1, NULL, 10);
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

	// to load a font from a ttf file:
	// st->text_font = LoadFontEx("NotoSansMono.ttf", 120, NULL, 0);
	st->text_font = LoadFont_NotoSansMonoTtf();

	SetTargetFPS(60); // idk
	// Main game loop
	while (!WindowShouldClose())
	{
		st->screenWidth = GetScreenWidth();
		st->screenHeight = GetScreenHeight();
		// Draw
		BeginDrawing();
		draw_ui_and_respond_input(st);
		EndDrawing();
    }

	// ExportFontAsCode(st->text_font, "noto_sans_mono_ttf.h");
	// De-Initialization
	CloseWindow();        // Close window and OpenGL context
	return 0;
}
