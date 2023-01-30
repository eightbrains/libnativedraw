//-----------------------------------------------------------------------------
// Copyright 2021 - 2022 Eight Brains Studios, LLC
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

#if __APPLE__

#if ! __has_feature(objc_arc)
#error "ARC is off"
#endif

#include "nativedraw.h"

#include "nativedraw_private.h"

#include <algorithm>
#include <string>
#include <unordered_map>

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#import <AppKit/AppKit.h>

namespace ND_NAMESPACE {
namespace {
CGPathDrawingMode calcCGDrawMode(PaintMode mode)
{
    switch (mode) {
        case kPaintStroke:
            return kCGPathStroke;
        case kPaintFill:
            return kCGPathFill;
        case kPaintStrokeAndFill:
            return kCGPathFillStroke;
    }
}

} // namespace

//----------------------------- BezierPath ------------------------------------
class CoreGraphicsPath : public BezierPath
{
public:
    CoreGraphicsPath() : BezierPath() {}
    ~CoreGraphicsPath()
    {
        clearNative();
    }

    void clearNative() override
    {
        for (auto &dpi2path : mPaths) {
            CGPathRelease(dpi2path.second);
        }
        mPaths.clear();
    }
    
    void* nativePathForDPI(float dpi, bool isFilled /*ignored*/) override
    {
        auto it = mPaths.find(dpi);
        if (it == mPaths.end()) {
            auto path = CGPathCreateMutable();
            for (auto &cmd : mImpl->commands) {
                switch (cmd.cmd) {
                    case BezierPath::Impl::Command::kMoveTo:
                        CGPathMoveToPoint(path, NULL, cmd.p1.x.toPixels(dpi), cmd.p1.y.toPixels(dpi));
                        break;
                    case BezierPath::Impl::Command::kLineTo:
                        CGPathAddLineToPoint(path, NULL, cmd.p1.x.toPixels(dpi), cmd.p1.y.toPixels(dpi));
                        break;
                    case BezierPath::Impl::Command::kQuadraticTo:
                        CGPathAddQuadCurveToPoint(path, NULL,
                                                  cmd.p1.x.toPixels(dpi), cmd.p1.y.toPixels(dpi),
                                                  cmd.p2.x.toPixels(dpi), cmd.p2.y.toPixels(dpi));
                        break;
                    case BezierPath::Impl::Command::kCubicTo:
                        CGPathAddCurveToPoint(path, NULL,
                                              cmd.p1.x.toPixels(dpi), cmd.p1.y.toPixels(dpi),
                                              cmd.p2.x.toPixels(dpi), cmd.p2.y.toPixels(dpi),
                                              cmd.p3.x.toPixels(dpi), cmd.p3.y.toPixels(dpi));
                        break;
                    case BezierPath::Impl::Command::kClose:
                        CGPathCloseSubpath(path);
                        break;
                }
            }
            mPaths[dpi] = path;
            it = mPaths.find(dpi);
        }
        return it->second;
    }

private:
    std::unordered_map<float, CGMutablePathRef> mPaths;
};

//----------------------------- Gradients -------------------------------------
class CoreGraphicsGradient;
std::unordered_map<Gradient::Id, CoreGraphicsGradient*> gGradients;  // this does not own the gradients

class CoreGraphicsGradient : public Gradient
{
public:
    CoreGraphicsGradient(CGGradient *g, const GradientInfo& info)
        : mInfo(info)
    {
        mGradient = g;  // transfers ownership, no retain needed
        assert(gGradients.find(calcId()) == gGradients.end());
        gGradients[calcId()] = this;
    }

    ~CoreGraphicsGradient()
    {
        CGGradientRelease(mGradient);
        mGradient = nullptr;
        auto it = gGradients.find(calcId());
        if (it != gGradients.end()) {
            gGradients.erase(it);
        }
    }

    bool isValid() const override { return mGradient != nil; }

    Id id() const override { return calcId(); }

    CGGradient* cgGradient() const { return mGradient; }
    const GradientInfo& info() const { return mInfo; }
    
private:
    CGGradient *mGradient;
    GradientInfo mInfo;

