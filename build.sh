platform=$(uname)
if [ "$platform" == "Darwin" ]; then
	clang -o cpick $(pkg-config --libs --cflags raylib) main.c
fi
