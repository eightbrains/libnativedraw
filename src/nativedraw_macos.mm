#if __APPLE__
#include "nativedraw.h"

#include "nativedraw_private.h"

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

void addPath(CGContextRef gc, const BezierPath& path)
{
    for (auto &cmd : path.commands) {
        switch (cmd.cmd) {
            case BezierPath::Command::kMoveTo:
                CGContextMoveToPoint(gc, cmd.p1.x.asFloat(), cmd.p1.y.asFloat());
                break;
            case BezierPath::Command::kLineTo:
                CGContextAddLineToPoint(gc, cmd.p1.x.asFloat(), cmd.p1.y.asFloat());
                break;
            case BezierPath::Command::kQuadraticTo:
                CGContextAddQuadCurveToPoint(gc,
                                             cmd.p1.x.asFloat(), cmd.p1.y.asFloat(),
                                             cmd.p2.x.asFloat(), cmd.p2.y.asFloat());
                break;
            case BezierPath::Command::kCubicTo:
                CGContextAddCurveToPoint(gc,
                                         cmd.p1.x.asFloat(), cmd.p1.y.asFloat(),
                                         cmd.p2.x.asFloat(), cmd.p2.y.asFloat(),
                                         cmd.p3.x.asFloat(), cmd.p3.y.asFloat());
                break;
            case BezierPath::Command::kClose:
                CGContextClosePath(gc);
                break;
        }
    }
}

} // namespace

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

Font::Metrics Font::metrics(const DrawContext& dc) const
{
    // We could get the 72 dpi version of the font, which is exactly in
    // PicaPt, but we get the actual size font so that we can attempt
    // get more accurate values due to hinting (or lack thereof at
    // higher resolutions). Although, on macOS I believe it's just
    // scaled up, so maybe no difference.
    auto dpi = dc.dpi();
    NSFont *font = gFontMgr.get(*this, dpi);
    Font::Metrics m;
    m.ascent = PicaPt::fromPixels(font.ascender, dpi);
    m.descent = PicaPt::fromPixels(abs(font.descender), dpi);
    m.leading = PicaPt::fromPixels(abs(font.leading), dpi);
    m.xHeight = PicaPt::fromPixels(font.xHeight, dpi);
    m.capHeight = PicaPt::fromPixels(font.capHeight, dpi);
    m.lineHeight = m.ascent + m.descent + m.leading;
    return m;
}

//--------------------------- DrawContext -------------------------------------
// Apple really makes it difficult to draw text without using NSAttributedString.
// This requires that we know the color, stroke color, and stroke width at the
// time of drawing, which means we need to record them. But we also have to
// include a stack so that save/restore works.
namespace {
struct ContextState {
    Color fillColor;
    Color strokeColor;
    PicaPt strokeWidth;
};
} // namespace
struct DrawContext::Private
{
    std::vector<ContextState> stateStack;
};

DrawContext::DrawContext(void *nativeDC_, int width, int height, float dpi)
    : mNativeDC(nativeDC_), mWidth(width), mHeight(height), mDPI(dpi)
    , mPrivate(new DrawContext::Private())
{
    if (nativeDC_) {
        setNativeDC(nativeDC_);
    }
}

void DrawContext::setNativeDC(void *nativeDC)
{
    mNativeDC = nativeDC;
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextTranslateCTM(gc, 0, mHeight);
    CGContextScaleCTM(gc, 1, -1);

    mPrivate->stateStack.clear();
    mPrivate->stateStack.push_back(ContextState());
    setFillColor(Color::kBlack);
    setStrokeColor(Color::kBlack);
    setStrokeEndCap(kEndCapButt);
    setStrokeWidth(PicaPt(1));  // 1pt line; this is probably different than platform default
    scale(mDPI / 72.0f, mDPI / 72.0f);
}

void DrawContext::save()
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextSaveGState(gc);
    mPrivate->stateStack.push_back(mPrivate->stateStack.back());
}


