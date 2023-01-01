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

#include <assert.h>
#include <stdio.h>

#include <algorithm>

#if __APPLE__
#include <TargetConditionals.h>
#endif

namespace ND_NAMESPACE {

//---------------------- defines from nativedraw_private.h --------------------
std::vector<int> utf8IndicesForUTF16Indices(const char *utf8)
{
    std::vector<int> utf16ToIndex;
    const uint8_t *c = (const uint8_t*)utf8;
    while (*c != '\0') {
        int utf8idx = int(c - (const uint8_t*)utf8);
        uint32_t utf32 = 0;
        int nMoreBytes = 0;
        if (((*c) & 0b10000000) == 0) {
            utf32 = uint32_t(*c++);
        } else if (((*c) & 0b11100000) == 0b11000000) {
            utf32 = (0b00011111 & (*c++)) << 6;
            nMoreBytes = 1;
        } else if (((*c) & 0b11110000) == 0b11100000) {
            utf32 = (0b00001111 & (*c++)) << 6;
            nMoreBytes = 2;
        } else {
            utf32 = (0b00000111 & (*c++)) << 6;
            nMoreBytes = 3;
        }
        for (int i = nMoreBytes - 1;  i >= 0;  --i) {
            utf32 |= (0b00111111 & (*c));
            // Handle truncated character, either by early \0 or by start of
            // a new character.
            if (*c != '\0' && ((*c) & 0b10000000) == 0b10000000) { c++; }
        }

        int utf16len = (utf32 >= 0x10000 ? 2 : 1);
        for (int i = 0;  i < utf16len;  ++i) {
            utf16ToIndex.push_back(utf8idx);
        }
    }

    // Add in the index to the end, too, it comes in handy for finding the
    // location of the caret positioned at the end of the string.
    int utf8idx = int(c - (const uint8_t*)utf8);
    utf16ToIndex.push_back(utf8idx);

    return utf16ToIndex;
}

std::vector<int> utf16IndicesForUTF8Indices(const char *utf8)
{
    auto utf16to8 = utf8IndicesForUTF16Indices(utf8);
    assert(!utf16to8.empty());  // should always have one past index
    assert(utf16to8[0] == 0);
    std::vector<int> utf8to16;
    utf8to16.reserve(int(utf16to8.back()) + 1);

    size_t idx16 = 1;
    while (idx16 < utf16to8.size()) {
        while (utf16to8[idx16] > int(utf8to16.size())) {
            utf8to16.push_back(int(idx16 - 1));
        }
        ++idx16;
    }
    utf8to16.push_back(int(idx16 - 1));
    assert(utf8to16.size() == size_t(int(utf16to8.back()) + 1));

    return utf8to16;
}

int nBytesForUtf8Char(const char* utf8)
{
    if (((*utf8) & 0b10000000) == 0) {
        return 1;
    } else if (((*utf8) & 0b11100000) == 0b11000000) {
        return 2;
    } else if (((*utf8) & 0b11110000) == 0b11100000) {
        return 3;
    } else {
        return 4;
    }
}

std::vector<float> createWavyLinePoints(float x0, float y0, float x1,
                                        float width)
{
    const float toothHeight = 1.0f * width;

    int nPoints = int(std::ceil((x1 - x0) / (2.0f * toothHeight))) + 1;
    std::vector<float> xy;
    xy.reserve(2 * (nPoints + 1));

    // Compute beginning state, so that two wavy lines connected to the same
    // point always connects smoothly. To do this, we consider a cycle to start
    // at x=0, y=y0, like a sine wave:
    //                ____
    //      /\            y-toothHeight
    //     .  \  .    ____y0
    //         \/     ____y+toothHeight
    //     | |
    //    -| |--toothHeight
    // Each tooth is composed of two right triangles, the x and y sides of which
    // are toothHeight units. Note that +y is down, unlike most mathematical
    // graphs of sine waves!
    float x = x0;
    float y;
    float phase = x0 / (4.0f * toothHeight);
    phase = phase - std::floor(phase);  // keep the (float) remainder
    if (phase <= 0.25f) {
        y = y0 - toothHeight * phase / 0.25f;
        xy.push_back(x);  xy.push_back(y);
        x += y - (y0 - toothHeight);  // we are using triangles with equal bases
        y = y0 - toothHeight;
        xy.push_back(x);  xy.push_back(y);
        phase = 1.0f;
    } else if (phase >= 0.75) {
        y = y0 + toothHeight - toothHeight * (phase - 0.75f) / 0.25f;
        xy.push_back(x);  xy.push_back(y);
        x += y - (y0 - toothHeight);  // we are using triangles with equal bases
        y = y0 - toothHeight;
        xy.push_back(x);  xy.push_back(y);
        phase = 1.0f;
    } else {
        y = y0 - toothHeight + 2.0f * toothHeight * (phase - 0.25f) / 0.5f;
        xy.push_back(x);  xy.push_back(y);
        x += (y0 + toothHeight) - y;  // we are using triangles with equal bases
        y = y0 + toothHeight;
        xy.push_back(x);  xy.push_back(y);
        phase = -1.0f;
    }

    while (x < x1) {
        if (x + 2.0f * toothHeight <= x1) {
            x += 2.0f * toothHeight;
            y += phase * 2.0f * toothHeight;
            xy.push_back(x);
            xy.push_back(y);
        } else {
            y += phase * (x1 - x);
            x = x1;
            xy.push_back(x);
            xy.push_back(y);
            x += 0.0001f;  // paranoia: force x > x1
        }
        phase = -phase;
    }

    return xy;
}

//-----------------------------------------------------------------------------
const PicaPt PicaPt::kZero(0.0f);

PicaPt operator+(float lhs, const PicaPt& rhs)
    { return PicaPt(lhs + rhs.pt); }
PicaPt operator*(float lhs, const PicaPt& rhs)
    { return PicaPt(lhs * rhs.pt); }

Point operator*(float lhs, const Point& rhs)
    { return Point(lhs * rhs.x, lhs * rhs.y); }

//-----------------------------------------------------------------------------
const Point Point::kZero;
const Size Size::kZero;
const Rect Rect::kZero;

Rect Rect::intersectedWith(const Rect& r) const
{
    auto xMin = std::max(this->x, r.x);
    auto xMax = std::min(this->x + this->width, r.x + r.width);
    auto yMin = std::max(this->y, r.y);
    auto yMax = std::min(this->y + this->height, r.y + r.height);
    return Rect(xMin, yMin, std::max(PicaPt::kZero, xMax - xMin), std::max(PicaPt::kZero, yMax - yMin));
}

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
const Color Color::kTextDefault(-1.0f, 0.0f, 0.0f, 1.0f);

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
Color HSVColor::toColor() const
{
    float h = _hsva[0] / 60.0f;
    float s = _hsva[1];
    float v = _hsva[2];
    float chroma = s * v;
    float hMod2 = 2.0f * (h / 2.0f - std::floor(h / 2.0f));
    float x = chroma * (1.0f - std::abs(hMod2 - 1.0f));
    float r, g, b;
    h = std::min(6.0f, std::max(0.0f, h));
    switch (int(h)) {
        case 0:  r = chroma;  g = x;       b = 0.0f;    break;
        case 1:  r = x;       g = chroma;  b = 0.0f;    break;
        case 2:  r = 0.0f;    g = chroma;  b = x;       break;
        case 3:  r = 0.0f;    g = x;       b = chroma;  break;
        case 4:  r = x;       g = 0.0f;    b = chroma;  break;
        case 5:  // fall through
        case 6:  r = chroma;  g = 0.0f;    b = x;       break;
        default:  r = 0.0f; g = 0.0f; b = 0.0f;  assert(false);  break;
    }
    float m = v - chroma;
    return Color(r + m, g + m, b + m, _hsva[3]);
}

//-----------------------------------------------------------------------------
#if defined(__APPLE__)
const Font kDefaultReplacementFont(".AppleSystemUIFont", PicaPt(12.0f)); // San Francisco since iOS9 / macOS 10.11
#elif defined(_WIN32) || defined(_WIN64)  // _WIN32 covers everything except 64-bit ARM
const Font kDefaultReplacementFont("Segoe UI", PicaPt(12.0f));  // Segoe UI has shipped since Windows 7
#else
const Font kDefaultReplacementFont("Arial", PicaPt(12.0f));  // Arial is available everywhere with msttfonts
#endif
const Color kDefaultReplacementColor(0.0f, 0.0f, 0.0f);  // Color::kBlack may not exist yet

bool isFamilyDefault(const Font& f) { return f.family().empty(); }
bool isPointSizeDefault(const Font& f) { return (f.pointSize() == PicaPt::kZero); }
Font fontSizedForSuperSubscript(const Font& f)
{
    // OpenOffice and Adobe use 58% and 58.3%, respectively, which implies a
    // baseline offset of 33% and 33.3%. This seems small, so I am trying out
    // 66%.
    return f.fontWithScaledPointSize(0.666f);
}

struct Font::Impl
{
    std::string family;
    PicaPt pointSize;
    FontStyle style;
    FontWeight weight;
    std::size_t hash = 0;

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
    : Font("", PicaPt::kZero)
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

Font& Font::setBold(bool isBold)
{
    if (isBold) {
        setStyle(FontStyle(style() | kStyleBold));
        setWeight(kWeightBold);
    } else {
        setStyle(FontStyle(style() & (~kStyleBold)));
        setWeight(kWeightAuto);
    }
    return *this;
}

Font& Font::setItalic(bool isItalic)
{
    if (isItalic) {
        setStyle(FontStyle(style() | kStyleItalic));
    } else {
        setStyle(FontStyle(style() & (~kStyleItalic)));
    }
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

Font Font::fontWithScaledPointSize(float scaling) const
{
    return Font(family(), scaling * pointSize(), style(), weight());
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
void setTextRun(Text& t, TextRun *run, int start, int len)
{
    run->startIndex = start;
    if (len == -1) {
        run->length = int(t.text().size()) - start;
    } else {
        run->length = len;
    }
    t.setTextRun(*run);
}

Text::Text()
: Text("", Font(), Color::kBlack)
{
}

Text::Text(const std::string& utf8, const Font& font, const Color& fgColor)
{
    mText = utf8;
    mParagraph.lineHeightMultiple = 0.0f;  // platform default
    mRuns.emplace_back();
    mRuns.back().startIndex = 0;
    mRuns.back().length = int(mText.length());
    mRuns.back().font = font;
    mRuns.back().color = fgColor;
}

const std::string& Text::text() const { return mText; }

Text& Text::setPointSize(const PicaPt& pointSize, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.pointSize = pointSize;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setBold(int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.bold = true;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setItalic(int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.italic = true;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setFont(const Font& font, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.font = font;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setBackgroundColor(const Color& bg, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.backgroundColor = bg;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setColor(const Color& fg, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.color = fg;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setUnderlineStyle(UnderlineStyle style, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.underlineStyle = style;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setUnderlineColor(const Color& c, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.underlineColor = c;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setStrikethrough(int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.strikethrough = true;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setStrikethroughColor(const Color& c, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.strikethroughColor = c;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setOutlineStrokeWidth(const PicaPt &width, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.outlineStrokeWidth = width;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setOutlineColor(const Color &c, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.outlineColor = c;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setSuperscript(int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.superscript = true;
    r.subscript = false;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setSubscript(int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.superscript = false;
    r.subscript = true;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setCharacterSpacing(const PicaPt& extraSpacing, int start /*= 0*/, int len /*= -1*/)
{
    TextRun r;
    r.characterSpacing = extraSpacing;
    ND_NAMESPACE::setTextRun(*this, &r, start, len);
    return *this;
}

Text& Text::setTextRun(const TextRun& run)
{
    if (run.startIndex < 0 || run.startIndex >= mText.size() || run.length == 0) {
        return *this;
    }
    int newRunStart = run.startIndex;
    int newRunLength = (run.length >= 0) ? std::min(run.length, int(mText.size()) - run.startIndex)
                                         : (int(mText.size()) - run.startIndex);
    int idx = runIndexFor(newRunStart);
    while (newRunLength > 0 && idx >= 0) {
        auto *r = &mRuns[idx];
        assert(r->startIndex <= newRunStart);

        // Split the run if we need to
        if (newRunStart == r->startIndex && newRunLength >= r->length) {
            // this is the happy path; we do not need to do anything!
        } else if (newRunStart == r->startIndex) {  // && newRunLength < r->length
            mRuns.insert(mRuns.begin() + idx, *r);
            r = &mRuns[idx];  // insert invalidates r (which is essentially an iterator)
            r->length = newRunLength;
            mRuns[idx + 1].length -= newRunLength;
            mRuns[idx + 1].startIndex += newRunLength;
        } else {  // newRunStart > r->startIndex
            mRuns.insert(mRuns.begin() + idx + 1, *r);
            r = &mRuns[idx];  // insert invalidates r (which is essentially an iterator)
            r->length = newRunStart - r->startIndex;
            idx += 1;
            r = &mRuns[idx];
            r->length -= newRunStart - r->startIndex;
            r->startIndex = newRunStart;
            continue;  // loop back around, in case newRunLength < r->length
        }

        // Copy (TextRun::operator=() handles set properties)
        int oldStart = r->startIndex;
        int oldLen = r->length;
        *r = run;
        r->startIndex = oldStart;  // if we override Text::operator=(), pretty likely
        r->length = oldLen;        // to forget a field when we add one at some point.

        newRunLength -= r->length;
        newRunStart = r->startIndex + r->length;  // may have continued earlier, can't do 'new += len'
        idx += 1;
    }

    return *this;
}

Text& Text::setTextRuns(const std::vector<TextRun>& runs)
{
    mRuns = runs;
    return *this;
}

const TextRun& Text::runAt(int index) const
{
    int idx = runIndexFor(index);
    if (idx < 0) {
        if (index >= mText.size()) {
            return mRuns.back();
        }
        return mRuns[0];
    }
    return mRuns[idx];
}

int Text::runIndexFor(int index) const
{
    if (index < 0) {
        return -1;
    }
    if (index >= mText.size()) {
        return -1;
    }

    auto less = [](const TextRun& lhs, const TextRun& rhs) -> bool {
        return lhs.startIndex < rhs.startIndex;
    };
    TextRun r;
    r.startIndex = index;
    auto it = std::upper_bound(mRuns.begin(), mRuns.end(), r, less);
    // 'it' is the iterator pointing to the run greater than index,
    // that is, the *next* run. So we need to go backwards one.
    assert(it != mRuns.begin());  // we did this check first thing
    it--;
    return int(it - mRuns.begin());
}

const std::vector<TextRun>& Text::runs() const
{
    return mRuns;
}

Text& Text::setLineHeightMultiple(float factor)
{
    mParagraph.lineHeightMultiple = factor;
    return *this;
}

float Text::lineHeightMultiple() const { return mParagraph.lineHeightMultiple; }

//-----------------------------------------------------------------------------
const TextLayout::Glyph* TextLayout::glyphAtPoint(const Point& p) const
{
    // TODO: can we do binary search?
    for (auto &glyph : glyphs()) {
        if (glyph.frame.contains(p)) {
            return &glyph;
        }
    }
    return nullptr;
}

Point TextLayout::pointAtIndex(long index) const
{
    auto &glyphs = this->glyphs();
    if (glyphs.empty()) {
        return Point(PicaPt::kZero, PicaPt::kZero);
    }

    if (index >= glyphs.back().indexOfNext) {
        return glyphs.back().frame.upperRight();
    }
    
    if (index < 0) {
        index = 0;
    }

    // Note that there are not necessarily as many glyphs as there are
    // bytes in the string! Do a fuzzy binary search (instead of a linear
    // search), since this will be used often in drawing for the cursor
    // and selection.
    bool isFirstIteration = true;
    int lowerIdx = 0, idx = int(glyphs.size() / 2), upperIdx = int(glyphs.size() - 1);
    while ((lowerIdx != idx && upperIdx != idx) || isFirstIteration) {
        if (index < glyphs[idx].index) {
            upperIdx = idx;
            auto dist = idx - lowerIdx;
            if (lowerIdx < idx) {
                idx -= std::max(1, dist / 2);
            }
        } else if (index > glyphs[idx].index) {
            lowerIdx = idx;
            auto dist = upperIdx - idx;
            if (upperIdx > idx) {
                idx += std::max(1, dist / 2);
            }
        } else {
            lowerIdx = idx;
            upperIdx = idx;
        }
        isFirstIteration = false;
        assert(lowerIdx <= idx);
        assert(upperIdx >= idx);
    }
    return glyphs[idx].frame.upperLeft();
}

Font::Metrics TextLayout::calcFirstLineMetrics(
                            const std::vector<Font::Metrics>& runMetrics,
                            const std::vector<TextRun>& runs,
                            int firstLineLength /*= -1*/) const
{
    if (runMetrics.size() == 1) {
        return runMetrics[0];
    }
    assert(runs.size() >= 2);
    assert(runMetrics.size() == runs.size());

    // Check to see if the metrics are different; we'd rather not generate
    // the glyphs (which will allocate memory and are likely not to be used
    // elsewhere) if we do not need to.
    bool hasDifferentMetrics = false;
    for (size_t i = 1;  i < runMetrics.size();  ++i) {
        if (runMetrics[i].ascent != runMetrics[i - 1].ascent ||
            runMetrics[i].capHeight != runMetrics[i - 1].capHeight ||
            runMetrics[i].descent != runMetrics[i - 1].descent)
        {
            hasDifferentMetrics = true;
            break;
        }
    }
    if (!hasDifferentMetrics) {
        return runMetrics[0];
    }

    // Ok, we have different size fonts, so we need to get the glyphs so that
    // we know where the line breaks are.
    Font::Metrics firstLineMetrics = runMetrics[0];
    auto &glyphs = this->glyphs();
    // we can assume at least two characters (how else can we get two runs?)
    assert(glyphs.size() >= 2);

    int firstLineEndIdx;
    if (firstLineLength >= 0) {
        firstLineEndIdx = firstLineLength - 1;
    } else {
        firstLineEndIdx = 0;
        PicaPt minNewLineY = glyphs[0].frame.maxY();
        while (firstLineEndIdx < int(glyphs.size())
                && glyphs[firstLineEndIdx].line == 0) {
            ++firstLineEndIdx;
        }
        firstLineEndIdx -= 1;
    }
    assert(firstLineEndIdx >= 0);

    int runIdx = 0;
    firstLineMetrics = runMetrics[runIdx];
    while (runIdx < int(runMetrics.size()) && firstLineEndIdx >= runs[runIdx].startIndex) {
        if (runMetrics[runIdx].ascent > firstLineMetrics.ascent) {
            firstLineMetrics = runMetrics[runIdx];
        }
        ++runIdx;
    }
    return firstLineMetrics;
}

Point TextLayout::calcOffsetForAlignment(int alignment, const Size &size,
                                         const Font::Metrics& firstLineMetrics)
{
    if (alignment == Alignment::kNone) {
        return Point::kZero;
    }

    Rect r(PicaPt::kZero, PicaPt::kZero, size.width, size.height);
    auto tm = metrics();
    bool isOneLine = (tm.height < 1.5f * firstLineMetrics.lineHeight);
    bool isNoWrap = (size.width <= PicaPt::kZero);
    Point pt;
    // Vertical alignment
    if (isOneLine) {
        if (alignment & Alignment::kBottom) {
            pt.y = r.maxY() - (firstLineMetrics.ascent + firstLineMetrics.descent);
        } else if (alignment & Alignment::kVCenter) {
            // Visually the descenders (if any) do not feel like they are part
            // of the block of text, so just the cap-height should be centered.
            // However, drawing will start from the ascent (which may be above
            // the cap-height). The descent below acts as the lower margin.
            pt.y = r.midY() - 0.5f * firstLineMetrics.capHeight - (firstLineMetrics.ascent - firstLineMetrics.capHeight);
        } else {
            // The ascent value is kind of arbitrary, and many fonts seem to use
            // it to put the leading in, so it is taller than necessary (either
            // that or there are some really tall glyphs somewhere in those
            // Unicode characters). The cap-height is the visual ascent.
            pt.y = r.minY() - (firstLineMetrics.ascent - firstLineMetrics.capHeight);
        }
    } else {
        if (alignment & Alignment::kBottom) {
            pt.y = r.maxY() - tm.height;
        } else if (alignment & Alignment::kVCenter) {
            pt.y = r.midY() - 0.5f * tm.height;
        } else {
            pt.y = r.minY() - (firstLineMetrics.ascent - firstLineMetrics.capHeight);
        }
    }

    // Horizontal alignment
    if (isNoWrap) {
        if (alignment & Alignment::kRight) {
            pt.x = r.maxX() - tm.width;
        } else if (alignment & Alignment::kHCenter) {
            pt.x = r.midX() - 0.5f * tm.width;
        } else {
            pt.x = r.minX();
        }
    } else {
        pt.x = r.minX();
    }

    // It's not clear what alignment means if there is no size.
    // We have defined it to mean that no adjustment is performed.
    if (size.width == PicaPt::kZero) {
        pt.x = PicaPt::kZero;
    }
    if (size.height == PicaPt::kZero) {
        pt.y = PicaPt::kZero;
    }

    return pt;
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

void BezierPath::quarterEllipseTo(const Point& forwardCorner, const Point& endPt)
{
    // This is the weight for control points for a 4-curve sphere.
    // Normally 4 cubic splines use 0.55228475, but a better number was
    // computed by http://www.tinaja.com/glib/ellipse4.pdf.
    // It has an error of .76 px/in at 1200 DPI (0.0633%).
    float tangentWeight = 0.551784f;
    Point start = mImpl->commands.back().endPt();
    Point side1dir = forwardCorner - start;
    Point side2dir = endPt - forwardCorner;

    Point cp1 = start + tangentWeight * (forwardCorner - start);
    Point cp2 = endPt - tangentWeight * (endPt - forwardCorner);
    mImpl->commands.emplace_back(BezierPath::Impl::Command::kCubicTo, cp1, cp2, endPt);
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

void BezierPath::addCircle(const Point& center, const PicaPt& radius)
{
    addEllipse(Rect(center.x - radius, center.y - radius, 2.0f * radius, 2.0f * radius));
}

//-----------------------------------------------------------------------------
int calcPixelBytes(ImageFormat format)
{
    switch (format) {
        case kImageRGBA32:
        case kImageRGBA32_Premultiplied:
        case kImageBGRA32:
        case kImageBGRA32_Premultiplied:
        case kImageARGB32:
        case kImageARGB32_Premultiplied:
        case kImageABGR32:
        case kImageABGR32_Premultiplied:
        case kImageRGBX32:
        case kImageBGRX32:
            return 4;
        case kImageRGB24:
        case kImageBGR24:
            return 3;
        case kImageGreyscaleAlpha16:
            return 2;
        case kImageGreyscale8:
            return 1;
        case kImageEncodedData_internal:
            assert(false);
            return 4;  // guaranteed to be big enough; almost certainly too large
    }
    assert(false);
    return 0;  // Visual Studio thinks we might get here
}

struct Image::Impl
{
    int width;
    int height;
    float dpi;
    ImageFormat format = (ImageFormat)-1;
    uint8_t *data = nullptr;
    size_t size = 0;
#ifdef __APPLE__  // no sense making other platforms waste this memory
    std::function<void(void*)> onDestruct;
#endif // __APPLE__

    Impl() {}
    Impl(uint8_t *dd /* takes ownership */, size_t s, int w, int h, ImageFormat f, float d)
        : width(w), height(h), dpi(d), format(f), data(dd), size(s)
    {}
    ~Impl()
    {
#ifdef __APPLE__
        if (onDestruct) {
            onDestruct(*(void**)data);
        }
#endif // __APPLE__
        delete [] data;
    }

    // In the dusty appendix of the tome of magic lies this function. macOS does not
    // have a way to use the system functions to determine if an image is valid without
    // reading it. Since an NSImage* is valid globally, we'll just read it and pass it
    // here. Now, The Right Way (tm) to do this is to inherit from Image, but then
    // the user of the library needs to deal with pointers (since returning a derived
    // class as an object of the base class is b.a.d.), which is a major inconvenience
    // caused by unnecessary leakage of this implementation detail. So we need to store
    // Apple's cooked image. We need to allocate this->data anyway, so that isInvalid()
    // returns true, so we might as well stuff the pointer in there.
    Impl(void *native, int w, int h, float d, std::function<void(void*)> od)
        : width(w), height(h), dpi(d), format(kImageEncodedData_internal), onDestruct(od)
    {
        this->size = sizeof(void*);
        this->data = new uint8_t[this->size];
        memcpy(this->data, &native, this->size);
    }
};

// These are implemented in the native files
//Image Image::fromFile(const char *path);
//Image Image::fromEncodedData(const uint8_t *encodedImage, int size)
//Image Image::fromCopyOfBytes(const uint8_t *bytes, int w, int h,
//                             ImageFormat f, float dpi /*= 0*/)

Image::Image()
{
    reset();
}

Image::Image(int w, int h, ImageFormat f, float dpi /*= 0*/)
{
    size_t size = size_t(calcPixelBytes(f) * w * h);
    uint8_t *data = new uint8_t[size];

    if (dpi == 0) { dpi = kDefaultImageDPI; }
    mImpl = std::make_shared<Impl>(data, size, w, h, f, dpi);
}

Image::Image(uint8_t *bytes, size_t size, int w, int h, ImageFormat f, float dpi)
{
    // takes ownership of 'bytes'
    mImpl = std::make_shared<Impl>(bytes, size, w, h, f, dpi);
}

Image::Image(void *handle, int w, int h, float dpi, std::function<void(void*)> onDestruct)
{
    mImpl = std::make_shared<Impl>(handle, w, h, dpi, onDestruct);
}

Image::~Image()
{
}

void Image::reset()
{
    mImpl = std::make_shared<Impl>(nullptr, 0, 0, 0, (ImageFormat)-1, 0.0f);
}

// If the image is encoded data, width/height will be 0, so check if data is nullptr
bool Image::isValid() const { return (mImpl->data != nullptr); }

ImageFormat Image::format() const { return mImpl->format; }
int Image::widthPx() const { return mImpl->width; }
int Image::heightPx() const { return mImpl->height; }
float Image::dpi() const { return mImpl->dpi; }
PicaPt Image::width() const
    { return PicaPt::fromPixels(float(mImpl->width), mImpl->dpi); }
PicaPt Image::height() const
    { return PicaPt::fromPixels(float(mImpl->height), mImpl->dpi); }
uint8_t* Image::data() { return mImpl->data; }
const uint8_t* Image::data() const { return mImpl->data; }
size_t Image::size() const { return mImpl->size; }

void Image::premultiplyAlpha()
{
    if (mImpl->format == kImageRGBA32_Premultiplied ||
        mImpl->format == kImageBGRA32_Premultiplied) {
        // BGRA and RGBA are equivalent for the premultiply calculations
        premultiplyBGRA(mImpl->data, mImpl->width, mImpl->height);
    } else if (mImpl->format == kImageARGB32_Premultiplied &&
               mImpl->format == kImageABGR32_Premultiplied) {
        // ARGB and ABGR are equivalent for the premultiply calculations
        premultiplyARGB(mImpl->data, mImpl->width, mImpl->height);
    } else {
        return;
    }
}

//-----------------------------------------------------------------------------
uint8_t* createBGRAFromABGR(const uint8_t *src, int width, int height)
{
    uint8_t* out = new uint8_t[4 * width * height];
    uint8_t* dst = out;
    const uint8_t* srcEnd = src + 4 * width * height;
    while (src < srcEnd) {
        dst[3] = *src++;
        dst[0] = *src++;
        dst[1] = *src++;
        dst[2] = *src++;
        dst += 4;
    }
    return out;
}

uint8_t* createBGRAFromRGBA(const uint8_t* src, int width, int height)
{
    uint8_t* out = new uint8_t[4 * width * height];
    uint8_t* dst = out;
    const uint8_t* srcEnd = src + 4 * width * height;
    while (src < srcEnd) {
        dst[2] = *src++;
        dst[1] = *src++;
        dst[0] = *src++;
        dst[3] = *src++;
        dst += 4;
    }
    return out;
}

uint8_t* createBGRAFromARGB(const uint8_t* src, int width, int height)
{
    uint8_t* out = new uint8_t[4 * width * height];
    uint8_t* dst = out;
    const uint8_t* srcEnd = src + 4 * width * height;
    while (src < srcEnd) {
        dst[3] = *src++;
        dst[2] = *src++;
        dst[1] = *src++;
        dst[0] = *src++;
        dst += 4;
    }
    return out;
}

uint8_t* createBGRAFromRGB(const uint8_t* src, int width, int height)
{
    uint8_t* out = new uint8_t[4 * width * height];
    uint8_t* dst = out;
    const uint8_t* srcEnd = src + 3 * width * height;
    while (src < srcEnd) {
        dst[2] = *src++;
        dst[1] = *src++;
        dst[0] = *src++;
        dst[3] = 0xff;
        dst += 4;
    }
    return out;
}

uint8_t* createBGRAFromBGR(const uint8_t* src, int width, int height)
{
    uint8_t* out = new uint8_t[4 * width * height];
    uint8_t* dst = out;
    const uint8_t* srcEnd = src + 3 * width * height;
    while (src < srcEnd) {
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = 0xff;
    }
    return out;
}

uint8_t* createBGRAFromGreyAlpha(const uint8_t* src, int width, int height)
{
    uint8_t* out = new uint8_t[4 * width * height];
    const uint8_t *srcEnd = src + 2 * width * height;
    uint8_t* dst = out;
    while (src < srcEnd) {
        *dst++ = *src;
        *dst++ = *src;
        *dst++ = *src++;
        *dst++ = *src++;
    }
    return out;
}

uint8_t* createBGRAFromGrey(const uint8_t* src, int width, int height)
{
    uint8_t* out = new uint8_t[4 * width * height];
    const uint8_t* srcEnd = src + 1 * width * height;
    uint8_t* dst = out;
    while (src < srcEnd) {
        *dst++ = *src;
        *dst++ = *src;
        *dst++ = *src++;
        *dst++ = 0xff;
    }
    return out;
}

void premultiplyBGRA(uint8_t* bgra, int width, int height)
{
    uint8_t* end = bgra + 4 * width * height;
    float alpha;
    while (bgra < end) {
        if (bgra[3] < 0xff) {  // the common case is alpha = 1.0f, so no work necessary
            alpha = float(bgra[3]) / 255.0f;
            bgra[0] = uint8_t(std::round(alpha * float(bgra[0])));
            bgra[1] = uint8_t(std::round(alpha * float(bgra[1])));
            bgra[2] = uint8_t(std::round(alpha * float(bgra[2])));
        }
        bgra += 4;
    }
}

void premultiplyARGB(uint8_t* argb, int width, int height)
{
    uint8_t* end = argb + 4 * width * height;
    float alpha;
    while (argb < end) {
        if (argb[0] < 0xff) {  // the common case is alpha = 1.0f, so no work necessary
            alpha = float(argb[0]) / 255.0f;
            argb[1] = uint8_t(std::round(alpha * float(argb[1])));
            argb[2] = uint8_t(std::round(alpha * float(argb[2])));
            argb[3] = uint8_t(std::round(alpha * float(argb[3])));
        }
        argb += 4;
    }
}

std::vector<uint8_t> readFile(const char *path)
{
    std::vector<uint8_t> data;
#if defined(_WIN32) || defined(_WIN64)
    FILE *in = nullptr;
    if (fopen_s(&in, path, "rb, ccs=UTF-8") != 0) {
        return data;
    }
#else
    FILE *in = fopen(path, "rb");
#endif
    if (in) {
        fseek(in, 0, SEEK_END);
        auto size = ftell(in);
        fseek(in, 0, SEEK_SET);
        data.resize(size);
        fread(data.data(), size, 1, in);
    }
    return data;
}

Image readImage(const uint8_t *imgdata, int size)
{
#ifdef __APPLE__
    assert(false);  // use native functions
    return Image();
#elif defined(_WIN32) || defined(_WIN64)
    assert(false);  // use native functions
    return Image();
#else
    Image image;
    // PNG validates very quickly, so test first
    image = readPNG(imgdata, size);
    if (image.isValid()) {
        return image;
    }
    // JPEG requires some setup to validate
    image = readJPEG(imgdata, size);
    if (image.isValid()) {
        return image;
    }
    // GIF is unlikely, do last
    image = readGIF(imgdata, size);
    if (image.isValid()) {
        return image;
    }
#endif
}

//-----------------------------------------------------------------------------
DrawContext::DrawContext(void* nativeDC, int width, int height, float dpi, float nativeDPI)
    : mNativeDC(nativeDC), mWidth(width), mHeight(height), mDPI(dpi), mNativeDPI(nativeDPI)
{
}

PicaPt DrawContext::onePixel() const
{
    return PicaPt::fromPixels(1.0f, mNativeDPI);
}

PicaPt DrawContext::floorToNearestPixel(const PicaPt& p) const
{
    auto onePx = onePixel();
    float n = std::floor(p.toPixels(mNativeDPI) / onePx.toPixels(mNativeDPI));
    return PicaPt(n * onePx);
}

PicaPt DrawContext::roundToNearestPixel(const PicaPt& p) const
{
    auto onePx = onePixel();
    float n = std::round(p.toPixels(mNativeDPI) / onePx.toPixels(mNativeDPI));
    return PicaPt(n * onePx);
}

PicaPt DrawContext::ceilToNearestPixel(const PicaPt& p) const
{
    auto onePx = onePixel();
    float n = std::ceil(p.toPixels(mNativeDPI) / onePx.toPixels(mNativeDPI));
    return PicaPt(n * onePx);
}

PicaPt DrawContext::offsetPixelForStroke(const PicaPt& p, const PicaPt& strokeWidth) const
{
    auto onePx = onePixel();
    int n = int(std::round(p.toPixels(mNativeDPI) / onePx.toPixels(mNativeDPI)));
    if (n & 0x1) {  // odd lines need to add half a pixel
        return p + 0.5f * onePx;
    }
    return p;
}

void DrawContext::setInitialState()
{
    setFillColor(Color::kBlack);
    setStrokeColor(Color::kBlack);
    setStrokeEndCap(kEndCapButt);
    setStrokeJoinStyle(kJoinMiter);
    setStrokeWidth(PicaPt::fromStandardPixels(1));
    setStrokeDashes({}, PicaPt(0));
}

void DrawContext::drawRoundedRect(const Rect& rect, const PicaPt& radius, PaintMode mode)
{
    auto path = createBezierPath();
    path->addRoundedRect(rect, radius);
    drawPath(path, mode);
}

void DrawContext::drawText(const char *textUTF8, const Rect& r, int alignment,
                           TextWrapping wrap, const Font& font, PaintMode mode)
{
    std::shared_ptr<TextLayout> layout;
    auto size = r.size();
    layout = createTextLayout(textUTF8, font, fillColor(), size, alignment, wrap);
    drawText(*layout, r.upperLeft());
}

} // namespace $ND_NAMESPACE
