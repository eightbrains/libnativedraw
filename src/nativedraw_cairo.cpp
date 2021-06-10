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

#if defined(__unix__) && !defined(__APPLE__)
#include "nativedraw.h"
#include "nativedraw_private.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo-xlib-xrender.h>

#include <iostream>

namespace ND_NAMESPACE {

namespace {

void printError(const std::string& message)
{
    std::cerr << "[ERROR] " << message << std::endl;
}

void setCairoSourceColor(cairo_t *gc, const Color& color)
{
    cairo_set_source_rgba(gc, double(color.red()), double(color.green()),
                          double(color.blue()), double(color.alpha()));
}

} // namespace

//------------------------------- BezierPath ----------------------------------
class CairoPath : public BezierPath
{
public:
    CairoPath() : BezierPath() {}
    ~CairoPath()
    {
        clearNative();
    }

    void clearNative() override
    {
        for (auto& dpi2path : mPaths) {
            free(dpi2path.second.data);
            dpi2path.second.data = nullptr; // just to be safe
        }
        mPaths.clear();
    }

    void* nativePathForDPI(float dpi, bool isFilled) override
    {
        float key = dpi;
        auto it = mPaths.find(key);
        if (it == mPaths.end()) {
            // Count the number of data needed
            int num_data = 0;
            for (auto& cmd : mImpl->commands) {
                switch (cmd.cmd) {
                    case BezierPath::Impl::Command::kClose:
                        num_data += 1;
                        break;
                    case BezierPath::Impl::Command::kMoveTo:
                    case BezierPath::Impl::Command::kLineTo:
                        num_data += 2;
                        break;
                    case BezierPath::Impl::Command::kQuadraticTo:
                    case BezierPath::Impl::Command::kCubicTo:
                        num_data += 4;
                        break;
                }
            }

            mPaths[key] = cairo_path_t();
            it = mPaths.find(key);
            cairo_path_t &path = it->second;
            path.status = CAIRO_STATUS_SUCCESS;
            path.data = (cairo_path_data_t*)malloc(num_data * sizeof(cairo_path_data_t));
            path.num_data = num_data;

            int dataIdx = 0;
            Point startPoint;
            Point lastPoint;
            for (auto& cmd : mImpl->commands) {
                switch (cmd.cmd) {
                    case BezierPath::Impl::Command::kMoveTo:
                        path.data[dataIdx].header.type = CAIRO_PATH_MOVE_TO;
                        path.data[dataIdx].header.length = 2;
                        dataIdx += 1;
                        path.data[dataIdx].point.x = cmd.p1.x.toPixels(dpi);
                        path.data[dataIdx].point.y = cmd.p1.y.toPixels(dpi);
                        startPoint = cmd.p1;
                        lastPoint = cmd.p1;
                        dataIdx += 1;
                        break;
                    case BezierPath::Impl::Command::kLineTo:
                        path.data[dataIdx].header.type = CAIRO_PATH_LINE_TO;
                        path.data[dataIdx].header.length = 2;
                        dataIdx += 1;
                        path.data[dataIdx].point.x = cmd.p1.x.toPixels(dpi);
                        path.data[dataIdx].point.y = cmd.p1.y.toPixels(dpi);
                        dataIdx += 1;
                        lastPoint = cmd.p1;
                        break;
                    case BezierPath::Impl::Command::kQuadraticTo: {
                        // Convert quadratic to cubic:
                        // https://fontforge.org/docs/techref/bezier.html#converting-truetype-to-postscript
                        Point end = cmd.p2;
                        Point cp1 = lastPoint + (2.0f / 3.0f) * (cmd.p1 - lastPoint);
                        Point cp2 = end + (2.0f / 3.0f) * (cmd.p1 - end);
                        path.data[dataIdx].header.type = CAIRO_PATH_CURVE_TO;
                        path.data[dataIdx].header.length = 4;
                        dataIdx += 1;
                        path.data[dataIdx].point.x = cp1.x.toPixels(dpi);
                        path.data[dataIdx].point.y = cp1.y.toPixels(dpi);
                        dataIdx += 1;
                        path.data[dataIdx].point.x = cp2.x.toPixels(dpi);
                        path.data[dataIdx].point.y = cp2.y.toPixels(dpi);
                        dataIdx += 1;
                        path.data[dataIdx].point.x = end.x.toPixels(dpi);
                        path.data[dataIdx].point.y = end.y.toPixels(dpi);
                        dataIdx += 1;
                        lastPoint = end;
                        break;
                    }
                    case BezierPath::Impl::Command::kCubicTo:
                        path.data[dataIdx].header.type = CAIRO_PATH_CURVE_TO;
                        path.data[dataIdx].header.length = 4;
                        dataIdx += 1;
                        path.data[dataIdx].point.x = cmd.p1.x.toPixels(dpi);
                        path.data[dataIdx].point.y = cmd.p1.y.toPixels(dpi);
                        dataIdx += 1;
                        path.data[dataIdx].point.x = cmd.p2.x.toPixels(dpi);
                        path.data[dataIdx].point.y = cmd.p2.y.toPixels(dpi);
                        dataIdx += 1;
                        path.data[dataIdx].point.x = cmd.p3.x.toPixels(dpi);
                        path.data[dataIdx].point.y = cmd.p3.y.toPixels(dpi);
                        dataIdx += 1;
                        lastPoint = cmd.p3;
                        break;
                    case BezierPath::Impl::Command::kClose:
                        path.data[dataIdx].header.type = CAIRO_PATH_CLOSE_PATH;
                        path.data[dataIdx].header.length = 1;
                        dataIdx += 1;
                        lastPoint = startPoint;
                        break;
                }
            }
            assert(dataIdx == num_data);
        }
        return &it->second;
    }

private:
    std::unordered_map<float, cairo_path_t> mPaths;
};

//---------------------------------- Fonts ------------------------------------
namespace {

class CairoFont
{
public:
    bool metricsInitialized = false;
    Font::Metrics metrics;
};

static CairoFont* createFont(const Font& font, float dpi)
{
    return new CairoFont();
}

static void destroyFont(CairoFont *fontResource)
{
    delete fontResource;
}

static ResourceManager<Font, CairoFont*> gFontMgr(createFont, destroyFont);

} // namespace
//---------------------------------- Image ------------------------------------
class CairoImage : public Image
{
public:
    CairoImage(cairo_surface_t *nativeHandle, int width, int height, float dpi)
        : Image(nativeHandle, width, height, dpi)
    {}

