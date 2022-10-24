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

#ifndef ND_NAMESPACE
#define ND_NAMESPACE eb
#endif // ND_NAMESPACE

#include "../src/nativedraw.h"

#include <cstdlib>  // getenv()
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include <stdio.h>

// include isatty
#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#define isatty _isatty
#define STDOUT_FILENO 0
#else
#include <unistd.h>
#endif // windows

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(_WIN64)
#define USING_X11 1
#include "x11.h"
#else
#define USING_X11 0
#endif

using namespace ND_NAMESPACE;

std::shared_ptr<DrawContext> createBitmap(BitmapType type, int width, int height,
                                          float dpi)
{
#if __APPLE__
    return DrawContext::createCoreGraphicsBitmap(type, width, height, dpi);
#elif USING_X11
    return DrawContext::createCairoX11Bitmap(X11GetDisplay(), type, width,
                                             height, dpi);
#elif defined(_WIN32) || defined(_WIN64)
    return DrawContext::createDirect2DBitmap(type, width, height, dpi);
#else
    std::cerr << "[ERROR] this platform does not have a bitmap creator"
    assert(false);
    return nullptr;
#endif
}

void writeTIFF(const std::string& path, DrawContext& bitmap)
{
    constexpr uint16_t kTagWidth = 256;
    constexpr uint16_t kTagHeight = 257;
    constexpr uint16_t kTagBitsPerSample = 258;
    constexpr uint16_t kTagPhotometricInterpretation = 262;
    constexpr uint16_t kTagStripOffsets = 273;
    constexpr uint16_t kTagSamplesPerPixel = 277;
    constexpr uint16_t kTagRowsPerStrip = 278;
    constexpr uint16_t kTagStripByteCounts = 279;

    constexpr uint16_t kTagValueShort = 3;
    constexpr uint16_t kTagValueLong = 4;

    // MSVC bug: should automatically capture const variables
    auto writeTag = [kTagValueLong](uint16_t tag, uint32_t value, FILE* out) {
        uint16_t tagType = kTagValueLong;
        uint32_t count = 1;
        fwrite(&tag, sizeof(tag), 1, out);
        fwrite(&tagType, sizeof(tagType), 1, out);
        fwrite(&count, sizeof(count), 1, out);
        fwrite(&value, sizeof(value), 1, out);
    };

    FILE *out = fopen(path.c_str(), "w");
    if (out) {
        // Header
        putc('I', out);  putc('I', out);  // TIFF header, little-endian
        putc(0x2a, out);   putc(0x0, out);
        // Offset to tags directory
        uint32_t size = bitmap.width() * bitmap.height() * 3;
        uint32_t offset = size + 8;
        fwrite(&offset, sizeof(offset), 1, out);
        // Contents
        for (int y = 0;  y < bitmap.height();  ++y) {
            for (int x = 0;  x < bitmap.width();  ++x) {
                auto rgba = bitmap.pixelAt(x, y).toRGBA();
                fputc((rgba & 0xff000000) >> 24, out);
                fputc((rgba & 0x00ff0000) >> 16, out);
                fputc((rgba & 0x0000ff00) >> 8, out);
            }
        }
        // Tags (must be in ascending order!)
        uint16_t nTags = 7;
        fwrite(&nTags, sizeof(nTags), 1, out);
        writeTag(kTagWidth, bitmap.width(), out);
        writeTag(kTagHeight, bitmap.height(), out);
        writeTag(kTagBitsPerSample, 8, out);
        writeTag(kTagPhotometricInterpretation, 3, out);  // 3 = RGB
        writeTag(kTagStripOffsets, 8, out);  // one strip, immediately after header
        writeTag(kTagSamplesPerPixel, 3, out);  // 3 channels
        writeTag(kTagStripByteCounts, size, out);
        // End of tags
        uint32_t zero = 0;
        fwrite(&zero, sizeof(zero), 1, out);

        fclose(out);
    } else {
        std::cerr << "Could not open '" << path << "' for writing." << std::endl;
    }
}

class Test
{
public:
    Test(const std::string& name)
        : mName(name)
    {}
    virtual ~Test() {}

    const std::string& name() const { return mName; }

    virtual void setup(float dpi) {}
    virtual std::string run() = 0;
    virtual std::string debugImage() const = 0;
    virtual void writeBitmapToFile(const std::string& path) const = 0;
    virtual void teardown() {}

    std::string createFloatError(const std::string& msg, float expected,
                                 float got, std::string units = "")
    {
        if (!units.empty()) {
            units = std::string(" ") + units;
        }
        std::stringstream err;
        err << msg << ": expected " << expected << units << ", got " << got
            << units;
        return err.str();
    }

    std::string createColorError(const std::string& msg, const Color& expected,
                                 const Color& got)
    {
        std::stringstream err;
        err << msg << ": expected " << expected.toHexString() << ", got "
            << got.toHexString();
        return err.str();
    }

protected:
    std::string mName;
};

class BitmapTest : public Test
{
public:
    BitmapTest(const std::string& name, int width, int height,
               BitmapType type = kBitmapRGBA)
        : Test(name)
        , mType(type), mWidth(width), mHeight(height)
        , mBGColor(1.0f, 1.0f, 1.0f, 1.0f)
    {}

    void setup(float dpi) override
    {
        if (dpi != 72.0f) {
            std::stringstream dpistr;
            dpistr << dpi;
            mName += " [" + dpistr.str() + " dpi]";
        }

        mBitmap = createBitmap(mType, mWidth, mHeight, dpi);
    }

    void teardown() override
    {
        mBitmap = nullptr;
    }

    std::string debugImage() const override
    {
        const char kHex[] = "0123456789abcdef";

        std::string s = "";
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                s += kHex[int(pixel.red() * 15.999f)];
                s += kHex[int(pixel.green() * 15.999f)];
                s += kHex[int(pixel.blue() * 15.999f)];
                s += " ";
            }
            s += "\n";
        }

        return s;
    }

    void writeBitmapToFile(const std::string& path) const override
    {
        writeTIFF(path, *mBitmap);
    }

    std::string verifyFillRect(int xpx, int ypx, int widthpx, int heightpx,
                               const Color& bg, const Color& fg)
    {
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                if (x >= xpx && x < xpx + widthpx && y >= ypx && y < ypx + heightpx) {
                    if (pixel.toRGBA() != fg.toRGBA()) {
                        return createPixelError("wrong fill pixel", x, y, pixel, fg);
                    }
                } else {
                    if (pixel.toRGBA() != bg.toRGBA()) {
                        return createPixelError("wrong background pixel", x, y, pixel, bg);
                    }
                }
            }
        }
        return "";
    }

    std::string createPixelError(const std::string& msg, int x, int y,
                                 const Color& expected, const Color& got)
    {
        std::stringstream err;
        err << msg << " at (" << x << ", " << y << "): expected " << expected.toHexString()
            << ", got " << got.toHexString();
        return err.str();
    }

protected:
    BitmapType mType;
    int mWidth;
    int mHeight;
    std::shared_ptr<DrawContext> mBitmap;
    Color mBGColor;
};

class HSVTest : public BitmapTest
{
public:
    HSVTest() : BitmapTest("HSVColor() test", 1, 1) {}

    std::string run() override
    {
        mBitmap->beginDraw();
        mBitmap->setFillColor(Color::kBlack);
        mBitmap->drawRect(Rect(PicaPt::kZero, PicaPt::kZero,
                               PicaPt::fromPixels(mWidth, mBitmap->dpi()),
                               PicaPt::fromPixels(mHeight, mBitmap->dpi())), kPaintFill);
        mBitmap->endDraw();

        std::string err;
        // white
        if (!testHsvToRgb(HSVColor(0.0f, 0.0f, 1.0f), 0xffffffff, &err)) { return err; }
        if (!testHsvToRgb(HSVColor(180.0f, 0.0f, 1.0f), 0xffffffff, &err)) { return err; }
        // black
        if (!testHsvToRgb(HSVColor(0.0f, 1.0f, 0.0f), 0x000000ff, &err)) { return err; }
        if (!testHsvToRgb(HSVColor(180.0f, 1.0f, 0.0f), 0x000000ff, &err)) { return err; }
        // color
        if (!testHsvToRgb(HSVColor(0.0f, 1.0f, 1.0f), 0xff0000ff, &err)) { return err; }
        if (!testHsvToRgb(HSVColor(60.0f, 1.0f, 1.0f), 0xffff00ff, &err)) { return err; }
        if (!testHsvToRgb(HSVColor(120.0f, 1.0f, 1.0f), 0x00ff00ff, &err)) { return err; }
        if (!testHsvToRgb(HSVColor(180.0f, 1.0f, 1.0f), 0x00ffffff, &err)) { return err; }
        if (!testHsvToRgb(HSVColor(240.0f, 1.0f, 1.0f), 0x0000ffff, &err)) { return err; }
        if (!testHsvToRgb(HSVColor(300.0f, 1.0f, 1.0f), 0xff00ffff, &err)) { return err; }

        return "";
    }

    bool testHsvToRgb(const HSVColor& hsv, int expectedRgba, std::string *err)
    {
        Color rgb = hsv.toColor();
        if (rgb.toRGBA() != expectedRgba) {
            std::stringstream e;
            e << "hsv(" << hsv.hueDeg() << ", " << hsv.saturation() << ", " << hsv.value() << ") -> color: expected rgb 0x" << std::hex << expectedRgba << ", got 0x" << rgb.toRGBA();
            *err = e.str();
            return false;
        }
        return true;
    }
};

class CoordinateTest : public BitmapTest
{
public:
    CoordinateTest() : BitmapTest("coordinate system is +x: right, +y: down", 13, 13) {}

    std::string run() override
    {
        const int size = 3;
        Color fg(0.0f, 0.0f, 0.0f, 1.0f);
        mBitmap->beginDraw();
        mBitmap->setFillColor(fg);
        mBitmap->drawRect(Rect::fromPixels(0, 0, 2 * size, size, mBitmap->dpi()), kPaintFill);
        mBitmap->endDraw();

        for (int y = 0;  y < size;  ++y) {
            for (int x = 0;  x < size;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                if (x < size && y < size) {
                    if (pixel.toRGBA() != fg.toRGBA()) {
                        return createPixelError("wrong filled pixel", x, y, fg, pixel);
                    }
                } else {
                    if (pixel.toRGBA() != mBGColor.toRGBA()) {
                        return createPixelError("wrong background pixel", x, y, mBGColor, pixel);
                    }
                }
            }
        }
        return "";
    }
};

class ColorTest : public BitmapTest
{
public:
    ColorTest(const std::string& name, BitmapType type)
        : BitmapTest(name, 10, 1, type)
    {}

    std::string run()
    {
        return test(Color(0.25f, 0.5f, 0.75f, 1.0f));
    }

    std::string test(const Color& color)
    {
        Color writeColor = color;
        Color readColor;
        switch (mType) {
            case kBitmapRGBA:
            case kBitmapRGB:
                readColor = writeColor;
                break;
            case kBitmapGreyscale:
                readColor = writeColor.toGrey();
                break;
            case kBitmapAlpha:
                writeColor = Color(color.red(), color.green(), color.blue(), 0.7f);
                readColor = Color(0.0f, 0.0f, 0.0f, 0.7f);
                break;
        }

        mBitmap->beginDraw();
        // Don't use fill(), in case it takes a different code path than drawRect()
        mBitmap->setFillColor(writeColor);
        auto dpi = mBitmap->dpi();
        mBitmap->drawRect(Rect::fromPixels(0.0f, 0.0f, mWidth, mHeight, dpi), kPaintFill);
        mBitmap->endDraw();

        // We only need to sample one pixel, but this way we also
        // test that our indexing is correct.
        for (int x = 0; x < mWidth; ++x) {
            auto pixel = mBitmap->pixelAt(x, 0);
            if (pixel.toRGBA() != readColor.toRGBA()) {
                std::stringstream err;
                err << "Wrote " << writeColor.toHexString()
                    << ", expected " << readColor.toHexString()
                    << ", got " << pixel.toHexString();
                return err.str();
            }
        }

        return "";
    }
};

class FillFuncTest : public BitmapTest
{
public:
    FillFuncTest() : BitmapTest("fill() function", 5, 5) {}

    std::string run() override
    {
        Color origColor = Color::kBlue;
        Color fillFuncColor = Color::kGreen;
        mBitmap->beginDraw();
        mBitmap->setFillColor(origColor);

        // Test that fill sets all the pixels to fillFuncColor
        mBitmap->fill(fillFuncColor);
        mBitmap->endDraw();
        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                if (pixel.toRGBA() != fillFuncColor.toRGBA()) {
                    return createPixelError("wrong color", x, y, fillFuncColor, pixel);
                }
            }
        }

        // Test fill doesn't alter DrawContext's fill color:
        // Draw a rectangle over all the pixels without calling
        // setFillColor() and see if we get the proper pixels
        auto dpi = mBitmap->dpi();
        mBitmap->beginDraw();
        mBitmap->drawRect(Rect::fromPixels(0.0f, 0.0f, mWidth, mHeight, dpi), kPaintFill);
        mBitmap->endDraw();
        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                if (pixel.toRGBA() != origColor.toRGBA()) {
                    return createPixelError("fill() altered fill color", x, y, origColor, pixel);
                }
            }
        }

        return "";
    }
};

class TestTransform : public BitmapTest
{
public:
    TestTransform() : BitmapTest("transform (translate, rotate, scale)", 13, 13) {}

    std::string run() override
    {
        float margin = 1.0f;
        auto dpi = mBitmap->dpi();
        Color fg = Color::kBlack;
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setFillColor(fg);
        mBitmap->translate(PicaPt::fromPixels(margin + 2.0f, dpi),
                           PicaPt::fromPixels(margin, dpi));
        mBitmap->rotate(-90.0f);
        mBitmap->scale(3.0, 2.0);
        mBitmap->drawRect(Rect::fromPixels(0.0f, 0.0f, 3.0f, 1.0f, dpi), kPaintFill);
        mBitmap->endDraw();

        return verifyFillRect(1, 1, 2, 9, mBGColor, fg);
    }
};

// tests kPaintFill, not fill();  contrasts with StrokeTest
class FillTest : public BitmapTest
{
public:
    FillTest() : BitmapTest("fill", 10, 15) {}

    std::string run() override
    {
        int margin = 3;
        Color color(1.0f, 1.0f, 0.0f, 1.0f); // asymmetric, tests RGBA/BGRA swap
        auto dpi = mBitmap->dpi();
        auto r = Rect::fromPixels(0.0f, 0.0f, mWidth, mHeight, dpi);
        r.inset(PicaPt::fromPixels(float(margin), dpi), PicaPt::fromPixels(float(margin), dpi));
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setFillColor(color);
        mBitmap->drawRect(r, kPaintFill);
        mBitmap->endDraw();

        return verifyFillRect(margin, margin, mWidth - 2 * margin, mHeight - 2 * margin, mBGColor, color);
    }
};

class HairlineStrokeTest : public BitmapTest
{
public:
    HairlineStrokeTest()
        : BitmapTest("hairline stroke (1px)", 13, 10)
    {}