void DrawContext::translate(const PicaPt& dx, const PicaPt& dy)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextTranslateCTM(gc, dx.asFloat(), dy.asFloat());
}

void DrawContext::rotate(float degrees)
{
    static CGFloat toRadians = M_PI / 180.f;
    CGContextRef gc = (CGContextRef)mNativeDC;
    // Our coordinate system is y-flipped with respect to CoreGraphics' mathematical
    // one, which make +angle rotate clockwise. We still want +angle to be counterclockwise
    // so that the angle works like people expect it, so we need to negate it.
    CGContextRotateCTM(gc, -degrees * toRadians);
}

void DrawContext::scale(float sx, float sy)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextScaleCTM(gc, sx, sy);
}

void DrawContext::restore()
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextRestoreGState(gc);
    mPrivate->stateStack.pop_back();
}

void DrawContext::fill(const Color& color)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    if (color.alpha() < 1.0f) {
        CGContextClearRect(gc, CGRectMake(0, 0, mWidth, mHeight));
    }
    if (color.alpha() > 0.0f) {
        // We DON'T want to change the fill color state, but we do want
        // to take advantage of future improvements to Color (such as more
        // colorspaces).
        Color orig = mPrivate->stateStack.back().fillColor;
        setFillColor(color);
        CGContextFillRect(gc, CGRectMake(0, 0, mWidth, mHeight));
        setFillColor(orig);
    }
}
void DrawContext::clearRect(const Rect& rect)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextClearRect(gc, CGRectMake(rect.x.asFloat(), rect.y.asFloat(),
                                      rect.width.asFloat(), rect.height.asFloat()));
}

void DrawContext::setFillColor(const Color& color)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextSetRGBFillColor(gc, color.red(), color.green(),
                             color.blue(), color.alpha());
    mPrivate->stateStack.back().fillColor = color;
}

void DrawContext::setStrokeColor(const Color& color)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextSetRGBStrokeColor(gc, color.red(), color.green(),
                               color.blue(), color.alpha());
    mPrivate->stateStack.back().strokeColor = color;
}

void DrawContext::setStrokeWidth(const PicaPt& w)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextSetLineWidth(gc, w.asFloat());
    mPrivate->stateStack.back().strokeWidth = w;
}

void DrawContext::setStrokeEndCap(EndCapStyle cap)
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
}

void DrawContext::setStrokeJoinStyle(JoinStyle join)
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
}

void DrawContext::setStrokeDashes(const std::vector<PicaPt> lengths, const PicaPt& offset)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    std::vector<CGFloat> lengthsPx;
    lengthsPx.reserve(lengths.size());
    for (size_t i = 0;  i < lengths.size();  ++i) {
        lengthsPx.push_back(lengths[i].asFloat());
    }
    // CoreGraphics seems to offset away from the line direction, whereas
    // it seems more intuitive for a positive offset to go forward.
    CGContextSetLineDash(gc, -offset.asFloat(), lengthsPx.data(), lengthsPx.size());
}

void DrawContext::drawLines(const std::vector<Point>& lines)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    std::vector<CGPoint> pts;
    pts.reserve(2 * lines.size());
    for (auto &p : lines) {
        pts.push_back(CGPointMake(p.x.asFloat(), p.y.asFloat()));
    }
    CGContextAddLines(gc, pts.data(), pts.size());
    CGContextStrokePath(gc);
}

void DrawContext::drawRect(const Rect& rect, PaintMode mode)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextAddRect(gc, CGRectMake(rect.x.asFloat(), rect.y.asFloat(),
                                    rect.width.asFloat(), rect.height.asFloat()));
    CGContextDrawPath(gc, calcCGDrawMode(mode));
}

// drawRoundedRect is in nativedraw.cpp

void DrawContext::drawEllipse(const Rect& rect, PaintMode mode)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    auto r = CGRectMake(rect.x.asFloat(), rect.y.asFloat(),
                        rect.width.asFloat(), rect.height.asFloat());
    CGContextAddEllipseInRect(gc, r);
    CGContextDrawPath(gc, calcCGDrawMode(mode));
}

