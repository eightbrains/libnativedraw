#include "nativedraw.h"
#include "nativedraw_private.h"

namespace ND_NAMESPACE {

PicaPt operator+(float lhs, const PicaPt& rhs)
    { return PicaPt(lhs + rhs.pt); }
PicaPt operator*(float lhs, const PicaPt& rhs)
    { return PicaPt(lhs * rhs.pt); }

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

Font::Font(const Font& f)
    : mImpl(new Font::Impl())
{
    *mImpl = *f.mImpl;  // copy
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
}

PicaPt Font::pointSize() const { return mImpl->pointSize; }

Font& Font::setPointSize(const PicaPt& size)
{
    mImpl->pointSize = size;
    mImpl->computeHash();
}

FontStyle Font::style() const { return mImpl->style; }

Font& Font::setStyle(FontStyle style)
{
    mImpl->style = style;
    mImpl->computeHash();
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
}

// lineHeight(): platform-specific
// metrics(): platform-specific

Font Font::fontWithPointSize(const PicaPt& pointSize) const
{
    return Font(family(), pointSize, style(), weight());
}

Font Font::fontWithStyle(FontStyle style) const
{
    return Font(family(), pointSize(), style, weight());
}

Font Font::fontWithWeight(FontWeight w) const
{
    return Font(family(), pointSize(), style(), w);
}

//-----------------------------------------------------------------------------
void BezierPath::moveTo(const Point& p)
{
    commands.emplace_back(BezierPath::Command::kMoveTo, p);
}

void BezierPath::lineTo(const Point& end)
{
    commands.emplace_back(BezierPath::Command::kLineTo, end);
}

void BezierPath::quadraticTo(const Point& cp1, const Point& end)
{
    commands.emplace_back(BezierPath::Command::kQuadraticTo, cp1, end);
}

void BezierPath::cubicTo(const Point& cp1, const Point& cp2, const Point& end)
{
    commands.emplace_back(BezierPath::Command::kCubicTo, cp1, cp2, end);
}

void BezierPath::close()
{
    commands.emplace_back(BezierPath::Command::kClose);
}

void BezierPath::addRect(const Rect& r)
{
    commands.reserve(commands.size() + 4);
    moveTo(r.upperLeft());
    lineTo(r.upperRight());
    lineTo(r.lowerRight());
    lineTo(r.lowerLeft());
    close();
}

void BezierPath::addRoundedRect(const Rect& r, const PicaPt& radius)
{
    commands.reserve(commands.size() + 9);

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

//-----------------------------------------------------------------------------
void DrawContext::drawRoundedRect(const Rect& rect, const PicaPt& radius, PaintMode mode)
{
    BezierPath path;
    path.addRoundedRect(rect, radius);
    drawPath(path, mode);
}

} // namespace $ND_NAMESPACE