    std::string run() override
    {
        // Color the background red so that it's easy to tell if any
        // part of the line gets antialiased onto another pixel.
        Color bg = Color::kRed;
        Color fg = Color::kBlue;

        auto dpi = mBitmap->dpi();
        auto r = Rect::fromPixels(0, 0, mWidth, mHeight, dpi);
        mBitmap->beginDraw();
        mBitmap->setFillColor(bg);
        mBitmap->drawRect(r, kPaintFill);

        r.inset(PicaPt::fromPixels(3, dpi), PicaPt::fromPixels(3, dpi));
        r.inset(PicaPt::fromPixels(0.5, dpi), PicaPt::fromPixels(0.5, dpi));
        mBitmap->setStrokeWidth(PicaPt::fromPixels(1, dpi));
        mBitmap->setStrokeColor(fg);
        mBitmap->drawRect(r, kPaintStroke);
        mBitmap->endDraw();

        int xLeft = int(r.x.toPixels(dpi));
        int xRight = int(r.maxX().toPixels(dpi));
        int yTop = int(r.y.toPixels(dpi));
        int yBottom = int(r.maxY().toPixels(dpi));
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                bool inLine = ((x == xLeft || x == xRight) && (y >= yTop && y <= yBottom)) ||
                              ((y == yTop || y == yBottom) && (x >= xLeft && x < xRight));
                if (inLine) {
                    bool isExpected = (pixel.toRGBA() == fg.toRGBA());
#if __APPLE__
                    // CoreGraphics seems to be a little off for the corner pixels
                    if ((x == xLeft && y == yTop) || (x == xRight && y == yTop) ||
                        (x == xLeft && y == yBottom) || (x == xRight && y == yBottom)) {
                        isExpected = (abs(pixel.red() - fg.red()) <= 1.0f / 255.0f &&
                                      abs(pixel.green() - fg.green()) <= 1.0f / 255.0f &&
                                      abs(pixel.blue() - fg.blue()) <= 1.0f / 255.0f &&
                                      pixel.alpha() == fg.alpha());
                    }
#endif // __APPLE__
                    if (!isExpected) {
                        return createPixelError("bad stroke pixel", x, y, fg, pixel);
                    }
                } else {
                    if (pixel.toRGBA() != bg.toRGBA()) {
                        return createPixelError("bad background pixel", x, y, bg, pixel);
                    }
                }
            }
        }
        return "";
    }
};

class StrokeTest : public BitmapTest
{
public:
    StrokeTest(int w)
        : BitmapTest(std::string("stroke (") + std::to_string(w) + "px)", 13, 10)
        , mStrokeWidth(w)
    {}

    std::string run() override
    {
        auto dpi = mBitmap->dpi();
        Color color(0.0f, 0.0f, 1.0f, 1.0f);
        auto r = Rect::fromPixels(0, 0, mWidth, mHeight, dpi);
        r.inset(PicaPt::fromPixels(3, dpi), PicaPt::fromPixels(3, dpi));
        if (mStrokeWidth % 2 == 1) {
            r.inset(PicaPt::fromPixels(0.5, dpi), PicaPt::fromPixels(0.5, dpi));
        }
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setStrokeWidth(PicaPt::fromPixels(mStrokeWidth, dpi));
        mBitmap->setStrokeColor(color);
        mBitmap->drawRect(r, kPaintStroke);
        mBitmap->endDraw();

        float halfWidth = 0.5 * float(mStrokeWidth);
        int xLeftMin = (r.x.toPixels(dpi) - halfWidth);
        int xLeftMax = (r.x.toPixels(dpi) + halfWidth);
        int xRightMin = (r.maxX().toPixels(dpi) - halfWidth);
        int xRightMax = (r.maxX().toPixels(dpi) + halfWidth);
        int yTopMin = (r.y.toPixels(dpi) - halfWidth);
        int yTopMax = (r.y.toPixels(dpi) + halfWidth);
        int yBottomMin = (r.maxY().toPixels(dpi) - halfWidth);
        int yBottomMax = (r.maxY().toPixels(dpi) + halfWidth);
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                bool inLeft = ((x >= xLeftMin && x < xLeftMax) &&
                               (y >= yTopMin && y < yBottomMax));
                bool inRight = ((x >= xRightMin && x < xRightMax) &&
                                (y >= yTopMin && y < yBottomMax));
                bool inTop = ((y >= yTopMin && y < yTopMax) &&
                              (x >= xLeftMin && x < xRightMax));
                bool inBottom = ((y >= yBottomMin && y < yBottomMax) &&
                                 (x >= xLeftMin && x < xRightMax));
                if (inLeft || inRight || inTop || inBottom) {
                    if (pixel.toRGBA() != color.toRGBA()) {
                        return createPixelError("bad stroke color", x, y, color, pixel);
                    }
                } else {
                    if (pixel.toRGBA() != mBGColor.toRGBA()) {
                        return createPixelError("bad background color", x, y, mBGColor, pixel);
                    }
                }
            }
        }

        return "";
    }

private:
    int mStrokeWidth;
};

class EndCapTest : public BitmapTest
{
public:
    EndCapTest(EndCapStyle capStyle)
      : BitmapTest("end cap", 12, 24), mCapStyle(capStyle)
    {
        switch (capStyle) {
            case kEndCapButt:   mName += " (butt)"; break;
            case kEndCapRound:  mName += " (round)"; break;
            case kEndCapSquare: mName += " (square)"; break;
        }
    }

    std::string run() override
    {
        auto dpi = mBitmap->dpi();
        int margin = 2;
        int strokeWidth = mWidth - 2 * margin;
        int strokeLength = mHeight - strokeWidth - 2 * margin;
        if (strokeLength < 1) { return "bitmap not tall enough"; }

        // We color the background black and add up all the white pixels,
        // including antialiased ones as fractional pixels.
        Color bg = Color::kBlack;
        Color fg = Color::kWhite;
        mBitmap->beginDraw();
        mBitmap->setFillColor(bg);
        mBitmap->drawRect(Rect::fromPixels(0, 0, mWidth, mHeight, dpi), kPaintFill);

        mBitmap->setStrokeColor(fg);
        mBitmap->setStrokeWidth(PicaPt::fromPixels(strokeWidth, dpi));
        mBitmap->setStrokeEndCap(mCapStyle);
        int x = margin + strokeWidth / 2;
        int y = margin + strokeWidth / 2;
        mBitmap->drawLines({ Point::fromPixels(x, y, dpi),
                             Point::fromPixels(x, y + strokeLength, dpi) });
        mBitmap->endDraw();

        float total = 0.0f;
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                total += (pixel.red() + pixel.blue() + pixel.green()) / 3.0f;
            }
        }

        float acceptableErr;
        float lineArea = float(strokeLength * strokeWidth);
        float capArea;
        switch (mCapStyle) {
            case kEndCapButt:
                capArea = 0.0f;
                acceptableErr = 0.0f;
                break;
            case kEndCapRound: {
                float r = 0.5 * strokeWidth;
                capArea = 3.141592f * r * r;
#if defined(_WIN32) || defined(_WIN64)
                acceptableErr = 1.0f;
#else
                acceptableErr = 0.5f;
#endif
                break;
            }
            case kEndCapSquare:
                capArea = strokeWidth * strokeWidth;
                acceptableErr = 0.0f;
                break;
        }
        float expectedArea = lineArea + capArea;

        if (std::abs(total - expectedArea) > acceptableErr) {
            return createFloatError("wrong area", expectedArea, total, "px^2");
        }
        return "";
    }

private:
    EndCapStyle mCapStyle;
};

class JoinTest : public BitmapTest
{
public:
    JoinTest(JoinStyle joinStyle)
      : BitmapTest("join", 18, 18), mJoinStyle(joinStyle)
    {
        switch (joinStyle) {
            case kJoinMiter: mName += " (miter)"; break;
            case kJoinRound: mName += " (round)"; break;
            case kJoinBevel: mName += " (bevel)"; break;
        }
    }

    std::string run() override
    {
        auto dpi = mBitmap->dpi();
        int margin = 2;
        int strokeWidth = 6;
        int strokeLength = std::min(mWidth, mHeight) - strokeWidth - 2 * margin;
        if (strokeLength < 2) { return "bitmap not tall enough"; }

        // We color the background black and add up all the white pixels,
        // including antialiased ones as fractional pixels.
        Color bg = Color::kBlack;
        Color fg = Color::kWhite;
        mBitmap->beginDraw();
        mBitmap->setFillColor(bg);
        mBitmap->drawRect(Rect::fromPixels(0, 0, mWidth, mHeight, dpi), kPaintFill);
        mBitmap->setStrokeColor(fg);
        mBitmap->setStrokeWidth(PicaPt::fromPixels(strokeWidth, dpi));
        mBitmap->setStrokeJoinStyle(mJoinStyle);

        int x = margin + strokeWidth / 2;
        int y = margin + strokeWidth / 2;
        auto r = Rect::fromPixels(x, y, strokeLength, strokeLength, dpi);
        mBitmap->drawLines({ r.upperLeft(), r.upperRight(), r.lowerRight(), r.lowerLeft(), r.upperLeft() });
        mBitmap->endDraw();

        float total = 0.0f;
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                total += (pixel.red() + pixel.blue() + pixel.green()) / 3.0f;
            }
        }

        float acceptableErr;
        float halfWidth = 0.5 * strokeWidth;
        float lineArea = 4.0f * float(strokeLength * strokeWidth) - 4.0f * halfWidth * halfWidth;
        float joinArea;
        // Note that drawLines() does not join the start and end, so there are
        // only *3* three joins (not 4).
        switch (mJoinStyle) {
            case kJoinMiter:
                joinArea = 3.0f * halfWidth * halfWidth;
                acceptableErr = 0.0f;
                break;
            case kJoinRound: {
                joinArea = 0.75f * 3.141592f * halfWidth * halfWidth;
#if defined(_WIN32) || defined(_WIN64)
                acceptableErr = 1.0f;
#else
                acceptableErr = 0.5f;
#endif
                break;
            }
            case kJoinBevel:
                // The bevel is miter chopped off, so in the case of a square,
                // the chopped off area is a triangle
                joinArea = 3.0f * (0.5f * halfWidth * halfWidth);
#if defined(_WIN32) || defined(_WIN64)
                acceptableErr = 2.0f;
#else
                acceptableErr = 0.25f;
#endif
                break;
        }
        float expectedArea = lineArea + joinArea;

        if (std::abs(total - expectedArea) > acceptableErr) {
            return createFloatError("wrong area", expectedArea, total, "px^2");
        }
        return "";
    }

private:
    JoinStyle mJoinStyle;
};

class DashTest : public BitmapTest
{
public:
    DashTest() : BitmapTest("dashes", 15, 9) {}

    std::string run() override
    {
        auto dpi = mBitmap->dpi();
        int margin = 1;
        Color fg = Color::kBlack;
        std::vector<PicaPt> dashes = { PicaPt::fromPixels(1, dpi), PicaPt::fromPixels(1, dpi),
                                       PicaPt::fromPixels(2, dpi), PicaPt::fromPixels(1, dpi),
                                       PicaPt::fromPixels(3, dpi), PicaPt::fromPixels(1, dpi) };

        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setStrokeColor(fg);
        mBitmap->setStrokeWidth(PicaPt::fromPixels(1, dpi));
        mBitmap->setStrokeDashes(dashes, PicaPt(0));
        // The x coordinate will line up with a pixel boundary, since we will
        // start at x=1 (the left side of the pixel) and continue N pixels' worth.
        std::vector<Point> points = { Point::fromPixels(float(margin), 3.5, dpi),
                                      Point::fromPixels(float(mWidth) - margin, 3.5, dpi) };
        mBitmap->drawLines(points);
        mBitmap->endDraw();
        auto err = verify(margin, 3, dashes, fg, "bad pixel (offset=0)");
        if (!err.empty()) { return err; }

        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setStrokeDashes(dashes, PicaPt::fromPixels(1, dpi));
        mBitmap->drawLines(points);
        mBitmap->endDraw();
        err = verify(margin + 1, 3, dashes, fg, "bad pixel (offset=1)");
        if (!err.empty()) { return err; }

        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setStrokeDashes(dashes, PicaPt::fromPixels(2, dpi));
        mBitmap->drawLines(points);
        mBitmap->endDraw();
        err = verify(margin + 2, 3, dashes, fg, "bad pixel (offset=2)");
        if (!err.empty()) { return err; }

        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setStrokeDashes({}, PicaPt(0));
        mBitmap->drawLines(points);
        mBitmap->endDraw();
        int y = 3;
        for (int x = margin;  x < mWidth - 1;  ++x) {
            auto pixel = mBitmap->pixelAt(x, y);
            if (fg.toRGBA() != pixel.toRGBA()) {
                return createColorError("bad pixel (solid)", fg, pixel);
            }
        }

        return "";
    }

    std::string verify(int startX, int y, const std::vector<PicaPt>& dashes, const Color& fg,
                       const std::string& msg)
    {
        auto testDashes = dashes;
        testDashes.push_back(dashes[0]);  // test that it does wrap around
        testDashes.push_back(dashes[1]);

        std::vector<Color> expectedPixels;
        for (int i = 0;  i < testDashes.size();  ++i) {
            for (int j = 0;  j < int(testDashes[i].toPixels(mBitmap->dpi()));  ++j) {
                if (i % 2 == 0) {
                    expectedPixels.push_back(fg);
                } else {
                    expectedPixels.push_back(mBGColor);
                }
            }
        }

        int x = startX;
        for (auto &expected : expectedPixels) {
            auto pixel = mBitmap->pixelAt(x, y);
            if (pixel.toRGBA() != expected.toRGBA()) {
                return createPixelError(msg, x, y, expected, pixel);
            }
            x += 1;
        }

        return "";
    }
};

class RectStrokeAndFillTest : public BitmapTest
{
public:
    RectStrokeAndFillTest(int width)
        : BitmapTest(std::string("rect stroke and fill (") + std::to_string(width) + "px)", 13, 15)
        , mStrokeWidth(width)
    {}

    std::string run() override
    {
        auto dpi = mBitmap->dpi();
        int margin = 2;
        Color strokeColor = Color::kBlack;
        Color fillColor = Color::kGreen;
        auto r = Rect::fromPixels(0, 0, mWidth, mHeight, dpi);
        r.inset(PicaPt::fromPixels(margin, dpi), PicaPt::fromPixels(margin, dpi));
        if (mStrokeWidth % 2 == 1) {
            r.inset(PicaPt::fromPixels(0.5, dpi), PicaPt::fromPixels(0.5, dpi));
        }
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setStrokeColor(strokeColor);
        mBitmap->setStrokeWidth(PicaPt::fromPixels(mStrokeWidth, dpi));
        mBitmap->setFillColor(fillColor);
        mBitmap->drawRect(r, kPaintStrokeAndFill);
        mBitmap->endDraw();

        // Adjust bounds so we can use Rect::contains(). We need to shrink the width and
        // height by a pixel because we are comparing pixels (an area) with a mathematical
        // rectangle.
        auto halfStrokeWidth = PicaPt::fromPixels(0.5 * mStrokeWidth, dpi);
        auto onePx = PicaPt::fromPixels(1.0f, dpi);
        Rect strokeBounds = r.insetted(-halfStrokeWidth, -halfStrokeWidth);
        strokeBounds.width -= onePx;
        strokeBounds.height -= onePx;
        Rect fillBounds = r.insetted(halfStrokeWidth, halfStrokeWidth);
        fillBounds.width -= onePx;
        fillBounds.height -= onePx;
        int xLeft = int(r.x.toPixels(dpi));
        int xRight = int(r.maxX().toPixels(dpi));
        int yTop = int(r.y.toPixels(dpi));
        int yBottom = int(r.maxY().toPixels(dpi));

        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                auto p = Point::fromPixels(x, y, dpi);
                if (fillBounds.contains(p)) {
                    if (pixel.toRGBA() != fillColor.toRGBA()) {
                        return createPixelError("bad fill pixel", x, y, fillColor, pixel);
                    }
                } else if (strokeBounds.contains(p)) {
                    bool isExpected = (pixel.toRGBA() == strokeColor.toRGBA());
#if __APPLE__
                    // CoreGraphics seems to be a little off for the corner pixels on 1px lines
                    if (mStrokeWidth == 1 &&
                        ((x == xLeft && y == yTop) || (x == xRight && y == yTop) ||
                         (x == xLeft && y == yBottom) || (x == xRight && y == yBottom))) {
                        isExpected = (abs(pixel.red() - strokeColor.red()) <= 1.0f / 255.0f &&
                                      abs(pixel.green() - strokeColor.green()) <= 1.0f / 255.0f &&
                                      abs(pixel.blue() - strokeColor.blue()) <= 1.0f / 255.0f &&
                                      pixel.alpha() == strokeColor.alpha());
                    }
#endif // __APPLE__
                    if (!isExpected) {
                        return createPixelError("bad stroke pixel", x, y, strokeColor, pixel);
                    }
                } else {
                    if (pixel.toRGBA() != mBGColor.toRGBA()) {
                        return createPixelError("bad background pixel", x, y, mBGColor, pixel);
                    }
                }
            }
        }

        return "";
    }