void DrawContext::drawPath(const BezierPath &path, PaintMode mode)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    addPath(gc, path);
    CGContextDrawPath(gc, calcCGDrawMode(mode));
}

void DrawContext::drawText(const char *textUTF8, const Point& topLeft, const Font& font,
                           PaintMode mode)
{
    CGContextRef gc = (CGContextRef)mNativeDC;

    Color fillColor;
    if (mode & kPaintFill) {
        fillColor = mPrivate->stateStack.back().fillColor;
    } else {
        fillColor = Color::kTransparent;
    }
    NSFont *nsfont72 = gFontMgr.get(font, 72.0f);
    NSColor *nsfill = [NSColor colorWithRed:CGFloat(fillColor.red())
                                      green:CGFloat(fillColor.green())
                                       blue:CGFloat(fillColor.blue())
                                      alpha:CGFloat(fillColor.alpha())];
    NSMutableDictionary *attr = [[NSMutableDictionary alloc] init];
    attr[NSFontAttributeName] = nsfont72;
    attr[NSForegroundColorAttributeName] = nsfill;
    // attr[NSParagraphStyleAttributeName] = paragraphStyle;

    if (mode & kPaintStroke) {
        Color strokeColor = mPrivate->stateStack.back().strokeColor;
        // Apple's documentation says that NSStrokeWidthAttributeName is a
        // percentage of the font height (NOT the raw stroke width). Since the
        // DPI is being handled by the transform, stroke width is already in the
        // correct units (namely PicaPt), just like nsfont72.pointSize is (since
        // we got the 72 dpi version of the font).
        float strokeWidth = mPrivate->stateStack.back().strokeWidth.asFloat();
        strokeWidth = strokeWidth / nsfont72.pointSize * 100.0f;
        NSColor *nsstroke = [NSColor colorWithRed:CGFloat(strokeColor.red())
                                            green:CGFloat(strokeColor.green())
                                             blue:CGFloat(strokeColor.blue())
                                            alpha:CGFloat(strokeColor.alpha())];
        attr[NSStrokeColorAttributeName] = nsstroke;
        if (mode & kPaintFill) {
            // Negative width signals to both stroke and fill
            attr[NSStrokeWidthAttributeName] = @(-strokeWidth);
        } else {
            attr[NSStrokeWidthAttributeName] = @(strokeWidth);
        }
    }

    auto *attrText = [[NSAttributedString alloc]
                      initWithString:[NSString stringWithUTF8String:textUTF8]
                          attributes:attr];
    auto layout = CTFramesetterCreateWithAttributedString((CFAttributedStringRef)attrText);
    auto r = CGRectMake(0, 0, 10000, 10000);
    auto path = CGPathCreateWithRect(r, nullptr);
    auto ctframe = CTFramesetterCreateFrame(layout,
                                            CFRangeMake(0, 0), // entire string
                                            path, nullptr);

    // You'd think that adding the ascent to the upper left y would give the
    // baseline, but it can be off by a pixel (notably between Arial and Helvetica).
    // Use the line origin for more consistent results.
    NSArray* lines = (NSArray*)CTFrameGetLines(ctframe);
    CGPoint origin;
    CTFrameGetLineOrigins(ctframe, CFRangeMake(0, 1), &origin);

    CGContextSaveGState(gc);
    CGContextSetTextMatrix(gc, CGAffineTransformIdentity);
    // (Note that macOS will properly align the text to the pixel boundary; we don't need to.)
    // Note that we've scaled the coordinates so that one unit is one PicaPt, not one pixel.
    // TopLeft *is* in PicaPt; origin and nsfont.ascender are in text-bitmap units which apparently
    // take into account the scale factor.
    CGContextTranslateCTM(gc,
                          topLeft.x.asFloat(),
                          topLeft.y.asFloat() + origin.y + nsfont72.ascender - PicaPt::fromPixels(1, mDPI).asFloat());
    CGContextScaleCTM(gc, 1, -1);
    CTFrameDraw(ctframe, gc);
    CGContextRestoreGState(gc);
}

