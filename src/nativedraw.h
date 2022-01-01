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

#ifndef _NATIVE_DRAW_H
#define _NATIVE_DRAW_H

#ifndef ND_NAMESPACE
#define ND_NAMESPACE eb
#endif // ND_NAMESPACE

#include <assert.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace ND_NAMESPACE {

// This is a typographical "point", which is a unit of measurement equal to
// 1/72 inch. It can usually be used as "pixel" (even though on-screen it
// 1 point may be multiple pixels), and in 72 dpi, a PicaPt and a pixel are
// the same measurement. Note that that one "pica" is actually
// 1/12 of an inch, but since "point" usually means an (x, y) pair in
// computer graphics, it's hard to come up with a meaningful name.
struct PicaPt
{
    static const PicaPt kZero;

    PicaPt() : pt(0.0f) {}
    explicit PicaPt(float pt_) : pt(pt_) {}

    static PicaPt fromPixels(float pixels, float dpi) {
        return PicaPt(pixels * 72.0f / dpi);
    }

    float asFloat() const { return pt; }
    float toPixels(float dpi) const { return pt * dpi / 72.0f; }

    PicaPt operator-() const { return PicaPt(-pt); }
    PicaPt operator+(const PicaPt& v) const { return PicaPt(pt + v.pt); }
    PicaPt operator+(float v) const { return PicaPt(pt + v); }
    PicaPt operator-(const PicaPt& v) const { return PicaPt(pt - v.pt); }
    PicaPt operator-(float v) const { return PicaPt(pt - v); }
    PicaPt operator*(const PicaPt& v) const { return PicaPt(pt * v.pt); }
    PicaPt operator*(float v) const { return PicaPt(pt * v); }
    float operator/(const PicaPt& v) const { return pt / v.pt; } // length/length is unitless
    PicaPt operator/(float v) const { return PicaPt(pt / v); }
    PicaPt& operator+=(const PicaPt& v) { pt += v.pt; return *this; }
    PicaPt& operator+=(float v) { pt += v; return *this; }
    PicaPt& operator-=(const PicaPt& v) { pt -= v.pt; return *this; }
    PicaPt& operator-=(float v) { pt -= v; return *this; }
    PicaPt& operator*=(const PicaPt& v) { pt *= v.pt; return *this; }
    PicaPt& operator*=(float v) { pt *= v; return *this; }
    PicaPt& operator/=(float v) { pt /= v; return *this; }

    bool operator==(const PicaPt& rhs) const { return (pt == rhs.pt); }
    bool operator!=(const PicaPt& rhs) const { return (pt != rhs.pt); }
    bool operator<(const PicaPt& rhs) const { return (pt < rhs.pt); }
    bool operator<=(const PicaPt& rhs) const { return (pt <= rhs.pt); }
    bool operator>(const PicaPt& rhs) const { return (pt > rhs.pt); }
    bool operator>=(const PicaPt& rhs) const { return (pt >= rhs.pt); }

    float pt;
};

PicaPt operator+(float lhs, const PicaPt& rhs);
PicaPt operator*(float lhs, const PicaPt& rhs);

struct Point
{
    static const Point kZero;

    Point() : x(PicaPt(0.0f)), y(PicaPt(0.0f)) {}
    explicit Point(const PicaPt& x_, const PicaPt& y_)
        : x(x_), y(y_)
    {}

    static Point fromPixels(float xPx, float yPx, float dpi)
        { return Point(PicaPt::fromPixels(xPx, dpi), PicaPt::fromPixels(yPx, dpi)); }

    Point operator+(const Point& rhs) const
        { return Point(x + rhs.x, y + rhs.y); }
    Point& operator+=(const Point& rhs)
        { x += rhs.x; y += rhs.y; return *this; }

    Point operator-(const Point& rhs) const
        { return Point(x - rhs.x, y - rhs.y); }
    Point& operator-=(const Point& rhs)
        { x -= rhs.x; y -= rhs.y; return *this; }

    bool operator==(const Point& rhs) const
        { return (x == rhs.x && y == rhs.y); }
    bool operator!=(const Point& rhs) const
        { return (x != rhs.x || y != rhs.y); }

    PicaPt x;
    PicaPt y;
};

