#include <X11/Xlib.h>

#include <string.h>  // for memset()
#include <memory>

#include "../src/nativedraw.h"
#include "timings.h"

namespace {
    static const int kWidth = 1024;
    static const int kHeight = 768;
}

bool OnDraw(Display *display, Window window, Timings& timings, eb::DrawContext& dc);

int main(int argc, const char *argv[])
{
    Display* display = XOpenDisplay(NULL);
    Window window = XCreateSimpleWindow(display, XDefaultRootWindow(display),
                                        100, 100, kWidth, kHeight, 1, 0, 0);
    XMapWindow(display, window);
    XSelectInput(display, window, ExposureMask);

    auto timings = std::make_shared<Timings>();
    auto dc = eb::DrawContext::fromX11(display, &window, kWidth, kHeight, 72.0f);

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

bool OnDraw(Display *display, Window window, Timings& timings, eb::DrawContext& dc)
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
