libnativedraw
=============

libnativedraw is a wrapper around native drawing calls aiming to be as "pixel-exivalent" across platforms as the native rendering supports. The goal is that if you run the same commands on different platforms you will get results that look correct for that platform. Different platforms render shapes slightly differently--especially text--and these differences will be retained, so pixel-perfect is explicitly not a goal.

Currently supports:
- CoreGraphics (macOS)
- Direct2D (Windows)
- X11/Cairo (Linux/unix with X11 + Xrender)

TODO: PDF, load Image from memory, gradients, underlines, pixel-aligned multi-line text, pre-laid-out text, performance optimizations (especially for Direct2D).

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