void DrawContext::drawBitmap(const Bitmap& bitmap, const Rect& rect)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    auto cgrect = CGRectMake(rect.x.asFloat(), rect.y.asFloat(),
                             rect.width.asFloat(), rect.height.asFloat());
    auto image = CGBitmapContextCreateImage((CGContextRef)bitmap.nativeDC());
    // We need to flip coordinates, as the blit will just write directly
    // into the bitmap. Note that we've scaled the coordinates so that
    // one unit is one PicaPt, not one pixel.
    CGContextSaveGState(gc);
    CGContextTranslateCTM(gc, 0, PicaPt::fromPixels(mHeight, mDPI).asFloat());
    CGContextScaleCTM(gc, 1, -1);
    CGContextDrawImage(gc, cgrect, image);
    CGContextRestoreGState(gc);
}

void DrawContext::clipToRect(const Rect& rect)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    CGContextClipToRect(gc, CGRectMake(rect.x.asFloat(), rect.y.asFloat(),
                                       rect.width.asFloat(), rect.height.asFloat()));
}

void DrawContext::clipToPath(const BezierPath& path)
{
    CGContextRef gc = (CGContextRef)mNativeDC;
    addPath(gc, path);
    CGContextClip(gc);
}

//-----------------------------------------------------------------------------
class Bitmap::Impl {
public:
    BitmapType type;
    int width;
    int height;
    int bytesPerPixel;
    float dpi;

    unsigned char *data = nullptr;
    CGColorSpaceRef colorspace = nullptr;
};

Bitmap::Bitmap(int width, int height, BitmapType type, float dpi /*= 72.0f*/)
    : DrawContext(nullptr, width, height, dpi)
    , mImpl(new Bitmap::Impl())
{
    mImpl->width = width;
    mImpl->height = height;
    mImpl->type = type;
    mImpl->dpi = dpi;
    CGImageAlphaInfo alphaInfo;
    switch (type) {
        case kBitmapRGBA:
            mImpl->colorspace = CGColorSpaceCreateDeviceRGB();
            mImpl->bytesPerPixel = 4;
            // kCGImageAlphaLast fails to create context
            alphaInfo = kCGImageAlphaPremultipliedLast;
            break;
        case kBitmapRGB:
            mImpl->colorspace = CGColorSpaceCreateDeviceRGB();
            // bpp = 3, alpha = .none is invalid; RGB spaces must be 16, 32, 64, or 128 bpp
            mImpl->bytesPerPixel = 4;
            alphaInfo = kCGImageAlphaNoneSkipLast;
            break;
        case kBitmapGreyscale:
            mImpl->colorspace = CGColorSpaceCreateDeviceGray();
            mImpl->bytesPerPixel = 1;
            alphaInfo = kCGImageAlphaNone;
            break;
        case kBitmapAlpha:
            mImpl->colorspace = CGColorSpaceCreateDeviceGray();
            mImpl->bytesPerPixel = 1;
            alphaInfo = kCGImageAlphaOnly;
            break;
    }
    int nBytes = mImpl->bytesPerPixel * width * height;
    mImpl->data = new unsigned char[nBytes];
    auto gc = CGBitmapContextCreate(mImpl->data, width, height,
                                    8,                            // bits per component
                                    mImpl->bytesPerPixel * width, // bytes per row
                                    mImpl->colorspace,
                                    alphaInfo);
    setNativeDC(gc);
}

Bitmap::~Bitmap()
{
    CGContextRef ctx = (CGContextRef)mNativeDC;
    CGContextRelease(ctx);
    CGColorSpaceRelease(mImpl->colorspace);
    delete [] mImpl->data;
}

Color Bitmap::pixelAt(int x, int y) const
{
    unsigned char *rgba = mImpl->data + mImpl->bytesPerPixel * (y * mImpl->width + x);
    switch (mImpl->type) {
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

} // namespace $ND_NAMESPACE
#endif // __APPLE__