private:
    int mStrokeWidth;
};

class AlphaBlendTest : public BitmapTest
{
public:
    AlphaBlendTest() : BitmapTest("alpha blending", 13, 13) {}

    std::string run() override
    {
        Color bg = Color::kBlack;
        Color fill1(1.0f, 0.0f, 0.0f, 0.5f);
        Color fill2(0.0f, 0.0f, 1.0f, 0.5f);
        Color blended1(0.5f, 0.0f, 0.0f, 1.0f);
        Color blended2(0.0f, 0.0f, 0.5f, 1.0f);
#if __APPLE__
        Color blended(0x3f, 0x00, 0x80, 0xff);
#else
        Color blended(0x40, 0x00, 0x80, 0xff);
#endif // __APPLE__
        auto dpi = mBitmap->dpi();
        auto rect1 = Rect::fromPixels(1, 1, 6, 5, dpi);
        auto rect2 = Rect::fromPixels(3, 3, 6, 5, dpi);
        auto intersection = Rect::fromPixels(3, 3, 4, 3, dpi);

        mBitmap->beginDraw();
        mBitmap->fill(bg);
        mBitmap->setFillColor(fill1);
        mBitmap->drawRect(rect1, kPaintFill);
        mBitmap->setFillColor(fill2);
        mBitmap->drawRect(rect2, kPaintFill);
        mBitmap->endDraw();

        auto onePx = PicaPt::fromPixels(1, dpi);
        rect1.width -= onePx;
        rect1.height -= onePx;
        rect2.width -= onePx;
        rect2.height -= onePx;
        intersection.width -= onePx;
        intersection.height -= onePx;

        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                auto p = Point::fromPixels(x, y, dpi);
                if (intersection.contains(p)) {
                    if (pixel.toRGBA() != blended.toRGBA()) {
                        return createPixelError("bad blended pixel", x, y, blended, pixel);
                    }
                } else if (rect1.contains(p)) {
                    if (pixel.toRGBA() != blended1 .toRGBA()) {
                        return createPixelError("bad rect1 pixel", x, y, blended1, pixel);
                    }
                } else if (rect2.contains(p)) {
                    if (pixel.toRGBA() != blended2.toRGBA()) {
                        return createPixelError("bad rect2 pixel", x, y, blended2, pixel);
                    }
                } else {
                    if (pixel.toRGBA() != bg.toRGBA()) {
                        return createPixelError("bad background pixel", x, y, bg, pixel);
                    }
                }
            }
        }
        return "";
    }
};

class RoundedRectTest : public BitmapTest
{
public:
    RoundedRectTest(int radius = 5)
        : BitmapTest(std::string("rounded rect (r=") + std::to_string(radius) + ")", 15, 17)
        , mRadius(radius)
    {}

    std::string run() override
    {
        // We color the background black and add up all the white pixels,
        // including antialiased ones as fractional pixels.
        int margin = 2;
        Color bg = Color::kBlack;
        Color fg = Color::kWhite;
        auto dpi = mBitmap->dpi();
        auto r = Rect::fromPixels(0, 0, mWidth, mHeight, dpi);
        mBitmap->beginDraw();
        mBitmap->setFillColor(bg);
        mBitmap->drawRect(r, kPaintFill);
        mBitmap->setFillColor(fg);
        r.inset(PicaPt::fromPixels(margin, dpi), PicaPt::fromPixels(margin, dpi));
        mBitmap->drawRoundedRect(r, PicaPt::fromPixels(mRadius, dpi), kPaintFill);
        mBitmap->endDraw();

        float total = 0.0f;
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                total += (pixel.red() + pixel.blue() + pixel.green()) / 3.0f;
                // Check that the pixels fits inside the (square) rect
                if (x < margin || x >= mWidth - margin || y < margin || y >= mHeight - margin) {
                    if (pixel.toRGBA() != bg.toRGBA()) {
                        return createPixelError("bad background pixel", x, y, bg, pixel);
                    }
                }
            }
        }

#if defined(_WIN32) || defined(_WIN64)
        // Using D2D1_FILL_MODE_WINDING is much less less precise.
        // D2D1_FILL_MODE_ALTERNATE is the same error level as macOS
        float acceptableError = 0.7f * float(mRadius);
#else
        float acceptableError = 0.125f * float(mRadius);  // 0.1 is okay for macOS, cairo needs 0.125
#endif // _WIN32 || _WIN64
        float quarterCircle = 0.25f * 3.141592f * float(mRadius * mRadius);
        float squareMinusQuarterCircle = float(mRadius * mRadius) - quarterCircle;
        float expectedArea = r.width.toPixels(dpi) * r.height.toPixels(dpi) - 4.0f * squareMinusQuarterCircle;
        if (std::abs(total - expectedArea) > acceptableError) {
            return createFloatError("wrong area", expectedArea, total, "px^2");
        }
        return "";
    }

private:
    int mRadius;
};

class EllipseTest : public BitmapTest
{
public:
    EllipseTest() : BitmapTest("ellipse", 13, 10) {}
//    EllipseTest() : BitmapTest("ellipse", 19, 13) {}

    std::string run() override
    {
        // We color the background black and add up all the white pixels,
        // including antialiased ones as fractional pixels.
        int margin = 2;
        Color bg = Color::kBlack;
        Color fg = Color::kWhite;
        auto dpi = mBitmap->dpi();
        auto r = Rect::fromPixels(0, 0, mWidth, mHeight, dpi);
        mBitmap->beginDraw();
        mBitmap->setFillColor(bg);
        mBitmap->drawRect(r, kPaintFill);
        mBitmap->setFillColor(fg);
        r.inset(PicaPt::fromPixels(margin, dpi), PicaPt::fromPixels(margin, dpi));
        r.height -= PicaPt::fromPixels(1, dpi);  // test for flipped coordinates
        mBitmap->drawEllipse(r, kPaintFill);
        mBitmap->endDraw();

        float total = 0.0f;
        for (int y = 0; y < mHeight; ++y) {
            for (int x = 0; x < mWidth; ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                total += (pixel.red() + pixel.blue() + pixel.green()) / 3.0f;
                // Check that the ellipse fits inside the rect
                if (x < margin || x >= mWidth - margin || y < margin || y >= mHeight - margin - 1) {
                    if (pixel.toRGBA() != bg.toRGBA()) {
                        return createPixelError("bad background pixel", x, y, bg, pixel);
                    }
                }
            }
        }

        float acceptableErr = 0.99f;  // macOS < 0.25;  win32 < 1.0
        float expectedArea = 3.141592f * 0.5 * r.width.toPixels(dpi) * 0.5 * r.height.toPixels(dpi);
        if (std::abs(total - expectedArea) > acceptableErr) {
            return createFloatError("wrong area", expectedArea, total, "px^2");
        }
        return "";
    }
};

class ClipRectTest : public BitmapTest
{
public:
    ClipRectTest() : BitmapTest("clip rect", 13, 15) {}

    std::string run() override
    {
        int margin = 2;
        Color fg(1.0f, 0.0f, 1.0f, 1.0f);
        auto dpi = mBitmap->dpi();
        auto r = Rect::fromPixels(0, 0, mWidth, mHeight, dpi);
        auto clip = Rect::fromPixels(margin, margin, mWidth - 3 * margin, mHeight - 3 * margin, dpi);

        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->clipToRect(clip);
        mBitmap->setFillColor(fg);
        mBitmap->drawRect(r, kPaintFill);
        mBitmap->endDraw();
        return verifyFillRect(margin, margin, mWidth - 3 * margin, mHeight - 3 * margin, mBGColor, fg);
    }
};

class ClipPathTest : public BitmapTest
{
public:
    ClipPathTest() : BitmapTest("clip path", 13, 15) {}

    std::string run() override
    {
        int margin = 2;
        Color fg(1.0f, 0.0f, 1.0f, 1.0f);
        auto dpi = mBitmap->dpi();
        auto r = Rect::fromPixels(0, 0, mWidth, mHeight, dpi);
        auto clip = Rect::fromPixels(margin, margin, mWidth - 3 * margin, mHeight - 3 * margin, dpi);

        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        auto path = mBitmap->createBezierPath();
        path->addRect(clip);
        mBitmap->clipToPath(path);
        mBitmap->setFillColor(fg);
        mBitmap->drawRect(r, kPaintFill);
        mBitmap->endDraw();
        return verifyFillRect(margin, margin, mWidth - 3 * margin, mHeight - 3 * margin, mBGColor, fg);
    }
};

class SaveRestoreTest : public BitmapTest
{
public:
    SaveRestoreTest() : BitmapTest("save/restore", 13, 15) {}

    std::string run() override
    {
        Color fill1(1.0f, 0.0f, 0.0f, 1.0f);
        Color stroke1(1.0f, 1.0f, 0.0f, 1.0f);
        Color fill2(0.0f, 0.0f, 1.0f, 1.0f);
        auto dpi = mBitmap->dpi();
        auto r = Rect::fromPixels(0, 0, mWidth, mHeight, dpi);
        int inset1 = 1;
        auto clip1 = r.insetted(PicaPt::fromPixels(inset1, dpi), PicaPt::fromPixels(inset1, dpi));
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->clipToRect(clip1);
        mBitmap->setStrokeWidth(PicaPt::fromPixels(2, dpi));
        mBitmap->setStrokeColor(stroke1);
        mBitmap->setFillColor(fill1);
        mBitmap->save();

        mBitmap->setStrokeWidth(PicaPt::fromPixels(1, dpi));
        mBitmap->setStrokeColor(Color::kBlack);
        mBitmap->setStrokeDashes({ PicaPt::fromPixels(3, dpi), PicaPt::fromPixels(3, dpi) }, PicaPt(0));
        mBitmap->setFillColor(fill2);
        int inset2 = 3;
        auto path = mBitmap->createBezierPath();
        path->addRoundedRect(r.insetted(PicaPt::fromPixels(inset2, dpi),
                                        PicaPt::fromPixels(inset2, dpi)),
                             PicaPt::fromPixels(3, dpi));
        mBitmap->clipToPath(path);
        auto innerClip = Rect::fromPixels(0, 6, mWidth, 2, dpi);
        mBitmap->clipToRect(innerClip);
        auto innerRect = Rect::fromPixels(inset2, 6, mWidth - 2 * inset2, 2, dpi);  // this is the actual clip rect
        // Translate a bit to test that the transformations are restored properly
        mBitmap->translate(PicaPt::fromPixels(inset2, dpi), PicaPt::fromPixels(inset2, dpi));
        mBitmap->drawRect(r, kPaintFill);  // but paint the entire thing to test clipping
        mBitmap->restore();

        // Draw a rect so that it is clipped (to test proper clip rect)
        auto rect1requested = Rect::fromPixels(0, 0, mWidth, inset1 + 1, dpi);
        mBitmap->drawRect(rect1requested, kPaintFill);
        auto rect1 = Rect::fromPixels(inset1, inset1, mWidth - 2 * inset1, 1, dpi);

        // Draw a line; should have proper width (2), color, and dashes (solid)
        mBitmap->drawLines({ Point::fromPixels(0, 10, dpi),
                             Point::fromPixels(mWidth, 10, dpi) });
        auto lineRect = Rect::fromPixels(inset1, 9, mWidth - 2 * inset1, 2, dpi);

        mBitmap->endDraw();

        // Shrink the rects so that contains() will work will pixel values
        auto onePx = PicaPt::fromPixels(1, dpi);
        innerRect.width -= onePx;
        innerRect.height -= onePx;
        rect1.width -= onePx;
        rect1.height -= onePx;
        lineRect.width -= onePx;
        lineRect.height -= onePx;

        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                auto p = Point::fromPixels(x, y, dpi);
                if (innerRect.contains(p)) {
                    if (pixel.toRGBA() != fill2.toRGBA()) {
                        return createPixelError("bad innerRect pixel", x, y, fill2, pixel);
                    }
                } else if (rect1.contains(p)) {
                    if (pixel.toRGBA() != fill1.toRGBA()) {
                        return createPixelError("bad rect1 pixel", x, y, fill1, pixel);
                    }
                } else if (lineRect.contains(p)) {
                    if (pixel.toRGBA() != stroke1.toRGBA()) {
                        return createPixelError("bad line pixel", x, y, stroke1, pixel);
                    }
                } else if (pixel.toRGBA() != mBGColor.toRGBA()) {
                    return createPixelError("bad background pixel", x, y, mBGColor, pixel);
                }
            }
        }
        return "";
    }
};

class GettersTest : public BitmapTest
{
public:
    GettersTest() : BitmapTest("getters", 1, 1) {}

    std::string run() override
    {
        mBitmap->beginDraw();

        // Fill the background so that if we have an error at least we get
        // consistent values for the fill, even though they are essentially
        // meaningless.
        mBitmap->fill(mBGColor);

        Color fg1(1.0f, 0.0f, 0.0f, 1.0f);
        Color fg2(0.0f, 1.0f, 0.0f, 1.0f);
        Color strokeColor1(1.0f, 0.0f, 1.0f, 1.0f);
        Color strokeColor2(0.0f, 1.0f, 1.0f, 1.0f);
        PicaPt strokeWidth1(1);
        PicaPt strokeWidth2(2);
        auto endCap1 = kEndCapRound;
        auto endCap2 = kEndCapSquare;
        auto join1 = kJoinRound;
        auto join2 = kJoinBevel;

        // Verify defaults
        auto err = verifyGetters(Color::kBlack, Color::kBlack, PicaPt(1),
                                 kEndCapButt, kJoinMiter);

        mBitmap->setFillColor(fg1);
        mBitmap->setStrokeColor(strokeColor1);
        mBitmap->setStrokeWidth(strokeWidth1);
        mBitmap->setStrokeEndCap(endCap1);
        mBitmap->setStrokeJoinStyle(join1);
        err = verifyGetters(fg1, strokeColor1, strokeWidth1, endCap1, join1);
        if (!err.empty()) {
            return err;
        }

        mBitmap->save();
        mBitmap->setFillColor(fg2);
        mBitmap->setStrokeColor(strokeColor2);
        mBitmap->setStrokeWidth(strokeWidth2);
        mBitmap->setStrokeEndCap(endCap2);
        mBitmap->setStrokeJoinStyle(join2);
        err = verifyGetters(fg2, strokeColor2, strokeWidth2, endCap2, join2);
        if (!err.empty()) {
            return err;
        }

        mBitmap->restore();
        err = verifyGetters(fg1, strokeColor1, strokeWidth1, endCap1, join1);
        if (!err.empty()) {
            return err;
        }
        
        mBitmap->endDraw();
        return "";
    }

    std::string verifyGetters(const Color& fg, const Color& strokeColor,
                              const PicaPt& strokeWidth, EndCapStyle endCap,
                              JoinStyle join)
    {
        if (mBitmap->fillColor().toRGBA() != fg.toRGBA()) {
            return createColorError("fillColor()", fg, mBitmap->fillColor());
        }
        if (mBitmap->strokeColor().toRGBA() != strokeColor.toRGBA()) {
            return createColorError("strokeColor()", strokeColor,
                                    mBitmap->strokeColor());
        }
        if (mBitmap->strokeWidth() != strokeWidth) {
            std::stringstream err;
            err << "strokeWidth(): expected " << strokeWidth.asFloat()
                << " pt but got " << mBitmap->strokeWidth().asFloat() << " pt";
            return err.str();
        }
        if (mBitmap->strokeEndCap() != endCap) {
            std::stringstream err;
            err << "endCapStyle(): expected " << int(endCap) << " but got "
                << int(mBitmap->strokeEndCap());
            return err.str();
        }
        if (mBitmap->strokeJoinStyle() != join) {
            std::stringstream err;
            err << "joinStyle(): expected " << int(join) << " but got "
                << int(mBitmap->strokeJoinStyle());
            return err.str();
        }
        return "";
    }
};