    Id calcId() const { return (Id)mGradient; }
};

CoreGraphicsGradient* createGradient(const GradientInfo& info, float /*dpi*/)
{
    std::vector<CGFloat> components;
    std::vector<CGFloat> locations;
    components.reserve(4.0f * info.stops.size());
    locations.reserve(info.stops.size());
    for (const auto &stop : info.stops) {
        components.push_back(stop.color.red());
        components.push_back(stop.color.green());
        components.push_back(stop.color.blue());
        components.push_back(stop.color.alpha());
        locations.push_back(stop.location);
    }
    CGGradient *gradient;
    CGColorSpace *csRGB = CGColorSpaceCreateDeviceRGB();
    gradient = CGGradientCreateWithColorComponents(csRGB, components.data(),
                                                   locations.data(), info.stops.size());
    CGColorSpaceRelease(csRGB);
    return new CoreGraphicsGradient(gradient, info);  // Create() already did a retain, we transfer ownership here
}

void destroyGradient(CoreGraphicsGradient* gradient)
{
    delete gradient;
}

static ResourceManager<GradientInfo, CoreGraphicsGradient*> gGradientMgr(createGradient, destroyGradient);

//-------------------------------- Font ---------------------------------------
std::vector<std::string> Font::availableFontFamilies()
{
    NSArray<NSString*> *available = NSFontManager.sharedFontManager.availableFontFamilies;

    std::vector<std::string> fonts;
    fonts.reserve(size_t(available.count));
    for (NSString *f in available) {
        fonts.push_back(f.UTF8String);
    }
    // Turns out that these are mostly in order: alphabetized except for some Apple fonts
    // slapped on at the end. The worst-case for quick-sort, hopefully the std
    // implementors have an intelligent version.
    std::sort(fonts.begin(), fonts.end());
    return fonts;
}

//-------------------------------- Fonts --------------------------------------
namespace {
// Mapping CSS-style 0 - 900 onto 0 - 15 doesn't work too well, but it
// appears to be what WebKit does: https://bugs.webkit.org/show_bug.cgi?id=6484
std::unordered_map<FontWeight, NSInteger> gWeightTo16 = {
    { kWeightUltraLight, 1 },
    { kWeightThin, 2 },
    { kWeightLight, 4 },
    { kWeightAuto, 5 },  // we shouldn't be looking up auto, but make it Regular
    { kWeightRegular, 5 },  // docs say that this is regular weight
    { kWeightMedium, 6 },
    { kWeightSemibold, 7 },
    { kWeightBold, 9 },  // docs say that this is bold weight
    { kWeightHeavy, 12 },
    { kWeightBlack, 14 } };

std::unordered_map<NSInteger, FontWeight> g16ToWeight = {
    { 0, kWeightUltraLight },
    { 1, kWeightUltraLight },
    { 2, kWeightThin },
    { 3, kWeightThin },
    { 4, kWeightLight },
    { 5, kWeightRegular },
    { 6, kWeightMedium },
    { 7, kWeightSemibold },
    { 8, kWeightSemibold },
    { 9, kWeightBold },
    { 10, kWeightBold },
    { 11, kWeightBold },
    { 12, kWeightHeavy },
    { 13, kWeightHeavy },
    { 14, kWeightBlack },
    { 15, kWeightBlack } };

NSUInteger calcFontTraits(Style style)
{
    NSUInteger t = 0;
    if (style & kStyleItalic) {
        t |= NSItalicFontMask;
    }
    if (style & kStyleBold) {
        t |= NSBoldFontMask;
    }
    return t;
}

NSFont* createFont(const Font& desc, float dpi)
{
    NSString *nsfamily = [NSString stringWithUTF8String:desc.family().c_str()];
    auto t = calcFontTraits(desc.style());
    auto weight = desc.weight();
    assert(weight != kWeightAuto);
    NSInteger w = gWeightTo16[weight];
    CGFloat px = CGFloat(desc.pointSize().toPixels(dpi));
    return [NSFontManager.sharedFontManager
            fontWithFamily:nsfamily traits:t weight:w size:px];

}

void destroyFont(NSFont* rsrc)
{
    // Don't need to do anything, ObjC++ refcounting should do it for us
}

static ResourceManager<Font, NSFont*> gFontMgr(createFont, destroyFont);

} // namespace

//----------------------------- Text Obj --------------------------------------
class TextObj : public TextLayout
{
    static constexpr CGFloat kTextFrameHeight = 100000.0;

public:
    TextObj(const DrawContext& dc, const Text& text, const Size& size, int alignment,
            TextWrapping wrap = kWrapWord,
            const Font& defaultReplacementFont = kDefaultReplacementFont,
            const Color& defaultReplacementColor = kDefaultReplacementColor)
    {
        struct RawLine {
            int utf8start;
            int utf8end;
            Line::Type type;
            Color color;
            CGFloat width;
            CGFloat dy;
        };
        std::vector<RawLine> lines;

        auto makeNSColor = [](const Color& c) {
            return [NSColor colorWithRed:CGFloat(c.red())
                                   green:CGFloat(c.green())
                                    blue:CGFloat(c.blue())
                                   alpha:CGFloat(c.alpha())];
        };

        mLen = int(text.text().size());
        mDPI = dc.dpi();
        mFirstLineOffsetForGlyphs = PicaPt::kZero;

        assert(!text.runs().empty());
        assert(text.runs()[0].startIndex == 0);
        assert(text.runs().back().startIndex + text.runs().back().length == int(text.text().size()));

        std::vector<int> utf8to16;
        if (text.runs().size() == 1) {  // if we only have one run, it should be the entire text
            assert(text.runs()[0].startIndex == 0 && text.runs()[0].length == int(text.text().size()));
        } else {
            utf8to16 = utf16IndicesForUTF8Indices(text.text().c_str());
        }

        mUTF16To8 = utf8IndicesForUTF16Indices(text.text().c_str());

        std::vector<Font::Metrics> runMetrics;
        runMetrics.reserve(text.runs().size());

        auto *nsstring = [[NSMutableAttributedString alloc]
                          initWithString: [NSString stringWithUTF8String: text.text().c_str() ]];
        bool lastRunSetBackground = false;
        for (auto &run : text.runs()) {
            assert(run.font.isSet);
            assert(run.color.isSet);
            bool hasSuperscript = (run.superscript.isSet && run.superscript.value);
            bool hasSubscript = (run.subscript.isSet && run.subscript.value);
            CGFloat baselineOffset = 0.0;
            NSMutableDictionary *attr = [[NSMutableDictionary alloc] init];
            Font font = run.font.value;
            if (!run.font.isSet || isFamilyDefault(font)) {
                font.setFamily(defaultReplacementFont.family());
                if (isPointSizeDefault(font)) {
                    font.setPointSize(defaultReplacementFont.pointSize());
                }
            }
            if (run.pointSize.isSet) { font.setPointSize(run.pointSize.value); }
            if (run.bold.isSet) { font.setBold(run.bold.value); }
            if (run.italic.isSet) { font.setItalic(run.italic.value); }
            // Although there is NSSuperscriptAttributeName, it appears to only work for the
            // San Francisco font, and is not available on iOS (although @"NSSuperScript"
            // apparently works). So we need to do it ourselves.
            NSFont *nsfont = gFontMgr.get(font, mDPI);
            // The metrics, for purposes of determining the height of the first line
            // is the original font size.
            runMetrics.push_back(font.metrics(dc));
            if (hasSuperscript || hasSubscript) {
                font = fontSizedForSuperSubscript(font);
                NSFont *nssmall = gFontMgr.get(font, mDPI);
                // We do the super/sub-scripting by setting the baseline offset.
                // Note that macOS' origin is lower-left, instead of our upper-right
                if (hasSuperscript) {  // align top of super caps with top of regular caps
                    baselineOffset = nsfont.capHeight - nssmall.capHeight;
                } else {
                    baselineOffset = nsfont.descender - nssmall.descender;
                }
                attr[NSBaselineOffsetAttributeName] = @(baselineOffset);
                nsfont = nssmall;
            }
            // Now we can actually set the font
            attr[NSFontAttributeName] = nsfont;

            Color fg = run.color.value;
            if ((run.color.value.red() == Color::kTextDefault.red() &&
                 run.color.value.green() == Color::kTextDefault.green() &&
                 run.color.value.blue() == Color::kTextDefault.blue())) {
                fg = defaultReplacementColor;
                fg.setAlpha(run.color.value.alpha());
            }
            attr[NSForegroundColorAttributeName] = makeNSColor(fg);
            if (run.backgroundColor.isSet && run.backgroundColor.value.alpha() > 0.0f) {
                attr[NSBackgroundColorAttributeName] = makeNSColor(run.backgroundColor.value);
                lastRunSetBackground = true;
            } else {
                if (lastRunSetBackground) {
                    // This seems to be a bug in macOS? Setting background color will not unset
                    // the next run unless specifically set to clear (and not nil), although
                    // a line break resets it.
                    attr[NSBackgroundColorAttributeName] = NSColor.clearColor;
                    lastRunSetBackground = false;
                }
            }
            if (run.underlineStyle.isSet && run.underlineStyle.value != kUnderlineNone
                && !(run.underlineColor.isSet && run.underlineColor.value.alpha() == 0.0f))
            {
                if (baselineOffset == 0.0) {
                    switch (run.underlineStyle.value) {
                        case kUnderlineNone:  break;
                        case kUnderlineSingle:
                            attr[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle);
                            break;
                        case kUnderlineDouble:
                            attr[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleDouble);
                            break;
                        case kUnderlineDotted:
                            attr[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle | NSUnderlineStylePatternDot);
                            break;
                        case kUnderlineWavy: {
                            Color c = fg;
                            if (run.underlineColor.isSet) {
                                c = run.underlineColor.value;
                            }
                            lines.push_back({ run.startIndex, run.startIndex + run.length, Line::Type::kWavy,
                                              c, nsfont.underlineThickness,
                                              nsfont.ascender - baselineOffset + std::abs(nsfont.underlinePosition) });
                            break;
                        }
                    }
                    if (run.underlineColor.isSet) {
                        attr[NSUnderlineColorAttributeName] = makeNSColor(run.underlineColor.value);
                    }
                } else {
                    Color c = fg;
                    if (run.underlineColor.isSet) {
                        c = run.underlineColor.value;
                    }
                    CGFloat thickness = std::max(CGFloat(1.0), nsfont.underlineThickness);
                    Line::Type lineType = Line::Type::kSingle;
                    switch (run.underlineStyle.value) {
                        case kUnderlineNone:  break;
                        case kUnderlineSingle:
                            lineType = Line::Type::kSingle;  break;
                        case kUnderlineDouble:
                            lineType = Line::Type::kDouble;  break;
                        case kUnderlineDotted:
                            lineType = Line::Type::kDotted;  break;
                        case kUnderlineWavy: {
                            lineType = Line::Type::kWavy;  break;
                        }
                    }
                    lines.push_back({ run.startIndex, run.startIndex + run.length,
                                      lineType, c, thickness,
                                      nsfont.ascender - baselineOffset + std::abs(nsfont.underlinePosition) });
                }
            }
            if (run.strikethrough.isSet && run.strikethrough.value
                && !(run.strikethroughColor.isSet && run.strikethroughColor.value.alpha() == 0.0f))
            {
                // CTFrameDraw() does not handle strikethrough attributes, argh

                auto c = (run.strikethroughColor.isSet
                                ? run.strikethroughColor.value
                                : fg);
                // Strikethough should go through middle of x-height, so <strike>ABCabc</strike>
                // is nice and continuous. What thickness should it be? Underline thickness seems to be
                // as good as any, but truncate to increments of one unit.
                lines.push_back({ run.startIndex, run.startIndex + run.length, Line::Type::kSingle,
                                  c, std::max(1.0, nsfont.underlineThickness),
                                  nsfont.ascender - baselineOffset - 0.5f * nsfont.xHeight });
            }
            if (run.characterSpacing.isSet && run.characterSpacing.value != PicaPt::kZero) {
                attr[NSKernAttributeName] = @(run.characterSpacing.value.toPixels(mDPI));
            }

            if (run.outlineStrokeWidth.isSet && run.outlineStrokeWidth.value > PicaPt::kZero
                && !(run.outlineColor.isSet && run.outlineColor.value.alpha() == 0.0f))
            {
                // Apple's documentation says that NSStrokeWidthAttributeName is a
                // percentage of the font height (NOT the raw stroke width). Since
                // the DPI is being handled by the transform, stroke width is
                // already in the correct units (namely PicaPt), just like
                // nsfont72.pointSize is (since we got the 72 dpi version of the
                // font).
                float strokeWidth = run.outlineStrokeWidth.value.toPixels(mDPI);
                strokeWidth = strokeWidth / nsfont.pointSize * 100.0f;
                if (run.outlineColor.isSet) {
                    attr[NSStrokeColorAttributeName] = makeNSColor(run.outlineColor.value);
                }
                if (run.color.isSet && run.color.value.alpha() > 0.0f) {
                    // Negative width signals to both stroke and fill
                    attr[NSStrokeWidthAttributeName] = @(-strokeWidth);
                } else {
                    attr[NSStrokeWidthAttributeName] = @(strokeWidth);
                }
            }

            if (utf8to16.empty()) {  // common case: only one run
                [nsstring addAttributes:attr range:NSMakeRange(0, nsstring.length)];
            } else {
                auto start16 = utf8to16[run.startIndex];
                auto length16 = utf8to16[run.startIndex + run.length] - start16;
                [nsstring addAttributes:attr range:NSMakeRange(start16, length16)];
            }
        }

        NSMutableParagraphStyle *paragraphStyle = nil;
        auto horizAlign = (alignment & Alignment::kHorizMask);
        auto vertAlign = (alignment & Alignment::kVertMask);
        if ((horizAlign != Alignment::kLeft && horizAlign != Alignment::kNone) || wrap != kWrapWord) {
            paragraphStyle = [[NSMutableParagraphStyle alloc] init];
            if ((alignment & Alignment::kHorizMask) == Alignment::kRight) {
                [paragraphStyle setAlignment:NSTextAlignmentRight];
            } else if ((alignment & Alignment::kHorizMask) == Alignment::kHCenter) {
                [paragraphStyle setAlignment:NSTextAlignmentCenter];
            }
            if (wrap == kWrapNone) {
                [paragraphStyle setLineBreakMode:NSLineBreakByClipping];
            }
        }
        if (text.lineHeightMultiple() > 0.0) {
            if (paragraphStyle == nil) {
                NSMutableDictionary *attr = [[NSMutableDictionary alloc] init];
                paragraphStyle = [[NSMutableParagraphStyle alloc] init];
                attr[NSParagraphStyleAttributeName] = paragraphStyle;
                [nsstring addAttributes:attr range:NSMakeRange(0, nsstring.length)];
            }
            [paragraphStyle setLineHeightMultiple:text.lineHeightMultiple()];
        }
        if (paragraphStyle != nil) {
            NSMutableDictionary *attr = [[NSMutableDictionary alloc] init];
            attr[NSParagraphStyleAttributeName] = paragraphStyle;
            [nsstring addAttributes:attr range:NSMakeRange(0, nsstring.length)];
        }

        mLayout = CTFramesetterCreateWithAttributedString((CFAttributedStringRef)nsstring);
        CGFloat w = (size.width == PicaPt::kZero ? kTextFrameHeight : size.width.toPixels(mDPI));
        auto r = CGRectMake(0, 0, w, kTextFrameHeight);
        mPath = CGPathCreateWithRect(r, nullptr);
        mFrame = CTFramesetterCreateFrame(mLayout,
                                          CFRangeMake(0, 0), // entire string
                                          mPath, nullptr);

        mMetrics.width = PicaPt(-1);
        mMetrics.height = PicaPt(-1);
        mMetrics.advanceX = PicaPt(0);
        mMetrics.advanceY = PicaPt(0);

        // If we have multiple-sized fonts, we need to create the glyphs to see where
        // the line-breaks are.
        auto firstLineMetrics = calcFirstLineMetrics(runMetrics, text.runs());

/*            int lastLineEndIdx = int(text.text().size()) - 1;
            int lastLineNo = mGlyphs.back().line;
            PicaPt maxNewLineY = mGlyphs.back().frame.y;
            while (lastLineEndIdx >= 0 && mGlyphs[lastLineEndIdx].line == lastLineNo) {
                --lastLineEndIdx;
            }
            lastLineEndIdx += 1;
            assert(lastLineEndIdx >= 0);

            runIdx = int(runMetrics.size()) - 1;
            lastLineMetrics = runMetrics[runIdx];
            while (runIdx >= 0
                   && lastLineEndIdx >= text.runs()[runIdx].startIndex + text.runs()[runIdx].length) {
                if (runMetrics[runIdx].ascent > lastLineMetrics.ascent) {
                    lastLineMetrics = runMetrics[runIdx];
                }
                --runIdx;
            } */
        mFirstLineAscender = firstLineMetrics.ascent;

        // Cache here to speed drawing
        auto alignOffset = calcOffsetForAlignment(alignment, size, firstLineMetrics);
        mAlignmentOffsetPx = CGPointMake(alignOffset.x.toPixels(mDPI),
                                         alignOffset.y.toPixels(mDPI));

        // As far as I can tell, if -setLineHeightMultiple: is set on the paragraph style,
        // CTFrameGetLineOrigins() returns everything offset as if there were another line
        // above the first line. However, CTFrameDraw() does not include the top offset.
        // This looks like a bug in CoreText :( The documentation is ... minimal, so this
        // is an attempt to reverse-engineer what the calculation is. Save this here, so that
        // we can reduce the (already-excessive) number of space-eating member variables in
        // this class.
        if (text.lineHeightMultiple() > 0.0) {
            mFirstLineOffsetForGlyphs = -firstLineMetrics.lineHeight * (text.lineHeightMultiple() - 1.0f);
        }

        if (!lines.empty()) {
            glyphs();
            assert(mGlyphsInitialized); // paranoia: assert in case glyphs() gets optimized out

            mLines.reserve(lines.size());

            int glyphIdx = 0;
            for (auto &line : lines) {
                while (glyphIdx < mGlyphs.size() && mGlyphs[glyphIdx].index < line.utf8start) {
                    ++glyphIdx;
                }
                if (glyphIdx >= mGlyphs.size()) {
                    break;
                }

                auto yPt = mGlyphs[glyphIdx].frame.y;  // this is valid, otherwise we would have exited above
                auto xStartPt = mGlyphs[glyphIdx].frame.x;
                auto xEndPt = xStartPt;
                while (glyphIdx < mGlyphs.size() && mGlyphs[glyphIdx].index < line.utf8end) {
                    if (mGlyphs[glyphIdx].frame.y > yPt) {
                        CGFloat y = std::trunc(yPt.toPixels(mDPI) + line.dy) - 0.5;  // floor(x) differs for +ve and -ve x!
                        auto p1 = CGPointMake(xStartPt.toPixels(mDPI), y);
                        auto p2 = CGPointMake(xEndPt.toPixels(mDPI), y);
                        mLines.push_back({ line.type, line.color, line.width, { p1, p2 } });
                        yPt = mGlyphs[glyphIdx].frame.y;
                        xStartPt = mGlyphs[glyphIdx].frame.x;
                    }
                    xEndPt = mGlyphs[glyphIdx].frame.maxX();
                    ++glyphIdx;
                }
                CGFloat y = std::trunc(yPt.toPixels(mDPI) + line.dy) - 0.5;
                auto p1 = CGPointMake(xStartPt.toPixels(mDPI), y);
                auto p2 = CGPointMake(xEndPt.toPixels(mDPI), y);
                mLines.push_back({ line.type, line.color, line.width, { p1, p2 } });
            }
        }
        if (mGlyphsInitialized) {
            mGlyphs.clear();
            mGlyphs.shrink_to_fit();  // clear() does not release memory
            mGlyphsInitialized = false;
        }
    }

