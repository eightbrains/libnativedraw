//-----------------------------------------------------------------------------
// Copyright 2021 - 2023 Eight Brains Studios, LLC
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

#if defined(__unix__) && !defined(__APPLE__) && !(__EMSCRIPTEN__)
#include "nativedraw.h"
#include "nativedraw_private.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>
#include <cairo/cairo.h>
#include <cairo/cairo-pdf.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo-xlib-xrender.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <iostream>

#include <assert.h>

#define kDebugDraw	0

namespace ND_NAMESPACE {

namespace {

void printError(const std::string& message)
{
    std::cerr << "[ERROR] " << message << std::endl;
}

// Cairo image functions store data at uint32, not as 4 uint8_t, so endianness
// matters. Due to the success of Intel x86, a big-endian machine is unlikely
// but in that event it would be nice to have a warning. (If we do implement
// big-endian support we can remove this.)
bool verifyLittleEndian() 
{
    assert(isLittleEndian());
    if (!isLittleEndian()) {
        printError("This machine is big-endian; images will not work correctly");
        return false;
    }
    return true;
}
// The global is just so that we can ensure that the function runs automatically
const bool gPlatformIsOk = verifyLittleEndian();

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

//----------------------------- Gradients -------------------------------------
class CairoGradient;
// this does not own the gradients, it is just a mapping
std::unordered_map<Gradient::Id, CairoGradient*> gGradientId2Gradient;

class CairoGradient : public Gradient
{
public:
    CairoGradient(const GradientInfo& info, bool makeValid = true)
        : mInfo(info)
    {
        if (makeValid) {
            mId = gNextId++;
            assert(gGradientId2Gradient.find(mId) == gGradientId2Gradient.end());
            gGradientId2Gradient[mId] = this;
        } else {
            mId = 0;
        }
    }

    ~CairoGradient()
    {
        if (mLinearGradient) {
            cairo_pattern_destroy(mLinearGradient);
            mLinearGradient = nullptr;
        }
        for (auto &g : mRadialGradients) {
            cairo_pattern_destroy(g.second);
        }
        auto it = gGradientId2Gradient.find(mId);
        if (it != gGradientId2Gradient.end()) {
            gGradientId2Gradient.erase(it);
        }
    }

    bool isValid() const override
    {
        return (!mLinearGradient ||
                cairo_pattern_status(mLinearGradient) == CAIRO_STATUS_SUCCESS);
    }

    Id id() const override { return mId; }

    cairo_pattern_t* linearPattern()
    {
        if (!mLinearGradient) {
            mLinearGradient = cairo_pattern_create_linear(0.0, 0.0, 1.0, 0.0);
            for (const auto &stop : mInfo.stops) {
                cairo_pattern_add_color_stop_rgba(mLinearGradient,
                                                  stop.location,
                                                  stop.color.red(),
                                                  stop.color.green(),
                                                  stop.color.blue(),
                                                  stop.color.alpha());
            }
        }
        return mLinearGradient;
    }

    // It is assumed that endRadius=1.0, so startRadius should be in [0.0, 1.0].
    // This way we can simply scale to the actual endRadius size and everything
    // will work out perfectly. It also lets use re-use gradients that use
    // identical ratios (most probably start=0.0).
    cairo_pattern_t* radialPattern(float startRadius)
    {
        assert(startRadius >= 0.0f && startRadius <= 1.0f);
        // The other platforms are fine with startRadius = 1.0, but cairo
        // (or at least how we're using it) draws nothing.
        startRadius = std::min(startRadius, 0.999f);
        

        auto it = mRadialGradients.find(startRadius);
        if (it == mRadialGradients.end()) {
            auto *gradient = cairo_pattern_create_radial(0.0, 0.0, startRadius,
                                                         0.0, 0.0, 1.0);
            for (const auto &stop : mInfo.stops) {
                cairo_pattern_add_color_stop_rgba(gradient,
                                                  stop.location,
                                                  stop.color.red(),
                                                  stop.color.green(),
                                                  stop.color.blue(),
                                                  stop.color.alpha());
            }
            mRadialGradients[startRadius] = gradient;
            it = mRadialGradients.find(startRadius);
        }
        return it->second;
    }

    const GradientInfo& info() const { return mInfo; }
    
private:
    static Id gNextId;

    Id mId;
    cairo_pattern_t *mLinearGradient = nullptr;
    std::unordered_map<float, cairo_pattern_t *> mRadialGradients;
    GradientInfo mInfo;
};
Gradient::Id CairoGradient::gNextId = 1;

CairoGradient* createGradient(const GradientInfo& info, float /*dpi*/)
{
    return new CairoGradient(info);
}

void destroyGradient(CairoGradient* gradient)
{
    delete gradient;
}

// This owns the gradient
static ResourceManager<GradientInfo, CairoGradient*> gGradientMgr(createGradient, destroyGradient);

//---------------------------------- Font -------------------------------------
std::vector<std::string> Font::availableFontFamilies()
{
    int nFamilies;
    PangoFontFamily **pffFamilies;
    std::vector<std::string> families;

    PangoFontMap *fontmap = pango_cairo_font_map_get_default();
    pango_font_map_list_families(fontmap, &pffFamilies, &nFamilies);
    families.reserve(nFamilies);
    for (int i = 0; i < nFamilies; i++) {
        auto *familyName = pango_font_family_get_name(pffFamilies[i]);
        families.emplace_back(familyName);
    }
    g_free(pffFamilies);

    std::sort(families.begin(), families.end());
    return families;
}

//---------------------------------- Fonts ------------------------------------
namespace {

// Pango/Cairo always returns a font, so "" is the default.
static const Font kDefaultReplacementFont("", PicaPt(12.0f));
// Note that Color::kBlack is static and may not exist yet!
static const Color kDefaultReplacementColor(0.0f, 0.0f, 0.0f);
    
// This class exists so that the PangoContext will be automatically destroyed.
// This is not strictly necessary, as exiting would obviously free it,
// but it does prevent unnecesary noise in leak detectors.
class TextContext
{
public:
    TextContext()
    {
        mContext = pango_font_map_create_context(pango_cairo_font_map_get_default());
    }

    ~TextContext()
    {
        g_object_unref(mContext);
    }