class FontTest : public BitmapTest
{
    static constexpr int kMargin = 1;
public:
    FontTest(const std::string& family, float pointSize)
        : BitmapTest("text",
                     int(std::ceil(0.75 * pointSize)) + 2 * kMargin,
                     int(std::ceil(pointSize)) + 2 * kMargin + 10)
    {
        mFontName = family;
        mPointSize = pointSize;
        mName += " " + mFontName + " " + std::to_string(pointSize) + "pt";
    }

    std::string run() override
    {
        float acceptableErr = 1.0f;  // macOS: 0.5, Windows: 1.0
        Color bg = Color::kBlack;
        Color fg = Color::kRed;
        Color baselineColor = Color::kBlue;
        auto dpi = mBitmap->dpi();
        Font arial(mFontName, PicaPt::fromPixels(mPointSize, dpi));
        auto metrics = arial.metrics(*mBitmap);

        if (metrics.ascent.toPixels(dpi) == 0.0f || metrics.ascent.toPixels(dpi) == 0.0f) {
            return "Font does not exist: metrics are 0";
        }
        if (metrics.lineHeight != metrics.ascent + metrics.descent + metrics.leading) {
            return "incorrect line height, should be: ascent + descent + leading";
        }

        // Compute metrics for glyph. We want a glyph that does not go below
        // the baseline. Typically rounded shapes need to descend a little
        // below so that they give the impression that they are exactly on
        // the line, so we don't want to use those.
        const char *glyph = "T";
        float baselineY = kMargin + metrics.ascent.toPixels(dpi);
        mBitmap->beginDraw();
        mBitmap->fill(bg);
        mBitmap->setFillColor(fg);
        mBitmap->setStrokeColor(baselineColor);
        mBitmap->setStrokeWidth(PicaPt::fromPixels(1, dpi));
        // Draw a 1px line at pixel baselineYpx. Since the baseline is the
        // bottom of the ascenders, the line should be completely below an
        // ascender-only glyph like "T".
        float baselineYpx = std::floor(baselineY) + 0.5f;
        auto upperLeft = Point::fromPixels(kMargin, kMargin, dpi);
        mBitmap->drawLines({ Point::fromPixels(0, baselineYpx, dpi),
                             Point::fromPixels(mWidth, baselineYpx, dpi) });
        mBitmap->drawText(glyph, upperLeft, arial, kPaintFill);
        mBitmap->endDraw();
        // Compute height of glyph
        float yMin = 100.0f, yMax = 0.0f;
        int midY = int(mPointSize / 2);
        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                if (pixel.toRGBA() != bg.toRGBA()) {
                    if (y >= midY && pixel.red() > 0.0f) {
                        yMax = std::max(yMax, float(y) + pixel.red());
                    } else {
                        yMin = std::min(yMin, float(y + 1) - pixel.red());
                    }
                }
            }
        }
        float capHeight = yMax - yMin;
        if (abs(metrics.capHeight.toPixels(dpi) - capHeight) > acceptableErr) {
            return createFloatError("incorrect capHeight", metrics.capHeight.toPixels(dpi), capHeight);
        }

        // Verify that the glyph sits directly on top of the baseline-line (which
        // should be the pixels directly below the glyph)
        std::string result;
        result = verifySitsOnTopOfBaseline(arial, upperLeft);
        if (!result.empty()) {
            return "[baseline offset = 0px] " + result;
        }
        result = verifySitsOnTopOfBaseline(arial, upperLeft + Point(PicaPt::kZero, PicaPt::fromPixels(0.25f, dpi)));
        if (!result.empty()) {
            return "[baseline offset = 0.25px] " + result;
        }
        result = verifySitsOnTopOfBaseline(arial, upperLeft + Point(PicaPt::kZero, PicaPt::fromPixels(0.49f, dpi)));
        if (!result.empty()) {
            return "[baseline offset = 0.49px] " + result;
        }
        result = verifySitsOnTopOfBaseline(arial, upperLeft + Point(PicaPt::kZero, PicaPt::fromPixels(0.51f, dpi)));
        if (!result.empty()) {
            return "[baseline offset = 0.51px] " + result;
        }
        result = verifySitsOnTopOfBaseline(arial, upperLeft + Point(PicaPt::kZero, PicaPt::fromPixels(0.75f, dpi)));
        if (!result.empty()) {
            return "[baseline offset = 0.75px] " + result;
        }

        // writeTIFF("/tmp/debug-font.tiff", *mBitmap);

        // Compute metrics for "g"
        mBitmap->beginDraw();
        mBitmap->fill(bg);
        mBitmap->drawLines({ Point::fromPixels(0, baselineYpx, dpi),
                             Point::fromPixels(mWidth, baselineYpx, dpi) });  // helps with debugging
        mBitmap->drawText("g", upperLeft, arial, kPaintFill);
        mBitmap->endDraw();
        yMax = 0.0f;
        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                if (pixel.toRGBA() != bg.toRGBA()) {
                    if (float(y) >= baselineY) {
                        yMax = std::max(yMax, float(y) + pixel.red());
                    }
                }
            }
        }
        float descent = yMax - std::floor(baselineY);
        if (abs(metrics.descent.toPixels(dpi) - descent) > acceptableErr) {
            auto err = createFloatError("incorrect descent", metrics.descent.toPixels(dpi), descent);
            err += " (ascent: " + std::to_string(metrics.ascent.toPixels(dpi)) + ")";
            return err;
        }

        return "";
    }

    std::string verifySitsOnTopOfBaseline(const Font& font, const Point& upperLeft)
    {
        float dpi = mBitmap->dpi();
        Color bg = Color::kBlack;
        Color fg = Color::kRed;
        Color baselineColor = Color::kBlue;

        auto metrics = font.metrics(*mBitmap);
        auto baselineY = (upperLeft.y + metrics.ascent).toPixels(dpi);

        mBitmap->beginDraw();
        mBitmap->fill(bg);
        mBitmap->setFillColor(fg);
        mBitmap->setStrokeColor(baselineColor);
        mBitmap->setStrokeWidth(PicaPt::fromPixels(1, dpi));
        // Draw a 1px line at pixel baselineYpx. Since the baseline is the
        // bottom of the ascenders, the line should be completely below an
        // ascender-only glyph like "T".
        float baselineYpx = std::floor(baselineY) + 0.5f;
        mBitmap->drawLines({ Point::fromPixels(0, baselineYpx, dpi),
                             Point::fromPixels(mWidth, baselineYpx, dpi) });
        mBitmap->drawText("T", upperLeft, font, kPaintFill);
        mBitmap->endDraw();
        float totalAboveLine = 0.0f;
        int aboveY = int(baselineYpx) - 1;
        for (int x = 0;  x < mWidth;  ++x) {
            auto above = mBitmap->pixelAt(x, aboveY);
            totalAboveLine += above.red();
            auto on = mBitmap->pixelAt(x, int(baselineYpx));
            if (on.red() > 0.0f) {
                return createPixelError("glyph descends below the baseline",
                                        x, int(baselineYpx), baselineColor, above);
            }
        }
        if (totalAboveLine == 0.0f) {
            return std::string("glyph sits above baseline: y=") + std::to_string(aboveY) +
                   " is has no red pixels";
        }

        return "";
    }

protected:
    std::string mFontName;
    float mPointSize;
};

class BadFontTest : public BitmapTest
{
    static constexpr int kFontHeight = 20;
    static constexpr int kMargin = 1;
public:
    BadFontTest() : BitmapTest("non-existant font", 5, 5)
    {}

    std::string run() override
    {
        // Fill the background, otherwise if we fail, the pixel values are
        // garbage which makes the problem look like a memory problem instead
        // of a font problem.
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->endDraw();

        auto dpi = mBitmap->dpi();
        Font font("NonExistentFont", PicaPt::fromPixels(kFontHeight, dpi));
        auto metrics = font.metrics(*mBitmap);
        if (metrics.ascent != PicaPt(0) || metrics.descent != PicaPt(0)) {
            return "Expected non-existent font to have zero metrics";
        }
        // TODO: what is the expected behavior if we attempt to draw?
        //    macOS:   nothing shows up
        //    Windows: uses a font, but appears to be the wrong size

        return "";
    }
};

class FontStyleTest : public BitmapTest
{
    // Larger fonts make italics easier to detect since the tilt angle is small
    // and small fonts don't have enough "time" to move far horizontally between
    // the top and bottom of the glyph.
    static constexpr int kFontHeight = 20;
    static constexpr int kMargin = 1;
public:
    FontStyleTest() : BitmapTest("font style", 10, kFontHeight + 2 * kMargin + 5)
    {
        mBGColor = Color::kBlack;
    }

    std::string run() override
    {
        Color fg = Color::kRed;
        auto dpi = mBitmap->dpi();
        Font font("Arial", PicaPt::fromPixels(kFontHeight, dpi));
        // Assume that the ascents of the different styles don't change much
        mCapHeight = font.metrics(*mBitmap).capHeight.toPixels(dpi);
        auto p = Point::fromPixels(kMargin, kMargin, dpi);
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->setFillColor(fg);
        mBitmap->drawText("I", p, font, kPaintFill);
        mBitmap->endDraw();
        if (isItalic()) { return "glyph appears to be italic (expected regular)"; }
        if (isBold()) { return "glyph appears to be bold (expected regular)"; }

        auto bold = font.fontWithStyle(kStyleBold);
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->drawText("I", p, bold, kPaintFill);
        mBitmap->endDraw();
        if (isItalic()) { return "glyph appears to be italic (expected bold)"; }
        if (!isBold()) { return "glyph appears to be regular (expected bold)"; }

        auto italic = bold.fontWithStyle(kStyleItalic);  // also tests removing bold
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->drawText("I", p, italic, kPaintFill);
        mBitmap->endDraw();
        if (!isItalic()) { return "glyph appears to be regular (expected italic)"; }
        if (isBold()) { return "glyph appears to be bold (expected italic)"; }

        auto boldItalic = bold.fontWithStyle(kStyleBoldItalic);
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->drawText("I", p, boldItalic, kPaintFill);
        mBitmap->endDraw();
        if (!isItalic()) { return "glyph appears not to be italic (expected bold+italic)"; }
        if (!isBold()) { return "glyph appears not to be bold (expected bold+italic)"; }

        auto regular = boldItalic.fontWithStyle(kStyleNone);  // test removing bold and italic
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->drawText("I", p, font, kPaintFill);
        mBitmap->endDraw();
        if (isItalic()) { return "glyph appears to be italic (expected regular) [#2]"; }
        if (isBold()) { return "glyph appears to be italic (expected regular) [#2]"; }

        return "";
    }

    bool isItalic() const
    {
        // Check that we are not non-italic (regular/bold). If we have a column
        // with all the pixels in the bounding box set, it isn't italic.
        if (isVertical()) {
            return false;
        }

        // The tilt is not large, so we can't check number of pixels vertically.
        // Find the bounding box and check the aspect ratio.
        int minX = 100, maxX = 0;
        int minY = 100, maxY = 0;
        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                if (pixel.red() > 0.0f) {
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
        }
        int height = maxY - minY;
        int width = maxX - minX;

        return (float(height) / float(width) < 5.0f);
    }

    bool isBold() const
    {
        float total = 0.0f;
        int y = mHeight / 2;
        for (int x = 0;  x < mWidth;  ++x) {
            auto pixel = mBitmap->pixelAt(x, y);
            total += pixel.red();
        }
        // Bolding is subtle: 16pt Arial has 1.784 regular, 1.847 italic, 2.58 bold (macOS)
        //                    20pt Arial has 2.09 regular, 2.98 bold (Windows)
        //                                   2.23 regular, 2.31 italic, 3.22 bold (macOS)
        return total >= 2.5f;
    }

    bool isVertical() const
    {
        float avgColumnTotal = 0.0f;
        int n = 0;
        for (int x = 0;  x < mWidth;  ++x) {
            float columnTotal = 0.0f;
            for (int y = 0;  y < mHeight;  ++y) {
                auto pixel = mBitmap->pixelAt(x, y);
                if (pixel.red() > 0.8f) {
                    columnTotal += pixel.red();
                }
            }
            if (columnTotal > 0.0f) {
                avgColumnTotal += columnTotal;
                n += 1;
            }
        }
        avgColumnTotal /= float(n);
        return (avgColumnTotal >= mCapHeight - 2.0f);
    }

private:
    float mCapHeight = 0.0f;
};

class StrokedTextTest : public BitmapTest
{
    static constexpr int kPointSize = 20;
public:
    StrokedTextTest() : BitmapTest("stroked text", kPointSize, kPointSize) {}

    std::string run() override
    {
        float acceptableErr = 0.25f;
        float expectedFill = 2.0f;  // at least
        Color bg = Color::kBlack;
        Color stroke = Color::kRed;
        Color fill = Color::kBlue;
        auto dpi = mBitmap->dpi();
        auto strokeWidth = PicaPt::fromPixels(2, dpi);
        // We want a font that is heavy enough that we can tell if it is filled or not.
        // Georgia is thicker than, say, Arial, and bold will help.
        Font font("Georgia", PicaPt::fromPixels(kPointSize, dpi), kStyleBold);
        Point topLeft = Point::fromPixels(2, 0, dpi);
        int y = kPointSize / 2;

        mBitmap->beginDraw();
        mBitmap->setFillColor(fill);
        mBitmap->setStrokeColor(stroke);
        mBitmap->setStrokeWidth(strokeWidth);

        // Test stroke and no fill. Draw a horizontal line across the glyph
        // and count the number of stroked pixels.
        mBitmap->fill(bg);
        mBitmap->drawText("O", topLeft, font, kPaintStroke);
        mBitmap->endDraw();
        float nStroke = 0.0f;
        for (int x = 0;  x < mWidth;  ++x) {
            auto pixel = mBitmap->pixelAt(x, y);
            nStroke += pixel.red();
            if (pixel.blue() > 0.0f) {
                std::stringstream err;
                err << "pixel at (" << x << ", " << y << ") [" << pixel.toHexString()
                    << "] has unexpected fill color";
                return err.str();
            }
        }
        float drawnStrokeWidth = 0.25f * nStroke;  // horiz line crosses O border four times
        if (std::abs(drawnStrokeWidth - strokeWidth.toPixels(dpi)) > acceptableErr) {
            return createFloatError("bad stroke width (stroke only)", strokeWidth.toPixels(dpi), drawnStrokeWidth);
        }

        // Test stroke and fill
        mBitmap->beginDraw();
        mBitmap->fill(bg);
        mBitmap->drawText("O", topLeft, font, kPaintStrokeAndFill);
        mBitmap->endDraw();
        nStroke = 0.0f;
        float nFill = 0.0f;
        for (int x = 0;  x < mWidth;  ++x) {
            auto pixel = mBitmap->pixelAt(x, y);
            nStroke += pixel.red();
            nFill += pixel.blue();
        }
        drawnStrokeWidth = 0.25f * nStroke;  // horiz line crosses O border four times
        if (std::abs(drawnStrokeWidth - strokeWidth.toPixels(dpi)) > acceptableErr) {
            return createFloatError("bad stroke width (stroke and fill)", strokeWidth.toPixels(dpi), drawnStrokeWidth);
        }

        if (nFill < expectedFill) {
            std::stringstream err;
            err << "expected at least " << expectedFill << " fill pixels, got " << nFill;
            return err.str();
        }
        return "";
    }
};

class TextMetricsTest : public BitmapTest
{
public:
    TextMetricsTest() : BitmapTest("text metrics", 1, 1) {}