    ~TextObj()
    {
        CFRelease(mFrame);
        CFRelease(mLayout);
        CGPathRelease(mPath);
    }

    CTFrameRef ctframe() const { return mFrame; }

    // CTLineGetTypographicBounds sometimes gives ridiculous values for descent for
    // certain fonts ("Khmer MN", "Myanmar MN") in macOS 10.14 (and later?). It turns
    // out that the function to get the bounds on the *run* works fine. So we write our
    // own.
    static void _ctLineGetTypographicBounds(NSArray *lines, int idx,
                                            CGFloat *ascent, CGFloat *descent, CGFloat *leading)
    {
        *ascent = 0.0;
        *descent = 0.0;
        *leading = 0.0;
        NSArray *runs = (NSArray*)CTLineGetGlyphRuns((CTLineRef)[lines objectAtIndex:0]);
        int nRuns = runs.count;
        for (int r = 0;  r < nRuns;  ++r) {
            CTRunRef run = (__bridge CTRunRef)[runs objectAtIndex:r];
            CGFloat ascentRun, descentRun, leadingRun;
            CTRunGetTypographicBounds(run, CFRangeMake(0, 0),
                                      &ascentRun, &descentRun, &leadingRun);
            *descent = std::max(*descent, descentRun);
            *ascent = std::max(*ascent, ascentRun);
            *leading = std::max(*leading, leadingRun);
        }
    }