Point operator*(float lhs, const Point& rhs);

struct Size
{
    static const Size kZero;

    Size() : width(PicaPt(0.0f)), height(PicaPt(0.0f)) {}
    Size(const PicaPt& w, const PicaPt& h)
        : width(w), height(h)
    {}

    PicaPt width;
    PicaPt height;
};

struct Rect
{
    static const Rect kZero;

    Rect()
        : x(PicaPt(0.0f)), y(PicaPt(0.0f))
        , width(PicaPt(0.0f)), height(PicaPt(0.0f))
    {}

    Rect(const Point& origin, const Size& size)
        : x(origin.x), y(origin.y), width(size.width), height(size.height)
    {}

    Rect(const PicaPt& x_, const PicaPt& y_,
         const PicaPt& width_, const PicaPt& height_)
        : x(x_), y(y_), width(width_), height(height_)
    {}

    static Rect fromPixels(float xPx, float yPx, float widthPx, float heightPx, float dpi)
    {
        return Rect(PicaPt::fromPixels(xPx, dpi), PicaPt::fromPixels(yPx, dpi),
                    PicaPt::fromPixels(widthPx, dpi), PicaPt::fromPixels(heightPx, dpi));
    }

    bool isEmpty() const
        { return (width <= PicaPt(0.0f) || height <= PicaPt(0.0f)); }

    bool contains(const Point& p) const {
        return (p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height);
    }

    bool intersects(const Rect& r) const {
        return !((r.x < x && r.x + r.width < x) ||
                 (r.x > x + width && r.x + r.width > x + width) ||
                 (r.y < y && r.y + r.height < y) ||
                 (r.y > y + height && r.y + r.height > y + height));
    }

    Size size() const { return Size(width, height); }

    Point upperLeft() const  { return Point(x, y); }
    Point upperRight() const { return Point(maxX(), y); }
    Point lowerLeft() const  { return Point(x, maxY()); }
    Point lowerRight() const { return Point(maxX(), maxY()); }
    Point center() const     { return Point(midX(), midY()); }

    PicaPt minX() const { return x; }
    PicaPt midX() const { return x + 0.5f * width; }
    PicaPt maxX() const { return x + width; }
    PicaPt minY() const { return y; }
    PicaPt midY() const { return y + 0.5f * height; }
    PicaPt maxY() const { return y + height; }

    void translate(const PicaPt& dx, const PicaPt& dy) { x += dx; y += dy; }

    Rect translated(const PicaPt& dx, const PicaPt& dy) const
        { Rect r(*this); r.translate(dx, dy); return r; }

    void inset(const PicaPt& dx, const PicaPt& dy) {
        x += dx;  y += dy;
        width -= 2.0f * dx;  height -= 2.0f * dy;
    }

    Rect insetted(const PicaPt& dx, const PicaPt& dy) const {
        Rect r(*this);
        r.inset(dx, dy);
        return r;
    }

    PicaPt x;
    PicaPt y;
    PicaPt width;
    PicaPt height;
};

class Color
{
public:
    // Note that because these are static variable, they cannot be
    // used to initialize other static variables, as they may not
    // be constructed yet.
    static const Color kTransparent;
    static const Color kBlack;
    static const Color kWhite;
    static const Color kRed;
    static const Color kOrange;
    static const Color kYellow;
    static const Color kGreen;
    static const Color kBlue;
    static const Color kPurple;

public:
    Color() {
        _rgba[0] = 0.0f;
        _rgba[1] = 0.0f;
        _rgba[2] = 0.0f;
        _rgba[3] = 0.0f;
    }

    Color(int r, int g, int b, int a = 255) {
        _rgba[0] = float(r) / 255.0f;
        _rgba[1] = float(g) / 255.0f;
        _rgba[2] = float(b) / 255.0f;
        _rgba[3] = float(a) / 255.0f;
    }

    Color(float r, float g, float b, float a = 1.0f) {
        _rgba[0] = r;
        _rgba[1] = g;
        _rgba[2] = b;
        _rgba[3] = a;
    }

    Color(const Color& rgb, float a) {
        _rgba[0] = rgb.red();
        _rgba[1] = rgb.green();
        _rgba[2] = rgb.blue();
        _rgba[3] = a;
    }