    std::string run() override
    {
        // Fill the background so that if we have an error at least we get
        // consistent values for the fill, even though they are essentially
        // meaningless.
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->endDraw();

        float fontSize = 12.0f;
        float dpi = mBitmap->dpi();
        auto fontSizePt = PicaPt::fromPixels(fontSize, dpi);
        Font font("Arial", fontSizePt);

        // Make sure we don't crash or get bogus numbers for empty string
        auto tm = mBitmap->textMetrics("", font, kPaintFill);
        if (tm.width != PicaPt::kZero || tm.height != PicaPt::kZero) {
            std::stringstream err;
            err << "Empty string has non-zero size: (" << tm.width.toPixels(dpi)
                << ", " << tm.height.toPixels(dpi) << ")";
            return err.str();
        }

        tm = mBitmap->textMetrics("Ag", font, kPaintFill);
#if defined(__APPLE__)
        if (tm.width < PicaPt::fromPixels(14.6f, dpi) ||
            tm.width > PicaPt::fromPixels(14.7f, dpi) ||
            tm.height < PicaPt::fromPixels(13.4f, dpi) ||
            tm.height > PicaPt::fromPixels(13.5f, dpi)) {
#elif defined(_WIN32) || defined(_WIN64)
        if (tm.width < PicaPt::fromPixels(14.6f, dpi) ||
            tm.width > PicaPt::fromPixels(14.7f, dpi) ||
            tm.height < PicaPt::fromPixels(13.7f, dpi) ||
            tm.height > PicaPt::fromPixels(13.8f, dpi)) {
#else
        // Cairo toy font
        //if (tm.width != PicaPt::fromPixels(14.0f, dpi) ||
        //(tm.height != PicaPt::fromPixels(11.0f, dpi) &&
        // tm.height != PicaPt::fromPixels(12.0f, dpi))) {
        if (tm.width < PicaPt::fromPixels(14.0f, dpi) ||
            tm.width > PicaPt::fromPixels(15.0f, dpi) ||
            tm.height != PicaPt::fromPixels(14.0f, dpi)) {
#endif
            std::stringstream err;
            err << fontSize << "pt \"Ag\" has incorrect size: ("
                << tm.width.toPixels(dpi) << ", " << tm.height.toPixels(dpi) << ")";
            return err.str();
        }

        const char *text = "This is some long text that should wrap";
        auto noWrap = mBitmap->createTextLayout(text, font, Color::kWhite)->metrics();
        auto wrappedWidth = 0.6f * noWrap.width;
        auto wrap = mBitmap->createTextLayout(text, font, Color::kWhite,
                                              Size(wrappedWidth, PicaPt::kZero))->metrics();
        if (wrap.width >= wrappedWidth) {
            std::stringstream err;
            err << "Text did not wrap correctly: expected width <= "
                << wrappedWidth.toPixels(dpi) << " px, but got "
                << wrap.width.toPixels(dpi) << " px";
            return err.str();
        }
        if (wrap.height <= 1.5f * fontSizePt || wrap.height >= 2.5f * fontSizePt) {
            std::stringstream err;
            err << "Wrapped text has poor line spacing: expected height of "
                << "about " << 2.0f * fontSize << " px but got "
                << wrap.height.toPixels(dpi) << " px" << std::endl;
            return err.str();
        }

        return "";
    }
};

class TextAlignmentTest : public BitmapTest
{
    static constexpr int kPointSize = 13;
public:
    TextAlignmentTest() : BitmapTest("text alignment", 3.0 * kPointSize, 1.75 * kPointSize) {}

    std::string run() override
    {
        float dpi = mBitmap->dpi();
        auto fontSizePt = PicaPt::fromPixels(kPointSize, dpi);
        Font font("Arial", fontSizePt);
        Color fg = Color::kWhite;
        auto metrics = mBitmap->fontMetrics(font);

        const char *t = "MgM";
        PicaPt smallFontSize = PicaPt::fromPixels(std::floor(0.5f * kPointSize), dpi);
        Font smallFont = font.fontWithPointSize(smallFontSize);
        auto fm = mBitmap->fontMetrics(smallFont);
        auto tm = mBitmap->textMetrics(t, smallFont);
        int minWidthPx = std::ceil((tm.width + smallFontSize).toPixels(dpi));
        int minHeightPx = std::ceil((tm.height + smallFontSize).toPixels(dpi));
        if (mBitmap->width() < minWidthPx) {
            return "bitmap is too narrow, needs " + std::to_string(minWidthPx) + ", got " + std::to_string(mBitmap->width());
        }
        if (mBitmap->height() < minHeightPx) {
            return "bitmap is too narrow, needs " + std::to_string(minHeightPx) + ", got " + std::to_string(mBitmap->height());
        }

        // Alignment::kNone
        Rect drawRect(PicaPt::kZero, PicaPt::kZero,
                      PicaPt::fromPixels(mBitmap->width(), dpi),
                      PicaPt::fromPixels(mBitmap->height(), dpi));
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->setFillColor(Color::kWhite);
        mBitmap->drawText(t, drawRect, Alignment::kNone, kWrapWord, smallFont, kPaintFill);
        mBitmap->endDraw();
        auto expectedHeight = fm.capHeight + fm.descent;
        auto expectedY = fm.ascent - fm.capHeight;
        auto maybeErr = verifyTextRect(0.0f, expectedY.toPixels(dpi),
                                       tm.width.toPixels(dpi), expectedHeight.toPixels(dpi),
                                       "Alignment::kNone");
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // Alignment::kLeft | Alignment::kTop
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->setFillColor(Color::kWhite);
        mBitmap->drawText(t, drawRect, Alignment::kLeft | Alignment::kTop, kWrapWord, smallFont, kPaintFill);
        mBitmap->endDraw();
        maybeErr = verifyTextRect(0.0f, 0.0f, tm.width.toPixels(dpi), expectedHeight.toPixels(dpi),
                                  "Alignment::kLeft | kTop");
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // Alignment::kHCenter | Alignment::::kVCenter
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->setFillColor(Color::kWhite);
        mBitmap->drawText(t, drawRect, Alignment::kHCenter | Alignment::kVCenter, kWrapWord,
                          smallFont, kPaintFill);
        mBitmap->endDraw();
        auto xMargin = 0.5f * (PicaPt::fromPixels(mBitmap->width(), dpi) - tm.width);
        auto yMargin = 0.5f * (PicaPt::fromPixels(mBitmap->height(), dpi) - expectedHeight);
        maybeErr = verifyTextRect(xMargin.toPixels(dpi), yMargin.toPixels(dpi),
                                  tm.width.toPixels(dpi), expectedHeight.toPixels(dpi),
                                  "Alignment::kHCenter | kVCenter");
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // Alignment::kRight | Alignment::kBottom
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->setFillColor(Color::kWhite);
        mBitmap->drawText(t, drawRect, Alignment::kRight | Alignment::kBottom, kWrapWord,
                          smallFont, kPaintFill);
        mBitmap->endDraw();
        xMargin = PicaPt::fromPixels(mBitmap->width(), dpi) - tm.width;
        yMargin = PicaPt::fromPixels(mBitmap->height(), dpi) - expectedHeight;
        maybeErr = verifyTextRect(xMargin.toPixels(dpi), yMargin.toPixels(dpi),
                                  tm.width.toPixels(dpi), expectedHeight.toPixels(dpi),
                                  "Alignment::kRight | kBottom");
        if (!maybeErr.empty()) {
            return maybeErr;
        }
        // Double-check if bottom line has some pixel on from the descender
        float n = 0.0f;
        for (int xx = 0;  xx < mBitmap->width();  ++xx) {
            auto c = mBitmap->pixelAt(xx, mBitmap->height() - 1);
            if (c.red() > 0.0f) {
                n += c.red();
            }
        }
        if (n <= 0.001f) {
            return "Alignment::kRight | kBottom: bottom pixel should be the bottom of the descender";
        }

        // Verify alignment works correctly with no word wrap
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->setFillColor(Color::kWhite);
        mBitmap->drawText(t, drawRect, Alignment::kRight | Alignment::kBottom, kWrapNone,
                          smallFont, kPaintFill);
        mBitmap->endDraw();
        xMargin = PicaPt::fromPixels(mBitmap->width(), dpi) - tm.width;
        yMargin = PicaPt::fromPixels(mBitmap->height(), dpi) - expectedHeight;
        maybeErr = verifyTextRect(xMargin.toPixels(dpi), yMargin.toPixels(dpi),
                                  tm.width.toPixels(dpi), expectedHeight.toPixels(dpi),
                                  "Alignment::kRight | kBottom, no word wrap");
        if (!maybeErr.empty()) {
            return maybeErr;
        }
        
        return "";
    }

    std::string verifyTextRect(float x, float y, float width, float height, const std::string& msg)
    {
        float minX = 9999.0f, minY = 9999.0f, maxX = 0.0f, maxY = 0.0f;
        for (int yy = 0;  yy < mBitmap->height();  ++yy) {
            for (int xx = 0;  xx < mBitmap->width();  ++xx) {
                auto c = mBitmap->pixelAt(xx, yy);
                if (c.red() > 0.0f) {
                    minX = std::min(minX, float(xx + 1) - c.red());
                    minY = std::min(minY, float(yy + 1) - c.red());
                    maxX = std::max(maxX, float(xx) + c.red());
                    maxY = std::max(maxY, float(yy) + c.red());
                }
            }
        }
        const float maxErr = 1.1f;  // "M" has some space on left and right
        if (std::abs(minX - x) <= maxErr &&
            std::abs(minY - y) <= maxErr &&
            std::abs(maxX - (x + width)) <= maxErr &&
            std::abs(maxY - (y + height)) <= maxErr) {
            return "";
        } else {
            std::stringstream s;
            s << msg << ": expected rect(" << x << ", " << y << ", " << width << ", " << height
              << "), got (" << minX << ", " << minY << ", " << maxX - minX << ", " << maxY - minY << ")";
            return s.str();
        }
    }
};

class BasicTextLayoutTest : public BitmapTest
{
    static constexpr int kPointSize = 13;
public:
    BasicTextLayoutTest() : BitmapTest("basic createTextLayout()", 1, 1) {}

    std::string run() override
    {
        // Fill the background so that if we have an error at least we get
        // consistent values for the fill, as some of the tests do not draw.
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->endDraw();

        float dpi = mBitmap->dpi();
        auto fontSizePt = PicaPt::fromPixels(kPointSize, dpi);
        Font font("Arial", fontSizePt);
        Color fg = Color::kWhite;
        auto metrics = mBitmap->fontMetrics(font);

        // Make sure empty text works. (Failures here are more likely to crash
        // rather than give the wrong number of glyphs)
        auto glyphs = mBitmap->createTextLayout("", font, fg)->glyphs();
        if (glyphs.size() != 0) {
            return std::string("Incorrect number of glyphs for '': got ") + std::to_string(glyphs.size()) + ", expected 0";
        }

        // Basic test
        glyphs = mBitmap->createTextLayout("im", font, fg)->glyphs();
        if (glyphs.size() != 2) {
            return std::string("Incorrect number of glyphs for 'im': got ") + std::to_string(glyphs.size()) + ", expected 2";
        }
        if (glyphs[0].frame.y.toPixels(dpi) < -0.001f || glyphs[0].frame.y.toPixels(dpi) >= 1.0f) {
            return "Glyph frames should start at the top of the line (baseline - ascent); for idx=0, got y=" + std::to_string(glyphs[0].frame.y.toPixels(dpi)) + ", expected in [0.0, 1.0)";
        }
        if (glyphs[1].frame.y.toPixels(dpi) < -0.001f || glyphs[1].frame.y.toPixels(dpi) >= 1.0f) {
            return "Glyph frames should start at the top of the line (baseline - ascent); for idx=1, got y=" + std::to_string(glyphs[1].frame.y.toPixels(dpi)) + ", expected in [0.0, 1.0)";
        }

        // Spaces should have glyphs
        glyphs = mBitmap->createTextLayout("a  z", font, fg)->glyphs();
        if (glyphs.size() != 4) {
            return "Incorrect number of glyphs for 'a  z': got " + std::to_string(glyphs.size()) + ", expected 4";
        }

        // Multiple lines
        glyphs = mBitmap->createTextLayout("A \nA", font, fg)->glyphs();
        if (glyphs.size() != 4) {
            return "Incorrect number of glyphs for 'A \\nA': got " + std::to_string(glyphs.size()) + ", expected 4";
        }
        auto secondLineYPt = metrics.lineHeight;
        auto epsilon = PicaPt::fromPixels(.5, dpi); // spacing may vary due to baseline pixel snapping
        if (glyphs[3].frame.y < glyphs[0].frame.maxY() || glyphs[3].frame.y >= secondLineYPt + epsilon) {
            return "Top of frame in second line is incorrect: expected " +
                   std::to_string((glyphs[0].frame.maxY() + metrics.leading).toPixels(dpi)) +
                   " px, got " + std::to_string(glyphs[3].frame.y.toPixels(dpi)) + " px";
        }
        if (glyphs[0].line != 0) {
            return "glyph[0].line is incorrect: expected 0, got " + std::to_string(glyphs[0].line);
        }
        if (glyphs[1].line != 0) {
            return "glyph[1].line is incorrect: expected 0, got " + std::to_string(glyphs[1].line);
        }
        if (glyphs[2].line != 0) {
            return "glyph[2].line is incorrect: expected 0, got " + std::to_string(glyphs[2].line);
        }
        if (glyphs[3].line != 1) {
            return "glyph[3].line is incorrect: expected 1, got " + std::to_string(glyphs[3].line);
        }

        // Multiple lines, one empty
        glyphs = mBitmap->createTextLayout("A\n\nA", font, fg)->glyphs();
        if (glyphs.size() != 4) {
            return "Incorrect number of glyphs for 'A \\nA': got " + std::to_string(glyphs.size()) + ", expected 4";
        }

        // Word-wrap
        auto w = mBitmap->textMetrics("the", font).width + 0.5f * fontSizePt;
        glyphs = mBitmap->createTextLayout("the and", font, fg, Size(w, PicaPt::kZero))->glyphs();
        if (glyphs[5].frame.y < glyphs[0].frame.maxY() || glyphs[5].frame.y >= secondLineYPt + epsilon) {
            return "Expected two lines of word wrap";
        }
        if (glyphs[2].line != 0) {
            return "glyph[2].line is incorrect: expected 0, got " + std::to_string(glyphs[2].line);
        }
        if (glyphs[3].line != 0) {
            return "glyph[3].line is incorrect: expected 0, got " + std::to_string(glyphs[3].line);
        }
        if (glyphs[4].line != 1) {
            return "glyph[4].line is incorrect: expected 1, got " + std::to_string(glyphs[4].line);
        }

        // No word-wrap
        glyphs = mBitmap->createTextLayout("the and", font, fg, Size(w, PicaPt::kZero),
                                           Alignment::kNone, kWrapNone)->glyphs();
        if (std::abs((glyphs[5].frame.y - glyphs[0].frame.y).toPixels(dpi)) > epsilon.toPixels(dpi)) {
            return "Expected only one line with word-wrapping off";
        }

        // All glyph frames are ascent + descent in height
        glyphs = mBitmap->createTextLayout("Ag", font, fg)->glyphs();
        auto expectedHeightPx = (metrics.ascent + metrics.descent).toPixels(dpi);
        if (std::abs(glyphs[0].frame.height.toPixels(dpi) - expectedHeightPx) > epsilon.toPixels(dpi)) {
            return "Frame for glyph 'A' should have height equal to ascent + descent";
        }
        if (std::abs(glyphs[1].frame.height.toPixels(dpi) - expectedHeightPx) > epsilon.toPixels(dpi)) {
            return "Frame for glyph 'g' should have height equal to ascent + descent";
        }

        // Glyphs should adjust properly for alignment
        Size layoutSize(4.0f * fontSizePt, 3.0f * fontSizePt);
        glyphs = mBitmap->createTextLayout("Ag", font, fg, layoutSize,
                                           Alignment::kRight | Alignment::kBottom)->glyphs();
        w = glyphs[0].frame.width + glyphs[1].frame.width;
        auto expectedX = layoutSize.width - w;
        auto expectedY = layoutSize.height - glyphs[0].frame.height;
        if (glyphs[0].frame.x < expectedX - epsilon) {
            return createFloatError("glyph[0].frame.x incorrect for Alignment::kRight",
                                    expectedX.asFloat(), glyphs[0].frame.x.asFloat());
        }
        if (glyphs[0].frame.y < expectedY - epsilon) {
            return createFloatError("glyph[0].frame.y incorrect for Alignment::kBottom",
                                    expectedY.asFloat(), glyphs[0].frame.y.asFloat());
        }

        // TODO: test empty line glyphs in various alignments

        return "";
    }
};

class RichTextRunsTest : public BitmapTest
{
    static constexpr int kPointSize = 13;
public:
    RichTextRunsTest() : BitmapTest("rich text (runs)", 1, 1) {}

    std::string run() override
    {
        Text t;

        // Sanity checks for Text::setTextRun()

        t = Text("tests", Font(), Color::kBlack);
        t.setColor(Color::kRed, 0, -1);
        if (t.runs().size() != 1
            || t.runs()[0].startIndex != 0 || t.runs()[0].length != 5) {
            return "setTextRun() failed setting run with same index and size";
        }

        t = Text("tests", Font(), Color::kBlack);
        t.setColor(Color::kRed, 0, 1);
        if (t.runs().size() != 2
            || t.runs()[0].startIndex != 0 || t.runs()[0].length != 1
            || t.runs()[1].startIndex != 1 || t.runs()[1].length != 4 ) {
            return "setTextRun() failed setting run with same index and smaller size";
        }

        t = Text("tests", Font(), Color::kBlack);
        t.setColor(Color::kRed, 1, 3);
        if (t.runs().size() != 3
            || t.runs()[0].startIndex != 0 || t.runs()[0].length != 1
            || t.runs()[1].startIndex != 1 || t.runs()[1].length != 3
            || t.runs()[2].startIndex != 4 || t.runs()[2].length != 1 ) {
            return "setTextRun() failed setting run with larger index and smaller size";
        }

        t = Text("tests", Font(), Color::kBlack);
        t.setColor(Color::kRed, 2, 2);
        t.setBackgroundColor(Color::kBlue, 1, 3);
        if (t.runs().size() != 4
            || t.runs()[0].startIndex != 0 || t.runs()[0].length != 1
            || t.runs()[1].startIndex != 1 || t.runs()[1].length != 1
            || t.runs()[2].startIndex != 2 || t.runs()[2].length != 2
            || t.runs()[3].startIndex != 4 || t.runs()[3].length != 1 ) {
            return "setTextRun() failed setting run with larger index and larger size";
        }

        t = Text("abc", Font(), Color::kBlack);
        t.setBold(0, 1);
        t.setItalic(1, 1);
        t.setColor(Color::kRed);
        auto redRGBA = Color::kRed.toRGBA();
        if (t.runs().size() != 3
            || (!t.runs()[0].color.isSet || t.runs()[0].color.value.toRGBA() != redRGBA)
            || (!t.runs()[1].color.isSet || t.runs()[1].color.value.toRGBA() != redRGBA)
            || (!t.runs()[2].color.isSet || t.runs()[2].color.value.toRGBA() != redRGBA)) {
            return "setTextRun() failed setting run that covers more than two runs";
        }

        t = Text("tests", Font(), Color::kBlack);
        t.setColor(Color::kOrange, 2, 10);
        if (t.runs().back().startIndex + t.runs().back().length > int(t.text().size())) {
            return "setTextRun() should truncate length to text length";
        }

        return "";
    }
};

class RichTextTest : public BitmapTest
{
    static constexpr int kPointSize = 13;
public:
    RichTextTest() : BitmapTest("rich text (styling)", kPointSize + 3, 2.5f * kPointSize) {}

    std::string run() override
    {
        const auto dpi = mBitmap->dpi();
        const Color bg = Color::kRed;
        const Color fg = Color::kBlue;
        const Color strikeColor = Color::kGreen;
        // Use bold so that one of the pixels will be closer to 100% on a whole pixel
        const Font font("Arial", PicaPt::fromPixels(kPointSize, dpi), kStyleBold);
        const Point upperLeft = Point::kZero;

        // ----
        Text t("O", font, fg);
        t.setBackgroundColor(bg);
        t.setUnderlineStyle(kUnderlineSingle);
        t.setUnderlineColor(strikeColor);
        t.setStrikethrough();
        t.setStrikethroughColor(strikeColor);

        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();

        // Verify background color
        if (mBitmap->pixelAt(0, 0).toRGBA() != bg.toRGBA()) {
            return createColorError("incorrect background color", bg, mBitmap->pixelAt(0, 0));
        }
        if (mBitmap->pixelAt(1, 1).toRGBA() != bg.toRGBA()) {
            return createColorError("incorrect background color", bg, mBitmap->pixelAt(1, 1));
        }

        // Verify foreground color
        // We probably aren't going to get a 100% on pixel for the O, so there will be some
        // alpha blending from the red background.
        Color bluest;
        int y = int(0.4f * float(kPointSize));
        for (int x = 0;  x < mBitmap->width();  ++x) {
            auto c = mBitmap->pixelAt(x, y);
            if (c.blue() > bluest.blue()) {
                bluest = c;
            }
        }
        if (!(bluest.red() < 0.1f && bluest.green() == 0.0f && bluest.blue() > 0.9f)) {
            return createColorError("incorrect text color", fg, bluest);
        }

        // Verify underline drew
        auto metrics = mBitmap->fontMetrics(font);
        int x = mBitmap->width() / 2;
        int baseline = int((upperLeft.y + metrics.ascent).toPixels(dpi));
        int underlineY = int(std::round((upperLeft.y + metrics.ascent + metrics.underlineOffset).toPixels(dpi)));
        auto maybeErr = verifyLine(font.pointSize().toPixels(dpi) / 2, underlineY, strikeColor,
                                   "incorrect underline color at " + std::to_string(underlineY));
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // Verify strikethrough drew
        int midY = int((upperLeft.y + metrics.ascent - 0.5f * metrics.xHeight).toPixels(dpi));
        maybeErr = verifyLine(font.pointSize().toPixels(dpi) / 2, midY, strikeColor,
                              "incorrect strikethrough color at " + std::to_string(midY));
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // ----
        // Verify underline and strikethrough take foreground color if specific color not given
        // ("H" has empty space near underline and 1/2 x-height, important since the lines are
        // the same color as the glyph)
        t = Text("H", font, fg);
        t.setUnderlineStyle(kUnderlineSingle);
        t.setStrikethrough();
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        maybeErr = verifyLine(font.pointSize().toPixels(dpi) / 2, underlineY, fg,
                              "incorrect unset underline color at " + std::to_string(underlineY));
        if (!maybeErr.empty()) {
            return maybeErr;
        }
        maybeErr = verifyLine(font.pointSize().toPixels(dpi) / 2, midY, fg,
                              "incorrect unset strikethrough color at " + std::to_string(midY));
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // ----
        // Verify double underline
        t = Text("H", font, fg);
        t.setUnderlineStyle(kUnderlineDouble);
        t.setUnderlineColor(strikeColor);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        int n = 0;
        for (int y = baseline - 1;  y < mBitmap->height();  ++y) {
            auto c = mBitmap->pixelAt(x, y);
            if (c.red() == 0.0f && c.green() > 0.66f && c.blue() == 0.0f) {
                n += 1;
            }
        }
        if (n != 2) {
            return std::string("expected 2 lines, found ") + std::to_string(n);
        }

        // ----
        // Verify dotted underline
        t = Text("HH", font, fg);
        t.setUnderlineStyle(kUnderlineDotted);
        t.setUnderlineColor(strikeColor);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        n = 0;
        int nBlack = 0;
        for (int xx = x - mBitmap->width() / 4;  xx <= x + mBitmap->width() / 4;  ++xx) {
            if (verifyLine(xx, underlineY, strikeColor, "") == "") {
                n += 1;
            } else {
                nBlack += 1;
            }
        }
        if (!(n >= 2 && nBlack >= 2)) {
            return std::string("dotted underlines at " + std::to_string(underlineY) + " failed (") + std::to_string(n) + " green, " + std::to_string(nBlack) + " black)";
        }

        // ----
        // TODO: wavy underline

        // ----
        // Verify strikethrough works for one span and multiple rows
        // (macOS and Direct2D must implement strikethroughs manually)
        const char *twoLineText = "L\nL";
        Font smallFont("Arial", PicaPt::fromPixels(kPointSize / 2, dpi), kStyleBold);
        auto smallMetrics = mBitmap->fontMetrics(smallFont);
        t = Text(twoLineText, smallFont, fg);
        t.setStrikethrough();
        t.setStrikethroughColor(strikeColor);
        auto layout = mBitmap->createTextLayout(t);
        auto glyphs = layout->glyphs();
#ifdef __APPLE__
        // For small fonts Apple's rendering will place lines closer together than ascent + descent
        if (glyphs[2].line != glyphs[0].line + 1) {
#else
        if (glyphs[2].frame.y < glyphs[0].frame.maxY()) {
#endif // __APPLE__
            return std::string("text \"") + std::string(twoLineText) + "\" does not have two lines";
        }
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft);
        mBitmap->endDraw();
        midY = int((upperLeft.y + smallMetrics.ascent - 0.5f * smallMetrics.xHeight).toPixels(dpi));
        maybeErr = verifyLine(smallFont.pointSize().toPixels(dpi) / 2 - 1, midY, strikeColor,
                              "incorrect strikethrough color (two lines)");
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // ----
        // Verify strikethrough is offset correctly
        t = Text("Td", font, fg);
        t.setStrikethrough();
        t.setStrikethroughColor(strikeColor);
        layout = mBitmap->createTextLayout(t);
        Point strikeOffset(PicaPt::fromPixels(4, dpi), PicaPt::fromPixels(4, dpi));
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, strikeOffset);
        mBitmap->endDraw();
        midY = int((strikeOffset.y + metrics.ascent - 0.5f * metrics.xHeight).toPixels(dpi));
        int offsetXPx = int(strikeOffset.x.toPixels(dpi));
        for (int x = layout->glyphs()[0].frame.minX().toPixels(dpi) + offsetXPx;
             x <= layout->glyphs()[0].frame.maxX().toPixels(dpi) + offsetXPx;  ++x) {
            auto c1 = mBitmap->pixelAt(x, midY);
            auto c2 = mBitmap->pixelAt(x, midY + 1);
            if (c1.green() < 0.1f && c2.green() < 0.1f) {
                std::stringstream s;
                s << "strikethrough expected at (" << x << ", " << midY << ") or (" << x << ", "
                  << midY + 1 << ") but not found";
                return s.str();
            }
        }

        // ----
        // Verify bold override works.  (Note: 'font' is already bold!)
        Font arialNormal("Arial", PicaPt::fromPixels(kPointSize, dpi));
        t = Text("I", arialNormal, fg);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        y = (metrics.ascent / 2).toPixels(dpi);
        auto normalWidth = findLineWidth(y);
        t = Text("I", arialNormal, fg);
        t.setBold();
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        auto boldWidth = findLineWidth(y);
        if (boldWidth < normalWidth + 0.5f) {
            return "Text::setBold() does not work: expected width >= " + std::to_string(normalWidth) + " + 0.5, got " + std::to_string(boldWidth);
        }

        // ----
        // Verify italic override works.  (Note: 'font' is already bold!)
        t = Text("I", arialNormal, fg);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        int topY = int(std::ceil((metrics.ascent - metrics.capHeight).toPixels(dpi)));
        int bottomY = int(std::floor(metrics.ascent.toPixels(dpi)) - 1.0f);
        auto normalTopX = findLineStart(topY);
        auto normalBottomX = findLineStart(bottomY);
        assert(normalTopX >= 0.0f && normalBottomX >= 0.0f);
        auto xDelta = std::abs(normalTopX - normalBottomX);
        if (xDelta > 0.1f) {  // variance between system fonts, rendering, etc.
            return "Normal font seems to be italic (x coord delta: " + std::to_string(xDelta) + ")";
        }
        t = Text("I", arialNormal, fg);
        t.setItalic();
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        auto italicTopX = findLineStart(topY);
        auto italicBottomX = findLineStart(bottomY);
        assert(italicTopX >= 0.0f && italicBottomX >= 0.0f);
        if (italicTopX < italicBottomX + 1.0f) {
            return "Text::setItalic() does not work: expected top pixel to be >= 1.0f pixels farther than bottom, got " + std::to_string(italicTopX - italicBottomX);
        }

        // ----
        // Verify UTF-8 / UTF-16 issues work correctly
        t = Text("⾦βm", smallFont, fg);
        t.setColor(Color::kRed, 0, 3);    // first glyph (jin1) is 3 bytes in UTF-8
        t.setColor(Color::kGreen, 3, 2);  // second glyph (beta) is 2 bytes in UTF-8
        t.setColor(Color::kBlue, 5, 1);   // third glyph (ASCII m) is 1 byte in UTF-8
        layout = mBitmap->createTextLayout(t);
        glyphs = layout->glyphs();
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft);
        mBitmap->endDraw();
        if (!verifyGlyphColor(glyphs[0].frame, 1.0f, 0.0f, 0.0f)) {
            return "first glyph is wrong color (expected red)";
        }
        if (!verifyGlyphColor(glyphs[1].frame, 0.0f, 1.0f, 0.0f)) {
            return "second glyph is wrong color (expected green)";
        }
        if (!verifyGlyphColor(glyphs[2].frame, 0.0f, 0.0f, 1.0f)) {
            return "third glyph is wrong color (expected blue)";
        }

        // ----
        // Verify multiple font sizes works okay
        // ... test glyphs here rather than in previous layout text, so that glyph test
        //     earlier can pass even if different font sizes has not been implemented yet
        t = Text("TT", smallFont, fg);
        t.setFont(font, 1, 2);
        glyphs = mBitmap->createTextLayout(t)->glyphs();
        auto baselineYPt = metrics.ascent + glyphs[1].frame.y;
        auto expectedYPt = baselineYPt - smallMetrics.ascent;
        if (std::abs((glyphs[0].frame.y - expectedYPt).asFloat()) > 0.01f) {
            return createFloatError("'tT' small font glyph.frame.y is incorrect",
                                    expectedYPt.asFloat(), glyphs[0].frame.y.asFloat());
        }
        if (glyphs[1].frame.y.asFloat() <= -0.01f && glyphs[1].frame.y.toPixels(dpi) >= 1.0f) {
            return createFloatError("'tT' large font glyph.frame.y is incorrect",
                                    PicaPt::kZero.asFloat(), glyphs[1].frame.y.asFloat());
        }
        t = Text("TT", smallFont, fg);
        t.setFont(font, 0, 1);
        glyphs = mBitmap->createTextLayout(t)->glyphs();
        baselineYPt = metrics.ascent + glyphs[0].frame.y;
        expectedYPt = baselineYPt - smallMetrics.ascent;
        if (glyphs[0].frame.y.asFloat() <= -0.01f && glyphs[0].frame.y.toPixels(dpi) >= 1.0f) {
            return createFloatError("'Tt' large font glyph.frame.y is incorrect",
                                    PicaPt::kZero.asFloat(), glyphs[0].frame.y.asFloat());
        }
        if (std::abs((glyphs[1].frame.y - expectedYPt).asFloat()) > 0.01f) {
            return createFloatError("'Tt' small font glyph.frame.y is incorrect",
                                    expectedYPt.asFloat(), glyphs[1].frame.y.asFloat());
        }

        // ... test that drawing works
        t = Text("T T", Font(), fg);  // need space so kerning won't overlap glyphs
        t.setFont(smallFont, 0, 1);
        t.setFont(font, 1, 2);
        layout = mBitmap->createTextLayout(t);
        glyphs = layout->glyphs();
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft);
        mBitmap->endDraw();
        auto glyphRect = glyphs[0].frame + upperLeft;
        baselineYPt = upperLeft.y + metrics.ascent;
        maybeErr = verifyTextRect(int((upperLeft.x + glyphRect.x).toPixels(dpi)),
                                  int((upperLeft.x + glyphRect.maxX()).toPixels(dpi)),
                                  int(baselineYPt.toPixels(dpi)) + 1,
                                  (baselineYPt - smallMetrics.capHeight).toPixels(dpi),
                                  smallMetrics.capHeight.toPixels(dpi),
                                  "small-font T is wrong height");
        if (!maybeErr.empty()) {
            return maybeErr;
        }
        glyphRect = glyphs[2].frame + upperLeft;
        maybeErr = verifyTextRect(int((upperLeft.x + glyphRect.x).toPixels(dpi)),
                                  int((upperLeft.x + glyphRect.maxX()).toPixels(dpi)),
                                  int(baselineYPt.toPixels(dpi)) + 1,
                                  (baselineYPt - metrics.capHeight).toPixels(dpi),
                                  metrics.capHeight.toPixels(dpi),
                                  "big-font T is wrong height");
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // ----
        // Verify superscript
        Point upperLeft4y(PicaPt::fromPixels(0, dpi), PicaPt::fromPixels(4, dpi));
        t = Text("EE", font, fg);
        t.setSuperscript(1, 1);
        layout = mBitmap->createTextLayout(t);
        glyphs = layout->glyphs();
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft4y);
        mBitmap->endDraw();
        // ceil to next pixel on left to prevent previous (normal) glyph from bleeding over
        x = int(std::ceil(glyphs[1].frame.x.toPixels(dpi)));
        auto w = int(glyphs[1].frame.width.toPixels(dpi));
        auto expectedY = (upperLeft4y.y + metrics.ascent - metrics.capHeight).toPixels(dpi);
        auto expectedHeight = (0.666f * metrics.capHeight.toPixels(dpi));  // sub/super-script is 66% high
        maybeErr = verifyTextRect(x, x + w, mBitmap->height(), expectedY, expectedHeight,
                                  "superscript incorrectly located");
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // ----
        // Verify subscript
        t = Text("y y", font, fg);  // the space avoids the subscript y being kerned too close
        t.setSubscript(2, 1);
        layout = mBitmap->createTextLayout(t);
        glyphs = layout->glyphs();
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft4y);
        mBitmap->endDraw();
        x = int(glyphs[2].frame.x.toPixels(dpi));
        w = int(glyphs[2].frame.width.toPixels(dpi));
        expectedY = (upperLeft4y.y + metrics.ascent + metrics.descent).toPixels(dpi) - expectedHeight;
        maybeErr = verifyTextRect(x, x + w, mBitmap->height(), expectedY, expectedHeight,
                                  "subscript incorrectly located");
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // ----
        // Verify character spacing
        t = Text("HH", smallFont, Color::kRed);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft4y);
        mBitmap->endDraw();
        auto normalExtents = findRedExtents(0, 0, mBitmap->width(), mBitmap->height());
        float extraSpacingPx = 3.0f;
        t = Text("HH", smallFont, Color::kRed);
        t.setCharacterSpacing(PicaPt::fromPixels(extraSpacingPx, dpi));
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft4y);
        mBitmap->endDraw();
        auto extraExtents = findRedExtents(0, 0, mBitmap->width(), mBitmap->height());
        normalWidth = normalExtents.xMax - normalExtents.xMin;
        float extraWidth = extraExtents.xMax - extraExtents.xMin;
        if (std::abs((extraWidth - normalWidth) - extraSpacingPx) > 0.75f) {
            std::stringstream s;
            s << "expected " << extraSpacingPx << " px of character spacing" << ", got " << (extraWidth - normalWidth) << " px (normal width: " << normalWidth << ", width with spacing: " << extraWidth;
            return s.str();
        }

        // ----
        // Verify line height multiple
        // (Use regular size font because macOS uses lineHeight < 100% for small font,
        // leading to overlapping glyph frames)
        t = Text("E\nE", font, Color::kRed);
        t.setLineHeightMultiple(1.0f);
        layout = mBitmap->createTextLayout(t);
        auto normalGlyphs = layout->glyphs();  // local var is not reference: copies
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft);
        mBitmap->endDraw();
        normalExtents = findRedExtents(0, 0, mBitmap->width(), mBitmap->height());
        extraSpacingPx = 3.0f;
        t = Text("E\nE", font, Color::kRed);
        t.setLineHeightMultiple((metrics.lineHeight.toPixels(dpi) + extraSpacingPx) / metrics.lineHeight.toPixels(dpi));
        layout = mBitmap->createTextLayout(t);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        extraExtents = findRedExtents(0, 0, mBitmap->width(), mBitmap->height());
        auto normalHeight = normalExtents.yMax - normalExtents.yMin;
        auto extraHeight = extraExtents.yMax - extraExtents.yMin;
        if (std::abs((extraHeight - normalHeight) - extraSpacingPx) > 0.75f) {
            std::stringstream s;
            s << "expected " << extraSpacingPx << " px of line spacing" << ", got " << (extraHeight - normalHeight) << " px (normal height: " << normalHeight << ", height with spacing: " << extraHeight;
            return s.str();
        }
        auto normalGlyphLineHeight = normalGlyphs[2].frame.y - normalGlyphs[0].frame.y;
        auto extraGlyphLineHeight = layout->glyphs()[2].frame.y - layout->glyphs()[0].frame.y;
        if (std::abs((extraGlyphLineHeight - normalGlyphLineHeight).toPixels(dpi) - extraSpacingPx) > 0.1f) {
            std::stringstream s;
            s << "expected glyph line height of "
              << (normalGlyphLineHeight + PicaPt::fromPixels(extraSpacingPx, dpi)).toPixels(72.0f)
              << " pt, got " << extraGlyphLineHeight.toPixels(72.0f) << " pt";
            return s.str();
        }
        // Check that the line height doesn't cause the glyph tops to move
        t = Text("E\nE", font, Color::kRed);
        t.setLineHeightMultiple(2.0f);
        glyphs = mBitmap->createTextLayout(t)->glyphs();
        if (std::abs((glyphs[0].frame.y - normalGlyphs[0].frame.y).toPixels(dpi)) > 0.5f) {
            return "first line frame.y is not correct with lineHeight = 200%: expected " +
                    std::to_string(normalGlyphs[0].frame.y.asFloat()) + " pt, got " +
                    std::to_string(glyphs[0].frame.y.asFloat()) + " pt";
        }
        // Check that glyphs with line height and bottom alignment works
