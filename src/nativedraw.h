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

/// This is a typographical "point", which is a unit of measurement equal to
/// 1/72 inch. It can usually be used as "pixel" (even though on-screen it
/// 1 point may be multiple pixels), and in 72 dpi, a PicaPt and a pixel are
/// the same measurement. Note that that one "pica" is actually
/// 1/12 of an inch, but since "point" usually means an (x, y) pair in
/// computer graphics, it's hard to come up with a meaningful name.
/// Note: if you are using a DrawContext for a window, the physical unit may
///       be scaled according to the system's UI scaling settings. The native
///       resolution should produce PicaPt(72) = 1 inch. On macOS, the native
///       resolution may not be the default! On Windows, the UI scaling will
///       affect the actual length. On Linux, the DPI that the X server thinks
///       it is using and the value of the X resource Xft.dpi affect the scaling.
struct PicaPt
{
    static const PicaPt kZero;

    PicaPt() : pt(0.0f) {}
    explicit PicaPt(float pt_) : pt(pt_) {}

    static PicaPt fromPixels(float pixels, float dpi) {
        return PicaPt(pixels * 72.0f / dpi);
    }

    /// Returns the size of a "standard pixel", that is, 1 px at 96 dpi, which
    /// was the standard DPI for monitors for many years.
    static PicaPt fromStandardPixels(float pixels) {
        return fromPixels(pixels, 96.0f);
    }

    float asFloat() const { return pt; }
    float toPixels(float dpi) const { return pt * dpi / 72.0f; }
    float toStandardPixels() const { return toPixels(96.0f); }

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
    void translate(const Point& offset) { x += offset.x; y += offset.y; }

    Rect translated(const PicaPt& dx, const PicaPt& dy) const
        { Rect r(*this); r.translate(dx, dy); return r; }
    Rect translated(const Point& offset) const
        { Rect r(*this); r.translate(offset); return r; }

    void inset(const PicaPt& dx, const PicaPt& dy) {
        x += dx;  y += dy;
        width -= 2.0f * dx;  height -= 2.0f * dy;
    }

    Rect insetted(const PicaPt& dx, const PicaPt& dy) const {
        Rect r(*this);
        r.inset(dx, dy);
        return r;
    }

    Rect intersectedWith(const Rect& r) const;

    Rect operator+(const Point& rhs) const
        { return Rect(x + rhs.x, y + rhs.y, width, height); }
    Rect& operator+=(const Point& rhs)
        { x += rhs.x; y += rhs.y; return *this; }
    Rect operator-(const Point& rhs) const
        { return Rect(x - rhs.x, y - rhs.y, width, height); }
    Rect& operator-=(const Point& rhs)
        { x -= rhs.x; y -= rhs.y; return *this; }

    PicaPt x;
    PicaPt y;
    PicaPt width;
    PicaPt height;
};

class Color
{
public:
    /// Note that because these are static variable, they cannot be
    /// used to initialize other static variables, as they may not
    /// be constructed yet.
    static const Color kTransparent;
    static const Color kBlack;
    static const Color kWhite;
    static const Color kRed;
    static const Color kOrange;
    static const Color kYellow;
    static const Color kGreen;
    static const Color kBlue;
    static const Color kPurple;

    /// This represents the default text foreground color. It exists
    /// for higher-level user interface libraries that use Text, since
    /// you might want to create a Text before you know what the
    /// default color is, or if the default color changes. Passing this
    /// color to a drawing function result in an undefined color being
    /// used.
    static const Color kTextDefault;

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

    /// Blends two colors. Amount must be in [0, 1], and is essentially the
    /// alpha value. Blending is done by component, according to:
    ///     this * (1.0f - amount) + dest * amount
    /// Note that this not exactly alpha blending, and is intended to blend
    /// between two solid colors (although the alpha channels are blended, too).
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

class HSVColor
{
public:
    /// Hue should be in the range [0, 360], and s, v, a in the range [0, 1]
    HSVColor(float hueDeg, float s, float v, float a = 1.0f)
    {
        _hsva[0] = hueDeg;
        _hsva[1] = s;
        _hsva[2] = v;
        _hsva[3] = a;
    }

    float hueDeg() const { return _hsva[0]; }
    float saturation() const { return _hsva[1]; }
    float value() const { return _hsva[2]; }
    float alpha() const { return _hsva[3]; }