    float red() const { return _rgba[0]; }
    float green() const { return _rgba[1]; }
    float blue() const { return _rgba[2]; }
    float alpha() const { return _rgba[3]; }
    const float* rgba() const { return _rgba; }

    void setRed(float r) { _rgba[0] = r; }
    void setGreen(float g) { _rgba[1] = g; }
    void setBlue(float b) { _rgba[2] = b; }
    void setAlpha(float a) { _rgba[3] = a; }

    Color lighter(float amount = 0.1f) const;
    Color darker(float amount = 0.1f) const;

    // Blends two colors. Amount must be in [0, 1], and is essentially the
    // alpha value. Blending is done by component, according to:
    //     this * (1.0f - amount) + dest * amount
    // Note that this not exactly alpha blending, and is intended to blend
    // between two solid colors (although the alpha channels are blended, too).
    Color blend(const Color& dest, float amount);

    Color toGrey() const {
        float grey = 0.2126f * red() + 0.7152f * green() + 0.0722f * blue();
        return Color(grey, grey, grey, alpha());
    }

    uint32_t toRGBA() const {
        uint32_t rgba = (uint32_t(std::round(red() * 255.0f)) << 24) |
                        (uint32_t(std::round(green() * 255.0f)) << 16) |
                        (uint32_t(std::round(blue() * 255.0f)) << 8) |
                        (uint32_t(std::round(alpha() * 255.0f)));
        return rgba;
    }

    std::string toHexString() const {
        static const char *hex = "0123456789abcdef";
        std::string s;
        uint32_t rgba = toRGBA();
        for (int i = 7;  i >= 0;  --i) {
            int nibble = ((0xf << (4*i)) & rgba) >> (4*i);
            s += hex[nibble];
        }
        return s;
    }

    // Comparing floating point colors is likely to be troublesome;
    // small variations will cause the color to be "different". Also,
    // the requested color, when drawn, read back, and converted likely
    // will not have the same value, since each channel will be effectively
    // downsampled to 8-bits.
    /*bool operator==(const Color& rhs) const {
        return (_rgba[0] == rhs._rgba[0] && _rgba[1] == rhs._rgba[1] &&
                _rgba[2] == rhs._rgba[2] && _rgba[3] == rhs._rgba[3]);
    }
    bool operator!=(const Color& rhs) const { return !this->operator==(rhs); }*/

    std::size_t hash() const;

private:
    float _rgba[4];
};

class DrawContext;

// Design note:
// Q: This is strange?
// A: Enums do not OR together well. This gives enum class syntax at the
//    call site, but also permits ORing. The tradeoff is that the type
//    information at the definition (int) is not very helpful.
struct Alignment {
    static const int kLeft = (1 << 0);
    static const int kHCenter = (1 << 1);
    static const int kRight = (1 << 2);
    static const int kTop = (1 << 4);
    static const int kVCenter = (1 << 5);
    static const int kBottom = (1 << 6);
    static const int kCenter = kHCenter | kVCenter;
    static const int kHorizMask = 0b00001111;
    static const int kVertMask =  0b11110000;
};

struct TextWrapMode {
    static const int kNoWrap = 0;
    static const int kWordWrap = (1 << 0);
};

// Design note:
// Q: Why not use enum classes?
// A: We want to be able to export to straight C easily. For C++ they are still
//    in the $ND_NAMESPACE namespace so they aren't actually global.
enum FontStyle { kStyleNone = 0, kStyleItalic = 1, kStyleBold = 2, kStyleBoldItalic = 3 };
enum FontWeight { kWeightAuto = 0,
                  kWeightUltraLight = 100,
                  kWeightThin = 200,
                  kWeightLight = 300,
                  kWeightRegular = 400,
                  kWeightMedium = 500,
                  kWeightSemibold = 600,
                  kWeightBold = 700,
                  kWeightHeavy = 800,
                  kWeightBlack = 900 };