    const TextMetrics& metrics() const override
    {
        if (mMetrics.width >= PicaPt::kZero) {
            return mMetrics;
        }

        mMetrics.width = PicaPt(0);
        mMetrics.height = PicaPt(0);
        mMetrics.advanceX = PicaPt(0);
        mMetrics.advanceY = PicaPt(0);

        NSArray* lines = (NSArray*)CTFrameGetLines(mFrame);
        if (lines.count == 0) {  // early return here so we can assume lines > 0 later
            return mMetrics;
        }

        double width;
        CGFloat ascent, descent, leading;
        for (int i = 0;  i < lines.count;  ++i) {
            width = CTLineGetTypographicBounds((CTLineRef)[lines objectAtIndex:i],
                                               &ascent, &descent, &leading);
            mMetrics.width = std::max(mMetrics.width, PicaPt::fromPixels(width, mDPI));
        }
        mMetrics.advanceX = mMetrics.width;
        if (lines.count == 1) {
            _ctLineGetTypographicBounds(lines, 0, &ascent, &descent, &leading);
            mMetrics.height = PicaPt::fromPixels(ascent + descent, mDPI);
        } else {
            std::vector<CGPoint> lineOrigins;
            lineOrigins.resize(lines.count);
            CTFrameGetLineOrigins(mFrame, CFRangeMake(0, 0 /* 0=all */), &lineOrigins[0]);  // copies, grr
            CGFloat startY = lineOrigins[0].y + ascent;
            _ctLineGetTypographicBounds(lines, lines.count - 1, &ascent, &descent, &leading);
            CGFloat endY = lineOrigins.back().y - descent;
            // These are OS text coordinates which are flipped: the top of the text is at mFrame.size.height
            // and the bottom of the text is mFrame.size.height - height. So endY < startY.
            mMetrics.height = PicaPt::fromPixels(startY - endY, mDPI);
        }
        mMetrics.advanceY = PicaPt::fromPixels(leading, mDPI);

        // Note: This does not produce the same result as NSAttributedString -size, at least for
        // the height value. I am not sure what the difference is, or if it is important.
        // In practice, the only problem would be aligning with kBottom, possibly even only
        // with Text::lineHeightMultiple() set.

        return mMetrics;
    }

    const std::vector<Glyph>& glyphs() const override
    {
        if (!mGlyphsInitialized) {
            Point alignmentOffset(PicaPt::fromPixels(mAlignmentOffsetPx.x, mDPI),
                                  PicaPt::fromPixels(mAlignmentOffsetPx.y, mDPI));
            NSArray* lines = (NSArray*)CTFrameGetLines(mFrame);
            std::vector<CGPoint> lineOrigins;
            lineOrigins.resize(lines.count);
            CTFrameGetLineOrigins(mFrame, CFRangeMake(0, 0 /* 0=all */), &lineOrigins[0]);  // copies, grr
            int nGlyphs = 0;
            for (int i = 0;  i < lines.count;  ++i) {
                nGlyphs += CTLineGetGlyphCount((CTLineRef)[lines objectAtIndex:i]);
            }
            mGlyphs.reserve(nGlyphs);

            CGFloat x = 0.0;
            for (int i = 0;  i < lines.count;  ++i) {
                CGFloat lineAscent, lineDescent, lineLeading;
                CTLineGetTypographicBounds((CTLineRef)[lines objectAtIndex:i],
                                           &lineAscent, &lineDescent, &lineLeading);
                PicaPt baselinePt = PicaPt::fromPixels(kTextFrameHeight - lineOrigins[i].y, mDPI) + mFirstLineOffsetForGlyphs;
                x = lineOrigins[i].x;
                NSArray *runs = (NSArray*)CTLineGetGlyphRuns((CTLineRef)[lines objectAtIndex:i]);
                int nRuns = runs.count;
                for (int r = 0;  r < nRuns;  ++r) {
                    CTRunRef run = (__bridge CTRunRef)[runs objectAtIndex:r];
                    int n = CTRunGetGlyphCount(run);
                    const CGPoint *positions = CTRunGetPositionsPtr(run);
                    const CGSize *advances = CTRunGetAdvancesPtr(run);
                    const CFIndex *indices = CTRunGetStringIndicesPtr(run);
                    CGFloat ascent, descent, leading;
                    CTRunGetTypographicBounds(run, CFRangeMake(0, 0),
                                              &ascent, &descent, &leading);
                    PicaPt yPt = baselinePt - PicaPt::fromPixels(ascent, mDPI);
                    PicaPt hPt = PicaPt::fromPixels(ascent + descent, mDPI);
                    for (int g = 0;  g < n;  ++g) {
                        if (!mGlyphs.empty()) {
                            mGlyphs.back().indexOfNext = mUTF16To8[indices[g]];
                        }
                        mGlyphs.push_back({
                            mUTF16To8[indices[g]], i,
                            Rect(alignmentOffset.x + PicaPt::fromPixels(x + positions[g].x, mDPI),
                                 alignmentOffset.y + yPt,
                                 PicaPt::fromPixels(advances[g].width, mDPI),
                                 hPt),
                            });
                    }
                }
            }
            if (!mGlyphs.empty()) {
                mGlyphs.back().indexOfNext = mLen;
            }

            mGlyphsInitialized = true;
        }
        return mGlyphs;
    }

    void draw(DrawContext& dc, const Point& topLeft) const
    {
        // You'd think that adding the ascent to the upper left y would give the
        // baseline, but it can be off by a pixel (notably between Arial and Helvetica).
        // Use the line origin for more consistent results.
        CGPoint origin;
        CTFrameGetLineOrigins(mFrame, CFRangeMake(0, 1), &origin);

        CGContextRef gc = (CGContextRef)dc.nativeDC();
        // CTFrameDraw does not reset graphics state, also text needs y-axis inverted
        CGContextSaveGState(gc);
        CGContextSetTextMatrix(gc, CGAffineTransformIdentity);
        // (Note that macOS will properly align the text to the pixel boundary; we don't need to.)
        // Note that we've scaled the coordinates so that one unit is one PicaPt, not one pixel.
        // TopLeft *is* in PicaPt; origin and nsfont.ascender are in text-bitmap units which apparently
        // take into account the scale factor.
        auto dpi = dc.dpi();
        CGContextTranslateCTM(gc,
                              mAlignmentOffsetPx.x + topLeft.x.toPixels(dpi),
                              mAlignmentOffsetPx.y + topLeft.y.toPixels(dpi) + origin.y + mFirstLineAscender.toPixels(dpi) - /*PicaPt::fromPixels(1, dpi).asFloat()*/1.0);
        CGContextScaleCTM(gc, 1, -1);
        CTFrameDraw(mFrame, gc);
        CGContextRestoreGState(gc); // ok, graphics state is sane

        // CTFrameDraw() does not handle strikethroughs and does not even support wavy underlines,
        // so we have to do these ourselves. macOS appears to draw underlines on top of the text,
        // which I think is not great, but at least it is convenient for this.
        if (!mLines.empty()) {
            CGContextSaveGState(gc);
            CGContextTranslateCTM(gc, topLeft.x.toPixels(dpi), topLeft.y.toPixels(dpi));
            CGContextSetLineCap(gc, kCGLineCapButt);
            CGContextSetLineDash(gc, 0.0, nullptr, 0);
            int glyphIdx = 0;
            for (auto &line : mLines) {
                CGContextSetLineWidth(gc, line.width);
                CGContextSetRGBStrokeColor(gc, line.color.red(), line.color.green(),
                                           line.color.blue(), line.color.alpha());
                switch (line.type) {
                    case Line::Type::kSingle:
                        CGContextStrokeLineSegments(gc, line.line, 2);
                        break;
                    case Line::Type::kDouble:
                        CGContextStrokeLineSegments(gc, line.line, 2);
                        CGContextTranslateCTM(gc, 0.0, 2.0 * line.width);
                        CGContextStrokeLineSegments(gc, line.line, 2);
                        CGContextTranslateCTM(gc, 0.0, -2.0 * line.width);
                        break;
                    case Line::Type::kDotted: {
                        CGFloat dash = PicaPt(3).toPixels(mDPI);
                        CGContextSetLineDash(gc, 0.0, &dash, 1);
                        CGContextStrokeLineSegments(gc, line.line, 2);
                        CGContextSetLineDash(gc, 0.0, nullptr, 0);
                        break;
                    }
                    case Line::Type::kWavy: {
                        auto pts = createWavyLinePoints(line.line[0].x, line.line[0].y, line.line[1].x, line.width);
                        CGContextBeginPath(gc);
                        CGContextMoveToPoint(gc, pts[0], pts[1]);
                        for (size_t i = 2;  i < pts.size();  i += 2) {
                            CGContextAddLineToPoint(gc, pts[i], pts[i + 1]);
                        }
                        CGContextStrokePath(gc);
                        break;
                    }
                }
            }
            CGContextRestoreGState(gc);
        }
    }

private:
    struct Line {
        enum class Type { kSingle, kDouble, kDotted, kWavy } type;
        Color color;
        CGFloat width;
        CGPoint line[2];
    };

