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

#if defined(__EMSCRIPTEN__)

#include "nativedraw.h"
#include "nativedraw_private.h"

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <iostream>
#include <set>
#include <sstream>
#include <vector>

using EmVal = emscripten::val;

namespace ND_NAMESPACE {

static float kDefaultLineHeightMultiple = 1.0f;

static float kCSSPixelDPI = 96.0f;
    
class CanvasPath : public BezierPath
{
public:
    void clearNative() override
    {
    }

    void* nativePathForDPI(float dpi, bool isFilled) override
    {
        return nullptr;
    }

    void setPath(EmVal context, float dpi)
    {
        context.call<void>("beginPath");
        for (auto &cmd : mImpl->commands) {
            switch (cmd.cmd) {
                case BezierPath::Impl::Command::kMoveTo:
                    context.call<void>("moveTo",
                        cmd.p1.x.toPixels(dpi), cmd.p1.y.toPixels(dpi));
                    break;
                case BezierPath::Impl::Command::kLineTo:
                    context.call<void>("lineTo",
                        cmd.p1.x.toPixels(dpi), cmd.p1.y.toPixels(dpi));
                    break;
                case BezierPath::Impl::Command::kQuadraticTo:
                    context.call<void>("quadraticCurveTo",
                        cmd.p1.x.toPixels(dpi), cmd.p1.y.toPixels(dpi),
                        cmd.p2.x.toPixels(dpi), cmd.p2.y.toPixels(dpi));
                    break;
                case BezierPath::Impl::Command::kCubicTo:
                    context.call<void>("bezierCurveTo",
                        cmd.p1.x.toPixels(dpi), cmd.p1.y.toPixels(dpi),
                        cmd.p2.x.toPixels(dpi), cmd.p2.y.toPixels(dpi),
                        cmd.p3.x.toPixels(dpi), cmd.p3.y.toPixels(dpi));
                    break;
                case BezierPath::Impl::Command::kClose:
                    context.call<void>("closePath");
                    break;
            }
        }
    }
};

//------------------------------ CanvasGradient -------------------------------
class InvalidGradient : public Gradient
{
public:
    bool isValid() const override { return false; }
    Id id() const override { return 0; }
};

class CanvasGradient : public Gradient
{
public:
    explicit CanvasGradient(const EmVal &context, const GradientInfo& info)
    {
        mId = mNextId++;
        mInfo = info;
        mCanvasContext = context;

        // Automatically create the linear gradient, as we need to create
        // something so that a newly created gradient is valid, and this is
        // most likely to be used.
        mLinearGradient = mCanvasContext.call<EmVal>("createLinearGradient", 0, 0, 1, 0);
        if (mLinearGradient.as<bool>()) {
            for (auto &s : mInfo.stops) {
                mLinearGradient.call<void>("addColorStop", s.location, std::string("#") + s.color.toHexString());
            }
        }
    }

    EmVal& linearGradient() { return mLinearGradient; }

    // It is assumed that endRadius=1.0, so startRadius should be in [0.0, 1.0].
    // This way we can simply scale to the actual endRadius size and everything
    // will work out perfectly. It also lets use re-use gradients that use
    // identical ratios (most probably start=0.0).
    EmVal& radialGradient(float startRadius)
    {
        assert(startRadius >= 0.0f && startRadius <= 1.0f);

        auto it = mRadialGradients.find(startRadius);
        if (it == mRadialGradients.end()) {
            EmVal rgradient = mCanvasContext.call<EmVal>("createRadialGradient",
                                                        0.0f, 0.0f, startRadius, 
                                                        0.0f, 0.0f, 1.0f);
            for (auto &s : mInfo.stops) {
                rgradient.call<void>("addColorStop", s.location, std::string("#")  + s.color.toHexString());
            }
            mRadialGradients[startRadius] = rgradient;
            it = mRadialGradients.find(startRadius);
        }
        return it->second;
    }

    bool isValid() const override
    {
        return (mLinearGradient.as<bool>() == true || !mRadialGradients.empty());
    }

    Id id() const override { return mId; }

    bool ownedByContext(DrawContext *dc) { return mInfo.context == dc; }

private:
    static Id mNextId;

