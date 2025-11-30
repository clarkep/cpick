#!/bin/bash
gcc -g -o cpick cpick.c shapes.c $(pkg-config --libs --cflags raylib) -lm
