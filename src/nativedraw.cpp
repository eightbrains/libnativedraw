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

#include "nativedraw.h"
#include "nativedraw_private.h"

#include <iostream>

namespace ND_NAMESPACE {

const PicaPt PicaPt::kZero(0.0f);

PicaPt operator+(float lhs, const PicaPt& rhs)
    { return PicaPt(lhs + rhs.pt); }
PicaPt operator*(float lhs, const PicaPt& rhs)
    { return PicaPt(lhs * rhs.pt); }

Point operator*(float lhs, const Point& rhs)
    { return Point(lhs * rhs.x, lhs * rhs.y); }

//-----------------------------------------------------------------------------
const Color Color::kTransparent(0.0f, 0.0f, 0.0f, 0.0f);
const Color Color::kBlack(0.0f, 0.0f, 0.0f, 1.0f);
const Color Color::kWhite(1.0f, 1.0f, 1.0f, 1.0f);
const Color Color::kRed(1.0f, 0.0f, 0.0f, 1.0f);
const Color Color::kOrange(1.0f, 0.5f, 0.0f, 1.0f);
const Color Color::kYellow(1.0f, 1.0f, 0.0f, 1.0f);
const Color Color::kGreen(0.0f, 1.0f, 0.0f, 1.0f);
const Color Color::kBlue(0.0f, 0.0f, 1.0f, 1.0f);
const Color Color::kPurple(1.0f, 0.0f, 1.0f, 1.0f);

Color Color::lighter(float amount /*= 0.1f*/) const
{
    float maxVal = 1.0f - amount;
    float r = red();
    if (r < maxVal) {
        r += amount;
    } else {
        r = 0.5f * r + 0.5f;
    }

    float g = green();
    if (g < maxVal) {
        g += amount;
    } else {
        g = 0.5f * g + 0.5f;
    }

    float b = blue();
    if (b < maxVal) {
        b += amount;
    } else {
        b = 0.5f * b + 0.5f;
    }

    // (1, 1, 1, 0.25) should increase alpha; (0, 0, 0, 0.25) should not
    float a = alpha();
    if (a < 1.0f) {
        float grey = toGrey().red();
        if (grey > 0.5f) {
            if (a < maxVal) {
                a += amount;
            } else {
                a = 0.5f * a + 0.5f;
            }
        } else {
            if (a > amount) {
                a -= amount;
            } else {
                a = 0.5f * a;
            }
        }
    }

    return Color(r, g, b, a);
}

Color Color::darker(float amount /*= 0.1f*/) const
{
    float r = red();
    if (r > amount) {
        r -= amount;
    } else {
        r = 0.5f * r;
    }

    float g = green();
    if (g > amount) {
        g -= amount;
    } else {
        g = 0.5f * g;
    }

    float b = blue();
    if (b > amount) {
        b -= amount;
    } else {
        b = 0.5f * b;
    }


    // (0, 0, 0, 0.25) should increase alpha; (1, 1, 1, 0.25) should not
    float a = alpha();
    if (a < 1.0f) {
        float grey = toGrey().red();
        if (grey < 0.5f) {
            if (a < 1.0f - amount) {
                a += amount;
            } else {
                a = 0.5f * a + 0.5f;
            }
        } else {
            if (a > amount) {
                a -= amount;
            } else {
                a = 0.5f * a;
            }
        }
   }

    return Color(r, g, b, a);
}

Color Color::blend(const Color& dest, float amount)
{
    float srcAmount = 1.0f - amount;
    return Color(srcAmount * red() + amount * dest.red(),
                 srcAmount * green() + amount * dest.green(),
                 srcAmount * blue() + amount * dest.blue(),
                 srcAmount * alpha() + amount * dest.alpha());
}

std::size_t Color::hash() const
{
    std::size_t result = 0;
    hash_combine(result, _rgba[0]);
    hash_combine(result, _rgba[1]);
    hash_combine(result, _rgba[2]);
    hash_combine(result, _rgba[3]);
    return result;
}

//-----------------------------------------------------------------------------
struct Font::Impl
{
    std::string family;
    PicaPt pointSize;
    FontStyle style;
    FontWeight weight;
    std::size_t hash;