#ifdef __APPLE__
        // The fact that this must be > 0.001 pixel probably indicates a problem somewhere.
        // The nearest I can find is that NSAttributedString -size height is not the same as
        // TextObj::metrics().height, but I do not know why, or if it is even important.
        float glyphErrPx = 0.5f;
#else
        float glyphErrPx = 0.001f;
#endif
        auto glyphsHeight = glyphs.back().frame.maxY() - glyphs.front().frame.y;
        Size size(PicaPt(1000), PicaPt(1000));
        glyphs = mBitmap->createTextLayout(t, size, Alignment::kLeft | Alignment::kBottom)->glyphs();
        if (std::abs(size.height.toPixels(dpi) - glyphs.back().frame.maxY().toPixels(dpi)) > glyphErrPx) {
            return "expected bottom of last glyph with lineHeight = 200%, alignment = kLeft | kBottom to be " + std::to_string(size.height.toPixels(dpi)) + ", got " + std::to_string(glyphs.back().frame.maxY().toPixels(dpi));
        }
        if (std::abs((size.height - glyphsHeight - glyphs.front().frame.y).toPixels(dpi)) > glyphErrPx) {
            return "expected top of first glyph with lineHeight = 200%, alignment = kLeft | kBottom to be " + std::to_string((size.height - glyphsHeight).toPixels(dpi)) + ", got " + std::to_string(glyphs.front().frame.y.toPixels(dpi));
        }
        // Check that glyphs with line height and center alignment works
        glyphs = mBitmap->createTextLayout(t, size, Alignment::kLeft | Alignment::kVCenter)->glyphs();
        expectedY = 0.5f * size.height.toPixels(dpi) + 0.5f * glyphsHeight.toPixels(dpi);
        if (std::abs(expectedY - glyphs.back().frame.maxY().toPixels(dpi)) > glyphErrPx) {
            return "expected bottom of last glyph with lineHeight = 200%, alignment = kLeft | kVCenter to be " + std::to_string(expectedY) + ", got " + std::to_string(glyphs.back().frame.maxY().toPixels(dpi));
        }
        expectedY = 0.5f * size.height.toPixels(dpi) - 0.5f * glyphsHeight.toPixels(dpi);
        if (std::abs(expectedY - glyphs.front().frame.y.toPixels(dpi)) > glyphErrPx) {
            return "expected top of first glyph with lineHeight = 200%, alignment = kLeft | kVCenter to be " + std::to_string(expectedY) + ", got " + std::to_string(glyphs.front().frame.y.toPixels(dpi));
        }

        // ----
        // Verify default colors
        t = Text("L", font, Color::kTextDefault);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kRed);
        mBitmap->drawText(*mBitmap->createTextLayout(t), upperLeft);
        mBitmap->endDraw();
        y = int((upperLeft.y + metrics.ascent).toPixels(dpi));
        maybeErr = "default text color should be black";
        for (auto yy = y - 1;  yy <= y + 1;  ++yy) {
            auto c = mBitmap->pixelAt(4, yy);
            if (c.red() == 0.0f && c.green() == 0.0f && c.blue() == 0.0f) {
                maybeErr = "";
            }
        }
        if (!maybeErr.empty()) {
            return maybeErr;
        }
        auto notOverriddenColor = Color::kGreen;
        t = Text("LL", font, Color::kTextDefault);
        t.setColor(notOverriddenColor, 1, 1);
        layout = mBitmap->createTextLayout(t, Font(), Color::kRed);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft);
        mBitmap->endDraw();
        y = int((upperLeft.y + metrics.ascent).toPixels(dpi)) - 1;
        maybeErr = verifyLine(4, y, Color::kRed, "default text color should be override to red");
        if (!maybeErr.empty()) {
            return maybeErr;
        }
        maybeErr = verifyLine(4 + layout->glyphs()[1].frame.x.toPixels(dpi), y, notOverriddenColor,
                              "default text color should not override a color that is set");
        if (!maybeErr.empty()) {
            return maybeErr;
        }

        // ----
        // Verify font override. This is a little hard, so will just check the point size
        t = Text("E", Font(), Color::kTextDefault);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*mBitmap->createTextLayout(t, smallFont, Color::kRed), upperLeft);
        mBitmap->endDraw();
        auto ext = findRedExtents(0, 0, mBitmap->width(), mBitmap->height());
        float h = ext.yMax - ext.yMin;
        auto expectedCap = smallMetrics.capHeight.toPixels(dpi);
        if (std::abs(h - expectedCap) > 0.666f) {
            return "Expected default font overridden with cap-height " + std::to_string(expectedCap) + ", got " + std::to_string(h);
        }

        // ----
        // Verify that empty text doesn't crash,
        t = Text("", smallFont, fg);
        layout = mBitmap->createTextLayout(t);  // if this fails, it will probably crash here
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft);
        mBitmap->endDraw();

        // ----
        // It seems macOS 10.14 (and later?) has a bug where an NSAttributedString will
        // not reset its background color in the next run if the background color is simply
        // left out of the attributes, it must specifically be set to NSColor.clearColor.
        // Or the line can end, that works too...
        t = Text("cc", font, fg);
        t.setBackgroundColor(bg, 0, 1);
        layout = mBitmap->createTextLayout(t);
        mBitmap->beginDraw();
        mBitmap->fill(Color::kBlack);
        mBitmap->drawText(*layout, upperLeft);
        mBitmap->endDraw();
        auto c = mBitmap->pixelAt(4, 1);
        if (c.toRGBA() != bg.toRGBA()) {
            return createColorError("background color is incorrect", bg, c);
        }
        c = mBitmap->pixelAt(4 + int(layout->glyphs()[1].frame.x.toPixels(dpi)), 1);
        if (c.toRGBA() != Color::kBlack.toRGBA()) {
            return createColorError("background color did not get reset properly", bg, Color::kBlack);
        }

        return "";
    }

    std::string verifyLine(int x, int y, const Color& expected, const std::string msg)
    {
        Color brightest;
        for (int yy = y - 1;  yy <= y + 1;  ++yy) {
            auto c = mBitmap->pixelAt(x, yy);
            if (c.toGrey().red() > brightest.toGrey().red()) {
                brightest = c;
            }
        }
        const float kErr = 0.025f;
        if (!(brightest.red() >= expected.red() - kErr && brightest.red() <= expected.red() + kErr &&
              brightest.green() >= expected.green() - kErr && brightest.green() <= expected.green() + kErr &&
              brightest.blue() >= expected.blue() - kErr && brightest.blue() <= expected.blue() + kErr)) {
            return createColorError(msg + " at (" + std::to_string(x) + ", " + std::to_string(y) + ")",
                                    expected, brightest);
        }
        return "";
    }

    bool verifyGlyphColor(const Rect& r, float red, float green, float blue)
    {
        assert(red == 0.0 || red == 1.0f);
        assert(green == 0.0 || green == 1.0f);
        assert(blue == 0.0 || blue == 1.0f);
        const auto dpi = mBitmap->dpi();
        int y = (r.y + 0.75f * r.height).toPixels(dpi);
        assert(y < mBitmap->height());  // glyphs() might have failed in parent
        int xStart = int(r.x.toPixels(dpi)) + 1;  // add one to avoid possibly getting prev glyph
        int xEnd = int(r.maxX().toPixels(dpi));
        int n = 0;
        for (int yy = y - 1;  yy < y + 1;  ++yy) {
            for (int xx = xStart;  xx <= xEnd;  ++xx) {
                auto c = mBitmap->pixelAt(xx, yy);
                if ((c.red() == 0.0f || (red == 1.0f && c.red() > 0.333f)) &&
                    (c.green() == 0.0f || (green == 1.0f && c.green() > 0.333f)) &&
                    (c.blue() == 0.0f || (blue == 1.0f && c.blue() > 0.333f)))
                {
                    n += 1;
                }
            }
        }
        return (n >= 3);
    }

    std::string verifyTextRect(int startX, int endX, int endY,
                               float expectedY,
                               float expectedHeight, const std::string& msg)
    {
        int startY = 0;
        float minX = 9999.0f, minY = 9999.0f, maxX = 0.0f, maxY = 0.0f;
        for (int yy = startY;  yy < endY;  ++yy) {
            for (int xx = startX;  xx < endX;  ++xx) {
                auto c = mBitmap->pixelAt(xx, yy);
                if (c.blue() > 0.0f) {
                    minX = std::min(minX, float(xx + 1) - c.blue());
                    minY = std::min(minY, float(yy + 1) - c.blue());
                    maxX = std::max(maxX, float(xx) + c.blue());
                    // The smaller text may not have a full pixel on, which will artifically
                    // shorten the bottom, so pretend the pixel was fully on
                    maxY = std::max(maxY, float(yy + 1));
                }
            }
        }
        const float maxErr = 1.0f;  // e.g. "M" has some space on left and right
        if (std::abs(int(minX) - startX) <= maxErr &&
            std::abs(maxX - float(endX)) <= maxErr &&
            std::abs(minY - float(expectedY)) <= maxErr &&
            std::abs((maxY - minY) - expectedHeight) <= maxErr) {
            return "";
        } else {
            std::stringstream s;
            s << msg << ": expected rect(" << startX << ", " << expectedY << ", " << endX - startX << ", " << expectedHeight
              << "), got (" << minX << ", " << minY << ", " << maxX - minX << ", " << maxY - minY << ")";
            return s.str();
        }
    }

    struct Extents { float xMin; float xMax; float yMin; float yMax; };
    Extents findRedExtents(int x0, int y0, int width, int height)
    {
        float xMin = 10000.0f;
        float xMax = 0.0f;
        float yMin = 10000.0f;
        float yMax = 0.0f;
        for (int y = y0;  y < y0 + height;  ++y) {
            for (int x = x0;  x < x0 + width;  ++x) {
                auto c = mBitmap->pixelAt(x, y);
                if (c.red() > 0.0f) {
                    xMin = std::min(xMin, float(x) + 1.0f - c.red());
                    xMax = std::max(xMax, float(x) + c.red());
                    yMin = std::min(yMin, float(y) + 1.0f - c.red());
                    yMax = std::max(yMax, float(y) + c.red());
                }
            }
        }
        return { xMin, xMax, yMin, yMax };
    }

    float findLineStart(int y)
    {
        for (int x = 0;  x < mBitmap->width();  ++x) {
            auto c = mBitmap->pixelAt(x, y);
            if (c.blue() > 0.0) {
                // For the first pixel, the line will be to the right, so advance one
                // and subtract the percent the pixel is full.
                return float(x + 1) - c.blue();
            }
        }
        return -1.0f;
    }

    float findLineWidth(int y)
    {
        float start = -1.0f;
        float end = -1.0f;
        for (int x = 0;  x < mBitmap->width();  ++x) {
            auto c = mBitmap->pixelAt(x, y);
            if (c.blue() > 0.0) {
                if (start < 0.0f) {
                    // For the first pixel, the line will be to the right, so advance one
                    // and subtract the percent the pixel is full.
                    start = float(x + 1) - c.blue();
                } else {
                    end = float(x) + c.blue();
                }
            }
        }
        return std::max(0.0f, end - start);
    }
};