    PangoContext* context() { return mContext; }

private:
    PangoContext *mContext = nullptr;
};
TextContext gPangoContext;

struct PangoFontInfo
{
public:
    PangoFontDescription *fontDescription = nullptr;
    bool metricsInitialized = false;
    Font::Metrics metrics;
};

static PangoFontInfo* createFont(const Font& font, float dpi)
{
    auto *desc = pango_font_description_new();
    pango_font_description_set_family(desc, font.family().c_str());
    pango_font_description_set_style(desc,
                            (font.style() & kStyleItalic) ? PANGO_STYLE_ITALIC
                                                          : PANGO_STYLE_NORMAL);
    pango_font_description_set_weight(desc, PangoWeight(font.weight()));
    pango_font_description_set_size(desc,
    // Pango appears to assume the DPI is 96.0, so therefore the conversion
    // of one pica-pt is 96 pixels instead of 72. To undo that, multiply
    // the 72 dpi value by 72/96 = 0.75.
                                    int(std::round(0.75f * font.pointSize().toPixels(dpi) * float(PANGO_SCALE))));

    auto *info = new PangoFontInfo();
    info->fontDescription = desc;

    auto *metrics = pango_context_get_metrics(gPangoContext.context(),
                                              info->fontDescription,
                                              pango_language_get_default());
    if (metrics) {
        info->metrics.ascent = PicaPt::fromPixels(float(pango_font_metrics_get_ascent(metrics)) / float(PANGO_SCALE), dpi);
        info->metrics.descent = PicaPt::fromPixels(float(pango_font_metrics_get_descent(metrics)) / float(PANGO_SCALE), dpi);
        info->metrics.underlineOffset = PicaPt::fromPixels(float(-pango_font_metrics_get_underline_position(metrics)) / float(PANGO_SCALE), dpi);
        info->metrics.underlineThickness = PicaPt::fromPixels(float(pango_font_metrics_get_underline_thickness(metrics)) / float(PANGO_SCALE), dpi);
        pango_font_metrics_unref(metrics);

        // Pango's font metrics only provides ascent and descent, we
        // need to calculate cap-height, x-height and leading. It's not
        // clear how to calculate a consistent leading, so just set it
        // to zero (which many fonts do anyway).
        info->metrics.leading = PicaPt::kZero;


        // cap-height is for flat letters (H,I but not A,O, etc. which
        // may extend above)
        PangoRectangle ink;
        auto *layout = pango_layout_new(gPangoContext.context());
        pango_layout_set_text(layout, "H", -1 /* null terminated*/);
        pango_layout_set_font_description(layout, info->fontDescription);
        pango_layout_get_pixel_extents(layout, &ink, nullptr);
        g_object_unref(layout);
        info->metrics.capHeight = PicaPt::fromPixels(ink.height, dpi);

        // x-height is obviously height of "x"
        layout = pango_layout_new(gPangoContext.context());
        pango_layout_set_text(layout, "x", -1 /* null terminated*/);
        pango_layout_set_font_description(layout, info->fontDescription);
        pango_layout_get_pixel_extents(layout, &ink, nullptr);
        g_object_unref(layout);
        info->metrics.xHeight = PicaPt::fromPixels(ink.height, dpi);

        info->metrics.lineHeight = info->metrics.ascent + info->metrics.descent + info->metrics.leading;

        info->metricsInitialized = true;
    } else {
        info->metrics.ascent = PicaPt::kZero;
        info->metrics.descent = PicaPt::kZero;
        info->metrics.leading = PicaPt::kZero;
        info->metrics.capHeight = PicaPt::kZero;
        info->metrics.xHeight = PicaPt::kZero;
    }
    return info;
}

static void destroyFont(PangoFontInfo *fontResource)
{
    pango_font_description_free(fontResource->fontDescription);
    delete fontResource;
}

static ResourceManager<Font, PangoFontInfo*> gFontMgr(createFont, destroyFont);

} // namespace

//-------------------------------- Text Obj------------------------------------
namespace {

static constexpr float kInvPangoScale = 1.0f / float(PANGO_SCALE);

// Pango supports attributes and will chunk text into runs, but PangoCairo
// offers no way of drawing except with one set of attributes, which makes this
// feature almost useless. Also, Pango's attributes do not support colors with
// alpha < 1.0, nor do they support stroked text. This class is basically a
// record of what needs to be drawn (like a Windows meta-file), and is done so
// that we need not need to keep TextRuns around to draw, since they are pretty
// heavyweight from a memory perspective, and lots of text is rather common in
// a UI.
class DrawPangoText
{
public:
    enum Cmd { kPointData, kSizeData, kSetFG, kDrawRect, kDrawText, kStrokedText,
               kStroke, kDoubleStroke, kDottedStroke, kWavyStroke };
    // The idea is to keep this fairly small. On 64-bit systems the glyph
    // pointer will be the largest, and we can fit two floats into 64-bits.
    // It would be more convenient to have strokes and rects have the four
    // floats they need rather than have to assume another item in the draw
    // stack, but this way reduces memory.
    struct Command {
        Cmd cmd;
        union {
            int rgba;
            PangoGlyphItem *run;
            struct {
                float x;
                float y;
            } pt;
            struct {
                float w;
                float h;
            } size;
            struct {
                float w;
            } stroke;
        } arg;
    };

    DrawPangoText(float dpi) : mDPI(dpi) {}

    void setOffset(float offsetX, float offsetY)
    {
        mXOffset = offsetX;
        mYOffset = offsetY;
    }

    void addColor(int rgba)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kSetFG;
        mCmds.back().arg.rgba = rgba;
    }

    void addRect(const PangoRectangle& pr)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kDrawRect;
        mCmds.back().arg.pt.x = mXOffset + pr.x * kInvPangoScale;
        mCmds.back().arg.pt.y = mYOffset + pr.y * kInvPangoScale;
        mCmds.emplace_back();
        mCmds.back().cmd = kSizeData;
        mCmds.back().arg.size.w = pr.width * kInvPangoScale;
        mCmds.back().arg.size.h = pr.height * kInvPangoScale;
    }

    void addText(PangoGlyphItem *run, float pgX, float pgBaselineY)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kDrawText;
        mCmds.back().arg.run = run;
        mCmds.emplace_back();
        mCmds.back().cmd = kPointData;
        mCmds.back().arg.pt.x = mXOffset + pgX * kInvPangoScale;
        mCmds.back().arg.pt.y = mYOffset + pgBaselineY * kInvPangoScale;
    }

