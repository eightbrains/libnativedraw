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
#include <pango/pangocairo.h>

#include <iostream>

#include <assert.h>

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

class TextObj : public TextLayout
{
public:
    TextObj(const char *utf8, cairo_t *cairoContext, float dpi, const Font& font,
            const Color& fillColor,
            const Color& strokeColor, const PicaPt& strokeWidth,
            const PicaPt& width, int alignment)
    {
        static const int kNullTerminated = -1;

        mDPI = dpi;
        mFillColor = fillColor;
        mStrokeColor = strokeColor;
        mStrokeWidth = strokeWidth;
        mFont = font;
        mIsEmptyText = (utf8 == nullptr || *utf8 == '\0');
        // pango_cairo_create_layout() creates a new PangoContext for every
        // layout which is not ideal.
        mLayout = pango_layout_new(gPangoContext.context());
        pango_layout_set_text(mLayout, utf8, kNullTerminated);
        PangoFontInfo* pf = gFontMgr.get(font, dpi);
        pango_layout_set_font_description(mLayout, pf->fontDescription);

        if (width > PicaPt::kZero) {
            pango_layout_set_width(mLayout, int(std::ceil(width.toPixels(dpi) * PANGO_SCALE)));
        }
        switch (alignment & Alignment::kHorizMask) {
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
    }

    ~TextObj()
    {
        g_object_unref(mLayout);
    }

    const Color& fillColor() const { return mFillColor; }
    const Color& strokeColor() const { return mStrokeColor; }
    const PicaPt& strokeWidth() const { return mStrokeWidth; }
    const Font& font() const { return mFont; }
    PangoLayout* pangoLayout() const { return mLayout; }

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
            PangoLayoutIter *it = pango_layout_get_iter(mLayout);
            bool isEmpty = (pango_layout_iter_get_run(it) == NULL &&
                            pango_layout_iter_at_last_line(it));
            PangoRectangle logical;
            float invPangoScale = 1.0f / float(PANGO_SCALE);
            if (!isEmpty) {
                PangoLayoutLine *lastLine = pango_layout_get_line(mLayout, 0);
                do {
                    int textIdx = pango_layout_iter_get_index(it);
                    bool lastGlyphWasSpace = false;
                    PangoLayoutLine *line = pango_layout_iter_get_line(it);
                    if (!mGlyphs.empty()) {
                        mGlyphs.back().indexOfNext = textIdx;
                        lastGlyphWasSpace = (mGlyphs.back().frame.width == PicaPt::kZero);
                    }
                    // The logical rectangle is the entire line height, and
                    // also is non-zero width/height for spaces. The ink
                    // rectangle only contains pixels that were inked, so is
                    // not the line height high, and is zero-size for spaces.
                    pango_layout_iter_get_cluster_extents(it, nullptr, &logical);
                    Rect r(PicaPt::fromPixels(float(logical.x) * invPangoScale, mDPI),
                           PicaPt::fromPixels(float(logical.y) * invPangoScale, mDPI),
                           PicaPt::fromPixels(float(logical.width) * invPangoScale, mDPI),
                           PicaPt::fromPixels(float(logical.height) * invPangoScale, mDPI));
                    mGlyphs.emplace_back(textIdx, r);
                } while(pango_layout_iter_next_cluster(it));
            }
            pango_layout_iter_free(it);
            if (!mGlyphs.empty()) {
                int nLines = pango_layout_get_line_count(mLayout);
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

private:
    PangoLayout *mLayout;
    float mDPI;
    Color mFillColor;
    Color mStrokeColor;
    PicaPt mStrokeWidth;
    Font mFont;  // cannot be a ref, as cannot guarantee it will remain valid
    bool mIsEmptyText;

    mutable TextMetrics mMetrics;
    mutable bool mMetricsValid = false;

    mutable std::vector<Glyph> mGlyphs;
    mutable bool mGlyphsValid = false;
};

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

    std::shared_ptr<TextLayout> createTextLayout(
                const char *utf8, const Font& font, const Color& color,
                const PicaPt& width = PicaPt::kZero,
                int alignment = Alignment::kLeft) const override
    {
        auto *gc = cairoContext();
        return std::make_shared<TextObj>(utf8, gc, dpi(), font, color,
                                         Color::kTransparent, PicaPt::kZero,
                                         width, alignment);
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
        auto &font = text->font();

        auto *gc = cairoContext();
        cairo_save(gc);
        setCairoSourceColor(gc, text->fillColor());
        auto fm = fontMetrics(font);
        cairo_move_to(gc,
                      topLeft.x.toPixels(mDPI),
                      std::floor(topLeft.y.toPixels(mDPI)));
        //pango_cairo_update_context(gc, gPangoContext.context());
        //pango_cairo_update_layout(gc, text->pangoLayout());
        pango_cairo_show_layout(gc, text->pangoLayout());
        if (text->strokeWidth() > PicaPt::kZero) {
            setCairoSourceColor(gc, text->strokeColor());
            cairo_set_line_width(gc, text->strokeWidth().toPixels(mDPI));
            // _show_layout() has upper left of text at origin, but
            // _layout_path() has baseline at origin.
            cairo_move_to(gc,
                          0.0,
                          -(fm.ascent - fm.capHeight).toPixels(mDPI));
            //pango_cairo_update_context(gc, gPangoContext.context());
            //pango_cairo_update_layout(gc, text->pangoLayout());
            pango_cairo_layout_path(gc, text->pangoLayout());
            cairo_stroke(gc);
        }
        cairo_restore(gc);
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
        if (!fontInfo->metricsInitialized) {
            auto *gc = cairoContext();

            auto *metrics = pango_context_get_metrics(gPangoContext.context(),
                                                   fontInfo->fontDescription,
                                                   pango_language_get_default());
            if (metrics) {
                fontInfo->metrics.ascent = PicaPt::fromPixels(float(pango_font_metrics_get_ascent(metrics)) / float(PANGO_SCALE), mDPI);
                fontInfo->metrics.descent = PicaPt::fromPixels(float(pango_font_metrics_get_descent(metrics)) / float(PANGO_SCALE), mDPI);
                pango_font_metrics_unref(metrics);

                // Pango's font metrics only provides ascent and descent, we
                // need to calculate cap-height, x-height and leading. It's not
                // clear how to calculate a consistent leading, so just set it
                // to zero (which many fonts do anyway).
                fontInfo->metrics.leading = PicaPt::kZero;

                // cap-height is for flat letters (H,I not A,O which may extend
                // above)
                TextObj caps("H", gc, mDPI, font, Color::kBlack,
                             Color::kTransparent, PicaPt::kZero,
                             PicaPt::kZero, Alignment::kLeft);
                fontInfo->metrics.capHeight = caps.inkExtents().height;
                // x-height is obviously height of "x"
                TextObj x("x", gc, mDPI, font, Color::kBlack,
                          Color::kTransparent, PicaPt::kZero,
                          PicaPt::kZero, Alignment::kLeft);
                fontInfo->metrics.xHeight = x.inkExtents().height;
            } else {
                fontInfo->metrics.ascent = PicaPt(0);
                fontInfo->metrics.descent = PicaPt(0);
                fontInfo->metrics.leading = PicaPt(0);
                fontInfo->metrics.capHeight = PicaPt(0);
                fontInfo->metrics.xHeight = PicaPt(0);
            }

            fontInfo->metrics.lineHeight = fontInfo->metrics.ascent + fontInfo->metrics.descent + fontInfo->metrics.leading;

            fontInfo->metricsInitialized = true;
        }
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

    std::shared_ptr<Image> copyToImage() override
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
                return TextObj(textUTF8, gc, dpi(), font, Color::kTransparent,
                               state.strokeColor, state.strokeWidth,
                               PicaPt::kZero, Alignment::kLeft);
            case kPaintFill:
                return TextObj(textUTF8, gc, dpi(), font, state.fillColor,
                               Color::kTransparent, PicaPt::kZero,
                               PicaPt::kZero, Alignment::kLeft);
            case kPaintStrokeAndFill:
                return TextObj(textUTF8, gc, dpi(), font, state.fillColor,
                               state.strokeColor, state.strokeWidth,
                               PicaPt::kZero, Alignment::kLeft);
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