    Color toColor() const;

private:
    float _hsva[4];
};

class DrawContext;

// Design note:
// Q: This is strange?
// A: Enums do not OR together well. This gives enum class syntax at the
//    call site, but also permits ORing. The tradeoff is that the type
//    information at the definition (int) is not very helpful.
struct Alignment {
    /// If alignment is exactly kNone, no adjustment will be made to the
    /// upper-left drawing point, which may result in the top of the glyph not
    /// aligning with the top of the drawing rectangle, usually due to the font's
    /// ascent being larger than the cap-height. kNone is used internally by the
    /// DrawContext::drawText() function that takes no alignment. Generally
    /// you will get better results with (kLeft | kTop).
    static const int kNone = 0;
    static const int kLeft = (1 << 0);
    static const int kHCenter = (1 << 1);
    static const int kRight = (1 << 2);
    static const int kJustify = (1 << 3);  // not implemented
    static const int kTop = (1 << 4);
    static const int kVCenter = (1 << 5);
    static const int kBottom = (1 << 6);
    static const int kCenter = kHCenter | kVCenter;
    static const int kHorizMask = 0b00001111;
    static const int kVertMask =  0b11110000;
};

enum TextWrapping {
    kWrapNone,
    kWrapWord
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
    // Q: Why is font listing here, instead of at a higher, application level?
    // A: So that we can list only the fonts that we know we can render,
    //    otherwise the application level needs to know what fonts we cannot do,
    //    which may vary by platform (DirectX, in particular, has some
    //    limitations). While application-level wrappers should provide an
    //    appropriate function which calls our list (such as UITK's
    //    Application::availableFontFamilies()), users only wanting to draw
    //    (perhaps a python script outputting a PDF) may still find a list of
    //    available fonts helpful if they wish to be cross-platform.
public:
    struct Metrics
    {
        PicaPt ascent;
        PicaPt descent;
        PicaPt leading;
        PicaPt xHeight;
        PicaPt capHeight;
        PicaPt lineHeight;  // ascent + descent + leading
        PicaPt underlineOffset;
        PicaPt underlineThickness;
    };

    // Returns available font families (sorted alphabetically).
    static std::vector<std::string> availableFontFamilies();

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

    /// Convenience function to set (or unset) bold, leaving italic alone.
    Font& setBold(bool isBold);
    /// Convenience function to set (or unset) italic, leaving bold/weight alone.
    Font& setItalic(bool isItalic);

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

enum UnderlineStyle
{
    kUnderlineNone = 0,
    kUnderlineSingle,
    kUnderlineDouble,
    kUnderlineDotted,
    kUnderlineWavy
};

template <typename T>
struct TextAttr
{
    T value;     // the value of the attribute, only valid if isSet is true
    bool isSet;  // true if the user specifically set it, false if it is unset (default value)

    TextAttr() : value(T()), isSet(false) {}
    explicit TextAttr(const T& val) : value(val), isSet(true) {}

    TextAttr& operator=(const T& newVal) {
        value = newVal;
        isSet = true;
        return *this;
    }
    TextAttr& operator=(const TextAttr& rhs) {
        if (rhs.isSet) {
            value = rhs.value;
            isSet = true;
        } else if (isSet) {
            /* leave current value (our isSet is true but theirs is false) */
        } else { /* leave current value (isSet = false) */}
        return *this;
    }
};
typedef TextAttr<bool> BoolTextAttr;
typedef TextAttr<PicaPt> PointTextAttr;
typedef TextAttr<Color> ColorTextAttr;
typedef TextAttr<Font> FontTextAttr;
typedef TextAttr<UnderlineStyle> UnderlineStyleTextAttr;

struct TextRun
{
    PointTextAttr pointSize;  /// overrides font's point size setting, if set
    BoolTextAttr bold;        /// overrides font's bold setting, if set
    BoolTextAttr italic;      /// overrides font's italic setting, if set
    FontTextAttr font;
    ColorTextAttr backgroundColor;
    ColorTextAttr color;
    ColorTextAttr underlineColor;
    ColorTextAttr strikethroughColor;
    ColorTextAttr outlineColor;
    PointTextAttr outlineStrokeWidth;
    UnderlineStyleTextAttr underlineStyle;
    BoolTextAttr strikethrough;
    BoolTextAttr superscript;
    BoolTextAttr subscript;
    PointTextAttr characterSpacing;