    virtual ~CairoImage()
    {
        cairo_surface_destroy((cairo_surface_t*)nativeHandle());
        mNativeHandle = nullptr;
    }
};

//--------------------------- CairoDrawContext --------------------------------
class CairoDrawContext : public DrawContext
{
protected:
    enum class DrawingState { kNotDrawing = 0, kDrawing };

    mutable DrawingState mDrawingState;

public:
    CairoDrawContext(void *nativeDC, int width, int height, float dpi)
        : DrawContext(nativeDC, width, height, dpi)
    {
        mDrawingState = DrawingState::kNotDrawing;
        if (nativeDC) {
            setNativeDC((cairo_t*)nativeDC);
        }
    }

protected:
    void setNativeDC(cairo_t *dc)
    {
        mStateStack.clear();
        mStateStack.push_back(State());

        mNativeDC = dc;
        setInitialState();
    }

public:
    std::shared_ptr<BezierPath> createBezierPath() const override
    {
        return std::make_shared<CairoPath>();
    }

    void beginDraw() override
    {
        mDrawingState = DrawingState::kDrawing;
    }

    void endDraw() override
    {
        mDrawingState = DrawingState::kNotDrawing;
    }

    void save() override
    {
        cairo_save(cairoContext());
        mStateStack.push_back(mStateStack.back());  // copy current state
    }

    void restore() override
    {
        cairo_restore(cairoContext());
        mStateStack.pop_back();
    }

    void translate(const PicaPt& dx, const PicaPt& dy) override
    {
        cairo_translate(cairoContext(), dx.toPixels(mDPI), dy.toPixels(mDPI));
    }