    GradientInfo mInfo;
    Id mId;
    EmVal mCanvasContext;
    EmVal mLinearGradient;
    std::unordered_map<float, EmVal> mRadialGradients;
};
Gradient::Id CanvasGradient::mNextId = 1;

static InvalidGradient gInvalidGradient;
// Use unordered_map so that we can erase and continue iterating
static std::unordered_map<Gradient::Id, CanvasGradient*> gId2Gradients;

CanvasGradient* createGradient(const GradientInfo& info, float /*dpi*/)
{
    auto *g = new CanvasGradient(*(EmVal*)info.context->nativeDC(), info);
    gId2Gradients[g->id()] = g;
    return g;
}

void destroyGradient(CanvasGradient* gradient)
{
    if (gradient) {
        gId2Gradients.erase(gradient->id());
        delete gradient;
    }
}

static ResourceManager<GradientInfo, CanvasGradient*> gGradientMgr(createGradient, destroyGradient);
//--------------------------------- Image --------------------------------------
uint8_t* createNativeCopy(const uint8_t *data, int width, int height,
                          ImageFormat format, ImageFormat *nativeFormat)
{
    // HTML Canvas uses RGBA, unlike the other platforms
    // We can reuse some of the helper functions, size they just move bytes
    // around. Rename them to avoid confusion.
    auto *createRGBAFromBGRA = createBGRAFromRGBA; // swaps first 3 bytes
    auto *createRGBAFromARGB = createBGRAFromABGR; // swaps A from 0 to 3rd byte
    auto *createRGBAFromABGR = createBGRAFromARGB; // reverses bytes
    auto *createRGBAFromRGB = createBGRAFromBGR;   // append A
    auto *createRGBAFromBGR = createBGRAFromRGB;   // reverse, then append A
    auto *createRGBAFromGreyAlpha = createBGRAFromGreyAlpha;  // R=G=B
    auto *createRGBAFromGrey = createBGRAFromGrey; // R=G=B

    ImageFormat nullFormat;
    if (!nativeFormat) {
        nativeFormat = &nullFormat;
    }

    *nativeFormat = kImageRGBA32;
    uint8_t *nativeCopy = nullptr;
    switch (format) {
        case kImageRGBA32:
            nativeCopy = new uint8_t[4 * width * height];
            memcpy(nativeCopy, data, 4 * width * height);
            break;  // this is native
        case kImageRGBA32_Premultiplied:
            nativeCopy = new uint8_t[4 * width * height];
            memcpy(nativeCopy, data, 4 * width * height);
            unpremultiplyRGBA(nativeCopy, width, height);
            break;
        case kImageBGRA32:
            nativeCopy = createRGBAFromBGRA(data, width, height);
            unpremultiplyRGBA(nativeCopy, width, height);
            break;
        case kImageBGRA32_Premultiplied:
            nativeCopy = createRGBAFromBGRA(data, width, height);
            unpremultiplyRGBA(nativeCopy, width, height);
            break;
        case kImageARGB32:
            nativeCopy = createRGBAFromARGB(data, width, height);
            break;
        case kImageARGB32_Premultiplied:
            nativeCopy = createRGBAFromARGB(data, width, height);
            unpremultiplyRGBA(nativeCopy, width, height);
            break;
        case kImageABGR32:
            nativeCopy = createRGBAFromABGR(data, width, height);
            break;
        case kImageABGR32_Premultiplied:
            nativeCopy = createRGBAFromABGR(data, width, height);
            unpremultiplyRGBA(nativeCopy, width, height);
            break;
        case kImageRGBX32: {
            nativeCopy = new uint8_t[4 * width * height];
            memcpy(nativeCopy, data, 4 * width * height);
            uint8_t *end = nativeCopy + 4 * width * height;
            uint8_t *a = nativeCopy + 3;
            while (a < end) {
                *a = 0xff;
                a += 4;
            }
            break;
        }
        case kImageBGRX32: {
            nativeCopy = createRGBAFromBGRA(data, width, height);
            uint8_t *end = nativeCopy + 4 * width * height;
            uint8_t *a = nativeCopy + 3;
            while (a < end) {
                *a = 0xff;
                a += 4;
            }
            break;
        }
        case kImageRGB24:
            nativeCopy = createRGBAFromRGB(data, width, height);
            break;
        case kImageBGR24:
            nativeCopy = createRGBAFromBGR(data, width, height);
            break;
        case kImageGreyscaleAlpha16:
            nativeCopy = createRGBAFromGreyAlpha(data, width, height);
            break;
        case kImageGreyscale8:
            nativeCopy = createRGBAFromGrey(data, width, height);
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
    uint8_t *rgba = createNativeCopy(bytes, w, h, f, &nativeFormat);
    size_t size = size_t(calcPixelBytes(f) * w * h);
    return Image(rgba /* takes ownership */, size, w, h, nativeFormat,
                 (dpi != 0 ? dpi : kDefaultImageDPI));
}

//------------------------------ CanvasImage ----------------------------------
class CanvasImage : public DrawableImage
{
public:
    static std::shared_ptr<CanvasImage> fromImage(const Image& image)
    {
        // not used on this platform
        assert(image.format() != kImageEncodedData_internal);

        EmVal Uint8ClampedArray = EmVal::global("Uint8ClampedArray");
        assert(Uint8ClampedArray.as<bool>() == true);
        EmVal ImageData = EmVal::global("ImageData");
        assert(ImageData.as<bool>() == true);

        auto width = image.widthPx();
        auto height = image.heightPx();
        auto *rgba = createNativeCopy(image.data(), width, height,
                                      image.format(), nullptr);
        // Note that 'array' is a view, and the pointer must be valid for
        // as long as the resulting JS image is alive.
        EmVal array = EmVal(emscripten::typed_memory_view(4 * width * height, rgba));
        EmVal clampedArray = Uint8ClampedArray.new_(array);
        EmVal imageData = ImageData.new_(clampedArray, width, height);

        auto ci = fromJSImageData(imageData, width, height, image.dpi());
        ci->mRGBA = rgba;  // ensure rgba gets deleted when ci destructs

        return ci;
    }

    static std::shared_ptr<CanvasImage> fromJSImageData(EmVal imageData, int width, int height, float dpi)
    {
        EmVal createImageBitmap = EmVal::global("createImageBitmap");
        assert(createImageBitmap.as<bool>() == true);

        EmVal options = EmVal::object();
        options.set("resizeQuality", EmVal("high"));  // default is "low"
        EmVal imageBitmap = createImageBitmap(imageData, options).await();
        auto result = std::make_shared<CanvasImage>(imageBitmap, width, height, dpi);
        return result;
    }

    CanvasImage(EmVal imageBitmap, int width, int height, float dpi)
        : DrawableImage(&mImageBitmap, width, height, dpi)
        , mImageBitmap(imageBitmap)
    {}

    ~CanvasImage()
    {
        // If we created this from an RGBA pointer, we need to delete the pointer
        delete [] mRGBA;
    }

private:
    uint8_t *mRGBA = nullptr;
    EmVal mImageBitmap;
};

//---------------------------------- Font -------------------------------------
static const std::set<std::string> gGenericFonts = {
    "cursive", "emoji", "fantasy", "fangsong", "math", "monospace", "sans-serif",
    "serif", "monospace", "system-ui", "ui-serif", "ui-sans-serif",
    "ui-monospace", "ui-rounded"
};

std::vector<std::string> Font::availableFontFamilies()
{
    // The ui-* families do not seem to work on Firefox or Chrome (unable to
    // test on Safari).
    return { "sans-serif", "serif", "monospace", "cursive", "fantasy",
             "fangsong", "emoji", "math", "system-ui", };
}

//------------------------------- CanvasFont ----------------------------------
class CanvasFont
{
public:
    CanvasFont(const Font& f, float dpi)
    {
        mDPI = dpi;
        mMetrics.ascent = PicaPt::kZero;

        float sizePx = f.pointSize().toPixels(dpi);
        std::stringstream s;
        s << ((f.style() & kStyleItalic) ? "italic " : "")
          << f.weight() << " "
          << sizePx << "px ";
        // If this is a system font it must NOT be quoted (it is a keyword)
        // If it is not a system font we should quote it, in case it has spaces.
        auto it = gGenericFonts.find(f.family());
        if (it != gGenericFonts.end()) {
            s << f.family();
        } else {
            s << "\"" << f.family() << "\"";
        }
        mNativeHandle = EmVal(s.str());
    }

    const EmVal& nativeHandle() const { return mNativeHandle; }

    const Font::Metrics& fontMetrics(EmVal context) const
    {
        if (mMetrics.ascent == PicaPt::kZero) {
            // The canvas does not have a font object, just a string. So getting
            // the font metrics requires altering the state of the canvas.
            // We do not even own the canvas, but doing this in HTMLCanvasContext
            // is problematic because of text layout. Handling text layout
            // requires the font metrics, but they may not exist yet, and passing
            // the HTMLCanvasContext into the text layout object is even worse.
            // Basically the fundamental problem is that HTML does not have a
            // propert font object, and this mess comes from that.
            auto oldFont = context["font"];

            context.set("font", mNativeHandle);
            // js::TextMetrics values are in CSS pixels. We need to convert them
            // to pixels at our dpi.
            auto multiplier = fromCSSToDPIMultiplier();

            auto tm = context.call<EmVal>("measureText", EmVal("Ag"));
            Font::Metrics fm;
            bool usingMetricsFromFont = false; // not "fontBoundingBoxAscent"
            auto ascentVal = tm["actualBoundingBoxAscent"];
            auto descentVal = tm["actualBoundingBoxDescent"];
            fm.ascent = PicaPt::fromPixels(ascentVal.as<float>(), kCSSPixelDPI) * multiplier;
            fm.descent = PicaPt::fromPixels(descentVal.as<float>(), kCSSPixelDPI) * multiplier;
            if (usingMetricsFromFont) {
                fm.leading = PicaPt::kZero;
            } else {
                // Since we are using the measurements of actual pixels rendered,
                // we need to add some leading, otherwise the lines will have
                // no space and will look too crowded.
                fm.leading = 0.5f * fm.descent;
            }
            fm.capHeight = PicaPt::fromPixels(tm["actualBoundingBoxAscent"].as<float>(), kCSSPixelDPI) * multiplier;
            fm.lineHeight = fm.ascent + fm.descent + fm.leading;
            fm.underlineOffset = 0.15f * (fm.ascent + fm.descent);
            fm.underlineThickness = 0.05f * (fm.ascent + fm.descent);

            tm = context.call<EmVal>("measureText", EmVal("x"));
            fm.xHeight = PicaPt::fromPixels(tm["actualBoundingBoxAscent"].as<float>(), kCSSPixelDPI) * multiplier;

            mMetrics = fm;

            context.set("font", oldFont);
        }
        return mMetrics;
    }

    float fromCSSToDPIMultiplier() const
    {
        return kCSSPixelDPI / mDPI;
    }

private:
    float mDPI;
    EmVal mNativeHandle;  // so we don't keep re-copying this
    mutable Font::Metrics mMetrics;
};

CanvasFont* createFont(const Font& f, float dpi)
{
    auto *cf = new CanvasFont(f, dpi);
    return cf;
}

void destroyFont(CanvasFont *cf)
{
    delete cf;
}

static ResourceManager<Font, CanvasFont*> gFontMgr(createFont, destroyFont);

//-------------------------------- TextObj ------------------------------------
class TextObj : public TextLayout
{
public:
    TextObj(EmVal& context, /* not saved, only for font metrics */
            float dpi,
            const Text& text, const Size& size,
            int alignment,
            TextWrapping wrap = kWrapWord,
            Font defaultReplacementFont = kDefaultReplacementFont,
            const Color& defaultReplacementColor = kDefaultReplacementColor)
    {
        mDPI = dpi;
        mContextForGlyphs = context;  // needed for glyph(), sigh
        // ^--- finished essential initialization

        // In case anyone passes Font() to defaultReplacement font,
        // replace with kDefaultFont
        if (defaultReplacementFont.family().empty()) {
            auto pointSize = defaultReplacementFont.pointSize();
            if (pointSize > PicaPt::kZero) {
                defaultReplacementFont = kDefaultReplacementFont.fontWithPointSize(pointSize);
            } else {
                defaultReplacementFont = kDefaultReplacementFont;
            }
        }

        auto oldFont = context["font"];

        PicaPt firstLineIndent = PicaPt::kZero;
        PicaPt nonFirstLineIndent = PicaPt::kZero;
        if (text.indent() != PicaPt::kZero && (alignment & int(Alignment::kLeft))) {
            if (text.indent() > PicaPt::kZero) {
                firstLineIndent = text.indent();
            } else {
                nonFirstLineIndent = -text.indent();
            }
        }

        // Find all the \n's
        std::vector<int> newlineIndices;
        const char *str = text.text().c_str();
        const char *s = str;
        do {
            s = strchr(s, '\n');
            if (s) {
                newlineIndices.push_back(s - str);
                s++;
                if (!*s) {
                    s = nullptr;
                }
            }
        } while (s);

        // Create array of the runs, subdivided by any \n
        struct SubRun {
            const TextRun *run;  // ptr, not ref, so we can copy for insert()
            int startIndex;
            int length;
            bool isNewline;
        };
        std::vector<SubRun> subruns;
        subruns.reserve(text.runs().size());
        if (newlineIndices.empty()) {  // most common path; keep simple and fast
            if (!text.text().empty()) {
                for (auto &run : text.runs()) {
                    subruns.emplace_back(SubRun{ &run, run.startIndex, run.length, false });
                }
            }
        } else {
            const int kMaxBreakIdx = 1e9;
            size_t nliIdx = 0;
            int nextBreakIdx = kMaxBreakIdx;
            if (!newlineIndices.empty()) {
                nextBreakIdx = newlineIndices[nliIdx];
            }
            for (auto &run : text.runs()) {
                if (run.startIndex + run.length <= nextBreakIdx) {
                    subruns.emplace_back(SubRun{ &run, run.startIndex, run.length, false });
                } else {
                    int idx = run.startIndex;
                    int endIdx = run.startIndex + run.length;
                    while (idx < endIdx) {
                        if (nextBreakIdx > idx) {
                            subruns.emplace_back(SubRun{ &run, idx, std::min(nextBreakIdx, endIdx) - idx, false });
                            idx = nextBreakIdx;
                        }
                        if (idx < endIdx) {
                            assert(idx == nextBreakIdx);
                            subruns.emplace_back(SubRun{ &run, idx, 1, true });
                            idx += 1;
                            nliIdx += 1;
                            if (nliIdx < newlineIndices.size()) {
                                nextBreakIdx = newlineIndices[nliIdx];
                            } else {
                                nextBreakIdx = kMaxBreakIdx;
                            }
                        }
                    }
                }
            }
        }

        auto calcBaseFont = [defaultFont=defaultReplacementFont](const TextRun& run) -> Font {
            Font font = run.font.value;
            if (!run.font.isSet || isFamilyDefault(font)) {
                font.setFamily(defaultFont.family());
                if (isPointSizeDefault(font)) {
                    font.setPointSize(defaultFont.pointSize());
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
            return font;
        };

        // Now calculate the lines
        bool wantWrap = (wrap != kWrapNone && size.width > PicaPt::kZero);
        PicaPt x = firstLineIndent, y;
        CanvasFont *currentCanvasFont = nullptr;
        PicaPt currentCharSpacing = PicaPt::kZero;
        PicaPt leadingForLine;
        if (!subruns.empty()) {
            mLines.emplace_back();
            mLines.back().lineRect.x = x;
            mLines.back().lineRect.y = y;
        }
        float lineHeightMultiple = text.lineHeightMultiple();
        if (lineHeightMultiple == 0.0f) {
            lineHeightMultiple = kDefaultLineHeightMultiple;
        }
        for (size_t srunIdx = 0;  srunIdx < subruns.size();  ++srunIdx) {
            auto srun = subruns[srunIdx];  // copy (so, valid if subruns changes)
            auto &run = *srun.run;
            const bool isNewline = srun.isNewline;
            Font font = calcBaseFont(run);
            bool hasSuperscript = (run.superscript.isSet && run.superscript.value);
            bool hasSubscript = (run.subscript.isSet && run.subscript.value);
            if (hasSuperscript || hasSubscript) {
                font = fontSizedForSuperSubscript(font);
            }
            PicaPt charSpacing = PicaPt::kZero;
            if (run.characterSpacing.isSet) {
                charSpacing = run.characterSpacing.value;
            }

            CanvasFont *cf = gFontMgr.get(font, dpi);
            if (currentCanvasFont != cf) {
                currentCanvasFont = cf;
                context.set("font", cf->nativeHandle());
            }
            if (charSpacing != currentCharSpacing) {
                currentCharSpacing = charSpacing;
                context.set("letterSpacing", std::to_string(charSpacing.toPixels(dpi)) + "px");
            }

            PicaPt w;
            if (!isNewline && srun.length > 0 /* empty line */) {
                w = calcTextWidth(context, currentCanvasFont, text.text().substr(srun.startIndex, srun.length));
            }

            bool brokeLine = false;
            // if want line breaks and we went over the line width,
            // then break the line.
            if (wantWrap && x + w > size.width) {
                auto wrap = wordWrap(text.text().c_str(), srun.startIndex, srun.length, context, cf, size.width, x);
                w = wrap.newRunWidth;  // don't count spaces at end of line
                if (wrap.endRunAtIndex > srun.startIndex && wrap.endRunAtIndex < srun.startIndex + srun.length) {
                    int newLength = wrap.endRunAtIndex - srun.startIndex;
                    subruns[srunIdx].length = newLength;
                    // Ok, this is a little hacky: we subdivide the run
                    // we are processing while iterating over it. But it is
                    // okay since we do not hold an iterator or pointer to an
                    // element anywhere, and srun is a copy.
                    subruns.insert(subruns.begin() + srunIdx + 1, SubRun{ srun.run, wrap.endRunAtIndex, srun.length /*old srun*/ - newLength, false });
                    srun.length = newLength;  // update copy (after insertion calculations!)
                    brokeLine = true;
                }
            }

            const auto fm = cf->fontMetrics(context);
            mLines.back().runs.emplace_back();
            auto &r = mLines.back().runs.back();
            r.startIndex = srun.startIndex;
            r.length = srun.length;
            r.font = cf;
            r.size.width = w;
            r.size.height = fm.ascent + fm.descent;
            leadingForLine = std::max(leadingForLine, fm.leading); 
            if (fm.lineHeight > mLines.back().lineRect.height) {
                mLines.back().largestAscent = fm.ascent;
            }
            r.characterSpacing = charSpacing;
            r.hasSuperscript = hasSuperscript;
            r.hasSubscript = hasSubscript;
            r.text = EmVal(text.text().substr(srun.startIndex, srun.length));
            x += w;

            mLines.back().lineRect.width += r.size.width;
            mLines.back().lineRect.height = std::max(mLines.back().lineRect.height, r.size.height);

            if (isNewline) {
                // This is a forced newline, so we need subtract off the width
                // of any whitespace from previous run. (The previous run
                // obviously did not already have its width adjusted from a line
                // break, because if there was a line break, it was at the right
                // side of the break.)
                if (!mLines.back().runs.empty()) {
                    auto &lastRun = mLines.back().runs.back();
                    const char *start = text.text().c_str() + lastRun.startIndex;
                    const char *endOrig = start + lastRun.length;
                    const char *end = endOrig;
                    const char *prev = end;
                    uint32_t utf32;
                    do {
                        end = prev;
                        prev = prevCodePoint(end, &utf32);
                    } while(isWhitespace(utf32));
                    if (end < endOrig) {
                        auto oldW = lastRun.size.width;
                        lastRun.size.width = calcTextWidth(context, currentCanvasFont, std::string(start, end - start));
                        mLines.back().lineRect.width -= oldW - lastRun.size.width;
                    }
                }
            }

            if (isNewline || brokeLine) {
                if (isNewline) {
                    x = firstLineIndent;
                } else {
                    x = nonFirstLineIndent;
                }
                y += lineHeightMultiple * (mLines.back().lineRect.height + leadingForLine);
                mLines.emplace_back();
                mLines.back().lineRect.x = x;
                mLines.back().lineRect.y = y;
                leadingForLine = PicaPt::kZero;
            }
        }

        // Calculate the size for alignment
        Size textSize;
        if (!mLines.empty()) {
            for (auto &line : mLines) {
                textSize.width = std::max(textSize.width, line.lineRect.width);
            }
            if (text.text().size() == 1 && text.text()[0] == '\n') {
                textSize.height = PicaPt::kZero;
            } else {
                auto lastNonEmptyLineIdx = mLines.size() - 1;
                while (lastNonEmptyLineIdx > 0
                       && mLines[lastNonEmptyLineIdx].lineRect.height <= PicaPt::kZero) {
                    --lastNonEmptyLineIdx;
                }
                textSize.height = mLines[lastNonEmptyLineIdx].lineRect.maxY();
            }
        }
        mMetrics.width = textSize.width;
        mMetrics.height = textSize.height;
        mMetrics.advanceX = PicaPt::kZero;
        mMetrics.advanceY = textSize.height;

        // Finish layout
        Font::Metrics firstLineMetrics;
        if (!mLines.empty() && !mLines[0].runs.empty()) {
            for (auto &run : mLines[0].runs) {
                auto &fm = run.font->fontMetrics(context);
                if (fm.lineHeight > firstLineMetrics.lineHeight) {
                    firstLineMetrics = fm;
                }
            }
        }

        Point offset = calcOffsetForAlignment(alignment, size, firstLineMetrics);
        for (auto &line : mLines) {
            if (alignment & Alignment::kHCenter) {
                line.lineRect.x += (size.width - line.lineRect.width) / 2.0f;
            } else if (alignment & Alignment::kRight) {
                line.lineRect.x += size.width - line.lineRect.width;
            } else {
                line.lineRect.x += offset.x;
            }
            line.lineRect.y += offset.y;

            auto lastDescent = PicaPt::kZero;
            auto lastCapHeight = PicaPt::kZero;
            for (auto &run : line.runs) {
                if (run.hasSuperscript) {
                    auto &fm = run.font->fontMetrics(context);
                    run.yOffset = line.largestAscent - lastCapHeight + fm.capHeight - fm.ascent;
                } else if (run.hasSubscript) {
                    auto fm = run.font->fontMetrics(context);
                    auto lastBottom = line.largestAscent + lastDescent;
                    auto runLeading = line.lineRect.height - lastBottom;
                    auto baseline = lastBottom;
                    if (lastBottom + fm.descent > line.lineRect.maxY()) {
                        baseline = line.lineRect.maxY() - fm.descent;
                    }
                    run.yOffset = baseline - fm.ascent;
                } else {
                    auto &fm = run.font->fontMetrics(context);
                    run.yOffset = line.largestAscent - fm.ascent;
                    lastDescent = fm.descent;
                    lastCapHeight = fm.capHeight;
                }
            }
        }

        // Create drawing commands
        const auto defaultColorRGBA = Color::kTextDefault.toRGBA();
        struct {
            CanvasFont *font = nullptr;
            Color fgColor = Color::kTransparent;
            Color bgColor = Color::kTransparent;
            Color outlineColor = Color::kTransparent;
            Color underlineColor = Color::kTransparent;
            Color strikethroughColor = Color::kTransparent;
            PicaPt outlineWidth = PicaPt::kZero;
            UnderlineStyle underlineStyle = kUnderlineNone;
            PicaPt characterSpacing = PicaPt::kZero;
        } state;
        int subrunIdx = 0;
        for (auto &line : mLines) {
            auto x = line.lineRect.x;
            for (auto &run : line.runs) {
                assert(subrunIdx < subruns.size());
                auto &subrun = subruns[subrunIdx];
                Color fgColor = defaultReplacementColor;
                assert(subrun.run->color.isSet);
                if (subrun.run->color.isSet) {
                    fgColor = subrun.run->color.value;
                }
                if (fgColor.toRGBA() == defaultColorRGBA) {
                    fgColor = defaultReplacementColor;
                }
                if (state.fgColor.toRGBA() != fgColor.toRGBA()) {
                    mCommands.emplace_back(DrawCommand{Command::kSetFGColor, { fgColor }});
                }
                state.fgColor = fgColor;

                Color bgColor = Color::kTransparent;
                if (subrun.run->backgroundColor.isSet) {
                    bgColor = subrun.run->backgroundColor.value;
                }
                if (state.bgColor.toRGBA() != bgColor.toRGBA()) {
                    mCommands.emplace_back(DrawCommand{Command::kSetBGColor, { bgColor }});
                }
                state.bgColor = bgColor;

                Color outlineColor = Color::kTransparent;
                if (subrun.run->outlineColor.isSet) {
                    outlineColor = subrun.run->outlineColor.value;
                }
                if (state.outlineColor.toRGBA() != outlineColor.toRGBA()) {
                    mCommands.emplace_back(DrawCommand{Command::kSetOutlineColor, { outlineColor }});
                }
                state.outlineColor = outlineColor;

                PicaPt outlineWidth = PicaPt::kZero;
                if (subrun.run->outlineStrokeWidth.isSet) {
                    outlineWidth = subrun.run->outlineStrokeWidth.value;
                }
                if (state.outlineWidth != outlineWidth) {
                    mCommands.emplace_back(DrawCommand{Command::kSetOutlineWidth, { .pica = outlineWidth } });
                }
                state.outlineWidth = outlineWidth;

                Color underlineColor = Color::kTransparent;
                if (subrun.run->underlineColor.isSet) {
                    underlineColor = subrun.run->underlineColor.value;
                }
                if (state.underlineColor.toRGBA() != underlineColor.toRGBA()) {
                    mCommands.emplace_back(DrawCommand{Command::kSetUnderlineColor, { underlineColor }});
                }
                state.underlineColor = underlineColor;

                UnderlineStyle underlineStyle = kUnderlineNone;
                if (subrun.run->underlineStyle.isSet) {
                    underlineStyle = subrun.run->underlineStyle.value;
                }
                if (state.underlineStyle != underlineStyle) {
                    mCommands.emplace_back(DrawCommand{Command::kSetUnderlineStyle,  { .style = underlineStyle }});
                }
                state.underlineStyle = underlineStyle;

                PicaPt charSpacing = PicaPt::kZero;
                if (subrun.run->characterSpacing.isSet) {
                    charSpacing = subrun.run->characterSpacing.value;
                }
                if (state.characterSpacing != charSpacing) {
                    mCommands.emplace_back(DrawCommand{Command::kSetCharSpacing, { .pica = charSpacing }});
                }

                if (state.font != run.font) {
                    mCommands.emplace_back(DrawCommand{Command::kSetFont, { .ptr = (void*)run.font }});
                    state.font = run.font;
                }

                Color strikethroughColor = Color::kTransparent;
                if (subrun.run->strikethrough.isSet) {
                    if (subrun.run->strikethroughColor.isSet) {
                        strikethroughColor = subrun.run->strikethroughColor.value;
                    } else {
                        strikethroughColor = state.fgColor;
                    }
                }
                if (state.strikethroughColor.toRGBA() != strikethroughColor.toRGBA()) {
                    mCommands.emplace_back(DrawCommand{Command::kSetStrikethroughColor, { strikethroughColor } });
                }
                state.strikethroughColor = strikethroughColor;

                mCommands.emplace_back(DrawCommand{Command::kMove, { .upperLeft = Point(x, line.lineRect.y + run.yOffset) }});

                mCommands.emplace_back(DrawCommand{Command::kDrawText, { .ptr = (void*)&run } });

                x += run.size.width;
                subrunIdx += 1;
            }
        }

        context.set("font", oldFont);
        context.set("letterSpacing", "0px");
    }

private:
    struct WrapResult
    {
        int wrapAtIndex;
        int endRunAtIndex;
        PicaPt newRunWidth;
    };
    // This is a basic word-wrapping algorithm that handles text in languages
    // that use whitespace (which is most of them, including Korean, Hindi,
    // Arabic, and Hebrew), as well as Japanese. Chinese just breaks anywhere,
    // which is probably not ideal, since it might break within a word, but
    // it seems to be acceptable (which is good, since including a Chinese
    // dictionary and trying to figure out where words start/stop is out of
    // scope!). This should at least give passable results for the majority
    // of people's language.
    WrapResult wordWrap(const char *text, int startIndex, int length,
                        const EmVal& context, CanvasFont *font,
                        const PicaPt& lineWidth, const PicaPt& xStart)
    {
        assert(length > 0);
        assert(lineWidth > PicaPt::kZero);

        if (xStart >= lineWidth && lineWidth > PicaPt::kZero) {
            return { startIndex, startIndex, PicaPt::kZero };
        }

        const char *start = text + startIndex;
        const char *end = start + length;

        // Start with breaking at whitespace (\n will be handled already)
        uint32_t utf32;
        const char *wordStart = start;
        const char *c = start;
        const char *prevWordEnd = start;
        PicaPt x = xStart;
        PicaPt whitespaceFromLastWord = PicaPt::kZero;
        while (c <= end) {
            const char *nextC = c;
            if (c < end) {
                nextC = nextCodePoint(c, &utf32);
            }
            
            if (isWhitespace(utf32) || c == end) {
                auto w = calcTextWidth(context, font, std::string(wordStart, c - wordStart));

                // Skip trailing whitespace
                const char *wordEnd = c;
                c = nextC;
                if (c < end) {
                    do {
                        c = nextC;  // no-op first iteration, increments after
                        nextC = nextCodePoint(c, &utf32);
                    } while (isWhitespace(utf32));
                }

                if (x + w >= lineWidth) {
                    if (w <= lineWidth) {
                        // Success! This word fits on next line, so can wrap
                        // at previous word.
                        return { prevWordEnd - text, wordStart - text, x - xStart };
                    } else {
                        // Word is too big, so split on characters below
                        x += whitespaceFromLastWord;
                        break;
                    }
                }
                prevWordEnd = wordEnd;
                wordStart = c;
                // If we are not splitting, we need to add in that whitespace!
                x += whitespaceFromLastWord + w;
                whitespaceFromLastWord = calcTextWidth(context, font, std::string(wordEnd, c - wordEnd));
            } else {
                c = nextC;
            }
        }
        if (wordStart > start && x <= lineWidth) {  // at end, don't count whitespace
            return { end - text, end - text, x - xStart };
        }

        // Break by character instead
        // (This is linear by calls to get the width, but quadratic in string
        // creation. TODO: maybe use string views for EmVal?)
        struct CodePoint {
            int idx;
            uint32_t cp;
            PicaPt x;
            CharType cpType;
        };
        // Compute all the code points and widths until we go past the end of
        // the line. Note that we cannot just compute the widths of each
        // character, because ligatures affect this. And ligatures are variable
        // width (e.g. "fi" and "ffi").
        std::vector<CodePoint> codePoints;
        c = wordStart;
        while (x <= lineWidth && c < end) {
            const char *nextC = nextCodePoint(c, &utf32);
            codePoints.emplace_back(CodePoint{ c - text, utf32, x, calcCodePointType(utf32) });
            c = nextC;
            x = calcTextWidth(context, font, std::string(wordStart, c - wordStart));
        }
        // If the line is too small for one character, break at index=1 anyway
        assert(!codePoints.empty());
        if (codePoints.size() <= 1) {
            c = text + codePoints[0].idx;
            auto c2 = nextCodePoint(c, &utf32);
            auto x2 = calcTextWidth(context, font, std::string(c, c2 - c));
            return { c2 - text, c2 - text, x2 };
        }
        // Attempt to find a grammatically good place to break. Cannot break
        // at punctuation, since there are many non-breakable situations:
        // "247-7632", "user@server.company.com", "@#$!"
        auto rit = codePoints.rbegin();
        auto cpType = rit->cpType;
        ++rit;
        for (;  rit != codePoints.rend();  ++rit) {
            if (rit->cpType != cpType) {
                --rit;
                return { rit->idx, rit->idx, rit->x - xStart };
            }
        }

        // Well, rats. Just break at the line break.
        int runIdx = codePoints.back().idx;
        return { runIdx, runIdx, codePoints.back().x - xStart };
    }

    PicaPt calcTextWidth(const EmVal& context, CanvasFont *font,
                         const std::string& text) const
    {  // assumes that font is set in context
        auto tm = context.call<EmVal>("measureText", EmVal(text));
        return PicaPt::fromPixels(tm["width"].as<float>(), kCSSPixelDPI) * font->fromCSSToDPIMultiplier();
    };

    struct JSTextMetrics
    {
        PicaPt width;
        PicaPt baseline;
    };
    JSTextMetrics calcTextMetrics(const EmVal& context, CanvasFont *font,
                                  const std::string& text) const
    {  // assumes that font is set in context
        auto tm = context.call<EmVal>("measureText", EmVal(text));
        PicaPt baseline;
        if (tm.hasOwnProperty("alphabeticBaseline")) {
            baseline = PicaPt::fromPixels(tm["alphabeticBaseline"].as<float>(), kCSSPixelDPI) * font->fromCSSToDPIMultiplier();
        } else {
            baseline = PicaPt::fromPixels(tm["actualBoundingBoxAscent"].as<float>(), kCSSPixelDPI) * font->fromCSSToDPIMultiplier();
        }
        return {
            PicaPt::fromPixels(tm["width"].as<float>(), kCSSPixelDPI) * font->fromCSSToDPIMultiplier(),
            baseline,
        };
    };

    bool isWhitespace(const uint32_t c) const {
        // Organized so that most common break first. Excludes non-breaking
        // spaces.
        return (c == 0x0020 /* space */ || c == 0x0009 /* tab */ ||
                c == 0x3000 /* CJK space */ ||
                (c >= 0x2000 && c <= 0x200b) /* various width spaces */ ||
                c == 0x180e /* Mongolian vowel separator */ ||
                c == 0x205f /* mathematical space */);
    }

    enum CharType { kNormal, kKanji, kHiragana, kKatakana, kCJKPunctuation,
                    kCJKLatin, kHangul };
    CharType calcCodePointType(uint32_t cp) const
    {
        if (cp >= 0x3041 && cp <= 0x309f) {
            return kHiragana;
        }
        if ((cp >= 0x30a1 && cp <= 0x30fe) || cp == 0x30aa || cp == 0x30ff) {
            return kKatakana;
        }
        if (cp >= 0x4e00 && cp <= 0x9fff) {
            return kKanji;  // (and Chinese hanzi and Korean hanzi)
        }
        if (cp >= 0x1100 && cp <= 0x11ff) {
            return kHangul;
        }
        if (cp == 0x3001 || cp == 0x3002) {  // only safe to break after these
            return kCJKPunctuation;
        }
        if (cp >= 0xff01 && cp <= 0xff5e) {
            return kCJKLatin;
        }
        return kNormal;
    }

public:
    void draw(DrawContext& dc, const Point& topLeft, PaintMode mode)
    {
        EmVal context = *(EmVal*)dc.nativeDC();
        auto dpi = dc.dpi();
        const PicaPt onePx = dc.onePixel();

        Color fgColor = Color::kTransparent;
        Color bgColor = Color::kTransparent;
        Color outlineColor = Color::kTransparent;
        Color underlineColor = Color::kTransparent;
        Color strikethroughColor = Color::kTransparent;
        UnderlineStyle underlineStyle = kUnderlineNone;
        Point ul;
        PicaPt outlineWidth = PicaPt::kZero;
        CanvasFont *font = nullptr;
        PicaPt characterSpacing = PicaPt::kZero;
        Font::Metrics fontMetrics;

        dc.save();
        auto oldFont = context["font"];
        dc.setStrokeDashes({}, PicaPt::kZero);

        for (auto &cmd : mCommands) {
            switch (cmd.cmd) {
                case Command::kSetBGColor:
                    bgColor = cmd.value.color;
                    break;
                case Command::kSetFGColor:
                    fgColor = cmd.value.color;
                    break;
                case Command::kSetOutlineColor:
                    outlineColor = cmd.value.color;
                    break;
                case Command::kSetOutlineWidth:
                    outlineWidth = cmd.value.pica;
                    break;
                case Command::kSetUnderlineColor:
                    underlineColor = cmd.value.color;
                    break;
                case Command::kSetUnderlineStyle:
                    underlineStyle = cmd.value.style;
                    break;
                case Command::kSetStrikethroughColor:
                    strikethroughColor = cmd.value.color;
                    break;
                case Command::kSetCharSpacing:
                    characterSpacing = cmd.value.pica;
                    context.set("letterSpacing", std::to_string(characterSpacing.toPixels(dpi)) + "px");
                    break;
                case Command::kSetFont:
                    font = (CanvasFont*)cmd.value.ptr;
                    fontMetrics = font->fontMetrics(context);
                    context.set("font", font->nativeHandle());
                    break;
                case Command::kMove:
                    ul = topLeft + cmd.value.upperLeft;
                    break;
                case Command::kDrawText: {
                    Run &run = *(Run*)cmd.value.ptr;
                    if (bgColor.alpha() > 0.0f) {
                        dc.setFillColor(bgColor);
                        dc.drawRect(Rect(ul.x, ul.y, run.size.width, run.size.height), kPaintFill);
                    }
                    // Draw underline(s)
                    if (underlineStyle != kUnderlineNone) {
                        // Cover all of a pixel which has any bit of underline,
                        // so that underlines do not get a pixel gap between runs
                        auto x1 = dc.floorToNearestPixel(ul.x);
                        auto x2 = dc.ceilToNearestPixel(ul.x + run.size.width);
                        auto y0 = ul.y + fontMetrics.ascent + fontMetrics.underlineOffset;
                        auto lineWidth = fontMetrics.underlineThickness;
                        if (underlineStyle == kUnderlineDouble) {
                            y0 -= 0.5f * lineWidth;
                        }
                        if (lineWidth.toPixels(dpi) < 1.5f) {
                            y0 = dc.floorToNearestPixel(y0) + 0.5f * onePx;
                            lineWidth = std::max(onePx, lineWidth);
                        }
                        if (underlineColor.alpha() < 0.0001f) {
                            if (outlineColor.alpha() < 0.0001f) {
                                dc.setStrokeColor(fgColor);
                            } else {
                                dc.setStrokeColor(outlineColor);
                            }
                        } else {
                            dc.setStrokeColor(underlineColor);
                        }
                        dc.setStrokeWidth(lineWidth);

                        switch (underlineStyle) {
                            case kUnderlineNone:
                                assert(false);
                                break;
                            case kUnderlineSingle:
                                dc.drawLines({ Point(x1, y0), Point(x2, y0) });
                                break;
                            case kUnderlineDotted: {
                                auto len = 0.25f * (fontMetrics.ascent + fontMetrics.descent);
                                dc.setStrokeDashes({ len, len }, PicaPt::kZero);
                                dc.drawLines({ Point(x1, y0), Point(x2, y0) });
                                dc.setStrokeDashes({}, PicaPt::kZero);
                                break;
                            }
                            case kUnderlineDouble: {
                                auto y1 = y0 + 2.0f * std::max(onePx, dc.roundToNearestPixel(fontMetrics.underlineThickness));
                                dc.drawLines({ Point(x1, y0), Point(x2, y0) });
                                dc.drawLines({ Point(x1, y1), Point(x2, y1) });
                                break;
                            }
                            case kUnderlineWavy: {
                                float x1px = x1.toPixels(dpi);
                                float x2px = x2.toPixels(dpi);
                                float y0px = y0.toPixels(dpi);
                                auto xy = createWavyLinePoints(x1px, y0px, x2px, lineWidth.toPixels(dpi));
                                std::vector<Point> pts;
                                pts.reserve(xy.size() / 2);
                                for (size_t i = 0;  i < xy.size();  i += 2) {
                                    pts.emplace_back(PicaPt::fromPixels(xy[i], dpi), PicaPt::fromPixels(xy[i + 1], dpi));
                                }
                                dc.drawLines(pts);
                                break;
                            }
                        }
                    }

                    // Draw text
                    auto x = ul.x.toPixels(dpi);
                    auto y = ul.y.toPixels(dpi);
                    y += fontMetrics.ascent.toPixels(dpi);
                    y = std::floor(y);
                    if (fgColor.alpha() > 0.0f) {
                        dc.setFillColor(fgColor);
                        context.call<void>("fillText", run.text, x, y);
                    }
                    if (outlineColor.alpha() > 0.0f && outlineWidth > PicaPt::kZero) {
                        dc.setStrokeColor(outlineColor);
                        dc.setStrokeWidth(outlineWidth);
                        context.call<void>("strokeText", run.text, x, y);
                    }

                    // Draw strikethough last
                    if (strikethroughColor.alpha() > 0.0f) {
                        // Cover all of a pixel which has any bit of underline,
                        // so that lines do not get a pixel gap between runs
                        auto x1 = dc.floorToNearestPixel(ul.x);
                        auto x2 = dc.ceilToNearestPixel(ul.x + run.size.width);
                        auto y0 = ul.y + fontMetrics.ascent - 0.5f * fontMetrics.xHeight;
                        auto lineWidth = fontMetrics.underlineThickness;
                        if (fontMetrics.underlineThickness.toPixels(dpi) < 1.5f) {
                            y0 = dc.floorToNearestPixel(y0) + 0.5f * onePx;
                            lineWidth = std::max(onePx, lineWidth);
                        }
                        dc.setStrokeColor(strikethroughColor);
                        dc.setStrokeWidth(lineWidth);
                        dc.drawLines({ Point(x1, y0), Point(x2, y0) });
                    }
                    break;
                }
            }
        }
        
        context.set("font", oldFont);
        context.set("letterSpacing", "0px");
        dc.restore();
    }

    const TextMetrics& metrics() const override
    {
        return mMetrics;
    }

    const std::vector<Glyph>& glyphs() const override
    {
        if (mGlyphs.empty() && !mLines.empty()) {
            auto oldFont = mContextForGlyphs["font"];
            CanvasFont *font = nullptr;
            PicaPt characterSpacing = PicaPt::kZero;
            mGlyphs.reserve(4);  // many STLs allocate 1, 2, 4, 8, ... but
                                 // chances are we have more than 4 code points
            for (size_t lineNo = 0;  lineNo < mLines.size();  ++lineNo) {
                const auto &line = mLines[lineNo];
                PicaPt x = line.lineRect.x;
                // We need to calculate the width from a point where the is
                // no ligature for each frame, since doing each character would
                // produce incorrect results for ligatures. But since this is
                // O(n^2), which would be bad on a long run (say, one of the
                // paragraphs of list of sea creatures in "Twenty Thousand
                // Leagues Under the Sea"), so we use whitespace and punctuation
                // to update the start position. (Punctuation is for CJK scripts,
                // which may not use spaces, but also have ligatures.)
                for (const auto &run : line.runs) {
                    if (run.font != font) {
                        font = run.font;
                        mContextForGlyphs.set("font", font->nativeHandle());
                    }
                    if (run.characterSpacing != characterSpacing) {
                        mContextForGlyphs.set("letterSpacing", std::to_string(run.characterSpacing.toPixels(mDPI)) + "px");
                    }
                    auto text = run.text.as<std::string>();
                    auto *ctext = text.c_str();
                    auto *end = ctext + run.length;
                    auto *start = ctext;
                    auto *thisCP = start;
                    uint32_t cp;
                    auto *nextCP = nextCodePoint(thisCP, &cp);
                    PicaPt lastGlyphMaxX = x;
                    while (nextCP <= end) {
                        auto tm = calcTextMetrics(mContextForGlyphs, run.font, std::string(start, nextCP - start));
                        auto maxX = x + tm.width;
                        auto y = line.lineRect.y + run.yOffset;
                        mGlyphs.emplace_back(thisCP - ctext + run.startIndex, lineNo, tm.baseline, Rect(lastGlyphMaxX, y, maxX - lastGlyphMaxX, line.lineRect.height));
                        mGlyphs.back().indexOfNext = nextCP - ctext + run.startIndex;
                        lastGlyphMaxX = maxX;
                        thisCP = nextCP;

                        if (nextCP < end) {
                            if (isWhitespace(cp) ||
                                cp == 0x3001 || cp == 0x3002)  // CJK comma, period
                            {
                                 start = nextCP;
                                 x = maxX;
                            }
                            nextCP = nextCodePoint(thisCP, &cp);
                        } else {
                            nextCP += 1;
                        }
                    }
                    x = lastGlyphMaxX;
                }
            }

            mContextForGlyphs.set("font", oldFont);
            mContextForGlyphs.set("letterSpacing", "0px");
        }
        return mGlyphs;
    }

private:
    struct Run {
        int startIndex;
        int length;
        CanvasFont *font;
        PicaPt characterSpacing;
        PicaPt yOffset;  // relative to owning lineRect.y
        Size size;
        EmVal text;
        bool hasSuperscript;
        bool hasSubscript;
    };
    struct Line {
        std::vector<Run> runs;
        Rect lineRect;
        PicaPt largestAscent;
    };

    enum Command { kSetBGColor, kSetFGColor, kSetOutlineColor, kSetOutlineWidth,
                   kSetUnderlineColor, kSetUnderlineStyle,
                   kSetStrikethroughColor, kSetCharSpacing,
                   kSetFont, kMove, kDrawText };
    struct DrawCommand
    {
        Command cmd;
        union {
            Color color;      // 16 bytes (4 floats)
            PicaPt pica;      // 8 bytes (2 floats)
            Point upperLeft;  // 16 bytes (4 floats)
            UnderlineStyle style;         
            void *ptr;        // 4 bytes (WASM is 32-bit)
        } value;
    };

    std::vector<Line> mLines;
    std::vector<DrawCommand> mCommands;
    
    float mDPI;
    TextMetrics mMetrics;
    mutable std::vector<Glyph> mGlyphs;
    mutable EmVal mContextForGlyphs;
};

//---------------------------- HTMLCanvasContext ------------------------------
class HTMLCanvasContext : public DrawContext
{
public:
    HTMLCanvasContext(EmVal context, int width, int height, float dpi)
        : DrawContext(&mContext, width, height, dpi, dpi)
        , mContext(context)
    {
    }

    ~HTMLCanvasContext()
    {
        auto it = gId2Gradients.begin();
        while (it != gId2Gradients.end()) {
            if (it->second->ownedByContext(this)) {
                auto itCopy = it;
                it++;
                gId2Gradients.erase(itCopy);
            }
        }
    }

    std::shared_ptr<DrawContext> createBitmap(
                BitmapType type, int width, int height, float dpi /*= 72.0f*/) override
    {
        return DrawContext::createOffscreenCanvasBitmap(width, height, dpi);
    }

    std::shared_ptr<DrawableImage> createDrawableImage(const Image& image) const override
    {
        return CanvasImage::fromImage(image);
    }

    std::shared_ptr<BezierPath> createBezierPath() const override
    {
        return std::make_shared<CanvasPath>();
    }

    Gradient& getGradient(const std::vector<Gradient::Stop>& stops) override
    {
        return *gGradientMgr.get(GradientInfo{ this, stops }, dpi());
    }

    Gradient& getGradient(size_t id) const override
    {
        auto it = gId2Gradients.find(id);
        if (it != gId2Gradients.end()) {
            return *it->second;
        }
        return gInvalidGradient;
    }

    std::shared_ptr<TextLayout> createTextLayout(
                const char *utf8, const Font& font, const Color& color,
                const Size& size /*= Size::kZero*/,
                int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                TextWrapping wrap /*= kWrapWord*/) const override
    {
        return std::make_shared<TextObj>(mContext, mDPI, Text(utf8, font, color), size, alignment, wrap);
    }

    std::shared_ptr<TextLayout> createTextLayout(
                const Text& t,
                const Size& size /*= Size::kZero*/,
                int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                TextWrapping wrap /*= kWrapWord*/) const override
    {
        return std::make_shared<TextObj>(mContext, mDPI, t, size, alignment, wrap);
    }

    std::shared_ptr<TextLayout> createTextLayout(
                const Text& t,
                const Font& defaultReplacementFont,
                const Color& defaultReplacementColor,
                const Size& size /*= Size::kZero*/,
                int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                TextWrapping wrap /*= kWrapWord*/) const override
    {
        return std::make_shared<TextObj>(mContext, mDPI, t, size, alignment, wrap, defaultReplacementFont, defaultReplacementColor);
    }

    void beginDraw() override
    {
        if (mStateStack.empty()) {
            mStateStack.emplace_back();
            setFillColor(Color::kBlack);
            setStrokeColor(Color::kBlack);
            setStrokeWidth(PicaPt::fromStandardPixels(1.0f));
            setStrokeEndCap(kEndCapButt);
            setStrokeJoinStyle(kJoinMiter);
        }
        // Do not assert state stack because caller may to need to set clipping
        // path for a window before handing the DrawContext to the window for
        // drawing. Naturally the window will want to do a beginDraw(), but
        // our caller needs to call beginDraw() to set the clipping.
        // (Basically, beginDraw() after the first is a no-op.)
        // assert(mStateStack.size() == 1);
    }

    void endDraw() override
    {
        while (mStateStack.size() > 1) {
            restore();
        }
        assert(mStateStack.size() == 1);
    }

    void save() override
    {
        mStateStack.push_back(mStateStack.back());  // copies
        mContext.call<void>("save");
    }

    void restore() override
    {
        mContext.call<void>("restore");
        mStateStack.pop_back();
    }

    void translate(const PicaPt& dx, const PicaPt& dy) override
    {
        mContext.call<void>("translate", dx.toPixels(mDPI), dy.toPixels(mDPI));
    }

    void rotate(float degrees) override
    {
        mContext.call<void>("rotate", -degrees * 3.141592f / 180.0f);
    }

    void scale(float sx, float sy) override
    {
        mContext.call<void>("scale", sx, sy);
    }

    void setFillColor(const Color& color) override
    {
        mContext.set("fillStyle", std::string("#") + color.toHexString());
        mStateStack.back().fillColor = color;
    }

    void setStrokeColor(const Color& color) override
    {
        mContext.set("strokeStyle", EmVal(std::string("#") + color.toHexString()));
        mStateStack.back().strokeColor = color;
    }

    void setStrokeWidth(const PicaPt& w) override
    {
        mContext.set("lineWidth", EmVal(w.toPixels(mDPI)));
        mStateStack.back().strokeWidth = w;
    }

    void setStrokeEndCap(EndCapStyle cap) override
    {
        switch (cap) {
            case kEndCapButt:
                mContext.set("lineCap", "butt");
                break;
            case kEndCapRound:
                mContext.set("lineCap", "round");
                break;
            case kEndCapSquare:
                mContext.set("lineCap", "square");
                break;
        }
        mStateStack.back().endCap = cap;
    }

    void setStrokeJoinStyle(JoinStyle join) override
    {
        switch (join) {
            case kJoinRound:
                mContext.set("lineJoin", "round");
                break;
            case kJoinBevel:
                mContext.set("lineJoin", "bevel");
                break;
            case kJoinMiter:
                mContext.set("lineJoin", "miter");
                break;
        }
        mStateStack.back().joinStyle = join;
    }

    void setStrokeDashes(const std::vector<PicaPt> lengths,
                         const PicaPt& offset) override
    {
        std::vector<float> dash;
        dash.reserve(lengths.size());
        for (auto &len : lengths) {
            dash.push_back(len.toPixels(mDPI));
        }
        mContext.call<void>("setLineDash", EmVal::array(dash));
        mContext.set("lineDashOffset", EmVal(-offset.toPixels(mDPI)));
        mStateStack.back().dash = lengths;
        mStateStack.back().dashOffset = offset;
    }

    Color fillColor() const override
    {
        return mStateStack.back().fillColor;
    }

    Color strokeColor() const override
    {
        return mStateStack.back().strokeColor;
    }

    PicaPt strokeWidth() const override
    {
        return mStateStack.back().strokeWidth;
    }

    EndCapStyle strokeEndCap() const override
    {
        return mStateStack.back().endCap;
    }

    JoinStyle strokeJoinStyle() const override
    {
        return mStateStack.back().joinStyle;
    }

    void fill(const Color& color) override
    {
        if (color.alpha() < 1.0f) {
            mContext.call<void>("clearRect", 0, 0, mWidth, mHeight);
        }
        if (color.alpha() > 0.0f) {
            auto oldFill = fillColor();
            setFillColor(color);
            mContext.call<void>("fillRect", 0, 0, mWidth, mHeight);
            setFillColor(oldFill);
        }
    }

    void clearRect(const Rect& rect) override
    {
        mContext.call<void>(
                    "clearRect",
                    rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                    rect.width.toPixels(mDPI), rect.height.toPixels(mDPI));
    }

    void drawLines(const std::vector<Point>& lines) override
    {
        if (lines.size() < 2) {
            return;
        }

        mContext.call<void>("beginPath");
        mContext.call<void>("moveTo", lines[0].x.toPixels(mDPI), lines[0].y.toPixels(mDPI));
        for (size_t i = 1;  i < lines.size();  ++i) {
            mContext.call<void>("lineTo", lines[i].x.toPixels(mDPI), lines[i].y.toPixels(mDPI));
        }
        mContext.call<void>("stroke");
    }

    void drawRect(const Rect& rect, PaintMode mode) override
    {
        double x = double(rect.x.toPixels(mDPI));
        double y = double(rect.y.toPixels(mDPI));
        double w = double(rect.width.toPixels(mDPI));
        double h = double(rect.height.toPixels(mDPI));

        if (mode & kPaintFill) {
            mContext.call<void>("fillRect", x, y, w, h);
        }
        if (mode & kPaintStroke) {
            mContext.call<void>("strokeRect", x, y, w, h);
        }
    }

    void drawEllipse(const Rect& rect, PaintMode mode) override
    {
        mContext.call<void>("beginPath");
        mContext.call<void>(
                    "ellipse",
                    rect.midX().toPixels(mDPI),
                    rect.midY().toPixels(mDPI),
                    0.5f * rect.width.toPixels(mDPI),
                    0.5f * rect.height.toPixels(mDPI),
                    0.0f,
                    0.0f, 2.0f * 3.141592f);
        if (mode & kPaintFill) {
            mContext.call<void>("fill");
        }
        if (mode & kPaintStroke) {
            mContext.call<void>("stroke");
        }
    }

    void drawPath(std::shared_ptr<BezierPath> path, PaintMode mode) override
    {
        makePath(path);
        if (mode & kPaintFill) {
            mContext.call<void>("fill");
        }
        if (mode & kPaintStroke) {
            mContext.call<void>("stroke");
        }
    }

    void drawLinearGradientPath(std::shared_ptr<BezierPath> path,
                                Gradient& gradient,
                                const Point& start,
                                const Point& end) override
    {
        if (!gradient.isValid()) {  // if the gradient is not valid:
            return;                 //  a) it should not draw, and
        }                           //  b) it might not even be a CanvasGradient

        auto dx = (end.x - start.x).toPixels(mDPI);
        auto dy = (end.y - start.y).toPixels(mDPI);
        auto dist = std::sqrt(dx * dx + dy * dy);
        auto rotationRad = std::atan2(dy, dx);
        if (dist < 1e-6) {  // if dist == 0 the gradient will be invisible (and we cannot invert the matrix)
            return;
        }

        auto &nativeGradient = ((CanvasGradient*)&gradient)->linearGradient();

        save();
        clipToPath(path);

        // We created the gradient going from (0, 0) to (1, 0). So we can avoid
        // creating different gradients for each start/end that uses the same
        // stops by just transforming the draw matrix so that drawing from
        // (0, 0) to (1, 0) gives us the desired result.
        translate(start.x, start.y);
        scale(dist, dist);
        rotate(-rotationRad * 180.0f / 3.14159265f);

        drawGradient(nativeGradient);
        restore();
    }

    void drawRadialGradientPath(std::shared_ptr<BezierPath> path,
                                Gradient& gradient,
                                const Point& center,
                                const PicaPt& startRadius,
                                const PicaPt& endRadius) override
    {
        if (!gradient.isValid()) {  // if the gradient is not valid:
            return;                 //  a) it should not draw, and
        }                           //  b) it might not even be a CanvasGradient

        auto r0 = startRadius.toPixels(mDPI);
        auto r1 = endRadius.toPixels(mDPI);
        if (r1 < 1e-6) {
            return;
        }

        auto &nativeGradient = ((CanvasGradient*)&gradient)->radialGradient(r0 / r1);

        save();
        clipToPath(path);

        // We created the gradient going from (0, 0) to (1, 0). So we can avoid
        // creating different brushes (which presumably creates a gradient bitmap
        // for each one) for each start/end that uses the same stops
        // by just transforming the draw matrix so that drawing from (0, 0) to
        // (1, 0) gives us the desired result.
        translate(center.x, center.y);
        scale(r1, r1);

        drawGradient(nativeGradient);
        restore();
    }

private:
    void drawGradient(const EmVal& gradient)
    {
        auto DOMPoint = EmVal::global("DOMPoint");
        if (gradient.as<bool>() == false) {
            return;
        }

        // Draw a rect the size of the entire context using the gradient brush;
        // the clipping will limit to the path.
        auto ul = DOMPoint.new_(0.0f, 0.0f);
        auto lr = DOMPoint.new_(mWidth, mHeight);
        auto ur = DOMPoint.new_(lr["x"], EmVal(0.0f));
        auto ll = DOMPoint.new_(EmVal(0.0), lr["y"]);

        // We need to transform the corners of the context-rect so that they
        // are in the same coordinate system as the current transform matrix.
        // To do that, we transform by the inverse of the transform matrix.
        auto t = mContext.call<EmVal>("getTransform");
        t.call<void>("invertSelf");
        assert(t["is2D"].as<bool>());  // if invert failed will not be 2d; all translation matrix are invertible (if not, the x or y scale is probably 0)

        ul = t.call<EmVal>("transformPoint", ul);
        ur = t.call<EmVal>("transformPoint", ur);
        lr = t.call<EmVal>("transformPoint", lr);
        ll = t.call<EmVal>("transformPoint", ll);

        // Draw the full-context rect.
        mContext.call<void>("beginPath");
        mContext.call<void>("moveTo", ul["x"], ul["y"]);
        mContext.call<void>("lineTo", ur["x"], ur["y"]);
        mContext.call<void>("lineTo", lr["x"], lr["y"]);
        mContext.call<void>("lineTo", ll["x"], ll["y"]);
        mContext.call<void>("closePath");
        mContext.set("fillStyle", gradient);
        mContext.call<void>("fill");
        // We do not need to undo the fillStyle setting, since our caller should
        // have enclosed us in a save/restore.
    }

public:

    void drawText(const char *textUTF8, const Point& topLeft,
                  const Font& font, PaintMode mode) override
    {
        Color fillColor = Color::kTransparent;
        Color strokeColor = Color::kTransparent;
        if (mode & kPaintFill) {
            fillColor = mStateStack.back().fillColor;
        }
        Text t(textUTF8, font, fillColor);
        if (mode & kPaintStroke) {
            t.setOutlineColor(mStateStack.back().strokeColor);
            t.setOutlineStrokeWidth(mStateStack.back().strokeWidth);
        }
        TextObj layout(mContext, mDPI, t,
                       Size(PicaPt(100000.0f), PicaPt(100000.0f)),
                       (Alignment::kLeft | Alignment::kTop));
        layout.draw(*this, topLeft, mode);
    }

    void drawText(const TextLayout& layout, const Point& topLeft) override
    {
        ((TextObj*)&layout)->draw(*this, topLeft, kPaintFill);
    }

    void drawImage(std::shared_ptr<DrawableImage> image,
                   const Rect& destRect) override
    {
        mContext.call<void>("drawImage", *(EmVal*)image->nativeHandle(),
                            destRect.x.toPixels(mDPI),
                            destRect.y.toPixels(mDPI),
                            destRect.width.toPixels(mDPI),
                            destRect.height.toPixels(mDPI));
    }

    void clipToRect(const Rect& rect) override
    {
        mContext.call<void>("beginPath");
        mContext.call<void>("rect",
                    rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                    rect.width.toPixels(mDPI), rect.height.toPixels(mDPI));
        mContext.call<void>("clip");
    }

    void clipToPath(std::shared_ptr<BezierPath> path) override
    {
        makePath(path);
        mContext.call<void>("clip");
    }

    Color pixelAt(int x, int y) override
    {
        return Color::kPurple;
    }

    std::shared_ptr<DrawableImage> copyToImage() override
    {
        auto imageData = mContext.call<EmVal, double, double, double, double>("getImageData", 0.0f, 0.0f, double(mWidth), double(mHeight));
        return CanvasImage::fromJSImageData(imageData, mWidth, mHeight, mDPI);
    }

    Font::Metrics fontMetrics(const Font& font) const override
    {
        auto &cf = *gFontMgr.get(font, mDPI);
        return cf.fontMetrics(mContext);
    }

    TextMetrics textMetrics(const char *textUTF8, const Font& font,
                            PaintMode mode = kPaintFill) const override
    {
        // Note that the color of the text does not matter, we aren't drawing
        // this. But we do need to be able to call this if we are not drawing,
        // so we just pick a color, instead of using the end of the state stack,
        // which might not exist yet.
        return TextObj(mContext, mDPI,
                       Text(textUTF8, font, Color::kBlack),
                       Size(PicaPt(100000.0f), PicaPt(100000.0f)),
                       (Alignment::kLeft | Alignment::kTop))
            .metrics();
    }

    void calcContextPixel(const Point& point, float *x, float *y) override
    {
        auto DOMPoint = EmVal::global("DOMPoint");
        auto p = DOMPoint.new_(EmVal(point.x.toPixels(mDPI)),
                               EmVal(point.y.toPixels(mDPI)));
        auto t = mContext.call<EmVal>("getTransform");
        auto p_canvas = t.call<EmVal>("transformPoint", p);
        if (x) {
            *x = p_canvas["x"].as<float>();
        }
        if (y) {
            *y = p_canvas["y"].as<float>();
        }
    }

protected:
    void makePath(std::shared_ptr<BezierPath> path)
    {
        ((CanvasPath*)path.get())->setPath(mContext, mDPI);
    }

protected:
    struct ContextState {
        Color fillColor;
        Color strokeColor;
        EndCapStyle endCap;
        JoinStyle joinStyle;
        PicaPt strokeWidth;
        PicaPt dashOffset = PicaPt::kZero;
        std::vector<PicaPt> dash;
    };

    mutable EmVal mContext;  // needs to be mutable for font/textMetrics calls
    std::vector<ContextState> mStateStack;
};

//-------------------------- OffscreenCanvasContext ---------------------------
class OffscreenCanvasContext : public HTMLCanvasContext
{
    using Super = HTMLCanvasContext;
public:
    OffscreenCanvasContext(EmVal canvas, EmVal context, int width, int height, float dpi)
        : HTMLCanvasContext(context, width, height, dpi)
        , mCanvas(canvas)
    {
    }

    void beginDraw() override
    {
        mImageData = EmVal();
        Super::beginDraw();
    }

    Color pixelAt(int x, int y) override
    {
        if (x < 0 || y < 0 || x >= width() || y >= height()) {
            return Color::kPurple;
        }

        cacheImageData();

        int idx = 4 * (y * width() + x);
        uint8_t r = mImageData["data"][idx    ].as<uint8_t>();
        uint8_t g = mImageData["data"][idx + 1].as<uint8_t>();
        uint8_t b = mImageData["data"][idx + 2].as<uint8_t>();
        uint8_t a = mImageData["data"][idx + 3].as<uint8_t>();
        return Color(r, g, b, a);
    }

private:
    EmVal mCanvas;
    EmVal mImageData;

    void cacheImageData()
    {
        if (!mImageData.as<bool>()) {
            mImageData = mContext.call<EmVal, int, int, int, int>("getImageData", 0, 0, width(), height());
        }
    }
};

//-----------------------------------------------------------------------------
std::shared_ptr<DrawContext> DrawContext::fromHTMLCanvas(const char *canvasId)
{
    auto document = EmVal::global("document");
    auto canvas = document.call<EmVal, std::string>("getElementById", canvasId);
    if (canvas.as<bool>()) {
        auto window = EmVal::global("window");
        float pixelRatio = window["devicePixelRatio"].as<float>();
        auto rectCSS = canvas.call<EmVal>("getBoundingClientRect");  // CSS pixels
        int widthPx = int(std::round(rectCSS["width"].as<float>() * pixelRatio));
        int heightPx = int(std::round(rectCSS["height"].as<float>() * pixelRatio));
        canvas.set("width", widthPx);
        canvas.set("height", heightPx);
        auto dpi = 96.0f * pixelRatio;
        auto context = canvas.call<EmVal, std::string>("getContext", "2d");
        return std::make_shared<HTMLCanvasContext>(context, widthPx, heightPx, dpi);
    }
    std::cerr << "[libnativedraw] DrawContext::fromHTMLCanvas() could not get canvas '" << canvasId << "'" << std::endl;
    return std::shared_ptr<DrawContext>();
}

std::shared_ptr<DrawContext> DrawContext::createOffscreenCanvasBitmap(
            int width, int height, float dpi /* = 72.0f*/)
{
    auto OffscreenCanvas = EmVal::global("OffscreenCanvas");
    if (!OffscreenCanvas.as<bool>()) {
        std::cerr << "[libnativedraw] This browser does not support OffscreenCanvas!" << std::endl;
        return nullptr;
    }
    auto canvas = OffscreenCanvas.new_(width, height);
    if (!canvas.as<bool>()) {
        std::cerr << "[libnativedraw] Could not create OffscreenCanvas of size "
                  << width << " x " << height << std::endl;
        return nullptr;
    }
    auto context = canvas.call<EmVal, std::string>("getContext", "2d");
    if (!context.as<bool>()) {
        std::cerr << "[libnativedraw] Created OffscreenCanvas but could not get '2d' context" << std::endl;
        return nullptr;
    }
    return std::make_shared<OffscreenCanvasContext>(canvas, context, width, height, dpi);
}

}  // namespace ND_NAMESPACE
#endif // __EMSCRIPTEN__