    int startIndex = 0;
    int length = -1;
};

/// Rich text class, similar to NSAttributedText. The text is not drawable
/// directly, as text layout may be different depending on the DPI of the
/// DrawContext (glyphs and kerning may be different, especially at low point
/// sizes, which sometimes use custom glyphs). To draw text, create a
/// TextLayout from the DrawContext. Attributes like are unset by default.
/// Text is assumed to be UTF-8. The start index and length are indices into
/// the text (NOT characters or code points), and are assumed to be on valid
/// boundaries.
class Text
{
public:
    Text();
    Text(const std::string& utf8, const Font& font, const Color& fgColor);

    const std::string& text() const;

    /// Overrides the point size of the font (convenience)
    Text& setPointSize(const PicaPt& pointSize, int start = 0, int len = -1);
    /// Sets bold, overriding the weight of the font (convenience)
    Text& setBold(int start = 0, int len = -1);
    /// Sets italic style, override the italicness of the font (convenience)
    Text& setItalic(int start = 0, int len = -1);
    Text& setFont(const Font& font, int start = 0, int len = -1);
    Text& setBackgroundColor(const Color& bg, int start = 0, int len = -1);
    Text& setColor(const Color& fg, int start = 0, int len = -1);
    Text& setUnderlineStyle(UnderlineStyle style, int start = 0, int len = -1);
    Text& setUnderlineColor(const Color& c, int start = 0, int len = -1);
    Text& setStrikethrough(int start = 0, int len = -1);
    Text& setStrikethroughColor(const Color& c, int start = 0, int len = -1);
    Text& setSuperscript(int start = 0, int len = -1);
    Text& setSubscript(int start = 0, int len = -1);
    Text& setCharacterSpacing(const PicaPt& extraSpacing, int start = 0, int len = -1);
    Text& setOutlineStrokeWidth(const PicaPt& width, int start = 0, int len = -1);
    Text& setOutlineColor(const Color& c, int start = 0, int len = -1);
    Text& setTextRun(const TextRun& run);
    Text& setTextRuns(const std::vector<TextRun>& runs);

    // Sets the line height as a multiple of the natural font line height.
    // Default: 0.0 (unset: platform default)
    Text& setLineHeightMultiple(float factor);
    float lineHeightMultiple() const;

    const TextRun& runAt(int index) const;
    const std::vector<TextRun>& runs() const;

private:
    struct ParagraphStyle {
        float lineHeightMultiple;
    };

    std::string mText;
    std::vector<TextRun> mRuns;
    ParagraphStyle mParagraph;

    int runIndexFor(int index) const;
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
        long index = -1;  /// index into the original string
        long indexOfNext = 0;  /// where the next glyph starts in string;
                               /// this will be str.size() for last glyph
        int line = 0;
        Rect frame = Rect(PicaPt::kZero, PicaPt::kZero,
                          PicaPt::kZero, PicaPt::kZero);

        Glyph() {}
        Glyph(long i, int ln, const Rect& r) : index(i), line(ln), frame(r) {}
    };

    virtual ~TextLayout() {}
    virtual const Glyph* glyphAtPoint(const Point& p) const;
    virtual Point pointAtIndex(long index) const;

    virtual const TextMetrics& metrics() const = 0;
    virtual const std::vector<Glyph>& glyphs() const = 0;

protected:
    // This *may* call glyphs(). Note that if glyphs are cached, which is wise
    // from a performance standpoint, implementations of this class should the
    // cached glyphs at the end of the constructor. This is because the glyphs
    // are only needed by user code, and usually just for determing which
    // glyph a mouse click hit. Since most text is used for display, it is not
    // helpful to keep the glyphs in memory. Note that if using a vector,
    // vector::clear() does NOT release memory! Use vector::shrink_to_fit()
    // after clearing. Unfortunately I cannot think of a way to enforce this,
    // as some platforms may need the glyphs after this for calculations of
    // some things (for example, macOS does not draw strikethroughs) and there
    // is no point calculating them twice.
    Font::Metrics calcFirstLineMetrics(
                const std::vector<Font::Metrics>& runMetrics,
                const std::vector<TextRun>& runs,
                int firstLineLength = -1) const;
    Point calcOffsetForAlignment(int alignment, const Size& size,
                                 const Font::Metrics& firstLineMetrics);
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
    /// Draws a 90 deg elliptical arc. Note that current point -> forwardCorner
    /// and forwardCorner -> endPt should be at right angles, however, this is
    /// not checked by the function.
    virtual void quarterEllipseTo(const Point& forwardCorner, const Point& endPt);
    virtual void close();