    void addStrokedText(PangoGlyphItem *run, float pgX, float pgBaselineY,
                        float pgThickness)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kStrokedText;
        mCmds.back().arg.stroke.w = pgThickness * kInvPangoScale;
        mCmds.emplace_back();
        mCmds.back().cmd = kDrawText;
        mCmds.back().arg.run = run;
        mCmds.emplace_back();
        mCmds.back().cmd = kPointData;
        mCmds.back().arg.pt.x = mXOffset + pgX * kInvPangoScale;
        mCmds.back().arg.pt.y = mYOffset + pgBaselineY * kInvPangoScale;
    }

    void addLine(Cmd type, float pgX0, float pgY0, float pgX1, float pgY1,
                 float pgThickness)
    {
        float strokeWidth = pgThickness * kInvPangoScale;
        float pxYAlign = 0.0f;
        if (strokeWidth < 1.5f && strokeWidth > 0.75f) {
            pxYAlign = 0.5f;
        }
        mCmds.emplace_back();
        mCmds.back().cmd = type;
        mCmds.back().arg.stroke.w = strokeWidth;
        mCmds.emplace_back();
        mCmds.back().cmd = kPointData;
        mCmds.back().arg.pt.x = mXOffset + pgX0 * kInvPangoScale;
        mCmds.back().arg.pt.y = mYOffset + pgY0 * kInvPangoScale + pxYAlign;
        mCmds.emplace_back();
        mCmds.back().cmd = kPointData;
        mCmds.back().arg.pt.x = mXOffset + pgX1 * kInvPangoScale;
        mCmds.back().arg.pt.y = mYOffset + pgY1 * kInvPangoScale + pxYAlign;
    }

    void draw(cairo_t *gc) const
    {
#if kDebugDraw
        std::cout << "[debug] draw()" << std::endl;
#endif

        cairo_save(gc);

        size_t i = 0;
        while (i < mCmds.size()) {
            auto &cmd = mCmds[i];
            ++i;
            switch (cmd.cmd) {
                case kSetFG: {
                    double r = double((cmd.arg.rgba & 0xff000000) >> 24) / 255.0;
                    double g = double((cmd.arg.rgba & 0x00ff0000) >> 16) / 255.0;
                    double b = double((cmd.arg.rgba & 0x0000ff00) >> 8) / 255.0;
                    double a = double(cmd.arg.rgba & 0x000000ff) / 255.0;
                    cairo_set_source_rgba(gc, r, g, b, a);
#if kDebugDraw
                    std::cout << "[debug]   set fg: " << r << ", " << g << ", "
                              << b << ", " << a << std::endl;
#endif
                    break;
                }
                case kDrawRect: {
                    auto origin = cmd;
                    auto size = mCmds[i++];
                    cairo_new_path(gc);
                    cairo_rectangle(gc, origin.arg.pt.x, origin.arg.pt.y,
                                    size.arg.size.w, size.arg.size.h);
                    cairo_fill(gc);
#if kDebugDraw
                    std::cout << "[debug]   draw rect: " << origin.arg.pt.x
                              << ", " << origin.arg.pt.y << ", "
                              << size.arg.size.w << ", " << size.arg.size.h
                              << std::endl;
#endif
                    break;
                }
                case kStroke:
                case kDoubleStroke:
                case kDottedStroke: {
                    auto p0 = mCmds[i++];
                    auto p1 = mCmds[i++];
#if kDebugDraw
                    std::cout << "[debug]   line: ("
                              << p0.arg.pt.x << ", " << p0.arg.pt.y << ") - ("
                              << p1.arg.pt.x << ", " << p1.arg.pt.y << ")"
                              << std::endl;
#endif
                    cairo_new_path(gc);
                    cairo_move_to(gc, p0.arg.pt.x, p0.arg.pt.y);
                    cairo_line_to(gc, p1.arg.pt.x, p1.arg.pt.y);
                    if (cmd.cmd == kDoubleStroke) {
                        cairo_move_to(gc, p0.arg.pt.x,
                                      p0.arg.pt.y + 2.0f * cmd.arg.stroke.w);
                        cairo_line_to(gc, p1.arg.pt.x,
                                      p1.arg.pt.y + 2.0f * cmd.arg.stroke.w);
                    } else if (cmd.cmd == kDottedStroke) {
                        double x = p0.arg.pt.x;
                        double y = p0.arg.pt.y;
                        cairo_user_to_device(gc, &x, &y);
                        double pattern = 10.0f * 72.0f / mDPI;
                        double offset = x / pattern;
                        offset = offset - std::floor(offset);
                        cairo_set_dash(gc, &pattern, 1, offset);
                    }
                    cairo_set_line_width(gc, cmd.arg.stroke.w);
                    cairo_stroke(gc);
                    if (cmd.cmd == kDottedStroke) {
                        cairo_set_dash(gc, nullptr, 0, 0.0);
                    }
                    break;
                }
                case kWavyStroke: {
                    auto p0 = mCmds[i++];
                    auto p1 = mCmds[i++];
#if kDebugDraw
                    std::cout << "[debug]   wavy line: ("
                              << p0.arg.pt.x << ", " << p0.arg.pt.y << ") - ("
                              << p1.arg.pt.x << ", " << p1.arg.pt.y << ")"
                              << std::endl;
#endif
                    double realX = p0.arg.pt.x;
                    double realY = p0.arg.pt.y;
                    cairo_user_to_device(gc, &realX, &realY);

                    auto pts = createWavyLinePoints(p0.arg.pt.x, p0.arg.pt.y,
                                                    p1.arg.pt.x,
                                                    cmd.arg.stroke.w);

                    cairo_new_path(gc);
                    cairo_move_to(gc, pts[0], pts[1]);
                    for (size_t i = 2;  i < pts.size();  i += 2) {
                        cairo_line_to(gc, pts[i], pts[i + 1]);
                    }
                    cairo_set_line_width(gc, cmd.arg.stroke.w);
                    cairo_stroke(gc);
                    break;
                }
                case kDrawText: {
                    auto pt = mCmds[i++];
                    auto *font = cmd.arg.run->item->analysis.font;
                    cairo_translate(gc, pt.arg.pt.x, pt.arg.pt.y);
                    pango_cairo_show_glyph_string(gc, font, cmd.arg.run->glyphs);
                    cairo_translate(gc, -pt.arg.pt.x, -pt.arg.pt.y);
#if kDebugDraw
                    auto *dbg = pango_font_describe(font);
                    auto *dbg2 = pango_font_description_to_string(dbg);
                    std::cout << "[debug]   draw text: " << pt.arg.pt.x << ", "
                              << pt.arg.pt.y << ": " << dbg2 << std::endl;
                    g_free(dbg2);
                    pango_font_description_free(dbg);
#endif
                    break;
                }
                case kStrokedText: {
                    auto text = mCmds[i++];
                    auto pt = mCmds[i++];
                    auto *font = text.arg.run->item->analysis.font;
                    cairo_translate(gc, pt.arg.pt.x, pt.arg.pt.y);
                    pango_cairo_glyph_string_path(gc, font, text.arg.run->glyphs);
                    cairo_set_line_width(gc, cmd.arg.stroke.w);
                    cairo_stroke(gc);
                    cairo_translate(gc, -pt.arg.pt.x, -pt.arg.pt.y);
#if kDebugDraw
                    auto *dbg = pango_font_describe(font);
                    auto *dbg2 = pango_font_description_to_string(dbg);
                    std::cout << "[debug]   stroke text: " << pt.arg.pt.x << ", "
                              << pt.arg.pt.y << ": " << dbg2 << std::endl;
                    g_free(dbg2);
                    pango_font_description_free(dbg);
#endif
                    break;
                }
                case kPointData:  // data, nothing to do
                case kSizeData:
                    break;
            }
        }

        cairo_restore(gc);
#if kDebugDraw
        std::cout << "[debug] done drawing" << std::endl;
#endif
    }

private:
    std::vector<Command> mCmds;
    float mDPI;
    float mXOffset = 0.0f;
    float mYOffset = 0.0f;
};