// Linux: programs should call setlocale(LC_ALL, "") to initialize user defaults
//        otherwise metrics() will return the values for the "C" locale.
class Font
{
    // Design notes:
    // Q: Why not store the platform's font here?
    // A: We don't know how many pixels high the font is until we also
    //    know the DPI. This would require the caller to have the
    //    DrawContext on font creation, which would be inconvenient.
    //    (The point of PicaPt being a physical unit is to be context-
    //    independent.) Furthermore, the DrawContext is transient for a
    //    window, living only during the draw. Even worse, the user could
    //    move the window to a monitor with a different resolution or
    //    change the global scale factor in the OS. All these add up to
    //    make it better that Font is just a description, and the context
    //    creates the OS font as necessary (with heavy caching, of course).
    // Q: Since fonts are dependent on the graphics system, why not make a
    //    factory method in DrawContext, especially for getting font metrics?
    // A: Font needs to be allocatable without knowledge of the draw context,
    //    so that things like widgets can set their fonts in the constructor
    //    before they know where they will be rendered, otherwise using
    //    fonts gets really inconvenient.
    // Q: Why is textMetrics() a member of DrawContext instead of Font?
    // A: To ensure consistency between the metrics and the drawing, the glpyh
    //    layout should be done by the same code for both drawing and metrics.
    //    One could imagine a situation where different resolutions produce
    //    differently sized fonts, or even different displays use different
    //    rendering paths (say, a high-DPI monitor versus a low-resolution
    //    monitor, which might shape and kern the glyphs differently). In this
    //    case the metrics would depend on the draw context.
public:
    struct Metrics
    {
        PicaPt ascent;
        PicaPt descent;
        PicaPt leading;
        PicaPt xHeight;
        PicaPt capHeight;
        PicaPt lineHeight;  // ascent + descent + leading
    };

    Font();
    Font(const Font& f);
    Font(const std::string& family, const PicaPt& pointSize,
         FontStyle style = kStyleNone, FontWeight weight = kWeightAuto);
    ~Font();  // for unique_ptr
    Font& operator=(const Font& rhs) noexcept;

    std::string family() const;
    Font& setFamily(const std::string& family);
    PicaPt pointSize() const;
    Font& setPointSize(const PicaPt& size);
    FontStyle style() const;
    Font& setStyle(FontStyle style);
    FontWeight weight() const;
    Font& setWeight(FontWeight w);

    Metrics metrics(const DrawContext& dc) const;

    Font fontWithPointSize(const PicaPt& pointSize) const;
    Font fontWithScaledPointSize(float scaling) const;
    Font fontWithStyle(FontStyle style) const;
    Font fontWithWeight(FontWeight w) const;

    std::size_t hash() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

struct TextMetrics
{
    PicaPt width;
    PicaPt height;
    PicaPt advanceX;
    PicaPt advanceY;
};

class TextLayout
{
public:
    struct Glyph
    {
        long index = -1;  // index into the original string
        long indexOfNext = 0;  // where the next glyph starts in string;
                               // this will be str.size() for last glyph
        Rect frame = Rect(PicaPt::kZero, PicaPt::kZero,
                          PicaPt::kZero, PicaPt::kZero);

        Glyph() {}
        Glyph(long i, const Rect& r) : index(i), frame(r) {}
    };

    virtual ~TextLayout() {}
    virtual const Glyph* glyphAtPoint(const Point& p) const;
    virtual Point pointAtIndex(long index) const;

    virtual const TextMetrics& metrics() const = 0;
    virtual const std::vector<Glyph>& glyphs() const = 0;
};

enum JoinStyle { kJoinMiter = 0, kJoinRound = 1, kJoinBevel = 2 };
enum EndCapStyle { kEndCapButt = 0, kEndCapRound = 1, kEndCapSquare = 2 };
enum PaintMode { kPaintStroke = (1 << 0), kPaintFill = (1 << 1), kPaintStrokeAndFill = 3 };

class BezierPath
{
public:
    BezierPath();
    virtual ~BezierPath();

    virtual void moveTo(const Point& p);
    virtual void lineTo(const Point& end);
    virtual void quadraticTo(const Point& cp1, const Point& end);
    virtual void cubicTo(const Point& cp1, const Point& cp2, const Point& end);
    virtual void close();

    void addRect(const Rect& r);
    void addRoundedRect(const Rect& r, const PicaPt& radius);
    void addEllipse(const Rect& r);

