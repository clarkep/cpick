#!/bin/bash
gcc -g -o quickpick quickpick.c draw.c util.c glad.c $(pkg-config --libs --cflags freetype2) $(pkg-config --libs --cflags sdl2) -lGL -lm