class ImageTest : public BitmapTest
{
public:
    ImageTest() : BitmapTest("image", 13, 15) {}

    std::string run() override
    {
        Color bgColor(51, 255, 68, 255);      // 0x33ff44ff
        Color rect1Color(255, 187, 34, 255);  // 0xffbb22ff
        Color rect2Color(17, 255, 204, 255);  // 0x11ffccff
        auto src = createBitmap(kBitmapRGB, 9, 11, 72 /*dpi*/);
        auto destDPI = mBitmap->dpi();
        auto srcDPI = src->dpi();
        auto rect1 = Rect::fromPixels(0, 0, 4, 2, srcDPI);
        auto rect2 = Rect::fromPixels(5, 3, 3, 5, srcDPI);
        auto destRect = Rect::fromPixels(2, 2, src->width(), src->height(), destDPI);

        src->beginDraw();
        src->fill(bgColor);
        src->setFillColor(rect1Color);
        src->drawRect(rect1, kPaintFill);
        src->setFillColor(rect2Color);
        src->drawRect(rect2, kPaintFill);
        src->endDraw();
        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);
        mBitmap->drawImage(src->copyToImage(), destRect);
        mBitmap->endDraw();

        // writeTIFF("/tmp/debug-blit.tiff", src); // debugging

        auto onePx = PicaPt::fromPixels(1.0f, destDPI);
        auto destRect1 = Rect(PicaPt::fromPixels(rect1.x.toPixels(srcDPI), destDPI) + destRect.x,
                              PicaPt::fromPixels(rect1.y.toPixels(srcDPI), destDPI) + destRect.y,
                              PicaPt::fromPixels(rect1.width.toPixels(srcDPI), destDPI) - onePx,
                              PicaPt::fromPixels(rect1.height.toPixels(srcDPI), destDPI) - onePx);
        auto destRect2 = Rect(PicaPt::fromPixels(rect2.x.toPixels(srcDPI), destDPI) + destRect.x,
                              PicaPt::fromPixels(rect2.y.toPixels(srcDPI), destDPI) + destRect.y,
                              PicaPt::fromPixels(rect2.width.toPixels(srcDPI), destDPI) - onePx,
                              PicaPt::fromPixels(rect2.height.toPixels(srcDPI), destDPI) - onePx);