    void addRect(const Rect& r);
    void addRoundedRect(const Rect& r, const PicaPt& radius);
    void addEllipse(const Rect& r);
    void addCircle(const Point& center, const PicaPt& radius);

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

/// Implements an abstract drawable:
/// - Origin (0, 0) is in the upper left, +x is to the right, +y is down.
/// - (x, y) is the upper left of the pixel
/// - Lines are drawn centered. Thus a line 2 pixels width from
///   (1, 2) - (1, 10) will fully cover pixels x=0 and x=1. However,
///   a 1 pixel line from the same coordinates would straddle the two
///   pixels and both x=0 and x=1 would have 50% alpha. This also implies
///   that borders (e.g. a Rect painted with kPaintFillAndStroke) are
///   also centered along the path, so half the border is outside the
///   fill area.
class DrawContext
{
public:
#if __APPLE__
    // macOS lets the user scale the user interface. If the context represents
    // a window, `dpi` should be the scaled screen size:  window.screen.frame.size.
    // PicaPt(1.0f) at native resolution is exactly 1/72 inch.
    // `nativeDPI` is used by onePixel() and ...ToNearestPixel() functions.

    /// Returns:
    /// - uiDPI: this is the DPI that macOS is pretending that the UI is using.
    ///          In actual fact, this uses the native resolution of the screen (but
    ///          in CGFloat units, unscaled by backingScaleFactor).
    ///          If the DrawContext's DPI is set to `uiDPI`, then the native resolution
    ///          will have the correct physical measurements (PicaPt(72) should measure
    ///          1.0 inches with a ruler) but all other resolutions will scale like all
    ///          other applications. This is probably what you want for a DrawContext
    ///          used in an NSWindow.
    /// - cocoaDPI: the DPI of the user interface (the apparent DPI based on the
    ///          size of the display set in Settings >> Display >> Resolution), in
    ///          CGFloat units. This is the value `screen.frame.size / physicalSize`,
    ///          and is the DPI that CGContext is using. `cocoaDPI / uiDPI` gives the
    ///          scaling factor the user chose in Settings >> Display >> Resolution.
    ///          If the DrawContext's DPI is set to `cocoaDPI`, then the PicaPt(72)
    ///          will always be 1 inch physically, which is probably not what you want
    ///          because it ignores the user's settings.
    /// - hiresDPI: this is `cocoaDPI * nsscreen.backingScaleFactor`. If you pass this
    ///          this to the `nativeDPI` parameter of fromCoreGraphics(), onePixel()
    ///          return a value corresponding to one physical pixel.
    /// (It is a little counter-intuitive that `uiDPI * nsscreen.backingScaleFactor` is
    /// the actual native resolution of the screen. This is because Cocoa really wants
    /// you to pretend that the cocoaDPI is the real resolution, and scales things
    /// for you underneath.)
    static void getScreenDPI(void* nsscreen, float *uiDPI, float *cocoaDPI, float *hiresDPI);