    void rotate(float degrees) override
    {
        // Our coordinate system is y-flipped with respect to Cairo's
        // mathematical one, which make +angle rotate clockwise. We still
        // want +angle to be counterclockwise so that the angle works like
        // people expect it, so we need to negate it.
        cairo_rotate(cairoContext(), -degrees * 3.14159265358979323846f / 180.0f);
    }

    void scale(float sx, float sy) override
    {
        cairo_scale(cairoContext(), sx, sy);
    }

    void calcContextPixel(const Point& point, float *x, float *y) override
    {
        double xx = double(point.x.toPixels(mDPI));
        double yy = double(point.y.toPixels(mDPI));
        cairo_user_to_device(cairoContext(), &xx, &yy);
        if (x) {
            *x = float(xx);
        }
        if (y) {
            *y = float(yy);
        }
    }

    void setFillColor(const Color& color) override
    {
        mStateStack.back().fillColor = color;
    }

    void setStrokeColor(const Color& color) override
    {
        mStateStack.back().strokeColor = color;
    }

    void setStrokeWidth(const PicaPt& w) override
    {
        cairo_set_line_width(cairoContext(), w.toPixels(mDPI));
    }

    void setStrokeEndCap(EndCapStyle cap) override
    {
        switch(cap) {
            case kEndCapButt:
                cairo_set_line_cap(cairoContext(), CAIRO_LINE_CAP_BUTT);
                break;
            case kEndCapRound:
                cairo_set_line_cap(cairoContext(), CAIRO_LINE_CAP_ROUND);
                break;
            case kEndCapSquare:
                cairo_set_line_cap(cairoContext(), CAIRO_LINE_CAP_SQUARE);
                break;
        }
    }

    void setStrokeJoinStyle(JoinStyle join) override
    {
        switch(join) {
            case kJoinMiter:
                cairo_set_line_join(cairoContext(), CAIRO_LINE_JOIN_MITER);
                break;
            case kJoinRound:
                cairo_set_line_join(cairoContext(), CAIRO_LINE_JOIN_ROUND);
                break;
            case kJoinBevel:
                cairo_set_line_join(cairoContext(), CAIRO_LINE_JOIN_BEVEL);
                break;
        }
    }

    void setStrokeDashes(const std::vector<PicaPt> lengths, const PicaPt& offset) override
    {
        std::vector<double> dashes;
        dashes.reserve(lengths.size());
        for (auto length : lengths) {
            dashes.push_back(length.toPixels(mDPI));
        }
        cairo_set_dash(cairoContext(), dashes.data(), int(dashes.size()),
                       -offset.toPixels(mDPI));
    }

    void fill(const Color& color) override
    {
        auto *gc = cairoContext();
        setCairoSourceColor(gc, color);
        cairo_rectangle(gc, 0.0, 0.0, double(mWidth), double(mHeight));
        cairo_fill(cairoContext());
    }