class TextObj : public TextLayout
{
public:
    TextObj(const DrawContext& dc, cairo_t *cairoContext,
            const Color& strokeColor, const PicaPt& strokeWidth,
            const Text& text, const Size& size, int alignment,
            TextWrapping wrap = kWrapWord,
            const Font& defaultReplacementFont = kDefaultReplacementFont,
            const Color& defaultReplacementColor = kDefaultReplacementColor)
        : mDraw(dc.dpi())
    {
        static const int kNullTerminated = -1;

#if kDebugDraw
        std::cout << "[debug] TextObj(" << text.text() << ")" << std::endl;
#endif

        mDPI = dc.dpi();
        mIsEmptyText = text.text().empty();
        mHasEmptyLastLine = (mIsEmptyText || text.text().back() == '\n');
        // Note that pango_cairo_create_layout() creates a new PangoContext
        // for every layout, which is why we are not using it.
        mLayout = pango_layout_new(gPangoContext.context());
        pango_layout_set_text(mLayout, text.text().c_str(), kNullTerminated);

        if (size.width > PicaPt::kZero && wrap != kWrapNone) {
            pango_layout_set_width(mLayout, int(std::ceil(size.width.toPixels(mDPI) * PANGO_SCALE)));
            pango_layout_set_wrap(mLayout, PANGO_WRAP_WORD_CHAR);
        }
        switch (alignment & Alignment::kHorizMask) {
            default:
            case Alignment::kLeft:
                pango_layout_set_alignment(mLayout, PANGO_ALIGN_LEFT);
                break;
            case Alignment::kHCenter:
                pango_layout_set_alignment(mLayout, PANGO_ALIGN_CENTER);
                break;
            case Alignment::kRight:
                pango_layout_set_alignment(mLayout, PANGO_ALIGN_RIGHT);
                break;
        }

        // Since PangoCairo does not actually offer any way to draw a layout
        // with multiple attributes, we need to do it ourselves. So we assign
        // the attributes that will make a different to the layout (e.g. font,
        // letter spacing), and then assign a "color" value that is an index
        // into the array of TextRuns, for later.
        std::vector<Font::Metrics> runMetrics;
        std::vector<int> runBaselinePangoOffsets;
        runMetrics.reserve(text.runs().size());
        runBaselinePangoOffsets.reserve(text.runs().size());
        std::vector<PangoAttribute*> attrs;
        attrs.reserve(2 * text.runs().size());
        for (size_t i = 0;  i < text.runs().size();  ++i) {
            auto &run = text.runs()[i];
            assert(run.font.isSet);
            assert(run.color.isSet);
            bool hasSuperscript = (run.superscript.isSet && run.superscript.value);
            bool hasSubscript = (run.subscript.isSet && run.subscript.value);
            int baselineOffsetPango = 0;  // up is positive
            Font font = run.font.value;
            if (!run.font.isSet || isFamilyDefault(font)) {
                font.setFamily(defaultReplacementFont.family());
                if (isPointSizeDefault(font)) {
                    font.setPointSize(defaultReplacementFont.pointSize());
                }
            }
            if (run.pointSize.isSet) {
                font.setPointSize(run.pointSize.value);
            }
            if (run.bold.isSet) {
                font.setBold(run.bold.value);
            }
            if (run.italic.isSet) {
                font.setItalic(run.italic.value);
            }
            // For purposes of calculating the first line ascent, we want
            // the font metrics before changing the size for super/subscript.
            runMetrics.push_back(font.metrics(dc));
            {
                PangoFontInfo *pf = gFontMgr.get(font, mDPI);
                if (hasSuperscript || hasSubscript) {
                    font = fontSizedForSuperSubscript(font);
                    PangoFontInfo *pfSmall = gFontMgr.get(font, mDPI);
                    if (hasSuperscript) {
                        baselineOffsetPango = int(std::round((pf->metrics.capHeight - pfSmall->metrics.capHeight).toPixels(mDPI) * float(PANGO_SCALE)));
                    } else if (hasSubscript) {
                        baselineOffsetPango = -int(std::round((pf->metrics.descent- pfSmall->metrics.descent).toPixels(mDPI) * float(PANGO_SCALE)));
                    }
                    pf = pfSmall;
                    auto *a = pango_attr_rise_new(baselineOffsetPango);
                    a->start_index = run.startIndex;
                    a->end_index = run.startIndex + run.length;
                    attrs.push_back(a);
                }
                auto *a = pango_attr_font_desc_new(pf->fontDescription);
                a->start_index = run.startIndex;
                a->end_index = run.startIndex + run.length;
                attrs.push_back(a);
            }
            runBaselinePangoOffsets.push_back(baselineOffsetPango);

            if (run.characterSpacing.isSet && run.characterSpacing.value != PicaPt::kZero) {
                // TODO: maybe Pango assumes 96 DPI (see Font above)?
                auto spacing = int(std::round(run.characterSpacing.value.toPixels(mDPI) * PANGO_SCALE));
                auto *a = pango_attr_letter_spacing_new(spacing);
                a->start_index = run.startIndex;
                a->end_index = run.startIndex + run.length;
                attrs.push_back(a);
            }

            {
            guint16 r = guint16(i & 0x0000ffff);
            guint16 g = guint16((i & 0xffff0000) >> 16);
            auto *a = pango_attr_foreground_new(r, g, 0);
            a->start_index = run.startIndex;
            a->end_index = run.startIndex + run.length;
            attrs.push_back(a);
            }
        }
        assert(runMetrics.size() == runBaselinePangoOffsets.size());

        if (text.lineHeightMultiple() > 0.0f) {
#if PANGO_VERSION_CHECK(1, 44, 0)
            // This takes into account the line height (given by the largest
            // font in the line).
            pango_layout_set_line_spacing(mLayout, text.lineHeightMultiple());
#else
            // This is just a constant space between the lines. It will only
            // be correct if the fonts are the same height in all the runs,
            // (which at least is the common case).
            if (!runMetrics.empty()) {
                float spacing = (text.lineHeightMultiple() - 1.0f) * runMetrics[0].lineHeight.toPixels(mDPI) * float(PANGO_SCALE);
                pango_layout_set_spacing(mLayout, spacing);
            }
#endif
        }

        if (text.indent() != PicaPt::kZero) {
            pango_layout_set_indent(mLayout, text.indent().toPixels(mDPI) * PANGO_SCALE);
        }

        if (!attrs.empty()) {
            auto *attrList = pango_attr_list_new();
            for (auto *a : attrs) {
                // "insert" means "append" here, "insertBefore" is "insert"
                pango_attr_list_insert(attrList, a);
            }
            pango_layout_set_attributes(mLayout, attrList);
            pango_attr_list_unref(attrList);
        }

        // Calculate drawing offset
        //     The Pango documentation is ... sparse. If you happen across the
        // pango_font_get_glyph_extents() documentation, you will be informed
        // that the origin of a glyph rectangle is y = baseline. This also
        // appears to be what the pango-cairo command to draw text uses as y=0.
        // The pango_layout_iter functions return extents relative to the layout
        // coord (which includes the offsets due to alignment), while the other
        // pango_layout_* get_extents functions return coordinates relative to
        // the item's parents. Therefore, one should always use the iter
        // functions if you want accurate extents; you do not want to try keeping
        // track of all these coordinate offsets yourself, major pain.
        Font::Metrics firstLineMetrics;
        if (!runMetrics.empty()) {
            firstLineMetrics = calcFirstLineMetrics(runMetrics, text.runs());
        } else {
            firstLineMetrics = defaultReplacementFont.metrics(dc);
        }
        mAlignmentOffset = calcOffsetForAlignment(alignment, size,
                                                  firstLineMetrics);
        // If we are not wrapping, we need to do the horiz bit ourselves
        if (wrap == kWrapNone && size.width > PicaPt::kZero) {
            if (alignment & Alignment::kHCenter) {
                auto tm = metrics();
                mAlignmentOffset.x += 0.5f * (size.width - tm.width);
            } else if (alignment & Alignment::kRight) {
                auto tm = metrics();
                mAlignmentOffset.x += (size.width - tm.width);
            }
        }
        mDraw.setOffset(mAlignmentOffset.x.toPixels(mDPI),
                        mAlignmentOffset.y.toPixels(mDPI));

        // Create the draw commands.
        PangoRectangle lineExtents;
        int currentColor = 0;  // transparent black
        PangoLayoutIter *it = pango_layout_get_iter(mLayout);
        do {
            PangoLayoutRun *run = pango_layout_iter_get_run(it);
            if (run) {  // end of line always has a NULL run
                auto *attrs = run->item->analysis.extra_attrs;
                unsigned int runIdx = -1;
                while (attrs) {
                    auto *attr = (PangoAttribute*)attrs->data;
                    if (attr->klass->type == PANGO_ATTR_FOREGROUND) {
                        auto *colorAttr = (PangoAttrColor*)attr;
                        unsigned int lower = (unsigned int)(colorAttr->color.red);
                        unsigned int upper = ((unsigned int)(colorAttr->color.green)) << 16;
                        runIdx = upper | lower;
                        break;
                    }
                    attrs = attrs->next;
                }
                assert(runIdx >= 0);
                auto &textRun = text.runs()[runIdx];

                bool bgSet = (textRun.backgroundColor.isSet
                              && textRun.backgroundColor.value.alpha() > 0.0f);
                bool underlineSet = (textRun.underlineStyle.isSet
                    && textRun.underlineStyle.value != kUnderlineNone
                    && !(textRun.underlineColor.isSet
                         && textRun.underlineColor.value.alpha() == 0.0f));
                bool strikethroughSet = (textRun.strikethrough.isSet
                                         && textRun.strikethrough.value);
                PangoRectangle extents;
                PangoFont *pgfont = run->item->analysis.font;
                pango_layout_iter_get_run_extents(it, nullptr, &extents);
                int pgBaseline = pango_layout_iter_get_baseline(it);

                if (underlineSet || strikethroughSet) {
                    PangoFont *pgfont = run->item->analysis.font;
                    PangoLanguage *pglang = run->item->analysis.language;
                }

                // If a background color is set, it needs to be drawn first.
                if (bgSet) {
                    auto bgRGBA = textRun.backgroundColor.value.toRGBA();
                    if (bgRGBA != currentColor) {
                        mDraw.addColor(bgRGBA);
                        currentColor = bgRGBA;
                    }
                    mDraw.addRect(extents);
                }

                bool isFgDefault = false;
                Color fg = textRun.color.value;
                if ((textRun.color.value.red() == Color::kTextDefault.red() &&
                     textRun.color.value.green() == Color::kTextDefault.green() &&
                     textRun.color.value.blue() == Color::kTextDefault.blue())) {
                    fg = defaultReplacementColor;
                    fg.setAlpha(textRun.color.value.alpha());
                }
                int fgRGBA = fg.toRGBA();
                if (fgRGBA != currentColor) {
                    mDraw.addColor(fgRGBA );
                    currentColor = fgRGBA ;
                }

                // Draw underline *before* text, so text descenders are on top
                if (underlineSet) {
                    if (textRun.underlineColor.isSet) {
                        int rgba = textRun.underlineColor.value.toRGBA();
                        if (rgba != currentColor) {
                            mDraw.addColor(rgba);
                            currentColor = rgba;
                        }
                    }
                    
                    PangoFont *pgfont = run->item->analysis.font;
                    PangoLanguage *pglang = run->item->analysis.language;
                    PangoFontMetrics *pgmetrics = pango_font_get_metrics(pgfont, pglang);
                    // Note that underline position is *above* the baseline
                    // (so usually negative).
                    auto pgY = pgBaseline - runBaselinePangoOffsets[runIdx] - pango_font_metrics_get_underline_position(pgmetrics);
                    auto pgWidth = pango_font_metrics_get_underline_thickness(pgmetrics);
                    DrawPangoText::Cmd cmd = DrawPangoText::kStroke;
                    switch (textRun.underlineStyle.value) {
                        case kUnderlineNone: // to make compiler happy about enum
                        case kUnderlineSingle:
                            cmd = DrawPangoText::kStroke;
                            break;
                        case kUnderlineDouble:
                            cmd = DrawPangoText::kDoubleStroke;
                            break;
                        case kUnderlineDotted:
                            cmd = DrawPangoText::kDottedStroke;
                            break;
                        case kUnderlineWavy:
                            cmd = DrawPangoText::kWavyStroke;
                            break;
                    }
                    mDraw.addLine(cmd, extents.x, pgY,
                                  extents.x + extents.width, pgY, pgWidth);
                }

                // Draw the actual text (unless transparent)
                if (fgRGBA != currentColor) { // underline might have changed
                    mDraw.addColor(fgRGBA );
                    currentColor = fgRGBA ;
                }
                int pgTextY = pgBaseline;
                if (fg.alpha() > 0.0f) {
                    mDraw.addText(run, extents.x,
                                  pgBaseline - runBaselinePangoOffsets[runIdx]);
                }

                // Draw outlined text
                bool isOutlineSet = (textRun.outlineColor.isSet && textRun.outlineColor.value.alpha() > 0.0f && !(textRun.outlineStrokeWidth.isSet && textRun.outlineStrokeWidth.value == PicaPt::kZero));
                bool outlineOverrideSet = (strokeWidth > PicaPt::kZero && strokeColor.alpha() > 0.0f);
                if (isOutlineSet || outlineOverrideSet) {
                    int rgba;
                    if (textRun.outlineColor.isSet) {
                        rgba = textRun.outlineColor.value.toRGBA();
                    } else {
                        rgba = strokeColor.toRGBA();
                    }
                    if (rgba != currentColor) {
                        mDraw.addColor(rgba);
                        currentColor = rgba;
                    }

                    float thickness;
                    if (textRun.outlineStrokeWidth.isSet) {
                        thickness = textRun.outlineStrokeWidth.value.toPixels(mDPI);
                    } else {
                        thickness = strokeWidth.toPixels(mDPI);
                    }
                    mDraw.addStrokedText(run, extents.x,
                                   pgBaseline - runBaselinePangoOffsets[runIdx],
                                   thickness / kInvPangoScale);
                }

                // Draw strikethroughs *after* text
                if (textRun.strikethroughColor.isSet && textRun.strikethroughColor.value.alpha() > 0.0f) {
                    int rgba = textRun.strikethroughColor.value.toRGBA();
                    if (rgba != currentColor) {
                        mDraw.addColor(rgba);
                        currentColor = rgba;
                    }
                }
                if (strikethroughSet) {
                    PangoFont *pgfont = run->item->analysis.font;
                    PangoLanguage *pglang = run->item->analysis.language;
                    PangoFontMetrics *pgmetrics = pango_font_get_metrics(pgfont, pglang);
                    auto pgY = pgBaseline - runBaselinePangoOffsets[runIdx] - pango_font_metrics_get_strikethrough_position(pgmetrics);
                    auto pgWidth = pango_font_metrics_get_strikethrough_thickness(pgmetrics);
                    mDraw.addLine(DrawPangoText::kStroke,
                                  extents.x, pgY,
                                  extents.x + extents.width, pgY, pgWidth);
                }
            }
        } while(pango_layout_iter_next_run(it));
        pango_layout_iter_free(it);

        // So this is kind of hacky: calcFirstLineMetrics *might* have created
        // the glyphs in order to find line boundaries. We need to deallocate
        // them (see the comment for TextLayout in the header file). Also, they
        // will have been created without any alignment offsets (since that was
        // what we were computing).
        if (mGlyphsValid) {
            mGlyphs.clear();
            mGlyphs.shrink_to_fit();  // clear() does not release memory
            mGlyphsValid = false;
        }
    }