    int mLen;
    float mDPI;
    std::vector<Line> mLines;
    CGPathRef mPath;
    CTFramesetterRef mLayout;
    CTFrameRef mFrame;
    CGPoint mAlignmentOffsetPx;
    PicaPt mFirstLineAscender;
    PicaPt mFirstLineOffsetForGlyphs;
    std::vector<int> mUTF16To8;
    mutable std::vector<Glyph> mGlyphs;
    mutable bool mGlyphsInitialized = false;
    mutable TextMetrics mMetrics;
};

//------------------------ CoreGraphicsImage ----------------------------------
class CoreGraphicsImage : public DrawableImage
{
public:
    CoreGraphicsImage(CGImageRef image, int width, int height, float dpi)
        : DrawableImage(image, width, height, dpi)
    {}

    ~CoreGraphicsImage()
    {
        if (mNativeHandle) {
            CGImageRelease((CGImageRef)mNativeHandle);
        }
    }
};

void releaseImageProvider_internal(void *info, const void *data, size_t size)
{
    delete [] (char*)data;
}

//------------------------------ Image ----------------------------------------
void calcNSImageDetails(NSImage *nsimage, int *width, int *height, float *dpi)
{
    // NSImage.size is not the actual pixel size
    *width = 0;
    *height = 0;
    CGFloat cgWidth = 0.0;
    *dpi = kDefaultImageDPI;
    for (NSImageRep* rep in nsimage.representations) {
        if (rep.size.width > cgWidth) {
            *width = rep.pixelsWide;
            *height = rep.pixelsHigh;
            cgWidth = rep.size.width;
            *dpi = float(*width) / cgWidth * 96.0f;  // 96 dpi what a macOS virtual-pixel is
        }
    }
}

Image Image::fromFile(const char *path)
{
    // TODO: try -imageNamed:

    auto data = readFile(path);
    return Image::fromEncodedData(data.data(), data.size());
}

Image Image::fromEncodedData(const uint8_t *encodedImage, int size)
{
    NSData *data = [NSData dataWithBytes:encodedImage length:size];  // copies bytes (dataWithBytesNoCopy:length: doesn't)
    NSImage *img = [[NSImage alloc] initWithData:data];
    if (img == nil) {
        return Image();  // not an image we can decode
    }
    if (img.representations.count == 0) {
        return Image();  // corrupted image we thought we could decode
    }

    int width, height;
    float dpi;
    calcNSImageDetails(img, &width, &height, &dpi);

    CGImageRef ref = [img CGImageForProposedRect:nil context:nil hints:nil];
    CGImageRetain(ref);
    return Image((void*)ref, width, height, dpi,
                 [](void* p) { CGImageRelease((CGImageRef)p); });
}

Image Image::fromCopyOfBytes(const uint8_t *bytes, int w, int h,
                             ImageFormat f, float dpi /*= 0.0 */)
{
    size_t size = size_t(calcPixelBytes(f) * w * h);
    uint8_t *copy = new uint8_t[size];
    memcpy(copy, bytes, size);
    return Image(copy, size, w, h, f, dpi);
}

//----------------------- CoreGraphicsContext ---------------------------------
// Apple really makes it difficult to draw text without using NSAttributedString.
// This requires that we know the color, stroke color, and stroke width at the
// time of drawing, which means we need to record them. But we also have to
// include a stack so that save/restore works.
class CoreGraphicsContext : public DrawContext
{
    using Super = DrawContext;
private:
    struct ContextState {
        Color fillColor;
        Color strokeColor;
        PicaPt strokeWidth;
        EndCapStyle endCapStyle;
        JoinStyle joinStyle;
    };

    std::vector<ContextState> mStateStack;

public:
    CoreGraphicsContext(void *cgcontext, int width, int height, float dpi, float nativeDPI)
        : DrawContext(cgcontext, width, height, dpi, nativeDPI)
    {
        if (cgcontext) {
            setNativeDC(cgcontext);
        } else {
            mStateStack.push_back(ContextState());
        }
    }

    std::shared_ptr<DrawContext> createBitmap(BitmapType type,
                                              int width, int height,
                                              float dpi /*= 72.0f*/) override
    {
        return DrawContext::createCoreGraphicsBitmap(type, width, height, dpi);
    }

    std::shared_ptr<DrawableImage> createDrawableImage(const Image& image) const override
    {
        if (image.format() == kImageEncodedData_internal) {
            CGImageRef ref = (CGImageRef)(*(void**)image.data());
            CGImageRetain(ref);
            return std::make_shared<CoreGraphicsImage>(ref, image.widthPx(), image.heightPx(), image.dpi());
        } else {
            int bitsPerChannel, bitsPerPixel;
            CGBitmapInfo info = kCGImageAlphaNone;

            int width = image.widthPx();
            int height = image.heightPx();
            float dpi = image.dpi();

            switch (image.format()) {
                case kImageRGBA32:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    info = kCGImageAlphaLast | kCGBitmapByteOrder32Big;
                    break;
                case kImageRGBA32_Premultiplied:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    info = kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big;
                    break;
                case kImageBGRA32:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    info = kCGImageAlphaFirst | kCGBitmapByteOrder32Little;
                    break;
                case kImageBGRA32_Premultiplied:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    // Alpha is last, but because the endianness is reversed, it becomes first
                    info = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
                    break;
                case kImageARGB32:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    // Alpha is last, but because the endianness is reversed, it becomes first
                    info = kCGImageAlphaFirst | kCGBitmapByteOrder32Big;
                    break;
                case kImageARGB32_Premultiplied:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    info = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Big;
                    break;
                case kImageABGR32:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    // Alpha is first, but because the endianness is reversed, it becomes last
                    info = kCGImageAlphaLast | kCGBitmapByteOrder32Little;
                    break;
                case kImageABGR32_Premultiplied:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    // Alpha is first, but because the endianness is reversed, it becomes last
                    info = kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Little;
                    break;
                case kImageRGBX32:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    info = kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big;
                    break;
                case kImageBGRX32:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    // Alpha is last, but because the endianness is reversed, it becomes first
                    info = kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little;
                    break;
                case kImageRGB24:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    info = kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big;
                    break;
                case kImageBGR24:
                    bitsPerChannel = 8;
                    bitsPerPixel = 32;
                    // Alpha is last, but because the endianness is reversed, it becomes first
                    info = kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little;
                    break;
                case kImageGreyscaleAlpha16:
                    bitsPerChannel = 8;
                    bitsPerPixel = 16;
                    info = kCGImageAlphaLast | kCGBitmapByteOrderDefault;
                    break;
                case kImageGreyscale8:
                    bitsPerChannel = 8;
                    bitsPerPixel = 8;
                    info = kCGImageAlphaNone | kCGBitmapByteOrderDefault;
                    break;
                case kImageEncodedData_internal:
                    assert(false);
                    break;
            }
            size_t bytesPerRow = size_t(width) * size_t(bitsPerPixel / 8);
            size_t length = bytesPerRow * size_t(height);
            char* dataCopy = new char[length];
            if (image.format() != kImageRGB24 && image.format() != kImageBGR24) {
                memcpy(dataCopy, image.data(), image.size());
            } else {
                auto *src = image.data();
                auto *dst = dataCopy;
                auto *end = src + 3 * width * height;
                while (src < end) {
                    *dst++ = *src++;
                    *dst++ = *src++;
                    *dst++ = *src++;
                    *dst++ = (char)0x0;
                }
                assert(src == end);
                assert(dst == dataCopy + length);
            }

            CGDataProviderRef dataProvider = CGDataProviderCreateWithData(nullptr, // info for release callback
                                                                          dataCopy, // data pointer
                                                                          length,
                                                                          releaseImageProvider_internal);
            CGColorSpaceRef colorspace;
            if (image.format() == kImageGreyscaleAlpha16 || image.format() == kImageGreyscale8)  {
                colorspace = CGColorSpaceCreateDeviceGray();
            } else {
                colorspace = CGColorSpaceCreateDeviceRGB();
            }
            auto img = std::make_shared<CoreGraphicsImage>(
                            CGImageCreate(width, height, bitsPerChannel, bitsPerPixel, bytesPerRow,                     colorspace,
                                          info,
                                          dataProvider,
                                          nullptr, // decode array
                                          true,  // should interpolate when drawing to higher resolution
                                          // On the assumption the default intent is faster, we only use the
                                          // perceptual intent (which the docs say is good for photos) for
                                          // "large" images, so that icon images will be faster.
                                          (width < 256 || height < 256)
                                              ? kCGRenderingIntentDefault
                                              : kCGRenderingIntentPerceptual),
                            width, height, dpi);
            CGDataProviderRelease(dataProvider); // CGImageCreate retains, but we still need to release
            CGColorSpaceRelease(colorspace);
            return img;
        }
    }

