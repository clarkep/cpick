#!/bin/bash
clang -g -o quickpick quickpick.c draw.c util.c glad.c -Iinclude $(pkg-config --libs --cflags freetype2) $(pkg-config --libs --cflags sdl2) -lGL -lm
