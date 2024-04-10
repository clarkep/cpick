PREFIX = /usr/local
BINDIR = ${PREFIX}/bin

LDFLAGS_CPICK = -lraylib

cpick: main.c
	gcc -o cpick main.c ${LDFLAGS_CPICK}

install: cpick
	cp -f cpick ${BINDIR}/
	chmod 755 ${BINDIR}/cpick
