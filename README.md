# Cpick: a color picker

Cpick is a simple RGB and HSV color picker that can write output colors to files.

![screenshot](cpick.png)

Usage
-----
To launch cpick from a command line with an output file of test.txt, byte 7, use

     $ cpick test.txt@7

or without an output file,

     $ cpick

The central square shows a 2d slice of the RGB color space, and the slider
moves the slice in the third dimension.

Click on the buttons in the bottom left to rotate dimensions, and click anywhere on the central square to select a color. If using an output file, the selected color will be automatically written to the specified byte offset.

Building
--------
This project depends on [raylib](https://github.com/raysan5/raylib).

To build with cmake, run

     $ cmake -B build

followed by

     $ cmake --build build

This will fetch and build raylib if it is not found. On Windows, the executable is placed at `build\Debug\cpick.exe`
by default, and on macOS or Linux it is placed at `build\cpick`.

Alternatively, if you are on macOS or Linux and raylib is already installed, you can use the simple build script:

     $ ./build.sh