    std::shared_ptr<BezierPath> createBezierPath() const override
    {
        return std::make_shared<CoreGraphicsPath>();
    }

    std::shared_ptr<TextLayout> createTextLayout(
                         const char *utf8, const Font& font, const Color& color,
                         const Size& size /*= Size::kZero*/,
                         int alignment /*= Alignment::kAlign | Alignment::kTop*/,
                         TextWrapping wrap /*= kWrapWord*/) const override
    {
        return std::make_shared<TextObj>(*this, Text(utf8, font, color),
                                         size, alignment, wrap);
    }

    std::shared_ptr<TextLayout> createTextLayout(
                const Text& t,
                const Size& size = Size::kZero,
                int alignment = Alignment::kLeft | Alignment::kTop,
                TextWrapping wrap = kWrapWord) const override
    {
        return std::make_shared<TextObj>(*this, t, size, alignment, wrap);
    }

    std::shared_ptr<TextLayout> createTextLayout(
                        const Text& t,
                        const Font& defaultReplacementFont,
                        const Color& defaultReplacementColor,
                        const Size& size /*= Size::kZero*/,
                        int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                        TextWrapping wrap /*= kWrapWord*/) const override
    {
        return std::make_shared<TextObj>(*this, t, size, alignment, wrap,
                                         defaultReplacementFont, defaultReplacementColor);

    }

    Gradient& getGradient(const std::vector<Gradient::Stop>& stops) override
    {
        GradientInfo info = { stops };
        return *gGradientMgr.get(info, 72.0f);
    }

    Gradient& getGradient(size_t id) const override
    {
        static CoreGraphicsGradient gNullGradient(nullptr, GradientInfo());

        auto it = gGradients.find(id);
        if (it != gGradients.end()) {
            return *it->second;
        }
        return gNullGradient;
    }

    void setNativeDC(void *nativeDC)
    {
        mNativeDC = nativeDC;

        CGContextRef gc = (CGContextRef)nativeDC;
        CGContextTranslateCTM(gc, 0, mHeight);
        CGContextScaleCTM(gc, 1, -1);

        mStateStack.clear();
        mStateStack.push_back(ContextState());
     
        // Set the initial state *after* creating the state stack,
        // so that the state setting functions will set the state properly.
        setInitialState();
    }

    void beginDraw() override {}
    void endDraw() override {}

    void save() override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextSaveGState(gc);
        mStateStack.push_back(mStateStack.back());
    }

