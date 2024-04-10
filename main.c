/*******************************************************************************************
*  RGB color picker 
*
*  Copyright (c) 2024 Paul Clarke
*
********************************************************************************************/

#include <stdio.h>
#include <stdlib.h> // malloc
#include <sys/param.h> // MIN, MAX
#include "raylib.h"
#include "noto_sans_mono_ttf.h"

struct state {
	int screenWidth;
	int screenHeight;
	int which_fixed; // red(0), green(1) or blue(2)
	bool val_slider_dragging;
	// these two represent the same thing...
	int val_slider_offset; // offset in pixels
	int val_slider_value; // 0-255
	// last click in the square:
	int pointer_square_x;
	int pointer_square_y;
	Color text_color;
	Font text_font;
};

char *color_strings[3] = { "R", "G", "B" };

Color current_color(struct state *st) 
{
	int v1 = st->val_slider_value;
	int v2 = st->pointer_square_x / 2; 
	int v3 = st->pointer_square_y / 2;
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

void drawUIandRespondInput(struct state *st) {
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
	draw_gradient_n(grad_square_x, grad_square_y, 2, st->which_fixed, st->val_slider_value);
	if (IsMouseButtonDown(0)) {
		Vector2 pos = GetMousePosition();
		if (CheckCollisionPointRec(pos, (Rectangle) { grad_square_x, grad_square_y, 512, 512 })) {
			st->pointer_square_x = pos.x - grad_square_x;
			st->pointer_square_y = pos.y - grad_square_y;
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
		}
	}

	// fixed value slider 
	int val_slider_x = ind_button_x + ind_button_h + 20;
	int val_slider_y = ind_button_y;
	int val_slider_w = grad_square_x_end - val_slider_x;
	int val_slider_h = 60;
	DrawRectangle(val_slider_x, val_slider_y+26, val_slider_w, 6, st->text_color);
	int wf = st->which_fixed;
	Vector2 circle_center = { val_slider_x + st->val_slider_offset, val_slider_y+30 };
	DrawCircleV(circle_center, 15,
			   (Color) { wf == 0 ? 218 : 0, wf == 1 ? 216 : 0,  wf == 2 ? 216 : 0, 255 });
	if (IsMouseButtonDown(0)) {
		Vector2 pos = GetMousePosition();
		if (CheckCollisionPointCircle(pos, circle_center, 30) || st->val_slider_dragging) {
			st->val_slider_dragging = true;
			st->val_slider_offset = MIN(val_slider_w, MAX(0, pos.x - val_slider_x));
		} else if (CheckCollisionPointRec(pos, (Rectangle) { val_slider_x, val_slider_y, val_slider_w, val_slider_w } )) {
			st->val_slider_offset = pos.x - val_slider_x;
		}
		// keep in line with val_slider_offset
		st->val_slider_value = (int) ((float) 255*st->val_slider_offset / val_slider_w);
	} else {
		st->val_slider_dragging = false;
	}

	// color read out
	char value[3];
	sprintf(value, "r: %-3d g: %-3d b: %-3d", cur_color.r, cur_color.g, cur_color.b); 
	DrawTextEx(st->text_font, value, (Vector2) {grad_square_x, val_slider_y + 70}, 30., 1.5, st->text_color);
}

int main(void)
{
    // Initialization
	struct state *st = (struct state *) calloc(1, sizeof(struct state));
    st->screenWidth = 620;
    st->screenHeight = 680;
	st->which_fixed = 0;
	st->val_slider_offset = 0;
	st->val_slider_value = 0;
	st->pointer_square_x = 0;
	st->pointer_square_y = 0;
	st->text_color = WHITE;

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
			drawUIandRespondInput(st);
		EndDrawing();
    }

	// ExportFontAsCode(st->text_font, "noto_sans_mono_ttf.h");
    // De-Initialization
    CloseWindow();        // Close window and OpenGL context
    return 0;
}