    ~TextObj()
    {
        g_object_unref(mLayout);
    }

    // This only used to calculate the font metrics that aren't included
    // Pango's extremely limited set.
    Size inkExtents() const
    {
        // Note that we want the ink rectangle, because we will be calculating
        // cap-height and x-height, and we only want the height of what's
        // actually been inked. (The logical rectangle is the entire em-height.)
        PangoRectangle ink;
        pango_layout_get_pixel_extents(mLayout, &ink, nullptr);
        return Size(PicaPt::fromPixels(ink.width, mDPI),
                    PicaPt::fromPixels(ink.height, mDPI));
    }

    const TextMetrics& metrics() const override
    {
        if (!mMetricsValid) {
            if (!mIsEmptyText) {
                int w, h;
                pango_layout_get_pixel_size(mLayout, &w, &h);
                mMetrics.width = PicaPt::fromPixels(w, mDPI);
                mMetrics.height = PicaPt::fromPixels(h, mDPI);
                mMetrics.advanceX = mMetrics.width;

                if (mHasEmptyLastLine) {
                    auto &chars = glyphs();
                    if (chars.size() <= 1) {
                        mMetrics.height = PicaPt::kZero;
                    } else {
                        auto &backBack = chars[chars.size() - 2];
                        if (chars.back().line == backBack.line) {
                            mMetrics.height = backBack.frame.maxY();
                        } else {
                            mMetrics.height = chars.back().frame.maxY();
                        }
                    }
                }

                if (pango_layout_get_line_count(mLayout) > 1) {
                    mMetrics.advanceY = mMetrics.height;
                } else {
                    mMetrics.advanceY = PicaPt::kZero;
                }
            }
            mMetricsValid = true;
        }
        return mMetrics;
    }