    void translate(const PicaPt& dx, const PicaPt& dy) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextTranslateCTM(gc, dx.toPixels(mDPI), dy.toPixels(mDPI));
    }

    void rotate(float degrees) override
    {
        static CGFloat toRadians = M_PI / 180.f;
        CGContextRef gc = (CGContextRef)mNativeDC;
        // Our coordinate system is y-flipped with respect to CoreGraphics' mathematical
        // one, which make +angle rotate clockwise. We still want +angle to be counterclockwise
        // so that the angle works like people expect it, so we need to negate it.
        CGContextRotateCTM(gc, -degrees * toRadians);
    }

    void scale(float sx, float sy) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextScaleCTM(gc, sx, sy);
    }

    void calcContextPixel(const Point& point, float *x, float *y) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGAffineTransform t = CGContextGetCTM(gc);
        CGPoint p = CGPointMake(point.x.toPixels(mDPI), point.y.toPixels(mDPI));
        p = CGPointApplyAffineTransform(p, t);
        if (x) {
            *x = float(p.x);
        }
        if (y) {
            *y = float(p.y);
        }
    }

    void restore() override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextRestoreGState(gc);
        mStateStack.pop_back();
    }

    void fill(const Color& color) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        if (color.alpha() < 1.0f) {
            CGContextClearRect(gc, CGRectMake(0, 0, mWidth, mHeight));
        }
        if (color.alpha() > 0.0f) {
            // We DON'T want to change the fill color state, but we do want
            // to take advantage of future improvements to Color (such as more
            // colorspaces).
            Color orig = mStateStack.back().fillColor;
            setFillColor(color);
            CGContextFillRect(gc, CGRectMake(0, 0, mWidth, mHeight));
            setFillColor(orig);
        }
    }

    void clearRect(const Rect& rect) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextClearRect(gc, CGRectMake(rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                                          rect.width.toPixels(mDPI), rect.height.toPixels(mDPI)));
    }

    Color fillColor() const override { return mStateStack.back().fillColor; }

    Color strokeColor() const override { return mStateStack.back().strokeColor; }

    PicaPt strokeWidth() const override
        { return mStateStack.back().strokeWidth; }

    EndCapStyle strokeEndCap() const override
        { return mStateStack.back().endCapStyle; }

    JoinStyle strokeJoinStyle() const override
        { return mStateStack.back().joinStyle; }

    void setFillColor(const Color& color) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextSetRGBFillColor(gc, color.red(), color.green(),
                                 color.blue(), color.alpha());
        mStateStack.back().fillColor = color;
    }

    void setStrokeColor(const Color& color) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextSetRGBStrokeColor(gc, color.red(), color.green(),
                                   color.blue(), color.alpha());
        mStateStack.back().strokeColor = color;
    }

    void setStrokeWidth(const PicaPt& w) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextSetLineWidth(gc, w.toPixels(mDPI));
        mStateStack.back().strokeWidth = w;
    }

    void setStrokeEndCap(EndCapStyle cap) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        switch (cap) {
            case kEndCapButt:
                CGContextSetLineCap(gc, kCGLineCapButt); break;
            case kEndCapRound:
                CGContextSetLineCap(gc, kCGLineCapRound); break;
            case kEndCapSquare:
                CGContextSetLineCap(gc, kCGLineCapSquare); break;
        }
        mStateStack.back().endCapStyle = cap;
    }

    void setStrokeJoinStyle(JoinStyle join) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        switch (join) {
            case kJoinMiter:
                CGContextSetLineJoin(gc, kCGLineJoinMiter); break;
            case kJoinRound:
                CGContextSetLineJoin(gc, kCGLineJoinRound); break;
            case kJoinBevel:
                CGContextSetLineJoin(gc, kCGLineJoinBevel); break;
        }
        mStateStack.back().joinStyle = join;
    }

    void setStrokeDashes(const std::vector<PicaPt> lengths, const PicaPt& offset) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        std::vector<CGFloat> lengthsPx;
        lengthsPx.reserve(lengths.size());
        for (size_t i = 0;  i < lengths.size();  ++i) {
            lengthsPx.push_back(lengths[i].toPixels(mDPI));
        }
        // CoreGraphics seems to offset away from the line direction, whereas
        // it seems more intuitive for a positive offset to go forward.
        CGContextSetLineDash(gc, -offset.toPixels(mDPI), lengthsPx.data(), lengthsPx.size());
    }

    void drawLines(const std::vector<Point>& lines) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        std::vector<CGPoint> pts;
        pts.reserve(2 * lines.size());
        for (auto &p : lines) {
            pts.push_back(CGPointMake(p.x.toPixels(mDPI), p.y.toPixels(mDPI)));
        }
        CGContextAddLines(gc, pts.data(), pts.size());
        CGContextStrokePath(gc);
    }

    void drawRect(const Rect& rect, PaintMode mode) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextAddRect(gc, CGRectMake(rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                                        rect.width.toPixels(mDPI), rect.height.toPixels(mDPI)));
        CGContextDrawPath(gc, calcCGDrawMode(mode));
    }

    // drawRoundedRect is in nativedraw.cpp

    void drawEllipse(const Rect& rect, PaintMode mode) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        auto r = CGRectMake(rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                            rect.width.toPixels(mDPI), rect.height.toPixels(mDPI));
        CGContextAddEllipseInRect(gc, r);
        CGContextDrawPath(gc, calcCGDrawMode(mode));
    }

    void drawPath(std::shared_ptr<BezierPath> path, PaintMode mode) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextAddPath(gc, (CGPathRef)path->nativePathForDPI(mDPI, false));
        CGContextDrawPath(gc, calcCGDrawMode(mode));
    }

    void drawLinearGradientPath(std::shared_ptr<BezierPath> path, Gradient& gradient,
                                const Point& start, const Point& end) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        if (auto *cgg = dynamic_cast<CoreGraphicsGradient*>(&gradient)) {
            save();
            clipToPath(path);
            CGContextDrawLinearGradient(
                gc, cgg->cgGradient(),
                CGPointMake(start.x.toPixels(mDPI), start.y.toPixels(mDPI)),
                CGPointMake(end.x.toPixels(mDPI), end.y.toPixels(mDPI)),
                kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
            restore();
        }
    }

    void drawRadialGradientPath(std::shared_ptr<BezierPath> path, Gradient& gradient,
                                const Point& start, const PicaPt& startRadius,
                                const Point& end, const PicaPt& endRadius) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        if (auto *cgg = dynamic_cast<CoreGraphicsGradient*>(&gradient)) {
            save();
            clipToPath(path);
            CGContextDrawRadialGradient(
                gc, cgg->cgGradient(),
                CGPointMake(start.x.toPixels(mDPI), start.y.toPixels(mDPI)),
                startRadius.toPixels(mDPI),
                CGPointMake(end.x.toPixels(mDPI), end.y.toPixels(mDPI)),
                endRadius.toPixels(mDPI),
                kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
            restore();
        }
    }

    void drawText(const char *textUTF8, const Point& topLeft, const Font& font,
                  PaintMode mode) override
    {
        auto textObj = textLayoutForCurrent(textUTF8, font, mode);
        drawText(textObj, topLeft);
    }

    void drawText(const TextLayout& layout, const Point& p) override
    {
        // We know we have a TextObj, because that is the only thing
        // we give out, so dynamic casting would be unnecessarily slow,
        // which is not what you want in your drawing functions. But we
        // do not want to make it a virtual function, either, otherwise
        // it end up visible in the user-visible interface.
        const TextObj *to = static_cast<const TextObj*>(&layout);
        to->draw(*this, p);
    }

    void drawImage(std::shared_ptr<DrawableImage> image, const Rect& destRect) override
    {
        // We need to flip coordinates, as the blit will just write directly
        // into the context's buffer, whose +y is opposite to ours (namely up from
        // lower left instead of down from top right). The context may have all
        // kinds of transformations so we cannot use mHeight or anything. So:
        // 1) translate the coordinate system by the height of the bitmap, so that
        //    the rect will start from the "bottom" and end at the "top".
        // 2) flip the y direction so that the bitmap will draw right-side up
        //    in the (flipped) internal buffer.
        // 3) offset by -destRect.y, since +y is flipped.
        CGContextRef gc = (CGContextRef)mNativeDC;
        auto cgrect = CGRectMake(destRect.x.toPixels(mDPI), -destRect.y.toPixels(mDPI),
                                 destRect.width.toPixels(mDPI), destRect.height.toPixels(mDPI));
        CGContextSaveGState(gc);
        CGContextTranslateCTM(gc, 0, destRect.height.toPixels(mDPI));
        CGContextScaleCTM(gc, 1, -1);
        CGContextDrawImage(gc, cgrect, (CGImageRef)image->nativeHandle());
        CGContextRestoreGState(gc);
    }

    void clipToRect(const Rect& rect) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextClipToRect(gc, CGRectMake(rect.x.toPixels(mDPI), rect.y.toPixels(mDPI),
                                           rect.width.toPixels(mDPI), rect.height.toPixels(mDPI)));
    }

    void clipToPath(std::shared_ptr<BezierPath> path) override
    {
        CGContextRef gc = (CGContextRef)mNativeDC;
        CGContextAddPath(gc, (CGPathRef)path->nativePathForDPI(mDPI, false));
        CGContextClip(gc);
    }

    Font::Metrics fontMetrics(const Font& font) const override
    {
        NSFont *nsfont = gFontMgr.get(font, mDPI);
        Font::Metrics m;
        m.ascent = PicaPt::fromPixels(nsfont.ascender, mDPI);
        m.descent = PicaPt::fromPixels(abs(nsfont.descender), mDPI);
        m.leading = PicaPt::fromPixels(abs(nsfont.leading), mDPI);
        m.xHeight = PicaPt::fromPixels(nsfont.xHeight, mDPI);
        m.capHeight = PicaPt::fromPixels(nsfont.capHeight, mDPI);
        m.lineHeight = m.ascent + m.descent + m.leading;
        m.underlineOffset = PicaPt::fromPixels(-nsfont.underlinePosition, mDPI); // macOS +y is up; we go down
        m.underlineThickness = PicaPt::fromPixels(nsfont.underlineThickness, mDPI);
        return m;
    }

    TextMetrics textMetrics(const char *textUTF8, const Font& font,
                            PaintMode mode /*=kPaintFill*/) const override
    {
        TextMetrics tm;
        tm.width = PicaPt(0);
        tm.height = PicaPt(0);
        tm.advanceX = PicaPt(0);
        tm.advanceY = PicaPt(0);

        auto textObj = textLayoutForCurrent(textUTF8, font, mode);
        auto ctframe = textObj.ctframe();
        NSArray* lines = (NSArray*)CTFrameGetLines(ctframe);
        double width;
        CGFloat ascent, descent, leading;
        for (int i = 0;  i < lines.count;  ++i) {
            width = CTLineGetTypographicBounds((CTLineRef)[lines objectAtIndex:i],
                                               &ascent, &descent, &leading);
            tm.width = std::max(tm.width, PicaPt::fromPixels(width, mDPI));
            tm.height += PicaPt::fromPixels(ascent + descent, mDPI);
            if (i < lines.count - 1) {
                tm.height += PicaPt::fromPixels(leading, mDPI);
            } else {
                tm.advanceY += PicaPt::fromPixels(leading, mDPI);
            }
        }
        tm.advanceX = tm.width;
        if (lines.count > 1) {
            tm.advanceY += tm.height;  // height, plus leading from above
        } else {
            tm.advanceY = PicaPt(0);
        }

        return tm;
    }

    TextObj textLayoutForCurrent(const char *textUTF8, const Font& font,
                                 PaintMode mode) const
    {
        Text t(textUTF8, font, Color::kBlack);
        if (mode & kPaintFill) {
            t.setColor(fillColor());
        } else {
            t.setColor(Color::kTransparent);
        }
        if (mode & kPaintStroke) {
            t.setOutlineColor(strokeColor());
            t.setOutlineStrokeWidth(strokeWidth());
        }
        return TextObj(*this, t, Size::kZero, Alignment::kNone);
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
};
//-----------------------------------------------------------------------------
class CoreGraphicsBitmap : public CoreGraphicsContext {
private:
    BitmapType mType;
    int mBytesPerPixel;

