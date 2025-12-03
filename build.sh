#!/bin/bash
gcc -g -o quickpick quickpick.c shapes.c $(pkg-config --libs --cflags raylib) -lm