    virtual void clearNative() = 0;  // called when path changes
    virtual void* nativePathForDPI(float dpi, bool isFilled) = 0;

protected:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

class Image
{
public:
    Image(void* nativeHandle, int width, int height, float dpi)
        : mNativeHandle(nativeHandle), mWidth(width), mHeight(height), mDPI(dpi)
    {}
    virtual ~Image() {}
        
    int width() const { return mWidth;  }
    int height() const { return mHeight;  }
    float dpi() const { return mDPI;  }

    virtual void* nativeHandle() const { return mNativeHandle; }

protected:
    void* mNativeHandle;
    int mWidth;
    int mHeight;
    float mDPI;
};

enum BitmapType { kBitmapRGB = 0, kBitmapRGBA, kBitmapGreyscale, kBitmapAlpha };

// Implements an abstract drawable:
// - Origin (0, 0) is in the upper left, +x is to the right, +y is down.
// - (x, y) is the upper left of the pixel
// - Lines are drawn centered. Thus a line 2 pixels width from
//   (1, 2) - (1, 10) will fully cover pixels x=0 and x=1. However,
//   a 1 pixel line from the same coordinates would straddle the two
//   pixels and both x=0 and x=1 would have 50% alpha. This also implies
//   that borders (e.g. a Rect painted with kPaintFillAndStroke) are
//   also centered along the path, so half the border is outside the
//   fill area.
class DrawContext
{
public:
#if __APPLE__
    static std::shared_ptr<DrawContext> fromCoreGraphics(void* cgcontext, int width, int height, float dpi);
    static std::shared_ptr<DrawContext> createCoreGraphicsBitmap(BitmapType type, int width, int height,
                                                                 float dpi = 72.0f);
#elif defined(__unix__)
    // Note that an Xlib Window is NOT a pointer, so you will need to call this
    // as fromX11(display, &window, ...). (Window is actually a typedef of
    // long int, but we do not want to hard-code it, nor do we want to include
    // Xlib.h in this header.
    static std::shared_ptr<DrawContext> fromX11(void* display, const void* window, int width, int height, float dpi);
    static std::shared_ptr<DrawContext> createCairoX11Bitmap(void* display, BitmapType type, int width, int height, float dpi = 72.0f);
#elif defined(_WIN32) || defined(_WIN64)
    static std::shared_ptr<DrawContext> fromHwnd(void* hwnd, int width, int height, float dpi);
    static std::shared_ptr<DrawContext> createDirect2DBitmap(BitmapType type, int width, int height,
                                                             float dpi = 72.0f);
#endif

    DrawContext(void *nativeDC, int width, int height, float dpi);
    virtual ~DrawContext() {}

    // This is the preferred function to create a bitmap if you already have a
    // context (for instance, if you are creating a bitmap for a window).
    virtual std::shared_ptr<DrawContext> createBitmap(BitmapType type, int width, int height,
                                                      float dpi = 72.0f) = 0;

    virtual std::shared_ptr<BezierPath> createBezierPath() const = 0;
    /// Creates a text layout. If width is non-zero, the text will wrap to the
    /// width, and the horizontal alignment will be applied. A width of zero
    /// is a non-breaking line of left-aligned text.
    virtual std::shared_ptr<TextLayout> createTextLayout(
                const char *utf8, const Font& font, const Color& color,
                const PicaPt& width = PicaPt::kZero,
                int alignment = Alignment::kLeft) const = 0;

    int width() const { return mWidth; }
    int height() const { return mHeight; }
    float dpi() const { return mDPI; }

    virtual PicaPt onePixel() const;
    PicaPt floorToNearestPixel(const PicaPt& p) const;
    PicaPt roundToNearestPixel(const PicaPt& p) const;
    PicaPt ceilToNearestPixel(const PicaPt& p) const;

    virtual void beginDraw() = 0;
    virtual void endDraw() = 0;

    virtual void save() = 0;
    virtual void restore() = 0;

    virtual void translate(const PicaPt& dx, const PicaPt& dy) = 0;
    virtual void rotate(float degrees) = 0;
    virtual void scale(float sx, float sy) = 0;

