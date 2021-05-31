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

Performance
-----------
`make timing` will create a timing test app. Results for different platforms are summarized below. Linux offer much greater precision on the numbers because both macOS and Windows sync to VBlank, so the drawing quantum is 1/60 sec on Windows and 1/120 sec on macOS.

Numbers represent objects/sec.
| | macOS | Linux | Windows (Direct2D) |
|-|-------|-------|--------------------|
|Rects (fill)|53000|33250|1000000|
|Rects (stroke)|32000|27125|800000|
|Rects (stroke+fill)|19000|14775|575000|
|Rounded rects (fill)|24000|27225|200000|
|Rounded rects (stroke)|19000|14300|125000|
|Rounded rects (stroke+fill)|10000|9000|65000|
|Complex bezier (fill)|9800|23200|120000|
|Complex bezier (stroke)|3250|18750|22000|
|Complex bezier (stroke+fill)|2300|10350|20000|
|Image blit|887|226350|1100000|
|Clip rect|47000|33000|35000|
|Clip bezier|9000|23000|30000|

The cost for using a clip rect or clipping region was the same as for drawing that item, except for Direct2D where clipping imposed a large penalty for rects. There was not a noticeable cost for switching fill colors every primitive. There was no benefit to reusing a complex bezier path; the drawing system apparently recalculates the bezier curves each time the path is drawn, regardles of the operating system. Direct2D was substantially faster with rounded rects as bezier paths.

macOS images appear to be slow because it is converting the bitmaps between color profiles. The timings were run using "Color LCD", which is the default profile. Performance increased substatially if the profile was set to the native internal profile.

Timings on macOS 10.14 were done with a 15-inch 2018 MacBook Pro (2.9 GHz Intel i9, Intel UHD 630 + Radeon Pro Vega 20). macOS appeared to use the Intel graphics card. Linux (Ubuntu 18.04) and Windows 10.0.19041) used an Intel NUC NUC8BEH (3.0 GHz Intel i3, Intel Iris+ 655).
