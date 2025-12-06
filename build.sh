#!/bin/bash
clang -g -o quickpick quickpick.c $(pkg-config --libs --cflags sdl2) -lSDL2_ttf -lGL -lGLEW -lm