    const std::vector<Glyph>& glyphs() const override
    {
        if (!mGlyphsValid) {
            assert(mGlyphs.empty());

            // This is unnecessarily complicated because it is not clear how
            // to get glyph extents out of the run. It might be possible with
            // 1.50, but that is newer than we would like to support (as of
            // this writing), and it is not clear that it supports it, anyway.
            // However, iterating by cluster skips newlines, so we need a lot
            // of logic to put them in.
            PangoLayoutIter *it = pango_layout_get_iter(mLayout);
            PangoLayoutLine *lastLine = nullptr;
            int currentLineNo = -1;
            int nLines = pango_layout_get_line_count(mLayout);
            bool isEmpty = (pango_layout_iter_get_run(it) == NULL &&
                            pango_layout_iter_at_last_line(it));
            PangoRectangle logical;
            const PangoLayoutRun *lastRun = nullptr;
            const float invPangoScale = 1.0f / float(PANGO_SCALE);
            if (!isEmpty) {
                do {
                    int textIdx = pango_layout_iter_get_index(it);
                    bool lastGlyphWasSpace = false;
                    PangoLayoutLine *line = pango_layout_iter_get_line(it);
                    lastRun = pango_layout_iter_get_run_readonly(it);
#if PANGO_VERSION_CHECK(1, 50, 0)
                    int baseline = pango_layout_iter_get_run_baseline(it);
#else
                    int baseline = pango_layout_iter_get_baseline(it);  // baseline of LINE (incorrect for sub/super-script)
#endif
                    auto baselinePt = PicaPt::fromPixels(float(baseline) * invPangoScale, mDPI);
                    if (line != lastLine) {
                        if (lastLine) {
                            int lastLineEndIdx = lastLine->start_index + lastLine->length;
                            if (lastLineEndIdx < line->start_index) {
                                int idx = lastLineEndIdx;
                                PicaPt lastY = mAlignmentOffset.y;
                                // The layout iterator doesn't include glyphs for
                                // \n characters, including if there are blanks
                                // lines (e.g. "...\n\n..."). The PangoLayoutLine
                                // includes a run of NULL, though. (It _does_
                                // include glyphs for spaces, and there is a
                                // test for trailing CJK spaces.)
                                if (idx < line->start_index) {
                                    Rect r;
                                    if (!mGlyphs.empty()) {
                                        mGlyphs.back().indexOfNext = idx;
                                    }
                                    if (!mGlyphs.empty() && idx == lastLineEndIdx) {
                                        r = mGlyphs.back().frame;
                                        r.x = r.maxX();
                                    } else {
                                        pango_layout_iter_get_cluster_extents(it, nullptr, &logical);
                                        r = Rect(mAlignmentOffset.x, mAlignmentOffset.y, PicaPt::kZero, PicaPt::fromPixels(float(logical.height) * invPangoScale, mDPI));
                                    }
                                    r.width = PicaPt::kZero;
                                    mGlyphs.emplace_back(lastLineEndIdx, currentLineNo, baselinePt, r);

                                    lastY = mGlyphs.back().frame.maxY();
                                    idx++;
                                }
                                while (idx < line->start_index) {
                                    ++currentLineNo;
                                    Rect r;
                                    r.x = PicaPt::kZero;
                                    if (!mGlyphs.empty()) {
                                        r.y = mGlyphs.back().frame.maxY();
                                        r.height = mGlyphs.back().frame.height;
                                        mGlyphs.back().indexOfNext = idx;
                                    }
                                    r.width = PicaPt::kZero;
                                    mGlyphs.emplace_back(idx, currentLineNo, baselinePt, r);
                                    idx++;
                                }
                            }
                        }
                        ++currentLineNo;
                        lastLine = line;
                    }
                    if (!mGlyphs.empty()) {
                        mGlyphs.back().indexOfNext = textIdx;
                        lastGlyphWasSpace = (mGlyphs.back().frame.width == PicaPt::kZero);
                    }
                    // The logical rectangle is the entire line height, and
                    // also is non-zero width/height for spaces. The ink
                    // rectangle only contains pixels that were inked, so is
                    // not the line height high, and is zero-size for spaces.
                    pango_layout_iter_get_cluster_extents(it, nullptr, &logical);
                    Rect r(PicaPt::fromPixels(float(logical.x) * invPangoScale, mDPI) + mAlignmentOffset.x,
                           PicaPt::fromPixels(float(logical.y) * invPangoScale, mDPI) + mAlignmentOffset.y,
                           PicaPt::fromPixels(float(logical.width) * invPangoScale, mDPI),
                           PicaPt::fromPixels(float(logical.height) * invPangoScale, mDPI));
                    mGlyphs.emplace_back(textIdx, currentLineNo, baselinePt, r);
                } while(pango_layout_iter_next_cluster(it));
            }
            pango_layout_iter_free(it);

            // Add glyph for trailing \n's (if any)
            bool isEmptyFirstLine = (lastLine && lastLine->start_index == 0 && !lastLine->runs);
            if (currentLineNo >= 0 && currentLineNo < nLines - 1 && !isEmptyFirstLine) {
                if (!mGlyphs.empty()) {
                    auto r = mGlyphs.back().frame;
                    r.x = r.maxX();
                    r.width = PicaPt::kZero;
                    auto baseline = r.y + 0.8f * r.height;  // approx; this is zero-width anyway
                    mGlyphs.back().indexOfNext = mGlyphs.back().index + 1;
                    mGlyphs.emplace_back(mGlyphs.back().indexOfNext, currentLineNo, baseline, r);
                    ++currentLineNo;
                } else {
                    // Must be a \n at begining; handle below
                    // (Except this never actually happens, because the line
                    // actually exists even though the cluster is skipped, so it
                    // gets added on the main path)
                }
            }
            while (currentLineNo >= 0 && currentLineNo < nLines - 1 && !isEmptyFirstLine) {
                PangoLayoutLine *line = pango_layout_get_line(mLayout, currentLineNo);
                pango_layout_line_get_extents(line, nullptr, &logical);
                auto y = PicaPt::kZero;
                auto baseline = PicaPt::kZero;
                if (!mGlyphs.empty()) {
                    y = mGlyphs.back().frame.maxY();
                }
                Rect r(PicaPt::fromPixels(float(logical.x) * invPangoScale, mDPI) + mAlignmentOffset.x,
                       y,
                       PicaPt::kZero,
                       PicaPt::fromPixels(float(logical.height) * invPangoScale, mDPI));
                if (lastRun && lastRun->item && lastRun->item->analysis.font) {
                    auto *fm = pango_font_get_metrics(lastRun->item->analysis.font,
                                                      lastRun->item->analysis.language);
                    auto ascent = PicaPt::fromPixels(float(pango_font_metrics_get_ascent(fm)) * invPangoScale, mDPI);
                    auto descent = PicaPt::fromPixels(float(pango_font_metrics_get_descent(fm)) * invPangoScale, mDPI);
                    auto leading = r.height - (ascent + descent);
                    r.y += leading;
                    r.height = ascent + descent;
                    baseline = r.y + ascent;
                    pango_font_metrics_unref(fm);
                }
                if (!mGlyphs.empty()) {
                    mGlyphs.back().indexOfNext = line->start_index;
                }
                ++currentLineNo;
                mGlyphs.emplace_back(line->start_index, currentLineNo, baseline, r);
            }

            if (!mGlyphs.empty()) {
                if (nLines > 0) {
                    // Find last index. Maybe it would be quicker to use strlen?
                    PangoLayoutLine *line = pango_layout_get_line(mLayout,
                                                                  nLines - 1);
                    mGlyphs.back().indexOfNext = line->start_index + line->length;
                }
            }

            mGlyphsValid = true;
        }

        return mGlyphs;
    }

    void draw(cairo_t *gc) const { mDraw.draw(gc); }

private:
    PangoLayout *mLayout;
    DrawPangoText mDraw;
    float mDPI;
    Point mAlignmentOffset;
    bool mIsEmptyText;
    bool mHasEmptyLastLine;

    mutable TextMetrics mMetrics;
    mutable bool mMetricsValid = false;

    mutable std::vector<Glyph> mGlyphs;
    mutable bool mGlyphsValid = false;
};

} // namespace