    void computeHash()
    {
        this->hash = 0;
        hash_combine(this->hash, this->family);
        hash_combine(this->hash, this->pointSize.asFloat());
        hash_combine(this->hash, int(this->style));
        hash_combine(this->hash, int(this->weight));
    }
};

Font::Font()
    : Font("Arial", PicaPt::fromPixels(12.0f, 72.0f))
{}

Font::Font(const Font& f)
    : mImpl(new Font::Impl())
{
    *mImpl = *f.mImpl;  // copy
}

Font& Font::operator=(const Font& rhs) noexcept
{
    *mImpl = *rhs.mImpl;  // copy
    return *this;
}

Font::Font(const std::string& family, const PicaPt& pointSize,
           FontStyle style /*=kStyleNone*/, FontWeight weight /*=kWeightAuto*/)
    : mImpl(new Font::Impl())
{
    mImpl->family = family;
    mImpl->pointSize = pointSize;
    mImpl->style = style;
    if (weight == kWeightAuto) {
        weight = (style & kStyleBold) ? kWeightBold : kWeightRegular;
    }
    mImpl->weight = weight;
    mImpl->computeHash();
}

Font::~Font() {}

std::size_t Font::hash() const { return mImpl->hash; }

std::string Font::family() const { return mImpl->family; }

Font& Font::setFamily(const std::string& family)
{
    mImpl->family = family;
    mImpl->computeHash();
    return *this;
}

PicaPt Font::pointSize() const { return mImpl->pointSize; }

Font& Font::setPointSize(const PicaPt& size)
{
    mImpl->pointSize = size;
    mImpl->computeHash();
    return *this;
}

FontStyle Font::style() const { return mImpl->style; }

Font& Font::setStyle(FontStyle style)
{
    mImpl->style = style;
    mImpl->computeHash();
    return *this;
}

FontWeight Font::weight() const { return mImpl->weight; }

Font& Font::setWeight(FontWeight w)
{
    assert(w != kWeightAuto);
    if (w == kWeightAuto) {
        w = kWeightRegular;
    }

    mImpl->weight = w;
    mImpl->computeHash();
    return *this;
}

Font::Metrics Font::metrics(const DrawContext& dc) const
{
    // Q: Why call into DrawContext, why can't Font do it?
    // A: Font should be allocatable without any knowledge of the draw context,
    //    but it is the draw context that has the necessary information to
    //    return the font information.
    return dc.fontMetrics(*this);
}

Font Font::fontWithPointSize(const PicaPt& pointSize) const
{
    return Font(family(), pointSize, style(), weight());
}

Font Font::fontWithStyle(FontStyle style) const
{
    auto w = weight();
    if ((style & kStyleBold) && w < kWeightBold) {
        w = kWeightBold;
    }
    if (!(style & kStyleBold) && w >= kWeightBold) {
        w = kWeightRegular;
    }
    return Font(family(), pointSize(), style, w);
}

Font Font::fontWithWeight(FontWeight w) const
{
    auto s = style();
    if (w < kWeightBold) {
        s = FontStyle(int(s) & (~kStyleBold));
    }
    return Font(family(), pointSize(), s, w);
}

//-----------------------------------------------------------------------------
BezierPath::BezierPath()
    : mImpl(new BezierPath::Impl())
{
}

BezierPath::~BezierPath()
{
}

void BezierPath::moveTo(const Point& p)
{
    clearNative();
    mImpl->commands.emplace_back(BezierPath::Impl::Command::kMoveTo, p);
}

void BezierPath::lineTo(const Point& end)
{
    clearNative();
    mImpl->commands.emplace_back(BezierPath::Impl::Command::kLineTo, end);
}

void BezierPath::quadraticTo(const Point& cp1, const Point& end)
{
    clearNative();
    mImpl->commands.emplace_back(BezierPath::Impl::Command::kQuadraticTo, cp1, end);
}

void BezierPath::cubicTo(const Point& cp1, const Point& cp2, const Point& end)
{
    clearNative();
    mImpl->commands.emplace_back(BezierPath::Impl::Command::kCubicTo, cp1, cp2, end);
}

void BezierPath::close()
{
    mImpl->commands.emplace_back(BezierPath::Impl::Command::kClose);
    clearNative();
}

void BezierPath::addRect(const Rect& r)
{
    clearNative();
    mImpl->commands.reserve(mImpl->commands.size() + 4);
    moveTo(r.upperLeft());
    lineTo(r.upperRight());
    lineTo(r.lowerRight());
    lineTo(r.lowerLeft());
    close();
}

void BezierPath::addRoundedRect(const Rect& r, const PicaPt& radius)
{
    clearNative();
    mImpl->commands.reserve(mImpl->commands.size() + 9);

    // This is the weight for control points for a 4-curve sphere.
    // Normally 4 cubic splines use 0.55228475, but a better number was
    // computed by http://www.tinaja.com/glib/ellipse4.pdf.
    // It has an error of .76 px/in at 1200 DPI (0.0633%).
    PicaPt tangentWeight(0.551784f);
    PicaPt zero(0);
    PicaPt dTangent = tangentWeight * radius;

    auto topLeft = r.upperLeft() + Point(radius, zero);
    auto topRight = r.upperRight() + Point(-radius, zero);
    auto rightTop = r.upperRight() + Point(zero, radius);
    auto rightBottom = r.lowerRight() + Point(zero, -radius);
    auto bottomLeft = r.lowerLeft() + Point(radius, zero);
    auto bottomRight = r.lowerRight() + Point(-radius, zero);
    auto leftTop = r.upperLeft() + Point(zero, radius);
    auto leftBottom = r.lowerLeft() + Point(zero, -radius);

    moveTo(leftTop);
    cubicTo(leftTop + Point(zero, -dTangent),
            topLeft + Point(-dTangent, zero),
            topLeft);
    lineTo(topRight);
    cubicTo(topRight + Point(dTangent, zero),
            rightTop + Point(zero, -dTangent),
            rightTop);
    lineTo(rightBottom);
    cubicTo(rightBottom + Point(zero, dTangent),
            bottomRight + Point(dTangent, zero),
            bottomRight);
    lineTo(bottomLeft);
    cubicTo(bottomLeft + Point(-dTangent, zero),
            leftBottom + Point(zero, dTangent),
            leftBottom);
    close();
}

void BezierPath::addEllipse(const Rect& r)
{
    // This is the weight for control points for a sphere.
    // Normally 4 cubic splines use 0.55228475, but a better number was
    // computed by http://www.tinaja.com/glib/ellipse4.pdf.
    // It has an error of .76 px/in at 1200 DPI (0.0633%).
    float kCtrlWeight = 0.551784f;
    PicaPt zero(0.0f);

    clearNative();
    mImpl->commands.reserve(mImpl->commands.size() + 6);

    Point tanTop(r.midX(), r.y);
    Point tanRight(r.maxX(), r.midY());
    Point tanBottom(r.midX(), r.maxY());
    Point tanLeft(r.x, r.midY());
    Point horiz(0.5f * r.width, zero);
    Point vert(zero, 0.5f * r.height);

    moveTo(tanTop);
    cubicTo(tanTop + kCtrlWeight * horiz,
            tanRight - kCtrlWeight * vert,
            tanRight);
    cubicTo(tanRight + kCtrlWeight * vert,
            tanBottom + kCtrlWeight * horiz,
            tanBottom);
    cubicTo(tanBottom - kCtrlWeight * horiz,
            tanLeft + kCtrlWeight * vert,
            tanLeft);
    cubicTo(tanLeft - kCtrlWeight * vert,
            tanTop - kCtrlWeight * horiz,
            tanTop);
    close();
}

//-----------------------------------------------------------------------------
DrawContext::DrawContext(void* nativeDC, int width, int height, float dpi)
    : mNativeDC(nativeDC), mWidth(width), mHeight(height), mDPI(dpi)
{
}

PicaPt DrawContext::onePixel() const
{
    return PicaPt::fromPixels(1.0f, mDPI);
}

PicaPt DrawContext::floorToNearestPixel(const PicaPt& p) const
{
    auto onePx = onePixel();
    float n = std::floor(p.asFloat() / onePx.asFloat());
    return PicaPt(n * onePx);
}

PicaPt DrawContext::roundToNearestPixel(const PicaPt& p) const
{
    auto onePx = onePixel();
    float n = std::round(p.asFloat() / onePx.asFloat());
    return PicaPt(n * onePx);
}

PicaPt DrawContext::ceilToNearestPixel(const PicaPt& p) const
{
    auto onePx = onePixel();
    float n = std::ceil(p.asFloat() / onePx.asFloat());
    return PicaPt(n * onePx);
}

void DrawContext::setInitialState()
{
    setFillColor(Color::kBlack);
    setStrokeColor(Color::kBlack);
    setStrokeEndCap(kEndCapButt);
    setStrokeJoinStyle(kJoinMiter);
    setStrokeWidth(PicaPt(1));  // 1pt line; this is probably different than platform default
    setStrokeDashes({}, PicaPt(0));
}

void DrawContext::drawRoundedRect(const Rect& rect, const PicaPt& radius, PaintMode mode)
{
    auto path = createBezierPath();
    path->addRoundedRect(rect, radius);
    drawPath(path, mode);
}

} // namespace $ND_NAMESPACE
