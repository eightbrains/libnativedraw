libnativedraw
=============

libnativedraw is a wrapper around native drawing calls aiming to be as pixel-perfect across platforms as the native rendering supports. Currently supports CoreGraphics and Direct2D.

TODO: X11, PDF, load Image from memory, gradients, underlines, pixel-aligned multi-line text, pre-laid-out text, performance optimizations (especially for Direct2D).

Building
--------

libnativedraw uses CMake.

**Linux and macOS**

```
mkdir build
cd build
cmake ..
make -j
```

**Windows**

Run this in a Visual Studio terminal.

```
mkdir build
cd build
cmake ..
cmake --build .
```