    unsigned char *mData = nullptr;
    CGColorSpaceRef mColorspace = nullptr;

public:
    CoreGraphicsBitmap(BitmapType type, int width, int height, float dpi /*= 72.0f*/)
        : CoreGraphicsContext(nullptr, width, height, dpi, dpi)
    {
        mType = type;
        CGImageAlphaInfo alphaInfo;
        // If we create an image from this bitmap, drawing is really slow in Mojave (10.14)
        // using the default "Color LCD" profile. Using "sRGB IEC61966-2.1" gives
        // something like a 5X speedup. (It also increases speed of rects).
        // Using the colorspace of the monitor does not change anything, regardless
        // of whether we pass window.colorSpace.CGColorSpace here or use
        // CGDisplayCopyColorSpace(CGMainDisplayID()). Also, using
        // CGColorSpaceCreateFromName(kCGColorSpaceSRGB) does not change anything.
        switch (type) {
            case kBitmapRGBA:
                mColorspace = CGColorSpaceCreateDeviceRGB();
                mBytesPerPixel = 4;
                // kCGImageAlphaLast fails to create context
                alphaInfo = kCGImageAlphaPremultipliedLast;
                break;
            case kBitmapRGB:
                mColorspace = CGColorSpaceCreateDeviceRGB();
                // bpp = 3, alpha = .none is invalid; RGB spaces must be 16, 32, 64, or 128 bpp
                mBytesPerPixel = 4;
                alphaInfo = kCGImageAlphaNoneSkipLast;
                break;
            case kBitmapGreyscale:
                mColorspace = CGColorSpaceCreateDeviceGray();
                mBytesPerPixel = 1;
                alphaInfo = kCGImageAlphaNone;
                break;
            case kBitmapAlpha:
                mColorspace = CGColorSpaceCreateDeviceGray();
                mBytesPerPixel = 1;
                alphaInfo = kCGImageAlphaOnly;
                break;
        }
        int nBytes = mBytesPerPixel * width * height;
        mData = new unsigned char[nBytes];
        auto gc = CGBitmapContextCreate(mData, width, height,
                                        8,                      // bits per component
                                        mBytesPerPixel * width, // bytes per row
                                        mColorspace,
                                        alphaInfo);
        setNativeDC(gc);
    }

    ~CoreGraphicsBitmap()
    {
        CGContextRef ctx = (CGContextRef)mNativeDC;
        CGContextRelease(ctx);
        CGColorSpaceRelease(mColorspace);
        delete [] mData;
    }

    std::shared_ptr<DrawableImage> copyToImage() override
    {
        auto image = CGBitmapContextCreateImage((CGContextRef)mNativeDC);
        return std::make_shared<CoreGraphicsImage>(image, width(), height(), dpi());
    }

    Color pixelAt(int x, int y) override
    {
        unsigned char *rgba = mData + mBytesPerPixel * (y * width() + x);
        switch (mType) {
            case kBitmapRGBA: {
                // RGBA is premultiplied alpha
                float a = rgba[3] / 255.0f;
                float r = rgba[0] / 255.0f;
                float g = rgba[1] / 255.0f;
                float b = rgba[2] / 255.0f;
                // Prevent divide-by-zero errors. If alpha is zero, it doesn't
                // matter what rgb are (and if they are premultiplied by zero, they
                // will be zero anyway), so just pass r, g, b directly.
                if (a < 0.000001) {
                    a = 1.0;
                }
                return Color(r / a, g / a, b / a, a);
            }
            case kBitmapRGB:
                return Color(int(rgba[0]), int(rgba[1]), int(rgba[2]), 255);
            case kBitmapGreyscale:
                return Color(int(rgba[0]), int(rgba[0]), int(rgba[0]), 255);
            case kBitmapAlpha:
                return Color(0, 0, 0, int(rgba[0]));
        }
    }
};

//--------------------------- DrawContext -------------------------------------
void DrawContext::getScreenDPI(void* nsscreen, float *uiDPI, float *cocoaDPI, float *hiresDPI)
{
    const CGFloat kMillimetersToInches = 25.4;

    NSScreen *screen = (__bridge NSScreen*)nsscreen;
    unsigned int displayID = [[screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue];
    CGSize displaySizeMM = CGDisplayScreenSize(displayID);

    // NSScreen.frame, NSScreen.visibleFrame, and CGDisplayPixelsWide() all report the
    // number of virtual pixels (that is, CGFloat, and 1.0 CGFloats has a non-integer
    // relationship with actual device pixels. Now if you calculate the DPI from the
    // physical size and the current size, and draw a, say, 1x1 inch square, you will get
    // a square that has the same physical size in all modes. Which is kind of convenient,
    // if you are a drawing program displaying at 100%. But the expectation of users is
    // that changing the scaling actually changes the size of the UI. Apple seems to go out
    // of their way to avoid giving us this information, so we need to find out the native
    // resolution is so that we can calculate the scaling factor. In fact, this is virtually
    // a necessity, since the default mode for 15" MacBook Pros since 2016 is to make the
    // default size higher than the native resolution. Since all the units above are in
    // CGFloat (that is, as if screen.backingStore is 1.0), so we find the lowest native
    // resolution as use that for our calculations.
    const int kFlagNativeMode = 0x2000000;  // See IOGraphicsTypes.h
    const CGFloat kNoSize = 100000.0;
    CGSize nativeSize = CGSizeMake(kNoSize, kNoSize);
    CFStringRef keys[1] = { kCGDisplayShowDuplicateLowResolutionModes };
    CFBooleanRef values[1] = { kCFBooleanTrue };
    CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault, (const void**)keys, (const void**)values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks );
    CFArrayRef modes = CGDisplayCopyAllDisplayModes(displayID, options);
    int n = CFArrayGetCount(modes);
    for (int i = 0;  i < n;  i++) {
        CGDisplayModeRef mode = (CGDisplayModeRef) CFArrayGetValueAtIndex(modes, i);
        if (CGDisplayModeGetIOFlags(mode) & kFlagNativeMode) {
            int w = CGDisplayModeGetWidth(mode);
            if (w < nativeSize.width) {
                nativeSize.width = w;
                nativeSize.height = CGDisplayModeGetHeight(mode);
            }
        }
    }
    if (nativeSize.width == kNoSize) {
        nativeSize = screen.frame.size;
    }
    CFRelease(modes);
    CFRelease(options);

    float nativeDPI = nativeSize.width / displaySizeMM.width * kMillimetersToInches;
    float apparentDPI = screen.frame.size.width / displaySizeMM.width * kMillimetersToInches;

    // The DPI of the UI should be the native resolution, so that the everything is drawn
    // with the correct physical units for the native resolution, and scaled according to
    // the user's preferences otherwise.
    if (uiDPI) {
        *uiDPI = nativeDPI;
    }
    // The apparent resolution is what Cocoa thinks we are using. If we draw a line with
    // thickness of 1.0 / screen.backingScaleFactor, we will get a 1 px line.
    if (cocoaDPI) {
        *cocoaDPI = apparentDPI;
    }
    // This is the hires version of cocoaDPI, which we return so that we can get really
    // thin pixels if we call onePixel().
    if (hiresDPI) {
        *hiresDPI = screen.backingScaleFactor * screen.frame.size.width / displaySizeMM.width * kMillimetersToInches;
    }
}

std::shared_ptr<DrawContext> DrawContext::fromCoreGraphics(void* cgcontext, int width, int height, float dpi, float nativeDPI /*= 0*/)
{
    if (nativeDPI == 0) {
        nativeDPI = dpi;
    }
    return std::make_shared<CoreGraphicsContext>(cgcontext, width, height, dpi, nativeDPI);
}

std::shared_ptr<DrawContext> DrawContext::createCoreGraphicsBitmap(BitmapType type, int width, int height,
                                                                   float dpi /*= 72.0f*/)
{
    return std::make_shared<CoreGraphicsBitmap>(type, width, height, dpi);
}

} // namespace $ND_NAMESPACE

#endif // __APPLE__
