//-----------------------------------------------------------------------------
// Copyright 2021 Eight Brains Studios, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#include <X11/Xlib.h>

#include <string.h>  // for memset()
#include <memory>

#include "../src/nativedraw.h"
#include "timings.h"

namespace {
    static const int kWidth = 1024;
    static const int kHeight = 768;
}

bool OnDraw(Display *display, Window window, Timings& timings,
            ND_NAMESPACE::DrawContext& dc);

int main(int argc, const char *argv[])
{
    Display* display = XOpenDisplay(NULL);
    Window window = XCreateSimpleWindow(display, XDefaultRootWindow(display),
                                        100, 100, kWidth, kHeight, 1, 0, 0);
    XMapWindow(display, window);
    XSelectInput(display, window, ExposureMask);

    auto timings = std::make_shared<Timings>();
    auto dc = ND_NAMESPACE::DrawContext::fromX11(display, &window,
                                                 kWidth, kHeight, 72.0f);

    bool done = false;
    XEvent event;
    while (!done) {
        XNextEvent(display, &event);

        switch (event.type) {
            case Expose:  // GraphicsExpose only happens for XCopyArea/XCopyPlane
                done = OnDraw(event.xany.display, event.xany.window, *timings, *dc);
                break;
            default:
                break;
        }
    }

    dc.reset();
    timings.reset();

    XUnmapWindow(display, window);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

bool OnDraw(Display *display, Window window, Timings& timings,
            ND_NAMESPACE::DrawContext& dc)
{
    if (timings.runNext(&dc) == Timings::CONTINUE) {
        XEvent e;
        memset(&e, 0, sizeof(e));
        e.type = Expose;
        e.xexpose.window = window;
        XSendEvent(display, window, False, ExposureMask, &e);
        return false;  // not done
    } else {
        return true;  // done
    }
}
