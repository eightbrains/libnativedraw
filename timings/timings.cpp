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
#include "timings.h"

#include <chrono>
#include <iostream>

using namespace ND_NAMESPACE;

namespace {
static const int kNTimesPerRun = 50;
// kNObjs should ideally be set to a large number, since platforms may sync
// drawing to VBlank, which means that the minimum unit is 1/60 sec, which
// does offer much precision (especially when we subtract off drawing nothing,
// which also takes 1/60 sec).
#ifdef __APPLE__
// macOS' image blitting is so slow that 50000 would take a long time
static const int kNObjs = 1000;
#else
static const int kNObjs = 10000;
#endif
static const Color kBGColor(1.0f, 1.0f, 1.0f);

static const std::string kBaseRunName = "nothing";

struct LayoutInfo
{
    int nCols;
    PicaPt dx;
    PicaPt dy;

    LayoutInfo(DrawContext& dc, int n, int objWidthPx, int objHeightPx)
    {
        this->nCols = std::ceil(std::sqrt(n));
        this->dx = PicaPt::fromPixels((dc.width() - objWidthPx) / float(nCols), dc.dpi());
        this->dy = PicaPt::fromPixels((dc.height() - objHeightPx) / float(nCols), dc.dpi());
    }
};

void drawNothing(DrawContext& dc)
{
    dc.beginDraw();
    dc.fill(kBGColor);
    dc.endDraw();
}

void drawRects(DrawContext& dc, int n, int objWidthPx, int objHeightPx, PaintMode mode)
{
    auto w = PicaPt::fromPixels(objWidthPx, dc.dpi());
    auto h = PicaPt::fromPixels(objHeightPx, dc.dpi());
    LayoutInfo layout(dc, n, objWidthPx, objHeightPx);

    auto x0 = PicaPt::fromPixels(1, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(1, dc.dpi());
    int col = 0;

    dc.beginDraw();
    dc.fill(kBGColor);
    dc.setStrokeColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
    dc.setStrokeWidth(PicaPt::fromPixels(1, dc.dpi()));
    dc.setFillColor(Color(0.5f, 0.5f, 0.5f, 1.0f));
    for (int i = 0;  i < n;  ++i) {
        dc.drawRect(Rect(x, y, w, h), mode);
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawColoredRects(DrawContext& dc, int n, int objWidthPx, int objHeightPx)
{
    auto w = PicaPt::fromPixels(objWidthPx, dc.dpi());
    auto h = PicaPt::fromPixels(objHeightPx, dc.dpi());
    LayoutInfo layout(dc, n, objWidthPx, objHeightPx);

    auto x0 = PicaPt::fromPixels(1, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(1, dc.dpi());
    int col = 0;

    std::vector<Color> colors = { Color(1.0f, 0.0f, 0.0f), Color(1.0f, 0.5f, 0.0f),
                                  Color(1.0f, 1.0f, 0.0f), Color(0.5f, 1.0f, 0.0f),
                                  Color(0.0f, 1.0f, 0.0f), Color(0.0f, 1.0f, 0.5f),
                                  Color(0.0f, 1.0f, 1.0f), Color(0.0f, 0.5f, 1.0f),
                                  Color(0.0f, 0.0f, 1.0f), Color(0.5f, 0.0f, 1.0f),
                                  Color(1.0f, 0.0f, 1.0f), Color(1.0f, 0.0f, 0.5f) };
    size_t colorIdx = 0;
    dc.beginDraw();
    dc.fill(kBGColor);
    for (int i = 0;  i < n;  ++i) {
        dc.setFillColor(colors[colorIdx]);
        dc.drawRect(Rect(x, y, w, h), PaintMode::kPaintFill);
        colorIdx++;
        if (colorIdx >= colors.size()) {
            colorIdx = 0;
        }
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawRoundedRects(DrawContext& dc, int n, int objWidthPx, int objHeightPx, int radiusPx, PaintMode mode)
{
    auto w = PicaPt::fromPixels(objWidthPx, dc.dpi());
    auto h = PicaPt::fromPixels(objHeightPx, dc.dpi());
    auto r = PicaPt::fromPixels(radiusPx, dc.dpi());
    LayoutInfo layout(dc, n, objWidthPx, objHeightPx);

    auto x0 = PicaPt::fromPixels(1, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(1, dc.dpi());
    int col = 0;

    dc.beginDraw();
    dc.fill(kBGColor);
    dc.setStrokeColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
    dc.setStrokeWidth(PicaPt::fromPixels(1, dc.dpi()));
    dc.setFillColor(Color(0.5f, 0.5f, 0.5f, 1.0f));
    for (int i = 0;  i < n;  ++i) {
        dc.drawRoundedRect(Rect(x, y, w, h), r, mode);
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawBezier(DrawContext& dc, int n,
                std::function<std::shared_ptr<BezierPath>(DrawContext&, int, const Point&)> createPath,
                int radiusPx, PaintMode mode)
{
    auto r = PicaPt::fromPixels(radiusPx, dc.dpi());
    LayoutInfo layout(dc, n, radiusPx, radiusPx);

    auto x0 = r;
    auto x = x0;
    auto y = r;
    int col = 0;

    dc.beginDraw();
    dc.fill(kBGColor);
    dc.setStrokeColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
    dc.setStrokeWidth(PicaPt::fromPixels(1, dc.dpi()));
    dc.setFillColor(Color(0.5f, 0.5f, 0.5f, 1.0f));
    for (int i = 0;  i < n;  ++i) {
        dc.drawPath(createPath(dc, radiusPx, Point(x, y)), mode);
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawBezierTransformed(DrawContext& dc, int n,
                std::function<std::shared_ptr<BezierPath>(DrawContext&, int, const Point&)> createPath,
                           int radiusPx, PaintMode mode)
{
    auto r = PicaPt::fromPixels(radiusPx, dc.dpi());
    LayoutInfo layout(dc, n, radiusPx, radiusPx);

    auto x0 = r;
    auto x = x0;
    auto y = r;
    int col = 0;

    auto bezier = createPath(dc, radiusPx, Point(PicaPt(0.0f), PicaPt(0.0f)));

    dc.beginDraw();
    dc.fill(kBGColor);
    dc.setStrokeColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
    dc.setStrokeWidth(PicaPt::fromPixels(1, dc.dpi()));
    dc.setFillColor(Color(0.5f, 0.5f, 0.5f, 1.0f));
    for (int i = 0;  i < n;  ++i) {
        dc.translate(x, y);
        dc.drawPath(bezier, mode);
        dc.translate(-x, -y);
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawImages(DrawContext& dc, int n, std::shared_ptr<DrawableImage> img)
{
    auto w = img->width();
    auto h = img->height();
    LayoutInfo layout(dc, n, w.toPixels(dc.dpi()), w.toPixels(dc.dpi()));

    auto x0 = PicaPt::fromPixels(1, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(1, dc.dpi());
    int col = 0;

    dc.beginDraw();
    dc.fill(kBGColor);
    for (int i = 0;  i < n;  ++i) {
        dc.drawImage(img, Rect(x, y, w, h));
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void clipRects(DrawContext& dc, int n, int objWidthPx, int objHeightPx)
{
    auto w = PicaPt::fromPixels(objWidthPx, dc.dpi());
    auto h = PicaPt::fromPixels(objHeightPx, dc.dpi());
    LayoutInfo layout(dc, n, objWidthPx, objHeightPx);

    auto x0 = PicaPt::fromPixels(1, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(1, dc.dpi());
    int col = 0;

    dc.beginDraw();
    dc.fill(kBGColor);
    dc.setFillColor(Color(0.5f, 0.5f, 0.5f, 1.0f));
    for (int i = 0;  i < n;  ++i) {
        dc.save();
        dc.clipToRect(Rect(x, y, w, h));
        dc.fill(Color(0.5f, 0.5f, 0.5f, 1.0f));
        dc.restore();
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void clipBezier(DrawContext& dc, int n,
                std::function<std::shared_ptr<BezierPath>(DrawContext&, int, const Point&)> createPath,
                int radiusPx)
{
    auto r = PicaPt::fromPixels(radiusPx, dc.dpi());
    LayoutInfo layout(dc, n, radiusPx, radiusPx);

    auto x0 = r;
    auto x = x0;
    auto y = r;
    int col = 0;

    dc.beginDraw();
    dc.fill(kBGColor);
    for (int i = 0;  i < n;  ++i) {
        dc.save();
        dc.clipToPath(createPath(dc, radiusPx, Point(x, y)));
        dc.fill(Color(0.5f, 0.5f, 0.5f, 1.0f));
        dc.restore();
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawText(DrawContext& dc, int n)
{
    int dx = 10;
    int dy = 10;
    LayoutInfo layout(dc, n, dx, dy);

    auto x0 = PicaPt::fromPixels(dx, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(dy, dc.dpi());
    int col = 0;
    Font f("Arial", PicaPt(12.0f));

    // macOS: slightly slower as the string gets longer:
    //        30k items/sec for len=4, 29.5k items/sec for len=6
    char text[] = { 'a', 'a', 'a', 'a', 'a', 'a', '\0' };  // 4: 470k combinations
    std::function<void(int)> updateText;
    updateText = [text=text, &updateText](int idx) {
        if (text[idx] == '\0') {
            for (int i = 0;  i < idx;  ++i) {
                text[i] = 'a';
            }
            return;
        } else if (text[idx] == 'z') {
            updateText(idx + 1);
            text[idx] = 'a';
        } else {
            text[idx] += 1;
        }
    };

    dc.beginDraw();
    dc.fill(kBGColor);
    dc.setFillColor(Color(0.5f, 0.5f, 0.5f, 1.0f));
    for (int i = 0;  i < n;  ++i) {
        dc.drawText(text, Point(x, y), f, kPaintFill);
        updateText(0); // change the text so the OS can't cache it
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawTextLayout(DrawContext& dc, int n)
{
    int dx = 10;
    int dy = 10;
    LayoutInfo layout(dc, n, dx, dy);

    auto x0 = PicaPt::fromPixels(dx, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(dy, dc.dpi());
    int col = 0;

    Font font("Arial", PicaPt(12.0f));
    auto textLayout = dc.createTextLayout("abcdef", font, Color(0.5f, 0.5f, 0.5f, 1.0f));

    dc.beginDraw();
    dc.fill(kBGColor);
    for (int i = 0;  i < n;  ++i) {
        dc.drawText(*textLayout, Point(x, y));
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawLinearGradient(DrawContext& dc, int n, int sizePx)
{
    int dx = 10;
    int dy = 10;
    LayoutInfo layout(dc, n, dx, dy);

    auto x0 = PicaPt::fromPixels(dx, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(dy, dc.dpi());
    auto size = PicaPt::fromPixels(sizePx, dc.dpi());
    int col = 0;

    auto &gradient = dc.getGradient({ { Color::kGreen, 0.0f }, { Color::kBlue, 1.0f } });

    dc.beginDraw();
    dc.fill(kBGColor);
    for (int i = 0;  i < n;  ++i) {
        auto path = dc.createBezierPath();
        path->addRect(Rect(x, y, size, size));
        dc.drawLinearGradientPath(path, gradient, Point(x, y), Point(x + size, y + size));
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

void drawRadialGradient(DrawContext& dc, int n, int sizePx)
{
    int dx = 10;
    int dy = 10;
    LayoutInfo layout(dc, n, dx, dy);

    auto x0 = PicaPt::fromPixels(dx, dc.dpi());
    auto x = x0;
    auto y = PicaPt::fromPixels(dy, dc.dpi());
    auto size = PicaPt::fromPixels(sizePx, dc.dpi());
    int col = 0;

    auto &gradient = dc.getGradient({ { Color::kYellow, 0.0f }, { Color::kGreen, 1.0f } });

    dc.beginDraw();
    dc.fill(kBGColor);
    for (int i = 0;  i < n;  ++i) {
        auto path = dc.createBezierPath();
        path->addEllipse(Rect(x, y, size, size));
        dc.drawRadialGradientPath(path, gradient, Point(x, y), PicaPt::kZero, 0.5f * size /*radius*/);
        x += layout.dx;
        col++;
        if (col >= layout.nCols) {
            col = 0;
            x = x0;
            y += layout.dy;
        }
    }
    dc.endDraw();
}

std::shared_ptr<BezierPath> createSquare100(DrawContext& dc, int nPts, const Point& center)
{
    auto rect = dc.createBezierPath();
    PicaPt r = PicaPt::fromPixels(100.0f, dc.dpi());
    rect->addRect(Rect(center.x - r, center.y - r, 2.0f * r, 2.0f * r));
    return rect;
}

std::shared_ptr<BezierPath> createStar(DrawContext& dc, int nPts, int radiusPx, const Point& center)
{
    auto star = dc.createBezierPath();
    PicaPt outer = PicaPt::fromPixels(radiusPx, dc.dpi());
    PicaPt inner = PicaPt::fromPixels(0.666f * float(radiusPx), dc.dpi());
    PicaPt tanLen = PicaPt::fromPixels(0.2f * float(radiusPx), dc.dpi());
    float dtheta = 2.0f * 3.141592f / float(nPts);
    star->moveTo(Point(outer + center.x, center.y));
    for (int i = 0;  i < nPts;  ++i) {
        float thetaOuter = float(i) * dtheta;
        float thetaInner = thetaOuter + 0.5f * dtheta;
        float thetaOuter2 = float(i + 1) * dtheta;
        float outCosT = std::cos(thetaOuter);
        float outSinT = std::sin(thetaOuter);
        float inCosT = std::cos(thetaInner);
        float inSinT = std::sin(thetaInner);
        float out2CosT = std::cos(thetaOuter2);
        float out2SinT = std::sin(thetaOuter2);
        auto outX = outer * outCosT + center.x;
        auto outY = outer * outSinT + center.y;
        auto inX = inner * inCosT + center.x;
        auto inY = inner * inSinT + center.y;
        auto outX2 = outer * out2CosT + center.x;
        auto outY2 = outer * out2SinT + center.y;
        auto tangent_x = tanLen * inSinT;
        auto tangent_y = tanLen * -inCosT;
        star->cubicTo(Point(outX, outY),
                      Point(inX + tangent_x, inY + tangent_y),
                      Point(inX, inY));
        star->cubicTo(Point(inX - tangent_x, inY - tangent_y),
                      Point(outX2, outY2),
                      Point(outX2, outY2));
    }
    star->close();
    return star;
}

std::shared_ptr<BezierPath> createStar10(DrawContext& dc, int radiusPx, const Point& center)
{
    return createStar(dc, 10, radiusPx, center);
}

std::shared_ptr<DrawableImage> createImage(DrawContext& dc, int w, int h, float dpi)
{
    auto imgDC = dc.createBitmap(BitmapType::kBitmapRGB, w, h);
    imgDC->beginDraw();
    imgDC->fill(Color(0.5f, 1.0f, 0.75f));
    imgDC->setFillColor(Color(1.0f, 0.9f, 0.0f));
    imgDC->drawEllipse(Rect(PicaPt(0.0f), PicaPt(0.0f),
                         PicaPt::fromPixels(w, dpi), PicaPt::fromPixels(h, dpi)),
                    PaintMode::kPaintFill);
    imgDC->setFillColor(Color(0.5f, 0.75f, 1.0f));
    imgDC->translate(PicaPt::fromPixels(w / 2, dpi), PicaPt::fromPixels(h / 2, dpi));
    imgDC->rotate(45.0f);
    imgDC->drawRect(Rect(-PicaPt::fromPixels(0.354f * float(w), dpi),
                         -PicaPt::fromPixels(0.354f * float(h), dpi),
                         PicaPt::fromPixels(0.707f * float(w), dpi),
                         PicaPt::fromPixels(0.707f * float(h), dpi)),
                    PaintMode::kPaintFill);
    imgDC->endDraw();
    return imgDC->copyToImage();
}

} // namespace

Timings::Timings()
{
    const int radiusPx = 50;   // note that diameter = 2 * radius
    mRuns = { Run{"initialization", 0,
                  [this](DrawContext& dc, int nObjs) {
                      // this function is called multiple times; we only want to
                      // initialize once.
                      if (!this->mImg100) {
                          this->mImg100 = createImage(dc, 100, 100, 72.0f);
                      }
                  } },
              Run{kBaseRunName, 0, [](DrawContext& dc, int nObjs) { drawNothing(dc); } },
              Run{"rects (fill)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawRects(dc, nObjs, 100, 100,
                                                             PaintMode::kPaintFill); } },
              Run{"rects (stroke)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawRects(dc, nObjs, 100, 100,
                                                             PaintMode::kPaintStroke); } },
              Run{"rects (stroke+fill)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawRects(dc, nObjs, 100, 100,
                                                             PaintMode::kPaintStrokeAndFill); } },
              Run{"rounded rects (fill)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawRoundedRects(dc, nObjs, 100, 100, 10,
                                                             PaintMode::kPaintFill); } },
              Run{"rounded rects (stroke)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawRoundedRects(dc, nObjs, 100, 100, 10,
                                                             PaintMode::kPaintStroke); } },
              Run{"rounded rects (stroke+fill)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawRoundedRects(dc, nObjs, 100, 100, 10,
                                                             PaintMode::kPaintStrokeAndFill); } },
              Run{"bezier rects (fill)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezier(dc, nObjs, createSquare100, radiusPx, PaintMode::kPaintFill); } },
              Run{"bezier rects (stroke)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezier(dc, nObjs, createSquare100, radiusPx, PaintMode::kPaintStroke); } },
              Run{"bezier rects (stroke+fill)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezier(dc, nObjs, createSquare100, radiusPx, PaintMode::kPaintStrokeAndFill); } },
              Run{"star (fill)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezier(dc, nObjs, createStar10, radiusPx, PaintMode::kPaintFill); } },
              Run{"star (stroke)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezier(dc, nObjs, createStar10, radiusPx, PaintMode::kPaintStroke); } },
              Run{"star (stroke+fill)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezier(dc, nObjs, createStar10, radiusPx, PaintMode::kPaintStrokeAndFill); } },
              Run{"star transformed (fill)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezierTransformed(dc, nObjs, createStar10, radiusPx, PaintMode::kPaintFill); } },
              Run{"star transformed (stroke)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezierTransformed(dc, nObjs, createStar10, radiusPx, PaintMode::kPaintStroke); } },
              Run{"star transformed (stroke+fill)", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) {
                      drawBezierTransformed(dc, nObjs, createStar10, radiusPx,
                                            PaintMode::kPaintStrokeAndFill); } },
              Run{"images", kNObjs,
                  [this](DrawContext& dc, int nObjs) {
                      drawImages(dc, nObjs, this->mImg100); } },
              Run{"colored rect", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawColoredRects(dc, nObjs, 100, 100); } },
              Run{"clip rect", kNObjs,
                  [](DrawContext& dc, int nObjs) { clipRects(dc, nObjs, 100, 100); } },
              Run{"clip bezier", kNObjs,
                  [radiusPx](DrawContext& dc, int nObjs) { clipBezier(dc, nObjs, createStar10,
                                                           radiusPx); } },

              Run{"text (no caching)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawText(dc, nObjs); } },
              Run{"text (cached with TextLayout)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawText(dc, nObjs); } },

              Run{"linear gradient (10 px)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawLinearGradient(dc, nObjs, 100); } },
              Run{"linear gradient (50 px)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawLinearGradient(dc, nObjs, 50); } },
              Run{"radial gradient (10 px)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawRadialGradient(dc, nObjs, 10); } },
              Run{"radial gradient (50 px)", kNObjs,
                  [](DrawContext& dc, int nObjs) { drawRadialGradient(dc, nObjs, 50); } },
        };
}

Timings::State Timings::runNext(DrawContext *dc)
{
    auto& lastRun = mRuns[mRunIdx];
    auto resultIt = mResults.find(lastRun.name);
    if (resultIt != mResults.end()) {
        auto &result = resultIt->second;
        result.end = std::chrono::high_resolution_clock::now();
        result.n++;
        if (result.n >= kNTimesPerRun) {
            printResult(lastRun, result);
            mRunIdx++;
            if (mRunIdx >= mRuns.size()) {
                return Timings::DONE;
            }
        }
    }

    auto& thisRun = mRuns[mRunIdx];
    resultIt = mResults.find(thisRun.name);
    if (resultIt == mResults.end()) {
        mResults[thisRun.name] = Result();
        resultIt = mResults.find(thisRun.name);
    }
    auto &result = resultIt->second;
    if (result.n == 0) {
        result.start = std::chrono::high_resolution_clock::now();
    }

    thisRun.func(*dc, thisRun.nObjs);

    return Timings::CONTINUE;
}

void Timings::printResult(const Timings::Run& run, const Timings::Result& result)
{
    auto padStringTo = [](std::string s, int colWidth) -> std::string {
        int nSpaces = colWidth - s.size();
        for (int i = 0;  i < nSpaces;  ++i) {
            s += " ";
        }
        return s;
    };

    auto padNumberTo = [](long long n, int colWidth) {
        auto numStr = std::to_string(n);
        int nSpaces = colWidth - numStr.size();
        std::string s;
        for (int i = 0;  i < nSpaces;  ++i) {
            s += " ";
        }
        return s + numStr;
    };

    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(result.end - result.start).count();
    int n = result.n * run.nObjs;

    std::cout << padStringTo(run.name, 30) << " " << padNumberTo(dt, 6) << " ms  ";
    if (run.name != kBaseRunName) {
        auto& base = mResults[kBaseRunName];
        auto baseTime = std::chrono::duration_cast<std::chrono::milliseconds>(base.end - base.start).count();
        auto runTime = dt - baseTime;
        std::cout << padNumberTo(runTime, 6) << " ms  ";
        if (n != 0) {
            long long itemsPerSec = (long long)std::round(1.0 / double(runTime / 1000.0) * double(n));
            std::cout << padNumberTo(itemsPerSec, 6) << " items/sec";
        }
    }
    std::cout << std::endl;
}