    /// Creates a context from a CGContext. If this is from a window, the
    /// CGContext can be retrieved with `NSGraphicsContext.currentContext.CGContext` and
    /// the DrawContext should NOT live longer than the window's draw callback, as there
    /// is no guarantee that the context will be the same in the next call.
    /// Note: for a window context, this function should be called with:
    ///   float uiDPI, nativeDPI;
    ///   DrawContext::getScreenDPI((__bridge void*)screen, &uiDPI, nullptr, &hiresDPI);
    ///   auto dc = DrawContext::fromCoreGraphics(cgctxt, w, h, uiDPI, hiresDPI);
    /// This is because macOS allows the user to scale the display in
    /// Settings >> Display >> Resolution, and the expectation is that the size of the
    /// UI should scale accordingly. We want pass the native resolution so that
    /// onePixel() and friends return one actual pixel.
    /// (We do not provide a fromCoreGraphics(void* nswin) that does this all for you
    /// because the DrawContext must be recreated on each draw in macOS, and this avoids
    /// the surprisingly complicated work of figuring out what the scaling factor is.
    /// You should cache the values of getScreenDPI() for best performance.)
    static std::shared_ptr<DrawContext> fromCoreGraphics(void* cgcontext, int width, int height, float dpi,
                                                         float nativeDPI = 0.0f);
    static std::shared_ptr<DrawContext> createCoreGraphicsBitmap(BitmapType type, int width, int height,
                                                                 float dpi = 72.0f);
#elif defined(__unix__)
    /// Note that an Xlib Window is NOT a pointer, so you will need to call this
    /// as fromX11(display, &window, ...). (Window is actually a typedef of
    /// long int, but we do not want to hard-code it, nor do we want to include
    /// Xlib.h in this header.
    static std::shared_ptr<DrawContext> fromX11(void* display, const void* window, int width, int height, float dpi);
    static std::shared_ptr<DrawContext> createCairoX11Bitmap(void* display, BitmapType type, int width, int height, float dpi = 72.0f);
#elif defined(_WIN32) || defined(_WIN64)
    static std::shared_ptr<DrawContext> fromHwnd(void* hwnd, int width, int height, float dpi);
    static std::shared_ptr<DrawContext> createDirect2DBitmap(BitmapType type, int width, int height,
                                                             float dpi = 72.0f);
#endif

    virtual ~DrawContext() {}

    /// This is the preferred function to create a bitmap if you already have a
    /// context (for instance, if you are creating a bitmap for a window).
    virtual std::shared_ptr<DrawContext> createBitmap(BitmapType type, int width, int height,
                                                      float dpi = 72.0f) = 0;

    virtual std::shared_ptr<BezierPath> createBezierPath() const = 0;
    /// Creates a text layout. If width is non-zero, the text will wrap to the
    /// width, and the horizontal alignment will be applied. If height is non-zero
    /// the vertical alignment will be applied. Note that, despite the default
    /// argument (which exists for non-zero size), a zero-component is the same
    /// as specifying Alignment::kZero for that component.
    virtual std::shared_ptr<TextLayout> createTextLayout(
                const char *utf8, const Font& font, const Color& color,
                const Size& size = Size::kZero,
                int alignment = Alignment::kLeft | Alignment::kTop,
                TextWrapping wrap = kWrapWord) const = 0;
    virtual std::shared_ptr<TextLayout> createTextLayout(
                const Text& t,
                const Size& size = Size::kZero,
                int alignment = Alignment::kLeft | Alignment::kTop,
                TextWrapping wrap = kWrapWord) const = 0;
    virtual std::shared_ptr<TextLayout> createTextLayout(
                const Text& t,
                const Font& defaultReplacementFont,
                const Color& defaultReplacementColor,
                const Size& size = Size::kZero,
                int alignment = Alignment::kLeft | Alignment::kTop,
                TextWrapping wrap = kWrapWord) const = 0;

    int width() const { return mWidth; }
    int height() const { return mHeight; }
    float dpi() const { return mDPI; }

    virtual PicaPt onePixel() const;
    PicaPt floorToNearestPixel(const PicaPt& p) const;
    PicaPt roundToNearestPixel(const PicaPt& p) const;
    PicaPt ceilToNearestPixel(const PicaPt& p) const;
    /// Offsets p so that a straight line along that dimension with be centered
    /// on that physical pixel. Note that p should already be on a pixel
    /// boundary (use floorToNearestPixel(), roundToNearestPixel(),
    /// ceilToNearestPixel(), as desired). This is a convenience function for
    /// offsetting by half a pixel if the stroke width is an odd number of
    /// pixels. (Because a stroke is centered on the pixel boundary, an
    /// even-width stroke will paint exactly half the stroke width's work of
    /// pixels on either side, but an odd width will have a different amount.
    /// In fact, a one pixel line is really bad because it will become a
    /// 2 pixel line with alpha = 50%, which is definitely not what is usually
    /// desired.) The stroke width need not be an integer value; it is
    /// round()ed and then determined to be even/odd. So a 2.25 px line would
    /// be treated as 2 pixels, and a 2.8 px line would be treated as 3 pixels.
    PicaPt offsetPixelForStroke(const PicaPt& p, const PicaPt& strokeWidth) const;

