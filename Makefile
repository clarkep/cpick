PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

cpick: main.c
	gcc -o cpick main.c -lraylib

install:
	
