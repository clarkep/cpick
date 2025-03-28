# Cpick: a color picker

Cpick is a simple RGB color picker.

![screenshot](cpick.png)

Usage
-----
The central square shows a 2d slice of the RGB color space, and the slider
moves the slice in the third dimension. 

Click on the bottom left square to rotate dimensions, and click anywhere on 
the central square to select a color.

Building
--------
For now, this program is only distributed as source code. To use it, clone this
repository and follow the build instructions.

The only dependency is [raylib](https://www.raylib.com/). If you are on Linux,
run:

     $ make

and optionally

     $ sudo make install

If you are on Windows or Mac, you will need to edit `Makefile` to adjust
the install path.