    /// Convenience function for PicaPt::fromStandardPixels(), as it contrasts
    /// with onePixel();
    PicaPt oneStandardPixel() const { return PicaPt::fromStandardPixels(1.0f); }

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

    /// Sets the entire context to 'color'. For opaque colors this is the
    /// same as drawing a filled rectangle the same size as the context
    /// (but does not change the fill color like setFillColor() would).
    /// Is affected by clipping path.
    virtual void fill(const Color& color) = 0;
    /// Sets contents of rect to be transparent
    virtual void clearRect(const Rect& rect) = 0;

    virtual void drawLines(const std::vector<Point>& lines) = 0;
    virtual void drawRect(const Rect& rect, PaintMode mode) = 0;
    virtual void drawRoundedRect(const Rect& rect, const PicaPt& radius, PaintMode mode);  // has impl
    virtual void drawEllipse(const Rect& rect, PaintMode mode) = 0;
    virtual void drawPath(std::shared_ptr<BezierPath> path, PaintMode mode) = 0;
    /// Note that the text sits ON the baseline, which will be aligned with
    /// the vertical pixel boundary. As a result, if the baseline is at y=16,
    /// The ascent of the glyph will end at pixel 15 (since y=16 is in-between
    /// pixels 15 and 16). However, for smaller point sizes concessions need to
    /// be made for readability, and platforms may choose to to place the
    /// baseline so that in the above example the ascent actually ends at
    /// pixel 16.
    /// Note: this recreates a TextLayout every call, so do not use when drawing
    ///       repeatedly, such as when drawing the contents of a widget.
    virtual void drawText(const char *textUTF8, const Point& topLeft, const Font& font, PaintMode mode) = 0;

    /// Draws text within the provided rectangle. Use the values from Alignment
    /// in the alignment parameter (e.g. Alignment::kLeft | Alignment::kVCenter).
    /// Note: this recreates a TextLayout every call, so do not use when drawing
    ///       repeatedly, such as when drawing the contents of a widget.
    void drawText(const char *textUTF8, const Rect& r, int alignment,
                  TextWrapping wrap, const Font& font, PaintMode mode);

    /// Draws the text. If you need a layout, you should use this function to
    /// draw it, as it avoids the need to re-create the layout inside the other
    /// text-drawing functions. Only draw using the same context that created
    /// the text, except on macOS/iOS which use transient contexts (however, the
    /// DPI should be the same as the original context, which it normally is,
    /// except in cases like where the window moves to another monitor).
    virtual void drawText(const TextLayout& layout, const Point& topLeft) = 0;

    virtual void drawImage(std::shared_ptr<Image> image, const Rect& destRect) = 0;

    virtual void clipToRect(const Rect& rect) = 0;
    /// The path will be retained; the caller may let its copy go out of scope.
    /// (However, reusing same path on the next draw will give better performance,
    /// since the OS resources will not need to be recreated.)
    virtual void clipToPath(std::shared_ptr<BezierPath> path) = 0;

    void* nativeDC() const { return mNativeDC; }
    /// Cannot be called within a beginDraw()/endDraw() pair.
    /// Note that this function can be slow.
    // Design note: this is not const because it's easier in Windows that way.
    virtual Color pixelAt(int x, int y) = 0;
    /// Cannot be called within a beginDraw()/endDraw() pair.
    virtual std::shared_ptr<Image> copyToImage() = 0;

    virtual Font::Metrics fontMetrics(const Font& font) const = 0;

    /// Returns the metrics for a single line of text
    virtual TextMetrics textMetrics(const char *textUTF8, const Font& font,
                                    PaintMode mode = kPaintFill) const = 0;

    /// Multiplies point by the current transformation matrix and returns
    /// the point in context pixel coordinates. Note that the pixel coordinates
    /// are native to the underlying operating system not portable. In fact, they
    /// may even be different between a bitmap and a window on the same system.
    virtual void calcContextPixel(const Point& point, float *x, float *y) = 0;

protected:
    DrawContext(void *nativeDC, int width, int height, float dpi, float nativeDPI);

    void setInitialState();

protected:
    // This is void* so that we don't pull in platform header files.
    void *mNativeDC;
    float mDPI;
    float mNativeDPI;
    int mWidth;
    int mHeight;
};

} // namespace $ND_NAMESPACE
#endif // _NATIVE_DRAW_H