    void clearRect(const Rect& rect) override
    {
        auto *gc = cairoContext();
        cairo_set_source_rgba(gc, 0.0, 0.0, 0.0, 0.0);
        auto old_op = cairo_get_operator(gc);
        cairo_set_operator(gc, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(gc, rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                        rect.width.toPixels(mDPI), rect.height.toPixels(mDPI));
        cairo_fill(gc);
        cairo_set_operator(gc, old_op);
    }

    void drawLines(const std::vector<Point>& lines) override
    {
        auto path = createBezierPath();
        path->moveTo(lines[0]);
        for (size_t i = 1;  i < lines.size();  ++i) {
            path->lineTo(lines[i]);
        }
        drawPath(path, kPaintStroke);
    }

    void drawRect(const Rect& rect, PaintMode mode) override
    {
        auto *gc = cairoContext();
        cairo_rectangle(gc, rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                        rect.width.toPixels(mDPI), rect.height.toPixels(mDPI));
        drawCurrentPath(mode);
    }

    // drawRoundedRect() is defined in nativedraw.cpp

    void drawEllipse(const Rect& rect, PaintMode mode) override
    {
        auto path = createBezierPath();
        path->addEllipse(rect);
        drawPath(path, mode);
    }

    void drawPath(std::shared_ptr<BezierPath> path, PaintMode mode) override
    {
        const bool ignored = false;
        auto *gc = cairoContext();
        cairo_append_path(gc, (cairo_path_t*)path->nativePathForDPI(mDPI, ignored));
        drawCurrentPath(mode);
    }

    void drawText(const char *textUTF8, const Point& topLeft, const Font& font, PaintMode mode) override
    {
        auto *gc = cairoContext();
        setFont(font);
        cairo_move_to(gc,
                      topLeft.x.toPixels(mDPI),
                      std::floor((topLeft.y + fontMetrics(font).ascent).toPixels(mDPI)));
        cairo_text_path(gc, textUTF8);
        drawCurrentPath(mode);
    }

    void drawImage(std::shared_ptr<Image> image, const Rect& destRect) override
    {
        auto *gc = cairoContext();
        save();
        translate(destRect.x, destRect.y);
        float destWidthPx = destRect.width.toPixels(mDPI);
        float destHeightPx = destRect.height.toPixels(mDPI);
        float sx = destWidthPx / image->width();
        float sy = destHeightPx / image->height();
        scale(sx, sy);
        cairo_set_source_surface(gc, (cairo_surface_t*)image->nativeHandle()
                                 ,0.0, 0.0);
        cairo_paint(gc);
        restore();
    }

    void clipToRect(const Rect& rect) override
    {
        auto *gc = cairoContext();
        cairo_rectangle(gc, rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                        rect.width.toPixels(mDPI), rect.height.toPixels(mDPI));
        cairo_clip(gc);
    }

    void clipToPath(std::shared_ptr<BezierPath> path) override
    {
        const bool ignored = false;
        auto *gc = cairoContext();
        cairo_append_path(gc, (cairo_path_t*)path->nativePathForDPI(mDPI, ignored));
        cairo_clip(gc);
    }

    Font::Metrics fontMetrics(const Font& font) const override
    {
        // We could get the 72 dpi version of the font, which is exactly in
        // PicaPt, but we get the actual size font so that we can attempt
        // get more accurate values due to hinting (or lack thereof at
        // higher resolutions).
        CairoFont* fontInfo = gFontMgr.get(font, mDPI);
        if (!fontInfo->metricsInitialized) {
            auto *gc = cairoContext();
            setFont(font);

            auto *face = cairo_get_font_face(gc);
            std::string familyName = cairo_toy_font_face_get_family(face);
            // TODO: the toy font API apparently always succeeds
            if (familyName == font.family()) {
                cairo_font_extents_t extents;
                cairo_font_extents(gc, &extents);
                fontInfo->metrics.ascent = PicaPt::fromPixels(extents.ascent, mDPI);
                fontInfo->metrics.descent = PicaPt::fromPixels(extents.descent, mDPI);
                auto leadingPx = extents.height - extents.ascent - extents.descent;
                fontInfo->metrics.leading = PicaPt::fromPixels(leadingPx, mDPI);

                // Cairo doesn't have cap-height or x-height, so we need to
                // figure those out ourselves.
                cairo_text_extents_t tExt;
                // cap-height is for flat letters (H,I not A,O which may extend
                // above)
                cairo_text_extents(gc, "H", &tExt);
                fontInfo->metrics.capHeight = PicaPt::fromPixels(tExt.height, mDPI);
                // x-height is obviously height of "x"
                cairo_text_extents(gc, "x", &tExt);
                fontInfo->metrics.xHeight = PicaPt::fromPixels(tExt.height, mDPI);
            } else {
                fontInfo->metrics.ascent = PicaPt(0);
                fontInfo->metrics.descent = PicaPt(0);
                fontInfo->metrics.leading = PicaPt(0);
                fontInfo->metrics.capHeight = PicaPt(0);
                fontInfo->metrics.xHeight = PicaPt(0);
            }

            fontInfo->metricsInitialized = true;
        }
        return fontInfo->metrics;
    }

    Font::TextMetrics textMetrics(const char *textUTF8, const Font& font,
                                  PaintMode mode) const
    {
        cairo_text_extents_t extents;
        auto *gc = cairoContext();
        setFont(font);
        cairo_text_extents(gc, textUTF8, &extents);

        Font::TextMetrics tm;
        tm.width = PicaPt::fromPixels(float(extents.width), dpi());
        tm.height = PicaPt::fromPixels(float(extents.height), dpi());
        tm.advanceX = PicaPt::fromPixels(float(extents.x_advance), dpi());
        tm.advanceY = PicaPt::fromPixels(float(extents.y_advance), dpi());
        return tm;
    }

    Color pixelAt(int x, int y) override
    {
        assert(false);  // need a bitmap context
        return Color::kPurple;
    }

    std::shared_ptr<Image> copyToImage() override
    {
        assert(false);  // not implemented
        return nullptr;
    }

protected:
    inline cairo_t* cairoContext() const { return (cairo_t*)mNativeDC; }

    void drawCurrentPath(PaintMode mode)
    {
        auto *gc = cairoContext();
        auto &state = mStateStack.back();
        switch(mode) {
            case kPaintStroke:
                setCairoSourceColor(gc, state.strokeColor);
                cairo_stroke(gc);
                break;
            case kPaintFill:
                setCairoSourceColor(gc, state.fillColor);
                cairo_fill(gc);
                break;
            case kPaintStrokeAndFill:
                setCairoSourceColor(gc, state.fillColor);
                cairo_fill_preserve(gc);
                setCairoSourceColor(gc, state.strokeColor);
                cairo_stroke(gc);
                break;
        }
    }

    void setFont(const Font& font) const
    {
        auto *gc = cairoContext();
        cairo_select_font_face(gc, font.family().c_str(),
                               (font.style() & kStyleItalic) ? CAIRO_FONT_SLANT_ITALIC
                                                             : CAIRO_FONT_SLANT_NORMAL,
                               (font.style() & kStyleBold) ? CAIRO_FONT_WEIGHT_BOLD
                                                           : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(gc, font.pointSize().toPixels(mDPI));
    }

private:
    struct State
    {
        Color fillColor;
        Color strokeColor;
    };
    std::vector<State> mStateStack;
};
//-----------------------------------------------------------------------------
// This is a CPU-bound bitmap
class CairoBitmap : public CairoDrawContext
{
private:
    cairo_surface_t *mSurface;
    cairo_t *mDC;

public:
    CairoBitmap(BitmapType type, int width, int height, float dpi = 72.0f)
        : CairoDrawContext(nullptr, width, height, dpi)
    {
        switch (type) {
            case kBitmapRGBA:
                mSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
                break;
            case kBitmapRGB:
                mSurface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
                break;
            case kBitmapGreyscale:
                mSurface = cairo_image_surface_create(CAIRO_FORMAT_A8, width, height);
                break;
            case kBitmapAlpha:
                mSurface = cairo_image_surface_create(CAIRO_FORMAT_A8, width, height);
                break;
        }
        mDC = cairo_create(mSurface);
        setNativeDC(mDC);
    }

    ~CairoBitmap()
    {
        cairo_destroy(mDC);
        cairo_surface_destroy(mSurface);
    }

    Color pixelAt(int x, int y) override
    {
        if (mDrawingState == DrawingState::kDrawing) {
            printError("DrawContext::pixelAt() cannot be called between beginDraw() and endDraw()");
            endDraw();  // but make it work anyway...
        }
        cairo_surface_flush(mSurface);
        unsigned char *data = cairo_image_surface_get_data(mSurface);
        unsigned char *rgba = data + y * cairo_image_surface_get_stride(mSurface);
        switch(cairo_image_surface_get_format(mSurface)) {
            case CAIRO_FORMAT_ARGB32: {
                rgba = rgba + 4 * x;  // turns out to be BGRA
                float a = float(rgba[3]) / 255.0f;
                if (a < 0.001) {
                    return Color(0.0f, 0.0f, 0.0f, 0.0f);
                }
                float r = float(rgba[2]) / 255.0f;
                float g = float(rgba[1]) / 255.0f;
                float b = float(rgba[0]) / 255.0f;
                return Color(r/a, g/a, b/a, a);
            }
            case CAIRO_FORMAT_RGB24:
                rgba = rgba + 4 * x;  // same as ARGB32 above, but A is unused
                return Color(int(rgba[2]), int(rgba[1]), int(rgba[0]));
            case CAIRO_FORMAT_A8:
                rgba = rgba + x;
                return Color(int(rgba[0]), int(rgba[0]), int(rgba[0]));
            default:
                std::cerr << "[ERROR] CairoBitmap::pixelAt(): unsupported format" << std::endl;
                return Color::kPurple;
        }
    }

    std::shared_ptr<Image> copyToImage() override
    {
        return std::make_shared<CairoImage>(cairo_surface_reference(mSurface),
                                            width(), height(), dpi());
    }

    std::shared_ptr<DrawContext> createBitmap(BitmapType type,
                                              int width, int height,
                                              float dpi /*= 72.0f*/) override
    {
        return std::make_shared<CairoBitmap>(type, width, height, dpi);
    }
};


//-----------------------------------------------------------------------------
class ShareableX11Pixmap
{
public:
    Display* mDisplay;
    Pixmap mPixmap;

    explicit ShareableX11Pixmap(Display *d, Pixmap p) : mDisplay(d), mPixmap(p) {}
    ShareableX11Pixmap(const ShareableX11Pixmap&) = delete;
    ~ShareableX11Pixmap()
    {
        XFreePixmap(mDisplay, mPixmap);
    }

    ShareableX11Pixmap& operator=(const ShareableX11Pixmap&) = delete;

    Pixmap pixmap() const { return mPixmap; }
};

class CairoX11DrawContext : public CairoDrawContext
{
protected:
    // These are X11's pointers, we do not own them
    Display *mDisplay;
    Drawable mDrawable;  // NOT a pointer: Drawable typedef'd to long unsigned int

    // We own everything below here
    cairo_surface_t *mSurface = nullptr;
    cairo_t *mDC = nullptr;
    
public:
    // For derived classes. The object is incomplete until the derived class
    // calls finishConstructing().
    CairoX11DrawContext(Display* display, int width, int height, float dpi)
        : CairoDrawContext(nullptr, width, height, dpi)
        , mDisplay((Display*)display)
        , mDrawable(0)
    {
    }

    CairoX11DrawContext(void* display, const void* window,
                        int width, int height, float dpi)
        : CairoDrawContext(nullptr, width, height, dpi)
        , mDisplay((Display*)display)
        , mDrawable(*(Drawable*)window)  // Window is a Drawable
    {
        XWindowAttributes attrs;
        XGetWindowAttributes(mDisplay, (Window)mDrawable, &attrs);
        mSurface = cairo_xlib_surface_create(mDisplay, mDrawable, attrs.visual,
                                             width, height);
        finishConstructing(mDrawable, mSurface);
    }

    ~CairoX11DrawContext()
    {
        cairo_destroy(mDC);
        cairo_surface_destroy(mSurface);
    }

    std::shared_ptr<DrawContext> createBitmap(BitmapType type,
                                              int width, int height,
                                              float dpi /*= 72.0f*/) override
    {
        return DrawContext::createCairoX11Bitmap(mDisplay, type, width, height,
                                                 dpi);
    }

protected:
    void finishConstructing(Drawable drawable, 
                            cairo_surface_t* surface /* takes ownership */)
    {
        mDrawable = drawable;
        mSurface = surface;
        mDC = cairo_create(mSurface);
        setNativeDC(mDC);
    }
};

class CairoX11Image : public CairoImage
{
private:
    std::shared_ptr<ShareableX11Pixmap> mPixmap;

public:
    CairoX11Image(std::shared_ptr<ShareableX11Pixmap> pixmap, cairo_surface_t* surface,
                  int width, int height, float dpi)
        : CairoImage(cairo_surface_reference(surface),  // increment this ref
                     width, height, dpi)
        , mPixmap(pixmap)
    {}
};

class CairoX11Bitmap : public CairoX11DrawContext
{
    using Super = CairoX11DrawContext;
private:
    BitmapType mType;
    std::shared_ptr<ShareableX11Pixmap> mPixmap;
    CairoBitmap *mReadable = nullptr;

public:
    CairoX11Bitmap(Display *display, BitmapType type, int width, int height,
                   float dpi = 72.0f)
        : CairoX11DrawContext(nullptr, width, height, dpi)
    {
        mType = type;
        mDisplay = display;

        auto drawable = XRootWindow(display, XDefaultScreen(display));
        Screen *screen = DefaultScreenOfDisplay(display);
        XRenderPictFormat* pictFormat;
        Pixmap pixmap;
        switch (type) {
            case kBitmapRGBA:
                pictFormat = XRenderFindStandardFormat(display,
                                                       PictStandardARGB32);
                pixmap = XCreatePixmap(display, drawable, width, height, 32);
                mSurface = cairo_xlib_surface_create_with_xrender_format(
                            display, pixmap, screen, pictFormat, width, height);
                break;
            case kBitmapRGB:
                pictFormat = XRenderFindStandardFormat(display,
                                                       PictStandardRGB24);
                pixmap = XCreatePixmap(display, drawable, width, height, 24);
                mSurface = cairo_xlib_surface_create_with_xrender_format(
                            display, pixmap, screen, pictFormat, width, height);
                break;
            case kBitmapGreyscale:
                pictFormat = XRenderFindStandardFormat(display, PictStandardA8);
                pixmap = XCreatePixmap(display, drawable, width, height, 8);
                mSurface = cairo_xlib_surface_create_with_xrender_format(
                            display, pixmap, screen, pictFormat, width, height);
                break;
            case kBitmapAlpha:
                pictFormat = XRenderFindStandardFormat(display, PictStandardA8);
                pixmap = XCreatePixmap(display, drawable, width, height, 8);
                mSurface = cairo_xlib_surface_create_with_xrender_format(
                            display, pixmap, screen, pictFormat, width, height);
                break;
        }
        mPixmap = std::make_shared<ShareableX11Pixmap>(mDisplay, pixmap);
        finishConstructing(mPixmap->pixmap(), mSurface);
    }

    ~CairoX11Bitmap()
    {
        mPixmap.reset();  // happens automatically, is here for clarity
        delete mReadable;
    }

    void beginDraw() override
    {
        Super::beginDraw();
        delete mReadable;
        mReadable = nullptr;
    }

    Color pixelAt(int x, int y) override
    {
        if (!mReadable) {
            mReadable = new CairoBitmap(mType, width(), height(), dpi());
            auto *readableGC = (cairo_t *)mReadable->nativeDC();
            cairo_set_source_surface(readableGC, mSurface, 0.0, 0.0);
            cairo_paint(readableGC);
        }
        return mReadable->pixelAt(x, y);
    }

    std::shared_ptr<Image> copyToImage() override
    {
        return std::make_shared<CairoX11Image>(mPixmap, mSurface, width(), height(), dpi());
    }
};

//----------------------------- DrawContext -----------------------------------
//std::shared_ptr<DrawContext> DrawContext::fromCairo(void* cairo_t_, int width, int height, float dpi)
//{
//    return std::make_shared<CairoDrawContext>(cairo_t_, width, height, dpi);
//}

std::shared_ptr<DrawContext> DrawContext::fromX11(
            void* display, const void* window, int width, int height, float dpi)
{
    return std::make_shared<CairoX11DrawContext>(display, window, width, height,
                                                 dpi);
}

std::shared_ptr<DrawContext> DrawContext::createCairoX11Bitmap(
            void *display, BitmapType type, int width, int height,
            float dpi /*= 72.0f*/)
{
    return std::make_shared<CairoX11Bitmap>((Display*)display, type,
                                            width, height, dpi);
}

} // namespace ND_NAMESPACE

#endif // defined(__unix__) && !defined(__APPLE__)