//-------------------------------- Image ---------------------------------------
uint8_t* createNativeCopy(const uint8_t *data, int width, int height,
                          ImageFormat format, cairo_format_t *cairoFormat,
                          ImageFormat *nativeFormat)
{
    // Cairo's native format is mostly premultiplied ARGB32
    // **in native endian uint32** (!?). In practice, since most systems
    // will be little-endian (since what all the software uses) this will be
    // the BGRA32 that all the other platforms use. There is an assert at
    // the beginning of this file ensuring that the platform actually is
    // little-endian (no need to run it every call).

    // Since Cairo's enum is confusing, we are going to redefine them
    const cairo_format_t BGRA32 = CAIRO_FORMAT_ARGB32;
    const cairo_format_t BGRX32 = CAIRO_FORMAT_RGB24;

    // Cairo has stride requirements, but we only use the 32-bit formats
    // so hopefully the alignment requirements of the strides happen to
    // be 32-bit boundaries (that is, a scanline has no padding).
    // Check this assumption here.
    // TODO: this probably isn't the case for alpha8
    assert(cairo_format_stride_for_width(BGRA32, width) == 4 * width);

    cairo_format_t nullPF;
    if (!cairoFormat) {
        cairoFormat = &nullPF;
    }
    ImageFormat nullFormat;
    if (!nativeFormat) {
        nativeFormat = &nullFormat;
    }

    *cairoFormat = BGRA32;
    *nativeFormat = kImageBGRA32_Premultiplied;
    uint8_t *nativeCopy = nullptr;
    switch (format) {
        case kImageRGBA32:
            nativeCopy = createBGRAFromRGBA(data, width, height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageRGBA32_Premultiplied:
            nativeCopy = createBGRAFromRGBA(data, width, height);
            break;
        case kImageBGRA32:
            nativeCopy = new uint8_t[4 * width * height];
            memcpy(nativeCopy, data, 4 * width * height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageBGRA32_Premultiplied:
            nativeCopy = new uint8_t[4 * width * height];
            memcpy(nativeCopy, data, 4 * width * height);
            break;  // this is native
        case kImageARGB32:
            nativeCopy = createBGRAFromARGB(data, width, height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageARGB32_Premultiplied:
            nativeCopy = createBGRAFromARGB(data, width, height);
            break;
        case kImageABGR32:
            nativeCopy = createBGRAFromABGR(data, width, height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageABGR32_Premultiplied:
            nativeCopy = createBGRAFromABGR(data, width, height);
            break;
        case kImageRGBX32:
            *cairoFormat = BGRX32;
            *nativeFormat = kImageBGRX32;
            nativeCopy = createBGRAFromRGBA(data, width, height);
            break;
        case kImageBGRX32:
            *cairoFormat = BGRX32;
            *nativeFormat = kImageBGRX32;
            nativeCopy = new uint8_t[4 * width * height];
            memcpy(nativeCopy, data, 4 * width * height);
            break;  // this is native
        case kImageRGB24:
            *cairoFormat = BGRX32;
            *nativeFormat = kImageBGRX32;
            nativeCopy = createBGRAFromRGB(data, width, height);
            break;
        case kImageBGR24:
            *cairoFormat = BGRX32;
            *nativeFormat = kImageBGRX32;
            nativeCopy = createBGRAFromBGR(data, width, height);
            break;
        case kImageGreyscaleAlpha16: {
            nativeCopy = createBGRAFromGreyAlpha(data, width, height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        }
        case kImageGreyscale8:
            *cairoFormat = BGRX32;
            *nativeFormat = kImageBGRX32;
            nativeCopy = createBGRAFromGrey(data, width, height);
            break;
        case kImageEncodedData_internal:
            assert(false);
            break;
    }
    assert(nativeCopy);
    return nativeCopy;
}

Image Image::fromFile(const char *file)
{
    auto bytes = readFile(file);
    if (!bytes.empty()) {
        return fromEncodedData(bytes.data(), bytes.size());
    } else {
        return Image();
    }
}

Image Image::fromEncodedData(const uint8_t *encodedImage, int length)
{
    return readImage(encodedImage, length);
}

Image Image::fromCopyOfBytes(const uint8_t *bytes, int w, int h,
                             ImageFormat f, float dpi /*= 0*/)
{
    ImageFormat nativeFormat;
    uint8_t *bgra = createNativeCopy(bytes, w, h, f, nullptr, &nativeFormat);
    size_t size = size_t(calcPixelBytes(f) * w * h);
    return Image(bgra /* takes ownership */, size, w, h, nativeFormat,
                 (dpi != 0 ? dpi : kDefaultImageDPI));
}

//------------------------------ DrawImage -------------------------------------
struct CairoImageData
{
    int width;
    int height;
    uint8_t *bgra;

    CairoImageData(uint8_t *b, int w, int h)
        : width(w), height(h), bgra(b)
    {}

    ~CairoImageData()
    {
        delete [] bgra;
    }
};

class CairoImage : public DrawableImage
{
public:
    CairoImage(cairo_surface_t *nativeHandle, int width, int height, float dpi)
        : DrawableImage(nativeHandle, width, height, dpi)
    {}

    CairoImage(cairo_surface_t *nativeHandle,
               std::unique_ptr<CairoImageData>&& image, float dpi)
        : DrawableImage(nativeHandle, image->width, image->height, dpi)
        , mData(std::move(image))
    {}

    virtual ~CairoImage()
    {
        cairo_surface_destroy((cairo_surface_t*)nativeHandle());
        mNativeHandle = nullptr;
        // mData destructs itself
    }

protected:
    std::unique_ptr<CairoImageData> mData;
};

//--------------------------- CairoDrawContext --------------------------------
class CairoDrawContext : public DrawContext
{
protected:
    enum class DrawingState { kNotDrawing = 0, kDrawing };

    mutable DrawingState mDrawingState;

public:
    CairoDrawContext(void *nativeDC, int width, int height, float dpi)
        : DrawContext(nativeDC, width, height, dpi, dpi)
    {
        mDrawingState = DrawingState::kNotDrawing;
        if (nativeDC) {
            setNativeDC((cairo_t*)nativeDC);
        }
    }


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

    std::shared_ptr<TextLayout> createTextLayout(
                const char *utf8, const Font& font, const Color& color,
                const Size& size /*= Size::kZero*/,
                int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                TextWrapping wrap /*= kWrapWord*/) const override
    {
        auto *gc = cairoContext();
        Text text(utf8, font, color);
        return std::make_shared<TextObj>(*this, gc,
                                         Color::kTransparent, PicaPt::kZero,
                                         text, size, alignment, wrap);
    }

    std::shared_ptr<TextLayout> createTextLayout(
                const Text& t,
                const Size& size /*= Size::kZero*/,
                int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                TextWrapping wrap /*= kWrapWord*/) const override
    {
        auto *gc = cairoContext();
        return std::make_shared<TextObj>(*this, gc,
                                         Color::kTransparent, PicaPt::kZero,
                                         t, size, alignment, wrap);
    }

    std::shared_ptr<TextLayout> createTextLayout(
                const Text& t,
                const Font& defaultReplacementFont,
                const Color& defaultReplacementColor,
                const Size& size /*= Size::kZero*/,
                int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                TextWrapping wrap /*= kWrapWord*/) const override
    {
        auto *gc = cairoContext();
        return std::make_shared<TextObj>(*this, gc,
                                         Color::kTransparent, PicaPt::kZero,
                                         t, size, alignment, wrap,
                                         defaultReplacementFont,
                                         defaultReplacementColor);
    }

    std::shared_ptr<DrawableImage> createDrawableImage(const Image& image) const override
    {
        // The cairo surface externalizes the data, so we have to keep it
        // around for the duration of the surface. This means we need to
        // always copy.
        cairo_format_t pixelFormat;
        int width = image.widthPx();
        int height = image.heightPx();
        uint8_t* nativeCopy = createNativeCopy(image.data(), width, height,
                                               image.format(),
                                               &pixelFormat, nullptr);

        cairo_surface_t *surf = cairo_image_surface_create_for_data((uint8_t*)nativeCopy, pixelFormat, width, height, 4 * width);

        if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
            // ImageData(uint8_t*, int, int) takes ownership of the pointer
            // The actual format does not matter, it is only needed when reading
            // from a file.
            auto data = std::make_unique<CairoImageData>(
                                                     nativeCopy, width, height);
            return std::make_shared<CairoImage>(surf, std::move(data),
                                                image.dpi());
        } else {
            cairo_surface_destroy(surf);  // surf always exists, needs destroy
            delete [] nativeCopy;
            return std::make_shared<CairoImage>(nullptr, 0, 0, 0.0f);
        }
    }

    Gradient& getGradient(const std::vector<Gradient::Stop>& stops)
    {
        return *gGradientMgr.get({ this, stops }, mDPI);
    }

    Gradient& getGradient(size_t id) const
    {
        static CairoGradient gBad(GradientInfo(), false /* make invalid */);

        auto it = gGradientId2Gradient.find(id);
        if (it != gGradientId2Gradient.end()) {
            return *it->second;
        }
        return gBad;
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

    Color fillColor() const
    {
        return mStateStack.back().fillColor;
    }

    Color strokeColor() const
    {
        return mStateStack.back().strokeColor;
    }

    PicaPt strokeWidth() const
    {
        return mStateStack.back().strokeWidth;
    }

    EndCapStyle strokeEndCap() const
    {
        return mStateStack.back().endCapStyle;
    }

    JoinStyle strokeJoinStyle() const
    {
        return mStateStack.back().joinStyle;
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
        mStateStack.back().strokeWidth = w;
        cairo_set_line_width(cairoContext(), w.toPixels(mDPI));
    }

    void setStrokeEndCap(EndCapStyle cap) override
    {
        mStateStack.back().endCapStyle = cap;
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
        mStateStack.back().joinStyle = join;
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

    void drawLinearGradientPath(std::shared_ptr<BezierPath> path,
                                Gradient& gradient,
                                const Point& start, const Point& end)
    {
        auto dx = (end.x - start.x).toPixels(mDPI);
        auto dy = (end.y - start.y).toPixels(mDPI);
        auto dist = std::sqrt(dx * dx + dy * dy);
        auto rotationRad = std::atan2(dy, dx);

        // if dist == 0 the gradient will be invisible (and the matrix is not
        // invertible)
        if (dist < 1e-6) {
            return;
        }

        save();
        clipToPath(path);

        // We created the brush with the gradient going from (0, 0) to (1, 0).
        // So we can avoid creating different brushes (which presumably creates
        // a gradient bitmap for each one) for each start/end that uses the same
        // stops by just transforming the draw matrix so that drawing from
        // (0, 0) to (1, 0) gives us the desired result.
        translate(start.x, start.y);
        scale(dist, dist);
        rotate(-rotationRad * 180.0f / 3.14159265f);

        drawGradient(((CairoGradient&)gradient).linearPattern());
        restore();
    }

    void drawRadialGradientPath(std::shared_ptr<BezierPath> path,
                                Gradient& gradient,
                                const Point& center, const PicaPt& startRadius,
                                const PicaPt& endRadius)
    {
        save();
        clipToPath(path);

        translate(center.x, center.y);
        float radiusPx = endRadius.toPixels(mDPI);
        scale(radiusPx, radiusPx);

        auto *pattern = ((CairoGradient&)gradient).radialPattern(startRadius / endRadius);
        drawGradient(pattern);

        restore();
    }

private:
    void drawGradient(cairo_pattern_t *pattern)
    {
        auto *gc = cairoContext();

        // We assume that this is bracketed by save/restore, so that the
        // set_source() is undone
        cairo_set_source(gc, pattern);

        // Draw a rect the size of the entire context using the gradient brush;
        // the clipping will limit to the path.
        double ulX = 0.0, ulY = 0.0;
        double urX = double(mWidth), urY = 0.0;
        double lrX = double(mWidth), lrY = double(mHeight);
        double llX = 0.0, llY = double(mHeight);

        // We need to transform the four corners of the context-rect so that
        // they are in the same coordinate system as the current transform
        // matrix. To do that, we transform by the inverse of the transform
        // matrix. Happily, this is exactly what cairo_device_to_user() does.
        cairo_device_to_user(gc, &ulX, &ulY);
        cairo_device_to_user(gc, &urX, &urY);
        cairo_device_to_user(gc, &lrX, &lrY);
        cairo_device_to_user(gc, &llX, &llY);

        // Draw the full-context rect. Note that this might be rotated, so
        // we cannot just draw a rectangle from ul to lr.
        cairo_new_path(gc);
        cairo_move_to(gc, ulX, ulY);
        cairo_line_to(gc, urX, urY);
        cairo_line_to(gc, lrX, lrY);
        cairo_line_to(gc, llX, llY);
        cairo_close_path(gc);

        cairo_fill(gc);
    }

public:
    void drawText(const char *textUTF8, const Point& topLeft, const Font& font, PaintMode mode) override
    {
        drawText(layoutFromCurrent(textUTF8, font, mode), topLeft);
    }

    void drawText(const TextLayout& layout, const Point& topLeft) override
    {
        // This can only be our TextObj, but we need to cast, otherwise we
        // have to add a virtual function, which then starts putting our
        // internals in the definition (even if we make it protected, it still
        // needs to be in the class declaration).
        const TextObj *text = static_cast<const TextObj*>(&layout);

        auto *gc = cairoContext();
        cairo_save(gc);
        cairo_translate(gc, 
                        topLeft.x.toPixels(mDPI),
                        std::floor(topLeft.y.toPixels(mDPI)));
        text->draw(gc);
        cairo_restore(gc);
    }

    void drawImage(std::shared_ptr<DrawableImage> image, const Rect& destRect) override
    {
        auto *gc = cairoContext();
        save();
        translate(destRect.x, destRect.y);
        float destWidthPx = destRect.width.toPixels(mDPI);
        float destHeightPx = destRect.height.toPixels(mDPI);
        float sx = destWidthPx / image->widthPx();
        float sy = destHeightPx / image->heightPx();
        scale(sx, sy);
        cairo_set_source_surface(gc, (cairo_surface_t*)image->nativeHandle(),
                                 0.0, 0.0);
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
        PangoFontInfo* fontInfo = gFontMgr.get(font, mDPI);
        return fontInfo->metrics;
    }

    TextMetrics textMetrics(const char *textUTF8, const Font& font,
                            PaintMode mode /*=kPaintFill*/) const
    {
        return layoutFromCurrent(textUTF8, font, mode).metrics();
    }

    Color pixelAt(int x, int y) override
    {
        assert(false);  // need a bitmap context
        return Color::kPurple;
    }

    std::shared_ptr<DrawableImage> copyToImage() override
    {
        assert(false);  // not implemented
        return nullptr;
    }

protected:
    inline cairo_t* cairoContext() const { return (cairo_t*)mNativeDC; }

    TextObj layoutFromCurrent(const char *textUTF8, const Font& font,
                              PaintMode mode) const
    {
        auto *gc = cairoContext();
        auto &state = mStateStack.back();
        switch(mode) {
            case kPaintStroke:
                return TextObj(*this, gc,
                               state.strokeColor, state.strokeWidth,
                               Text(textUTF8, font, Color::kTransparent),
                               Size::kZero, Alignment::kLeft);
            case kPaintFill:
                return TextObj(*this, gc,
                               Color::kTransparent, PicaPt::kZero,
                               Text(textUTF8, font, state.fillColor),
                               Size::kZero, Alignment::kLeft);
            case kPaintStrokeAndFill:
                return TextObj(*this, gc,
                               state.strokeColor, state.strokeWidth,
                               Text(textUTF8, font, state.fillColor),
                               Size::kZero, Alignment::kLeft);
        }
    }

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
        PicaPt strokeWidth;
        EndCapStyle endCapStyle;
        JoinStyle joinStyle;
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

    std::shared_ptr<DrawableImage> copyToImage() override
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
class CairoPDF : public CairoDrawContext
{
    using Super = CairoDrawContext;
private:
    cairo_surface_t *mSurface;
    cairo_t *mDC;

public:
    CairoPDF(const char *filename, int width, int height, float dpi)
        : CairoDrawContext(nullptr, width, height, dpi)
    {
        mSurface = cairo_pdf_surface_create(filename,
                              double(PicaPt::fromPixels(width, dpi).asFloat()),
                              double(PicaPt::fromPixels(height, dpi).asFloat()));
        mDC = cairo_create(mSurface);
        setNativeDC(mDC);
    }

    ~CairoPDF()
    {
        cairo_destroy(mDC);
        cairo_surface_destroy(mSurface);
    }

    void endDraw() override
    {
        Super::endDraw();
        cairo_show_page(mDC);
    }

    void addPage() override
    {
        cairo_show_page(mDC);
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

    std::shared_ptr<DrawableImage> copyToImage() override
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

std::shared_ptr<DrawContext> DrawContext::createCairoPDF(
            const char *filename, int width, int height, float dpi /*= 72.0f*/)
{
    return std::make_shared<CairoPDF>(filename, width, height, double(dpi));
}

} // namespace ND_NAMESPACE

#endif // defined(__unix__) && !defined(__APPLE__)
