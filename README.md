# CPick: a color picker

CPick is a simple yet snazzy color picker written in C.

![screenshot](cpick.png)

Usage
-----
The central square shows a 2d slice of the RGB color space, while the slider
adjusts the value of the third color. 

Click on the square indicator button to change which color the slider controls,
and click anywhere on the central square to select a color.

Building
--------
The only dependency is [raylib](https://www.raylib.com/).
If you are on Linux, run:

     $ make

and optionally

     $ sudo make install

If you are on Windows or Mac, you will need to edit `Makefile` to adjust
the install path.