    virtual void setFillColor(const Color& color) = 0;
    virtual void setStrokeColor(const Color& color) = 0;
    virtual void setStrokeWidth(const PicaPt& w) = 0;
    virtual void setStrokeEndCap(EndCapStyle cap) = 0;
    virtual void setStrokeJoinStyle(JoinStyle join) = 0;
    virtual void setStrokeDashes(const std::vector<PicaPt> lengths, const PicaPt& offset) = 0;

    virtual Color fillColor() const = 0;
    virtual Color strokeColor() const = 0;
    virtual PicaPt strokeWidth() const = 0;
    virtual EndCapStyle strokeEndCap() const = 0;
    virtual JoinStyle strokeJoinStyle() const = 0;

    // Sets the entire context to 'color'. For opaque colors this is the
    // same as drawing a filled rectangle the same size as the context
    // (but does not change the fill color like setFillColor() would).
    // Is affected by clipping path.
    virtual void fill(const Color& color) = 0;
    // Sets contents of rect to be transparent
    virtual void clearRect(const Rect& rect) = 0;

    virtual void drawLines(const std::vector<Point>& lines) = 0;
    virtual void drawRect(const Rect& rect, PaintMode mode) = 0;
    virtual void drawRoundedRect(const Rect& rect, const PicaPt& radius, PaintMode mode);  // has impl
    virtual void drawEllipse(const Rect& rect, PaintMode mode) = 0;
    virtual void drawPath(std::shared_ptr<BezierPath> path, PaintMode mode) = 0;
    // Note that the text sits ON the baseline, which will be aligned with
    // the vertical pixel boundary. As a result, if the baseline is at y=16,
    // The ascent of the glyph will end at pixel 15 (since y=16 is in-between
    // pixels 15 and 16). However, for smaller point sizes concessions need to
    // be made for readability, and platforms may choose to to place the
    // baseline so that in the above example the ascent actually ends at
    // pixel 16.
    virtual void drawText(const char *textUTF8, const Point& topLeft, const Font& font, PaintMode mode) = 0;

    // Draws text within the provided rectangle. Use the values from Alignment
    // in the alignment parameter (e.g. Alignment::kLeft | Alignment::kVCenter).
    void drawText(const char *textUTF8, const Rect& r, int alignment,
                  int textWrapMode, const Font& font, PaintMode mode);

    // Draws the text. If you need a layout, you should use this function to
    // draw it, as it avoid the need to re-create the layout inside the other
    // text-drawing functions. Only draw using the same context that created
    // the text, except on macOS/iOS which use transient contexts (however, the
    // DPI should be the same as the original context, which it normally is,
    // except in cases like where the window moves to another monitor).
    virtual void drawText(const TextLayout& layout, const Point& topLeft) = 0;

    virtual void drawImage(std::shared_ptr<Image> image, const Rect& destRect) = 0;

    virtual void clipToRect(const Rect& rect) = 0;
    // The path will be retained; the caller may let its copy go out of scope.
    // (However, reusing same path on the next draw will give better performance,
    // since the OS resources will not need to be recreated.)
    virtual void clipToPath(std::shared_ptr<BezierPath> path) = 0;

    void* nativeDC() const { return mNativeDC; }
    // Cannot be called within a beginDraw()/endDraw() pair.
    // Note that this function can be slow.
    // Design note: this is not const because it's easier in Windows that way.
    virtual Color pixelAt(int x, int y) = 0;
    // Cannot be called within a beginDraw()/endDraw() pair.
    virtual std::shared_ptr<Image> copyToImage() = 0;

    virtual Font::Metrics fontMetrics(const Font& font) const = 0;

    // Returns the metrics for a single line of text
    virtual TextMetrics textMetrics(const char *textUTF8, const Font& font,
                                    PaintMode mode = kPaintFill) const = 0;

    // Multiplies point by the current transformation matrix and returns
    // the point in context pixel coordinates. Note that the pixel coordinates
    // are native to the underlying operating system not portable. In fact, they
    // may even be different between a bitmap and a window on the same system.
    virtual void calcContextPixel(const Point& point, float *x, float *y) = 0;

protected:
    void setInitialState();

protected:
    // This is void* so that we don't pull in platform header files.
    void *mNativeDC;
    float mDPI;
    int mWidth;
    int mHeight;
};

} // namespace $ND_NAMESPACE
#endif // _NATIVE_DRAW_H
