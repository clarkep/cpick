#!/bin/bash
gcc -g -o cpick cpick.c $(pkg-config --libs --cflags raylib) -lm
