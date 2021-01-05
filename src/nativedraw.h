#ifndef _NATIVE_DRAW_H
#define _NATIVE_DRAW_H

#ifndef ND_NAMESPACE
#define ND_NAMESPACE eb
#endif // ND_NAMESPACE

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
    PicaPt operator/(const PicaPt& v) const { return PicaPt(pt / v.pt); }
    PicaPt operator/(float v) const { return PicaPt(pt / v); }
    PicaPt& operator+=(const PicaPt& v) { pt += v.pt; return *this; }
    PicaPt& operator+=(float v) { pt += v; return *this; }
    PicaPt& operator-=(const PicaPt& v) { pt -= v.pt; return *this; }
    PicaPt& operator-=(float v) { pt -= v; return *this; }
    PicaPt& operator*=(const PicaPt& v) { pt *= v.pt; return *this; }
    PicaPt& operator*=(float v) { pt *= v; return *this; }
    PicaPt& operator/=(const PicaPt& v) { pt /= v.pt; return *this; }
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
    Point() : x(PicaPt(0.0f)), y(PicaPt(0.0f)) {}
    explicit Point(const PicaPt& x_, const PicaPt& y_)
        : x(x_), y(y_)
    {}

    static Point fromPixels(float xPx, float yPx, float dpi)
        { return Point(PicaPt::fromPixels(xPx, dpi), PicaPt::fromPixels(yPx, dpi)); }

    Point operator+(const Point& rhs) { return Point(x + rhs.x, y + rhs.y); }
    Point& operator+=(const Point& rhs)
        { x += rhs.x; y += rhs.y; return *this; }

    PicaPt x;
    PicaPt y;
};

struct Size
{
    Size() : width(PicaPt(0.0f)), height(PicaPt(0.0f)) {}
    Size(const PicaPt& w, const PicaPt& h)
        : width(w), height(h)
    {}

    PicaPt width;
    PicaPt height;
};

struct Rect
{
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

    float setRed(float r) { _rgba[0] = r; }
    float setGreen(float g) { _rgba[1] = g; }
    float setBlue(float b) { _rgba[2] = b; }
    float setAlpha(float a) { _rgba[3] = a; }

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

private:
    float _rgba[4];
};

class DrawContext;

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
public:
    struct Metrics
    {
        PicaPt ascent;
        PicaPt descent;
        PicaPt leading;
        PicaPt xHeight;
        PicaPt capHeight;
        PicaPt lineHeight;
    };

    Font(const Font& f);
    Font(const std::string& family, const PicaPt& pointSize,
         FontStyle style = kStyleNone, FontWeight weight = kWeightAuto);
    ~Font();  // for unique_ptr

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
    Font fontWithStyle(FontStyle style) const;
    Font fontWithWeight(FontWeight w) const;

    std::size_t hash() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

enum JoinStyle { kJoinMiter = 0, kJoinRound = 1, kJoinBevel = 2 };
enum EndCapStyle { kEndCapButt = 0, kEndCapRound = 1, kEndCapSquare = 2 };
enum PaintMode { kPaintStroke = (1 << 0), kPaintFill = (1 << 1), kPaintStrokeAndFill = 3 };

class BezierPath
{
public:
    // Command is really an implementation detail and it would be nicer if
    // we could put this in nativedraw_private.h, but we need it here so that
    // BezierPath can be allocated on the stack.
    struct Command
    {
        enum Action { kMoveTo = 0, kLineTo, kQuadraticTo, kCubicTo, kClose };

        Action cmd;
        Point p1;
        Point p2;
        Point p3;

        explicit Command(Action c) : cmd(c) { assert(c == kClose); }

        Command(Action c, const Point& p)
            : cmd(c), p1(p)
        { assert(c == kMoveTo || c == kLineTo); }

        Command(Action c, const Point& p1_, const Point& p2_)
            : cmd(c), p1(p1_), p2(p2_)
        { assert(c == kQuadraticTo); }

        Command(Action c, const Point& p1_, const Point& p2_, const Point& p3_)
            : cmd(c), p1(p1_), p2(p2_), p3(p3_)
        { assert(c == kCubicTo); }
    };

    void moveTo(const Point& p);
    void lineTo(const Point& end);
    void quadraticTo(const Point& cp1, const Point& end);
    void cubicTo(const Point& cp1, const Point& cp2, const Point& end);
    void close();

    void addRect(const Rect& r);
    void addRoundedRect(const Rect& r, const PicaPt& radius);

    std::vector<Command> commands;
};

class Bitmap;

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
    DrawContext(void *nativeDC_, int width, int height, float dpi);

    int width() const { return mWidth; }
    int height() const { return mHeight; }
    float dpi() const { return mDPI; }

    void save();
    void restore();

    void translate(const PicaPt& dx, const PicaPt& dy);
    void rotate(float degrees);
    void scale(float sx, float sy);

    void setFillColor(const Color& color);

    void setStrokeColor(const Color& color);
    void setStrokeWidth(const PicaPt& w);
    void setStrokeEndCap(EndCapStyle cap);
    void setStrokeJoinStyle(JoinStyle join);
    void setStrokeDashes(const std::vector<PicaPt> lengths, const PicaPt& offset);

    // Sets the entire context to 'color'. For opaque colors this is the
    // same as drawing a filled rectangle the same size as the context
    // (but does not change the fill color like setFillColor() would).
    // Is affected by clipping path.
    void fill(const Color& color);
    // Sets contents of rect to be transparent
    void clearRect(const Rect& rect);

    void drawLines(const std::vector<Point>& lines);
    void drawRect(const Rect& rect, PaintMode mode);
    void drawRoundedRect(const Rect& rect, const PicaPt& radius, PaintMode mode);
    void drawEllipse(const Rect& rect, PaintMode mode);
    void drawPath(const BezierPath& path, PaintMode mode);
    // Note that the text sits ON the baseline, which will be aligned with
    // the vertical pixel boundary. As a result, if the baseline is at y=16,
    // The ascent of the glyph will end at pixel 15 (since y=16 is in-between
    // pixels 15 and 16). However, for smaller point sizes concessions need to
    // be made for readability, and platforms may choose to to place the
    // baseline so that in the above example the ascent actually ends at
    // pixel 16.
    void drawText(const char *textUTF8, const Point& topLeft, const Font& font, PaintMode mode);
    void drawBitmap(const Bitmap& bitmap, const Rect& r);

    void clipToRect(const Rect& rect);
    void clipToPath(const BezierPath& path);

    void* nativeDC() const { return mNativeDC; }

protected:
    void setNativeDC(void *nativeDC);  // in case don't have it at construction time

protected:
    // This is void* so that we don't pull in platform header files.
    void *mNativeDC;
    float mDPI;
    int mWidth;
    int mHeight;

#if __APPLE__
private:
    struct Private;
    std::unique_ptr<Private> mPrivate;
#endif //__APPLE__
};

enum BitmapType { kBitmapRGB = 0, kBitmapRGBA, kBitmapGreyscale, kBitmapAlpha };

class Bitmap : public DrawContext
{
public:
    Bitmap(int width, int height, BitmapType type, float dpi = 72.0f);
    ~Bitmap();

    Color pixelAt(int x, int y) const;

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace $ND_NAMESPACE
#endif // _NATIVE_DRAW_H