        destRect.width -= onePx;
        destRect.height -= onePx;

        for (int y = 0;  y < mHeight;  ++y) {
            for (int x = 0;  x < mWidth;  ++x) {
                auto pixel = mBitmap->pixelAt(x, y);
                auto p = Point::fromPixels(x, y, destDPI);
                if (destRect1.contains(p)) {
                    if (pixel.toRGBA() != rect1Color.toRGBA()) {
                        return createPixelError("bad color for rect1", x, y, rect1Color, pixel);
                    }
                } else if (destRect2.contains(p)) {
                    if (pixel.toRGBA() != rect2Color.toRGBA()) {
                        return createPixelError("bad color for rect2", x, y, rect2Color, pixel);
                    }
                } else if (destRect.contains(p)) {
                    if (pixel.toRGBA() != bgColor.toRGBA()) {
                        return createPixelError("bad color for bitmap background", x, y, bgColor, pixel);
                    }
                } else if (pixel.toRGBA() != mBGColor.toRGBA()) {
                    return createPixelError("bad background color", x, y, mBGColor, pixel);
                }
            }
        }
        return "";
    }
};

class ImageCopyTest : public BitmapTest
{
public:
    ImageCopyTest() : BitmapTest("copyToImage lifetime", 13, 15) {}

    std::string run() override
    {
        // Tests to make sure that the result of copyToImage() is still usable after
        // the bitmap context that originally created it is gone. (Otherwise it is
        // not copyToImage, it is asImageRef).
        Color bgColor(255, 255, 255, 255);
        Color rectColor(0, 255, 0, 255);
        auto src = makeImage(rectColor);  // returns copy of the temp context

        mBitmap->beginDraw();
        mBitmap->drawImage(src, Rect::fromPixels(0, 0, mBitmap->width(), mBitmap->height(), mBitmap->dpi()));
        mBitmap->endDraw();

        return verifyFillRect(0, 0, mBitmap->width(), mBitmap->height(), bgColor, rectColor);
    }

protected:
    std::shared_ptr<Image> makeImage(const Color& fill)
    {
        auto src = createBitmap(kBitmapRGB, mBitmap->width(), mBitmap->height(),
                                mBitmap->dpi());
        src->beginDraw();
        src->fill(fill);
        src->endDraw();
        // 'src' will be destroyed, so if internal resources are destroyed,
        // the result of copyToImage() won't work.
        return src->copyToImage();
    }
};

class ColorFuncTest : public Test
{
public:
    ColorFuncTest() : Test("Color lighter() and darker()") {}

    std::string run() override
    {
        auto fuzzyFloatEqual = [](float x, float y) { return (std::abs(x - y) < 0.005); };
        auto fuzzyEqual = [fuzzyFloatEqual](const Color& c1, const Color& c2) {
            return (fuzzyFloatEqual(c1.red(), c2.red())
                    && fuzzyFloatEqual(c1.green(), c2.green())
                    && fuzzyFloatEqual(c1.blue(), c2.blue())
                    && fuzzyFloatEqual(c1.alpha(), c2.alpha()));
        };

        Color c(0.4f, 0.6f, 0.8f);
        Color got = c.lighter();
        Color expected = Color(0.5f, 0.7f, 0.9f);
        if (!fuzzyEqual(got, expected)) {
            return createColorError("Color(0.4, 0.5, 0.8).lighter()", expected,
                                    got);
        }

        got = c.darker();
        expected = Color(0.3f, 0.5f, 0.7f);
        if (!fuzzyEqual(got, expected)) {
            return createColorError("Color(0.4, 0.5, 0.8).darker()", expected,
                                    got);
        }

        got = Color(1.0f, 1.0f, 1.0f, 0.25f).lighter();
        expected = Color(1.0f, 1.0f, 1.0f, 0.35f);
        if (!fuzzyEqual(got, expected)) {
            return createColorError("Color(1, 1, 1, 0.25).lighter()", expected, got);
        }

        got = Color(1.0f, 1.0f, 1.0f, 0.25f).darker();
        expected = Color(0.9f, 0.9f, 0.9f, 0.15f);
        if (!fuzzyEqual(got, expected)) {
            return createColorError("Color(1, 1, 1, 0.25).darker()", expected, got);
        }

        got = Color(0.0f, 0.0f, 0.0f, 0.25f).lighter();
        expected = Color(0.1f, 0.1f, 0.1f, 0.15f);
        if (!fuzzyEqual(got, expected)) {
            return createColorError("Color(0, 0, 0, 0.25).lighter()", expected, got);
        }

        got = Color(0.0f, 0.0f, 0.0f, 0.25f).darker();
        expected = Color(0.0f, 0.0f, 0.0f, 0.35f);
        if (!fuzzyEqual(got, expected)) {
            return createColorError("Color(0, 0, 0, 0.25).darker()", expected, got);
        }

        return "";
    }

    std::string debugImage() const override { return ""; }
    void writeBitmapToFile(const std::string& path) const override {}
};

class TransformTest : public BitmapTest
{
public:
    TransformTest() : BitmapTest("translate/scale/rotate", 1, 1) {}

    std::string run() override
    {
        auto dpi = mBitmap->dpi();

        mBitmap->beginDraw();
        mBitmap->fill(mBGColor);  // so failure doesn't print unitialized colors
        mBitmap->endDraw();

        mBitmap->scale(2, 3);
        mBitmap->save();
        mBitmap->translate(PicaPt::fromPixels(1, dpi),
                           PicaPt::fromPixels(2, dpi));
        mBitmap->save();
        mBitmap->scale(0.1, 0.1);
        mBitmap->restore();

        float x, y;
        calcNativePixel(Point(PicaPt::kZero, PicaPt::kZero), &x, &y);
        // Easy to debug: should be (x:1, y:2) scaled by (sx:2, sy:3)
        if (x != 4 && y != 6) {
            std::stringstream s;
            s << "calcContextPixel((0, 0)): expected (4, 6), got ("
              << x << ", " << y << ")";
            return s.str();
        }
        calcNativePixel(Point(PicaPt::fromPixels(1.0f, dpi),
                              PicaPt::fromPixels(-1.0f, dpi)), &x, &y);
        if (x != 5 && y != 3) {
            std::stringstream s;
            s << "calcContextPixel((1, -1)): expected (5, 3), got ("
              << x << ", " << y << ")";
            return s.str();
        }

        mBitmap->rotate(30.0);
        calcNativePixel(Point(PicaPt::fromPixels(1.0f, dpi),
                              PicaPt::fromPixels(-1.0f, dpi)), &x, &y);
        if (std::abs(x - 4.540f) < 0.001f && std::abs(y - 5.159f) < 0.001f) {
            std::stringstream s;
            s << "calcContextPixel((1, -1)): expected (4.540, 5.159), got ("
              << x << ", " << y << ")";
            return s.str();
        }

        mBitmap->restore();

        return "";
    }

    void calcNativePixel(const Point& p, float *x, float *y)
    {
#ifdef __APPLE__
        mBitmap->calcContextPixel(p, x, y);
        if (y) {
            *y = float(mBitmap->height()) - *y;
        }
#else
        mBitmap->calcContextPixel(p, x, y);
#endif
    }
};

/*void TextDebug()
{
    Font font("Arial", PicaPt(20));
    Bitmap bitmap(100, 50, kBitmapRGB);
    auto metrics = font.metrics(bitmap);
    int baselineY = int(metrics.ascent.toPixels(bitmap.dpi()));
    bitmap.beginDraw();
    bitmap.fill(Color::kWhite);
    bitmap.setStrokeColor(Color::kBlue);
    bitmap.drawLines({ Point(PicaPt(0.5), PicaPt(baselineY + 0.5)),
                       Point(PicaPt(bitmap.width() + 0.5), PicaPt(baselineY + 0.5)) });
//    bitmap.drawText("TyAgAVA", Point(PicaPt(0), PicaPt(0)), font, kPaintFill);
    bitmap.drawText("T", Point(PicaPt(0), PicaPt(0)), font, kPaintFill);
    bitmap.drawText("g", Point(PicaPt(15), PicaPt(0)), font, kPaintFill);
    bitmap.endDraw();

    writeTIFF("/tmp/out.tiff", bitmap);
} */

static std::string kNormal = "\033[0m";
static std::string kRed = "\033[31m";
static std::string kGreen = "\033[32m";

int main(int argc, char *argv[])
{
#if USING_X11
    X11Open();
#endif

    std::vector<std::shared_ptr<Test>> test = {
        std::make_shared<HSVTest>(),
        std::make_shared<CoordinateTest>(),
        std::make_shared<ColorTest>("color readback (RGBA)", kBitmapRGBA),
        std::make_shared<ColorTest>("color readback (RGB)", kBitmapRGB),
        // TODO: macOS: unclear what writing a color to greyscale bitmap should do
        // std::make_shared<ColorTest>("Color readback (grey)", kBitmapGreyscale),
        // TODO: macOS: unclear what writing a color to an alpha bitmap should do
        // std::make_shared<ColorTest>("Color readback (alpha)", kBitmapAlpha),
        std::make_shared<FillFuncTest>(),
        std::make_shared<FillTest>(),
        std::make_shared<HairlineStrokeTest>(),
        std::make_shared<StrokeTest>(2),
        std::make_shared<StrokeTest>(3),
        std::make_shared<EndCapTest>(kEndCapButt),
        std::make_shared<EndCapTest>(kEndCapRound),
        std::make_shared<EndCapTest>(kEndCapSquare),
        std::make_shared<JoinTest>(kJoinMiter),
        std::make_shared<JoinTest>(kJoinRound),
        std::make_shared<JoinTest>(kJoinBevel),
        std::make_shared<DashTest>(),
        std::make_shared<RectStrokeAndFillTest>(1),
        std::make_shared<RectStrokeAndFillTest>(2),
        std::make_shared<AlphaBlendTest>(),
        std::make_shared<TestTransform>(),
        std::make_shared<EllipseTest>(),
        std::make_shared<RoundedRectTest>(),
        // Don't need to test drawing a BezierPath, since rounded rects use that internally
        std::make_shared<ClipRectTest>(),
        std::make_shared<ClipPathTest>(),
        std::make_shared<SaveRestoreTest>(),
        std::make_shared<GettersTest>(),
        std::make_shared<FontTest>("Arial", 20),
        std::make_shared<FontTest>("Georgia", 20),
        // std::make_shared<FontTest>("Courier New", 20),
#if !defined(_WIN32) && !defined(_WIN64)
        std::make_shared<FontTest>("Helvetica", 20),
        std::make_shared<FontTest>("Helvetica", 21),
        std::make_shared<FontTest>("Helvetica", 22),
#endif // !_WIN32 && !_WIN64
        std::make_shared<BadFontTest>(),
        std::make_shared<FontStyleTest>(),
        std::make_shared<StrokedTextTest>(),
        std::make_shared<TextMetricsTest>(),
        std::make_shared<TextAlignmentTest>(),
        std::make_shared<BasicTextLayoutTest>(),
        std::make_shared<RichTextRunsTest>(),
        std::make_shared<RichTextTest>(),
        std::make_shared<ImageTest>(),
        std::make_shared<ImageCopyTest>(),
        std::make_shared<ColorFuncTest>(),
        std::make_shared<TransformTest>()
    };

    const char *TERM = std::getenv("TERM");
    if (!isatty(STDOUT_FILENO) || !TERM || TERM[0] == '\0') {
        kNormal = kRed = kGreen = "";
    }

    auto runTest = [](Test& t, float dpi) -> int {
        int failed = 0;
        t.setup(dpi);
        auto err = t.run();

        std::cout << "[";
        if (!err.empty()) {
            failed = 1;
            std::cout << kRed << "FAIL" << kNormal;
        } else {
            std::cout << kGreen << "pass" << kNormal;
        }
        std::cout << "] " << t.name() << std::endl;

        if (failed) {
            std::cout << "       " << err << std::endl;
            std::cout << t.debugImage();  // has an ending \n
            t.writeBitmapToFile("/tmp/error.tiff");
        }

        t.teardown();
        return failed;
    };

    int nFailed = 0;
    for (auto t : test) {
        nFailed += runTest(*t, 72);
        nFailed += runTest(*t, 144);
    }

    if (nFailed == 0) {
        std::cout << kGreen << "All tests passed!" << kNormal << std::endl;
    } else {
        std::cout << kRed << nFailed << " test" << (nFailed == 1 ? "" : "s")
                  << " FAILED" << kNormal << std::endl;
    }

#if USING_X11
    X11Close();
#endif

    return nFailed;  // 0 = success
}
