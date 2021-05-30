#if defined(_WIN32) || defined(_WIN64)  // _WIN32 covers everything except 64-bit ARM
#include "nativedraw.h"

#include "nativedraw_private.h"

#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef DrawText
#include <comdef.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <stringapiset.h>

namespace ND_NAMESPACE {
namespace {
void printError(const std::string& msg, HRESULT err)
{
    std::cerr << "[ERROR] " << msg << ": " << _com_error(err).ErrorMessage() << std::endl;
    assert(false);
}

void printError(const std::string& msg)
{
    std::cerr << "[ERROR] " << msg << std::endl;
    assert(false);
}

// Direct2D uses device-independent pixels equal to 1/96 inch.
inline FLOAT toD2D(const PicaPt& p) { return p.toPixels(96.0f); }
inline D2D1_POINT_2F toD2DPoint(const PicaPt& x, const PicaPt& y) {
    return { toD2D(x), toD2D(y) };
}

class Direct2D
{
public:
    static Direct2D& instance()
    {
        if (!gInstance) {
            gInstance = std::make_unique<Direct2D>();
        }
        return *gInstance;
    }

    ID2D1Factory1* factory() { return mD2DFactory; }
    IDWriteFactory* writeFactory() { return mWriteFactory; }
    IDXGIDevice* dxgiDevice() { return mDXGIDevice; }

    ID2D1DeviceContext* createDeviceContext()
    {
        ID2D1DeviceContext *dc = nullptr;
        HRESULT err = mD2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
        if (err != S_OK) {
            printError("Could not create device context", err);
        }
        return dc;
    }

    Direct2D()
    {
        HRESULT err;

        // Create factories first; they don't depend on anything.
        err = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                __uuidof(ID2D1Factory1),  // get a Direct2D 1.1 factory,
                                (void**)&mD2DFactory);
        if (err != S_OK) {
            printError("fatal: Could not create Direct2D factory!", err);
            return;
        }

        err = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                  __uuidof(IDWriteFactory),
                                  (IUnknown**)&mWriteFactory);
        if (err != S_OK) {
            printError("fatal: Could not createIDWriteFactory!", err);
            return;
        }

        // Initialize DirectX 11.1
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1 };
        err = D3D11CreateDevice(NULL,                                 // first adapter
                                D3D_DRIVER_TYPE_HARDWARE,
                                NULL,                                 // software rasterizer DLL handle
                                D3D11_CREATE_DEVICE_SINGLETHREADED    // beter performance
                                  | D3D11_CREATE_DEVICE_BGRA_SUPPORT, // required for Direct2D
                                featureLevels,
                                sizeof(featureLevels) / sizeof(D3D_FEATURE_LEVEL),
                                D3D11_SDK_VERSION,                    // docs say use this
                                &mD3DDevice,
                                NULL,                                 // don't care what feature level we got
                                &mD3DDeviceContext);
        if (err != S_OK) {
            printError("fatal: could not create a Direct3D 11.1 device", err);
            return;
        }

        err = mD3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&mDXGIDevice);
        if (err != S_OK) {
            printError("fatal: Direct3D device is not IDXGIDevice", err);
            return;
        }

        // Initialize Direct2D
        err = mD2DFactory->CreateDevice(mDXGIDevice, &mD2DDevice);
        if (err != S_OK) {
            printError("fatal: could not create Direct2D device", err);
        }
    }

    virtual ~Direct2D()
    {
        if (mD2DDevice) { mD2DDevice->Release(); }
        if (mDXGIDevice) { mDXGIDevice->Release(); }
        if (mD3DDeviceContext) { mD3DDeviceContext->Release(); }
        if (mD3DDevice) { mD3DDevice->Release(); }
        if (mD2DFactory) { mD2DFactory->Release(); }
        if (mWriteFactory) { mWriteFactory->Release(); }
    }

private:
    static std::unique_ptr<Direct2D> gInstance;
    ID2D1Factory1 *mD2DFactory = nullptr;
    IDWriteFactory *mWriteFactory = nullptr;
    ID3D11Device *mD3DDevice = nullptr;
    ID3D11DeviceContext* mD3DDeviceContext = nullptr;
    IDXGIDevice *mDXGIDevice = nullptr;
    ID2D1Device *mD2DDevice = nullptr;
};
std::unique_ptr<Direct2D> Direct2D::gInstance = nullptr;

} // namespace

//-------------------------- Direct2DPath -------------------------------------
class Direct2DPath : public BezierPath
{
public:
    Direct2DPath() : BezierPath() {}
    ~Direct2DPath()
    {
        clearNative();
    }

    void clearNative() override
    {
        for (auto& dpi2path : mPaths) {
            dpi2path.second->Release();
        }
        mPaths.clear();
    }

    void* nativePathForDPI(float dpi, bool isFilled) override
    {
        float key = dpi;
        if (isFilled) {
            key = -dpi;
        }

        auto it = mPaths.find(key);
        if (it == mPaths.end()) {
            ID2D1GeometrySink *sink = nullptr;
            ID2D1PathGeometry* path = nullptr;
            HRESULT err = Direct2D::instance().factory()->CreatePathGeometry(&path);
            if (err != S_OK) {
                printError("Could not create path", err);
                return nullptr;
            }
            err = path->Open(&sink);

            auto d2dFilled = (isFilled ? D2D1_FIGURE_BEGIN_FILLED
                                       : D2D1_FIGURE_BEGIN_HOLLOW);

            bool isEnded = true;
            for (auto& cmd : mImpl->commands) {
                switch (cmd.cmd) {
                case BezierPath::Impl::Command::kMoveTo:
                    if (!isEnded) {
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    }
                    sink->BeginFigure(toD2DPoint(cmd.p1.x, cmd.p1.y), d2dFilled);
                    isEnded = false;
                    break;
                case BezierPath::Impl::Command::kLineTo:
                    if (isEnded) {
                        sink->BeginFigure({ 0.0f, 0.0f }, d2dFilled);
                    }
                    sink->AddLine(toD2DPoint(cmd.p1.x, cmd.p1.y));
                    isEnded = false;
                    break;
                case BezierPath::Impl::Command::kQuadraticTo:
                    if (isEnded) {
                        sink->BeginFigure({ 0.0f, 0.0f }, d2dFilled);
                    }
                    sink->AddQuadraticBezier({ toD2DPoint(cmd.p1.x, cmd.p1.y),
                                               toD2DPoint(cmd.p2.x, cmd.p2.y) });
                    isEnded = false;
                    break;
                case BezierPath::Impl::Command::kCubicTo:
                    if (isEnded) {
                        sink->BeginFigure({ 0.0f, 0.0f }, d2dFilled);
                    }
                    sink->AddBezier({ toD2DPoint(cmd.p1.x, cmd.p1.y),
                                      toD2DPoint(cmd.p2.x, cmd.p2.y),
                                      toD2DPoint(cmd.p3.x, cmd.p3.y) });
                    isEnded = false;
                    break;
                case BezierPath::Impl::Command::kClose:
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    isEnded = true;
                    break;
                }
            }
            if (!isEnded) {
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            err = sink->Close();
            if (err != S_OK) {
                printError("Path error", err);
            }
            sink->Release();

            mPaths[key] = path;
            it = mPaths.find(key);
        }
        return it->second;
    }

private:
    std::unordered_map<float, ID2D1PathGeometry*> mPaths;
};
//-------------------------------- Fonts --------------------------------------
namespace {

std::unordered_map<FontWeight, DWRITE_FONT_WEIGHT > gWeightToD2D = {
    { kWeightUltraLight, DWRITE_FONT_WEIGHT_THIN },
    { kWeightThin, DWRITE_FONT_WEIGHT_EXTRA_LIGHT },
    { kWeightLight, DWRITE_FONT_WEIGHT_LIGHT },
    { kWeightAuto, DWRITE_FONT_WEIGHT_NORMAL },  // we shouldn't be looking up auto, but make it Regular
    { kWeightRegular, DWRITE_FONT_WEIGHT_NORMAL },
    { kWeightMedium, DWRITE_FONT_WEIGHT_MEDIUM },
    { kWeightSemibold, DWRITE_FONT_WEIGHT_DEMI_BOLD },
    { kWeightBold, DWRITE_FONT_WEIGHT_BOLD },
    { kWeightHeavy, DWRITE_FONT_WEIGHT_EXTRA_BOLD },
    { kWeightBlack, DWRITE_FONT_WEIGHT_BLACK } };

std::unordered_map<DWRITE_FONT_WEIGHT, FontWeight> gD2DToWeight = {
    { DWRITE_FONT_WEIGHT_THIN, kWeightUltraLight },
    { DWRITE_FONT_WEIGHT_EXTRA_LIGHT, kWeightThin },
    { DWRITE_FONT_WEIGHT_LIGHT, kWeightLight },
    { DWRITE_FONT_WEIGHT_NORMAL, kWeightRegular },
    { DWRITE_FONT_WEIGHT_MEDIUM, kWeightMedium },
    { DWRITE_FONT_WEIGHT_DEMI_BOLD, kWeightSemibold },
    { DWRITE_FONT_WEIGHT_BOLD, kWeightBold },
    { DWRITE_FONT_WEIGHT_EXTRA_BOLD, kWeightHeavy },
    { DWRITE_FONT_WEIGHT_BLACK, kWeightBlack } };

struct FontInfo
{
    IDWriteTextFormat *format;
    Font::Metrics metrics;

    FontInfo() : format(nullptr) {}  // for STL containers
    FontInfo(IDWriteTextFormat* f, const Font::Metrics& fm)
        : format(f), metrics(fm)
    {}

    // Can't delete here, this object needs to be trivially copyable
};

std::wstring getSystemFontName()
{
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
    // ncm.lfMessageFont.lfFaceName is an array, not a pointer, so we
    // need to copy the contents because ncm will go away. Conveniently,
    // wstring() does this for us.
    return ncm.lfMessageFont.lfFaceName;
}

FontInfo createFont(const Font& desc, float dpi)
{
    const int kNullTerminated = -1;
    int nCharsNeeded = MultiByteToWideChar(CP_UTF8, 0, desc.family().c_str(),
                                           kNullTerminated, NULL, 0);
    WCHAR *family = new WCHAR[nCharsNeeded + 1];  // nCharsNeeded includes \0, but +1 just in case
    family[0] = '\0';  // in case conversion fails
    MultiByteToWideChar(CP_UTF8, 0, desc.family().c_str(), kNullTerminated, family, nCharsNeeded);

    IDWriteFactory *factory = Direct2D::instance().writeFactory();

    IDWriteFontCollection *collection;
    factory->GetSystemFontCollection(&collection, true);

    FLOAT d2dSize = toD2D(desc.pointSize());
    DWRITE_FONT_WEIGHT d2dWeight = gWeightToD2D[desc.weight()];
    DWRITE_FONT_STYLE d2dStyle = ((desc.style() & kStyleItalic) ? DWRITE_FONT_STYLE_ITALIC
                                                                : DWRITE_FONT_STYLE_NORMAL);
    DWRITE_FONT_STRETCH d2dStretch = DWRITE_FONT_STRETCH_NORMAL;
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    IDWriteTextFormat *format = nullptr;
    HRESULT err = factory->CreateTextFormat(family, collection,
                                            d2dWeight, d2dStyle, d2dStretch,
                                            toD2D(desc.pointSize()), locale,
                                            &format);
    if (err != S_OK) {
        // Using a non-existant family name does not cause an error, this will
        // only happen for parameter errors (which would be a programming error).
        printError("Error calling CreateTextFormat()", err);
    }
    delete [] family;

    nCharsNeeded = format->GetFontFamilyNameLength() + 1;
    family = new WCHAR[nCharsNeeded + 1];
    format->GetFontFamilyName(family, nCharsNeeded);
    BOOL exists = false;
    UINT32 idx;
    collection->FindFamilyName(family, &idx, &exists);
    Font::Metrics metrics;
    metrics.ascent = PicaPt(0);
    metrics.capHeight = PicaPt(0);
    metrics.descent = PicaPt(0);
    metrics.leading = PicaPt(0);
    metrics.lineHeight = PicaPt(0);
    metrics.xHeight = PicaPt(0);
    if (exists) {
        IDWriteFontFamily *fontFamily = nullptr;
        if (collection->GetFontFamily(idx, &fontFamily) == S_OK) {
            IDWriteFont* font;
            if (fontFamily->GetFirstMatchingFont(d2dWeight, d2dStretch, d2dStyle, &font) == S_OK) {
                DWRITE_FONT_METRICS d2dMetrics;
                font->GetMetrics(&d2dMetrics);
                float scaling = format->GetFontSize() / (float)d2dMetrics.designUnitsPerEm;
                metrics.ascent = PicaPt::fromPixels(float(d2dMetrics.ascent) * scaling, 96.0f);
                metrics.capHeight = PicaPt::fromPixels(float(d2dMetrics.capHeight) * scaling, 96.0f);
                metrics.descent = PicaPt::fromPixels(float(d2dMetrics.descent) * scaling, 96.0f);
                metrics.leading = PicaPt::fromPixels(float(d2dMetrics.lineGap) * scaling, 96.0f);
                metrics.xHeight = PicaPt::fromPixels(float(d2dMetrics.xHeight) * scaling, 96.0f);
                font->Release();
            }
            fontFamily->Release();
        }
    }
    collection->Release();
    metrics.lineHeight = metrics.ascent + metrics.descent + metrics.leading;
    delete [] family;
    return FontInfo(format, metrics);
}

void destroyFont(FontInfo rsrc)
{
    rsrc.format->Release();
}

static ResourceManager<Font, FontInfo> gFontMgr(createFont, destroyFont);

} // namespace

//---------------------- Custom text renderer ---------------------------------
// We need our own renderer so that we can draw outlines
class CustomTextRenderer : public IDWriteTextRenderer
{
public:
    // Note:  to make sure that AddRef() and Release() work right, this should
    //        be created with new(), NOT on the stack!. The new object will have
    //        the refcount already at 1, so you will need to call Release().
    CustomTextRenderer(D2D1::Matrix3x2F& matrix, ID2D1SolidColorBrush *brush,
                       const Color *fill,
                       const Color *stroke, FLOAT strokeWidth,
                       ID2D1StrokeStyle *style)
        : mMatrix(matrix), mBrush(brush), mFillColor(fill)
        , mStrokeColor(stroke), mStrokeWidth(strokeWidth), mStrokeStyle(style)
    {}

    ~CustomTextRenderer() {}

    STDMETHOD(DrawGlyphRun)(void* drawContext,
                            FLOAT baselineOriginX,
                            FLOAT baselineOriginY,
                            DWRITE_MEASURING_MODE measuringMode,
                            DWRITE_GLYPH_RUN const* glyphRun,
                            DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
                            IUnknown* clientDrawingEffect)
    {
        ID2D1PathGeometry *geometry = nullptr;
        ID2D1GeometrySink *sink = nullptr;
        HRESULT err;

        err = Direct2D::instance().factory()->CreatePathGeometry(&geometry);
        err = geometry->Open(&sink);

        err = glyphRun->fontFace->GetGlyphRunOutline(glyphRun->fontEmSize,
                                                     glyphRun->glyphIndices,
                                                     glyphRun->glyphAdvances,
                                                     glyphRun->glyphOffsets,
                                                     glyphRun->glyphCount,
                                                     glyphRun->isSideways,
                                                     glyphRun->bidiLevel,
                                                     sink
        );
        err = sink->Close();

        auto *gc = (ID2D1RenderTarget*)drawContext;

        D2D1::Matrix3x2F runMatrix;
        runMatrix.SetProduct(D2D1::Matrix3x2F::Translation(D2D1::SizeF(baselineOriginX, baselineOriginY)), mMatrix);
        gc->SetTransform(runMatrix);

        if (mFillColor) {
            mBrush->SetColor({ mFillColor->red(), mFillColor->green(),
                               mFillColor->blue(), mFillColor->alpha() });
            gc->FillGeometry(geometry, mBrush);
        }
        if (mStrokeColor) {
            mBrush->SetColor({ mStrokeColor->red(), mStrokeColor->green(),
                               mStrokeColor->blue(), mStrokeColor->alpha() });
            gc->DrawGeometry(geometry, mBrush, mStrokeWidth, mStrokeStyle);
        }

        gc->SetTransform(mMatrix);

        geometry->Release();
        sink->Release();

        return err;
    }

    STDMETHOD(DrawUnderline)(void* drawContext,
                             FLOAT baselineOriginX,
                             FLOAT baselineOriginY,
                             DWRITE_UNDERLINE const* underline,
                             IUnknown* clientDrawingEffect)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DrawStrikethrough)(void* drawContext,
                                 FLOAT baselineOriginX,
                                 FLOAT baselineOriginY,
                                 DWRITE_STRIKETHROUGH const* strikethrough,
                                 IUnknown* effect)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DrawInlineObject)(void* drawContext,
                                FLOAT originX,
                                FLOAT originY,
                                IDWriteInlineObject* inlineObject,
                                BOOL isSideways,
                                BOOL isRightToLeft,
                                IUnknown* effect)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(IsPixelSnappingDisabled)(void* drawContext, BOOL* isDisabled)
    {
        *isDisabled = TRUE;
        return S_OK;
    }

    STDMETHOD(GetCurrentTransform)(void* drawContext, DWRITE_MATRIX* transform)
    {
        auto* gc = (ID2D1RenderTarget*)drawContext;
        gc->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(transform));
        return S_OK;
    }

    STDMETHOD(GetPixelsPerDip)(void* drawContext, FLOAT* pixelsPerDip)
    {
        float x, yUnused;

        auto* gc = (ID2D1RenderTarget*)drawContext;
        gc->GetDpi(&x, &yUnused);
        *pixelsPerDip = x / 96;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void FAR* FAR* ppvObj)
    {
        if (iid == IID_IUnknown) //|| iid == IID_IDWritePixelSnapping || iid == IID_IDWriteTextRenderer)
        {
            *ppvObj = this;
            AddRef();
            return NOERROR;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        return (ULONG)++mRefCount;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        mRefCount--;
        auto refCount = mRefCount;
        if (0 == mRefCount) {
            delete this;
        }
        return (ULONG)refCount;
    }

protected:
    D2D1::Matrix3x2F mMatrix;
    size_t mRefCount = 1;
    // We don't own any of these pointers
    ID2D1SolidColorBrush *mBrush;
    const Color *mFillColor;
    const Color *mStrokeColor;
    FLOAT mStrokeWidth;
    ID2D1StrokeStyle *mStrokeStyle;

};
//-------------------------------- Image --------------------------------------
class Direct2DImage : public Image
{
public:
    using NativeType = ID2D1Bitmap1*;

    Direct2DImage(NativeType bitmap, int width, int height, float dpi)
        : Image(bitmap, width, height, dpi)
    {}

    ~Direct2DImage() {
        if (mNativeHandle) {
            ((NativeType)mNativeHandle)->Release();
        }
    }
};

//-------------------------- Direct2DContext ----------------------------------
/*class SolidBrushManager : ResourceManager<Color, ID2D1SolidColorBrush*>
{
public:
    SolidBrushManager(ID2D1RenderTarget* rt) : mRenderTarget(rt) {}

private:
    ID2D1RenderTarget *mRenderTarget;  // we don't own this

    static ID2D1SolidColorBrush* createBrush(const Color& color, float dpi)
    {

    }
}; */

class Direct2DContext : public DrawContext
{
    using Super = DrawContext;
protected:
    enum class DrawingState { kNotDrawing = 0, kDrawing };

    mutable DrawingState mDrawingState;

    struct ContextState {
        Color fillColor;
        Color strokeColor;
        PicaPt strokeWidth;
/*        JoinStyle join;
        EndCapStyle endCap;
        PicaPt dashOffset; */
        std::vector<PicaPt> dashes;
        D2D1_STROKE_STYLE_PROPERTIES strokeProperties;
        D2D1::Matrix3x2F transform;
        std::vector<std::shared_ptr<BezierPath>> clippingPaths;
    };
    std::vector<ContextState> mStateStack;

    // TODO: is it faster to create a brush for each color used, or just
    //       change the color each time?
    ID2D1SolidColorBrush *mSolidBrush = nullptr;
    // TODO: is it expensive to create a new stroke frequently?
    ID2D1StrokeStyle *mStrokeStyle = nullptr;

    bool mAntialias = true;

public:
    Direct2DContext(void* renderTarget, int width, int height, float dpi)
        : DrawContext(renderTarget, width, height, dpi)
    {
        mDrawingState = DrawingState::kNotDrawing;
        if (renderTarget) {
            setNativeDC(renderTarget);
        }
    }

    ~Direct2DContext()
    {
        if (mDrawingState == DrawingState::kDrawing) {
            printError("DrawContext::endDraw() not called before destruction");
        }
        cleanup();
    }

    std::shared_ptr<DrawContext> createBitmap(BitmapType type, int width, int height,
                                              float dpi /*= 72.0f*/) override
    {
        return DrawContext::createDirect2DBitmap(type, width, height, dpi);
    }

    std::shared_ptr<BezierPath> createBezierPath() const override
    {
        return std::make_shared<Direct2DPath>();
    }

private:
    void cleanup()
    {
        mStateStack.clear();
        if (mSolidBrush) {
            mSolidBrush->Release();
            mSolidBrush = nullptr;
        }
        clearStrokeStyle();
    }

    void clearStrokeStyle()
    {
        if (mStrokeStyle) {
            mStrokeStyle->Release();
            mStrokeStyle = nullptr;
        }
    }

public:
    void setNativeDC(void* nativeDC)
    {
        cleanup();
        mStateStack.push_back(ContextState());
        // Make sure we have sane defaults for anything that is unset
        // (in particular, the miter limit).
        mStateStack.back().strokeProperties = D2D1::StrokeStyleProperties();

        mDrawingState = DrawingState::kNotDrawing;
        auto *gc = (ID2D1DeviceContext*)nativeDC;
        gc->SetDpi(mDPI, mDPI);
        gc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
//        gc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
        gc->GetTransform(&mStateStack.back().transform);
        HRESULT err = gc->CreateSolidColorBrush({ 0.0f, 0.0f, 0.0f, 1.0f }, &mSolidBrush);
        if (err != S_OK) {
            printError("Could not create default black brush", err);
        }

        // Set the initial state *after* creating the state stack,
        // so that the state setting functions will set the state properly.
        mNativeDC = nativeDC;
        setInitialState();

        // Don't need to scale for DPI, Direct2D already handles that.
        // (But note that Direct2D coordinates are 1/96 inch, not 1/72 inch)
    }

    void beginDraw() override
    {
        if (mDrawingState == DrawingState::kNotDrawing) {
            auto* gc = deviceContext();
            gc->BeginDraw();
            mDrawingState = DrawingState::kDrawing;
        } else {
            printError("DrawContext::beginDraw() called within a beginDraw/endDraw pair");
        }
    }

    void endDraw() override
    {
        if (mDrawingState == DrawingState::kDrawing) {
            auto gc = (ID2D1RenderTarget*)mNativeDC;
            if (gc) {
                size_t nUnpoppedLayers = 0;
                for (auto& state : mStateStack) {
                    nUnpoppedLayers += state.clippingPaths.size();
                }
                for (size_t i = 0; i < nUnpoppedLayers; ++i) {
                    popClipLayer();
                }

                HRESULT err = gc->EndDraw();
                if (err != S_OK) {
                    printError("Error ocurred while drawing", err);
                }
            }
            mDrawingState = DrawingState::kNotDrawing;
        } else {
            printError("DrawContext::endDraw() called without calling beginDraw()");
        }
    }

    void save() override
    {
        mStateStack.push_back(mStateStack.back());
        // We need to clear the copy's clipping paths, since they belong to the
        // previous state, not this one.
        mStateStack.back().clippingPaths.clear();
    }

    void restore() override
    {
        auto *gc = deviceContext();
        // Note that primitive blend needs to be SRC_OVER when popping a layer,
        // otherwise the (transparent black) background pixels will be overwritten.
        for (size_t i = 0; i < mStateStack.back().clippingPaths.size(); ++i) {
            popClipLayer();
        }
        mStateStack.pop_back();
        deviceContext()->SetTransform(mStateStack.back().transform);
    }

    void translate(const PicaPt& dx, const PicaPt& dy) override
    {
        auto& m = mStateStack.back().transform;
        D2D1::Matrix3x2F newM;
        newM.SetProduct(D2D1::Matrix3x2F::Translation(D2D1::SizeF(toD2D(dx), toD2D(dy))), m);
        deviceContext()->SetTransform(newM);
        mStateStack.back().transform = newM;
    }

    void rotate(float degrees) override
    {
        // Our coordinate system is y-flipped with respect to the GPU's mathematical
        // one, which make +angle rotate clockwise. We still want +angle to be counterclockwise
        // so that the angle works like people expect it, so we need to negate it.
        auto& m = mStateStack.back().transform;
        D2D1::Matrix3x2F newM;
        newM.SetProduct(D2D1::Matrix3x2F::Rotation(-degrees, { 0.0f, 0.0f }), m);
        deviceContext()->SetTransform(newM);
        mStateStack.back().transform = newM;
    }

    void scale(float sx, float sy) override
    {
        auto& m = mStateStack.back().transform;
        D2D1::Matrix3x2F newM;
        newM.SetProduct(D2D1::Matrix3x2F::Scale(D2D1::SizeF(sx, sy), { 0.0f, 0.0f }), m);
        deviceContext()->SetTransform(newM);
        mStateStack.back().transform = newM;
    }

    void fill(const Color& color) override
    {
        auto* gc = deviceContext();
        gc->Clear(D2D1_COLOR_F{ color.red(), color.green(), color.blue(), color.alpha() });
    }

    void clearRect(const Rect& rect) override
    {
        assert(false);
        // TODO: this is not correct!
        deviceContext()->Clear(NULL);
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
    }

    void setStrokeEndCap(EndCapStyle cap) override
    {
        D2D1_CAP_STYLE d2dStyle = D2D1_CAP_STYLE_FLAT;
        switch (cap) {
            case kEndCapButt:
                d2dStyle = D2D1_CAP_STYLE_FLAT; break;
            case kEndCapRound:
                d2dStyle = D2D1_CAP_STYLE_ROUND; break;
            case kEndCapSquare:
                d2dStyle = D2D1_CAP_STYLE_SQUARE; break;
        }
        auto &strokeProperties = mStateStack.back().strokeProperties;
        strokeProperties.startCap = d2dStyle;
        strokeProperties.endCap = d2dStyle;
        strokeProperties.dashCap = d2dStyle;
        clearStrokeStyle();
    }

    void setStrokeJoinStyle(JoinStyle join) override
    {
        D2D1_LINE_JOIN d2dJoin = D2D1_LINE_JOIN_MITER_OR_BEVEL;
        switch (join) {
        case kJoinMiter:
            d2dJoin = D2D1_LINE_JOIN_MITER_OR_BEVEL; break;  // miter unless angle is very small
        case kJoinRound:
            d2dJoin = D2D1_LINE_JOIN_ROUND; break;
        case kJoinBevel:
            d2dJoin = D2D1_LINE_JOIN_BEVEL; break;
        }
        mStateStack.back().strokeProperties.lineJoin = d2dJoin;
        clearStrokeStyle();
    }

    void setStrokeDashes(const std::vector<PicaPt> lengths, const PicaPt& offset) override
    {
        if (lengths.empty()) {
            mStateStack.back().dashes.clear();
            mStateStack.back().strokeProperties.dashStyle = D2D1_DASH_STYLE_SOLID;
            mStateStack.back().strokeProperties.dashOffset = 0.0f;
        } else {
            mStateStack.back().dashes = lengths;
            mStateStack.back().strokeProperties.dashStyle = D2D1_DASH_STYLE_CUSTOM;
            // The documentation isn't clear, but empirically it seems that the units
            // for dashes is pixels, not DIPs.
            mStateStack.back().strokeProperties.dashOffset = -offset.toPixels(mDPI);
        }
        clearStrokeStyle();
    }

    void drawLines(const std::vector<Point>& lines) override
    {
        if (lines.empty()) {
            return;
        }

        // TODO: this is likely to be slow; maybe eliminate this function from API?
        auto path = createBezierPath();
        path->moveTo(lines[0]);
        for (size_t i = 0; i < lines.size(); ++i) {
            path->lineTo(lines[i]);
        }
        drawPath(path, kPaintStroke);
    }

    void drawRect(const Rect& rect, PaintMode mode) override
    {
        auto *gc = deviceContext();
        auto &state = mStateStack.back();
        D2D1_RECT_F r{ toD2D(rect.x), toD2D(rect.y),
                       toD2D(rect.maxX()), toD2D(rect.maxY()) };
        if (mode & kPaintFill) {
            gc->FillRectangle(r, getBrush(state.fillColor));
        }
        if (mode & kPaintStroke) {
            gc->DrawRectangle(r, getBrush(state.strokeColor), toD2D(state.strokeWidth), getStrokeStyle());
        }
    }

    // drawRoundedRect is in nativedraw.cpp
    // TODO: is Draw/FillRoundedRect() faster than using a new path?

    void drawEllipse(const Rect& rect, PaintMode mode) override
    {
        auto *gc = deviceContext();
        auto& state = mStateStack.back();
        D2D1_ELLIPSE e{ {toD2D(rect.midX()), toD2D(rect.midY()) },
                        0.5f * toD2D(rect.width), 0.5f * toD2D(rect.height) };
        if (mode & kPaintFill) {
            gc->FillEllipse(e, getBrush(state.fillColor));
        }
        if (mode & kPaintStroke) {
            gc->DrawEllipse(e, getBrush(state.strokeColor), toD2D(state.strokeWidth), getStrokeStyle());
        }
    }

    void drawPath(std::shared_ptr<BezierPath> path, PaintMode mode) override
    {
        auto *gc = deviceContext();
        auto& state = mStateStack.back();
        if (mode & kPaintFill) {
            gc->FillGeometry((ID2D1PathGeometry*)path->nativePathForDPI(mDPI, true),
                             getBrush(state.fillColor), nullptr);
        }
        if (mode & kPaintStroke) {
            gc->DrawGeometry((ID2D1PathGeometry*)path->nativePathForDPI(mDPI, false),
                             getBrush(state.strokeColor), toD2D(state.strokeWidth),
                             getStrokeStyle());
        }
    }

    void drawText(const char* textUTF8, const Point& topLeft, const Font& font,
                  PaintMode mode) override
    {
        // Convert from UTF8 -> WCHAR
        const int kNullTerminated = -1;
        int nCharsNeeded = MultiByteToWideChar(CP_UTF8, 0, textUTF8, kNullTerminated, NULL, 0);
        WCHAR* wtext = new WCHAR[nCharsNeeded + 1];  // nCharsNeeded includes \0, but +1 just in case
        wtext[0] = '\0';  // in case conversion fails
        MultiByteToWideChar(CP_UTF8, 0, textUTF8, kNullTerminated, wtext, nCharsNeeded);

        // Set alignment
        auto* format = gFontMgr.get(font, mDPI).format;
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR); // top

        // Create the text layout (don't snap to pixel)
        auto& state = mStateStack.back();
        auto* gc = deviceContext();
        auto textOpts = D2D1_DRAW_TEXT_OPTIONS_NO_SNAP;
        IDWriteTextLayout* layout = nullptr;
        HRESULT err = Direct2D::instance().writeFactory()
                              ->CreateTextLayout(wtext, nCharsNeeded - 1, // don't pass the \0
                format, 10000.0f, 10000.0f, &layout);

        // Snapping to pixel sometimes puts the text above the baseline
        // (as desired) or on the pixel of the baseline (not desired).
        // Do our own snapping so that we can get consistent results.
        // This requires getting the origin of the line ourself.
        DWRITE_LINE_METRICS* lineMetrics = nullptr;
        UINT32 nLines;
        layout->GetLineMetrics(lineMetrics, 0, &nLines);
        lineMetrics = new DWRITE_LINE_METRICS[nLines];
        layout->GetLineMetrics(lineMetrics, nLines, &nLines);
        float offsetPx = PicaPt::fromPixels(float(lineMetrics->baseline), 96.0f).toPixels(mDPI);
        offsetPx = offsetPx - std::floor(offsetPx);
        auto offset = PicaPt::fromPixels(offsetPx, mDPI);
        delete[] lineMetrics;

        // Finally, we can draw. gc->DrawTextLayout() works fine for filled text,
        // but has no support for outlines, so we need to use our own custom
        // text renderer :(
        const Color* fillColor = nullptr;
        const Color* strokeColor = nullptr;
        FLOAT strokeWidth = 0.0f;
        ID2D1StrokeStyle* strokeStyle = nullptr;
        if (mode & kPaintFill) {
            fillColor = &state.fillColor;
        }
        if (mode & kPaintStroke) {
            strokeColor = &state.strokeColor;
            strokeWidth = toD2D(state.strokeWidth);
            strokeStyle = getStrokeStyle();
        }
        auto *textRenderer = new CustomTextRenderer(
                                        state.transform, mSolidBrush, fillColor,
                                        strokeColor, strokeWidth, strokeStyle);
        layout->Draw(gc, textRenderer, toD2D(topLeft.x), toD2D(topLeft.y - offset));
        textRenderer->Release();

        delete [] wtext;
    }

    void drawImage(std::shared_ptr<Image> image, const Rect& rect) override
    {
        auto *gc = deviceContext();
        auto destRect = D2D1::RectF(toD2D(rect.x), toD2D(rect.y),
                                    toD2D(rect.maxX()), toD2D(rect.maxY()));
        // RectF constructor is RectF(xMin, yMin, xMax, yMax), but if
        // we start at (0, 0) then it is the same as (x, y, w, h).
        auto srcRect = D2D1::RectF(0.0f, 0.0f,
                                   toD2D(PicaPt::fromPixels(float(image->width()), image->dpi())),
                                   toD2D(PicaPt::fromPixels(float(image->height()), image->dpi())));
        gc->DrawBitmap((Direct2DImage::NativeType)image->nativeHandle(), destRect,
                       1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRect);
    }

    void clipToRect(const Rect& rect) override
    {
        auto *gc = deviceContext();
        // The Direct2D documentation says that using an axis-aligned clipping rect
        // would be faster (but more so for un-antialiased contexts). However, this
        // would require switching between axis-aligned contexts and layers, so
        // hopefully this isn't much of a performance penalty.
        auto path = createBezierPath();
        path->addRect(rect);
        clipToPath(path);

        // We don't appear to need to save the clipping path as Direct2D using ref-counting
        // but this way we also know how many PopLayer()s that restore() needs to call
    }

    void clipToPath(std::shared_ptr<BezierPath> path) override
    {
        pushClipLayer(path);
        mStateStack.back().clippingPaths.push_back(path);
    }

    Font::Metrics fontMetrics(const Font& font) const override
    {
        // We could get the 72 dpi version of the font, which is exactly in
        // PicaPt, but we get the actual size font so that we can attempt
        // get more accurate values due to hinting (or lack thereof at
        // higher resolutions).
        return gFontMgr.get(font, mDPI).metrics;
    }

    std::shared_ptr<Image> copyToImage() override
    {
        auto* gc = deviceContext();

        Direct2DImage::NativeType image = nullptr;

        D2D1_BITMAP_PROPERTIES1 bitmapProps;
        bitmapProps.pixelFormat = gc->GetPixelFormat();
        bitmapProps.dpiX = dpi();
        bitmapProps.dpiY = dpi();
        bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;  // can use for SetTarget()
        bitmapProps.colorContext = nullptr;
        HRESULT err = deviceContext()->CreateBitmap({ UINT32(width()), UINT32(height()) },
                                                    nullptr, 0,
                                                    bitmapProps, &image);
        if (err != S_OK) {
            printError("Could not create bitmap", err);
            return nullptr;
        }

        D2D1_POINT_2U destUpperLeft = { 0, 0 };
        D2D1_RECT_U srcRect = { 0, 0, UINT32(width()), UINT32(height()) };
        err = image->CopyFromRenderTarget(&destUpperLeft, gc, &srcRect);
        if (err != S_OK) {
            printError("Could not copy to image", err);
            return nullptr;
        }

        return std::make_shared<Direct2DImage>(image, width(), height(), dpi());
    }

    Color pixelAt(int x, int y) override
    {
        assert(false);  // need a bitmap context
        return Color::kPurple;
    }

protected:
    ID2D1DeviceContext* deviceContext()
    {
        return (ID2D1DeviceContext*)mNativeDC;
    }

    ID2D1SolidColorBrush* getBrush(const Color& c)
    {
        mSolidBrush->SetColor({ c.red(), c.green(), c.blue(), c.alpha() });
        return mSolidBrush;
    }

    ID2D1StrokeStyle* getStrokeStyle()
    {
        if (!mStrokeStyle) {
            auto *d2d = Direct2D::instance().factory();
            auto &curr = mStateStack.back();
            HRESULT err;
            if (curr.dashes.empty()) {
                err = d2d->CreateStrokeStyle(curr.strokeProperties, NULL, 0, &mStrokeStyle);
            } else {
                std::vector<FLOAT> dashes;
                dashes.reserve(curr.dashes.size());
                for (auto& d : curr.dashes) {
                    // The documentation isn't clear, but empirically it seems that the
                    // units for dashes is pixels, not DIPs.
                    dashes.push_back(d.toPixels(mDPI));
                }
                err = d2d->CreateStrokeStyle(curr.strokeProperties,
                                             dashes.data(), UINT32(dashes.size()),
                                             &mStrokeStyle);
            }
            if (err != S_OK) {
                printError("Could not create stroke", err);
                // If CreateStrokeStyle() fails, nullptr is a valid style
                mStrokeStyle = nullptr;
            }
        }

        return mStrokeStyle;
    }

    void pushClipLayer(std::shared_ptr<BezierPath> path)
    {
        auto *gc = deviceContext();
        D2D1_LAYER_PARAMETERS params;
        params.contentBounds = D2D1::InfiniteRect();
        params.geometricMask = (ID2D1Geometry*)path->nativePathForDPI(mDPI, true);
        params.layerOptions = D2D1_LAYER_OPTIONS_NONE;
        params.maskTransform = D2D1::Matrix3x2F::Identity();
        params.maskAntialiasMode = (mAntialias ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
                                               : D2D1_ANTIALIAS_MODE_ALIASED);
        params.opacity = 1.0f;
        params.opacityBrush = nullptr;
        // Windows 8 and later allows using nullptr to let Windows manage the layers
        gc->PushLayer(params, nullptr);
        gc->Clear({ 0.0f, 0.0f, 0.0f, 0.0f });
    }

    void popClipLayer()
    {
        auto *gc = deviceContext();
        gc->PopLayer();
    }
};

//-----------------------------------------------------------------------------
class Direct2DWindow : public Direct2DContext
{
    using Super = Direct2DContext;
protected:
    ID2D1DeviceContext* mDC;
    IDXGISwapChain1* mSwapChain = nullptr;
    ID2D1Bitmap1* mBackingStore = nullptr;

public:
    explicit Direct2DWindow(HWND hwnd)
        : Direct2DContext(nullptr, 0, 0, 96)
    {
        RECT r;
        GetClientRect(hwnd, &r);
        mWidth = r.right - r.left;
        mHeight = r.bottom - r.top;
        mDPI = float(GetDpiForWindow(hwnd));
        if (mDPI == 0.0) {  // only happens if invalid window
            mDPI = 96.0;
        }

        // The code below has convinced me to never program for
        // Windows without an abstraction layer. And if I'm going to use
        // an abstraction layer, it's going to be cross platform, because
        // the following is a gross abomination. It could be better, we
        // could do:
        //   auto size = D2D1::SizeU(r.right - r.left, r.bottom - r.top);
        //   HRESULT result = Direct2D::instance().factory()->CreateHwndRenderTarget(
        //       D2D1::RenderTargetProperties(),
        //       // Default third param indicates render target throws away its
        //       // contents after presenting.
        //       D2D1::HwndRenderTargetProperties(hwnd, size),
        //       &mRenderTarget);
        //   setNativeDC(mRenderTarget);
        // except, wait, you want alpha blending? Oh, sorry, ID2D1HwndRenderTarget
        // doesn't support SetPrimitiveBlend(), ha ha, it's to the gross for you!

        mDC = Direct2D::instance().createDeviceContext();

        // Mostly copy-pasted from the Microsoft docs, because how else are you
        // going to get this right?!
        // https://docs.microsoft.com/en-us/windows/win32/direct2d/devices-and-device-contexts

        // EB: So in order to render 2D to a window, we need to go through the 3D process
        //     and create a swap chain.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
        swapChainDesc.Width = 0;                           // automatic sizing
        swapChainDesc.Height = 0;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // the most common format
        swapChainDesc.Stereo = false;
        swapChainDesc.SampleDesc.Count = 1;                // don't use multi-sampling
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;                     // use double buffering to enable flip
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; // all apps must use this SwapEffect
        swapChainDesc.Flags = 0;

        HRESULT err;

        // EB: the abstraction is so low that we have to get the specific video
        //     card that this window is using! Which brings up the question of what
        //     happens if the user moves the window to a monitor using another card?
        IDXGIDevice* device = Direct2D::instance().dxgiDevice();
        IDXGIAdapter* adapter;
        err = device->GetAdapter(&adapter);
        if (err != S_OK) {
            printError("fatal: Cannot get adapter from IDXGIDevice", err);
        }

        // MS: Get the factory object that created the DXGI device.
        // EB: Because we can't create a swap chain for a window from somewhere
        //     like, maybe, the window? The device? The adapter?
        IDXGIFactory2 *dxgiFactory;
        err = adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
        if (err != S_OK) {
            printError("fatal: Cannot get DXGIFactory2 from adapter", err);
            return;
        }

        err = dxgiFactory->CreateSwapChainForHwnd(
                device,          // this is a D3DDevice, which IDXGIDevice is
                hwnd,
                &swapChainDesc,
                nullptr,         // no fullscreen
                nullptr,         // allow on all displays
                &mSwapChain);
        if (err != S_OK) {
            printError("fatal: Cannot create swap chain for window", err);
            return;
        }

        // MS: Ensure that DXGI doesn't queue more than one frame at a time.
        // EB: I guess if we have multiple windows, they are are going to have
        //     this set.
        //device->SetMaximumFrameLatency(1);

        // EB: Get the backbuffer so we can create the bitmap to be our backing store.
        IDXGISurface *backBuffer;
        err = mSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (err != S_OK) {
            printError("fatal: Cannot get swap chain's back buffer", err);
            return;
        }

        // EB: Create the bitmap now and set the context to render to it.
        D2D1_BITMAP_PROPERTIES1 bitmapProperties =
            D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                mDPI,
                mDPI);

        err = mDC->CreateBitmapFromDxgiSurface(
            backBuffer, &bitmapProperties, &mBackingStore);
        if (err != S_OK) {
            printError("fatal: Cannot get create bitmap for backing store", err);
            return;
        }

        mDC->SetTarget(mBackingStore);

        // So now that we've had to MAKE OUR OWN @#$! backing store--if everyone
        // is just going to copy-paste your example, Microsoft, for every window
        // someone writes, maybe this is something that could be, I don't, made
        // into a function somewhere?--we can finally hand off our device context
        // to the superclass.
        setNativeDC(mDC);

        adapter->Release();
        dxgiFactory->Release();
        backBuffer->Release();
    }

    ~Direct2DWindow()
    {
        // The documentation does not mention what order we need to release things.
        mDC->SetTarget(nullptr);
        mBackingStore->Release();
        mSwapChain->Release();
        mDC->Release();
    }

    void beginDraw() override
    {
        Super::beginDraw();
    }

    void endDraw() override
    {
        Super::endDraw();

        DXGI_PRESENT_PARAMETERS parameters = { 0 };
        parameters.DirtyRectsCount = 0;
        parameters.pDirtyRects = nullptr;
        parameters.pScrollRect = nullptr;
        parameters.pScrollOffset = nullptr;

        HRESULT err = mSwapChain->Present1(1, 0, &parameters);
        if (err != S_OK && err != DXGI_STATUS_OCCLUDED) {
            printError("swapChain->Present1() failed", err);
        }
    }
};

//-----------------------------------------------------------------------------
class Direct2DBitmap : public Direct2DContext
{
    using Super = Direct2DContext;
public:
    BitmapType mType;
    int mBytesPerPixel;
    ID2D1DeviceContext *mDC = nullptr;
    ID2D1Bitmap1 *mBitmap = nullptr;
    mutable ID2D1Bitmap1* mReadBitmap = nullptr;
    D2D1_PIXEL_FORMAT mPixelFormat;

    Direct2DBitmap(BitmapType type, int width, int height, float dpi /*= 72.0f*/)
        : Direct2DContext(nullptr, width, height, dpi)
    {
        mType = type;
        switch (type) {
            default:  // for MSVC to understand the variables are initialized
            case kBitmapRGBA:
                mBytesPerPixel = 4;
                mPixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                mPixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
                break;
            case kBitmapRGB:
                mBytesPerPixel = 4;
                mPixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                mPixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
                break;
            case kBitmapGreyscale:
                mBytesPerPixel = 1;
                // TODO: this probably doesnt work
                mPixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                mPixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
                break;
            case kBitmapAlpha:
                mBytesPerPixel = 1;
                mPixelFormat.format = DXGI_FORMAT_A8_UNORM;
                mPixelFormat.alphaMode = D2D1_ALPHA_MODE_STRAIGHT;
                break;
        }

        mDC = Direct2D::instance().createDeviceContext();
        if (mDC) {
            D2D1_BITMAP_PROPERTIES1 bitmapProps;
            bitmapProps.pixelFormat = mPixelFormat;
            bitmapProps.dpiX = dpi;
            bitmapProps.dpiY = dpi;
            bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;  // can use for SetTarget()
            bitmapProps.colorContext = nullptr;

            HRESULT err = mDC->CreateBitmap({ UINT32(width), UINT32(height) },
                                            nullptr, 0,
                                            bitmapProps, &mBitmap);
            if (err == S_OK) {
                mDC->SetTarget(mBitmap);
            }
            else {
                printError("Could not create bitmap (" + std::to_string(width) + "x" + std::to_string(height) + ")", err);
            }
        }
        setNativeDC(mDC);
    }

    ~Direct2DBitmap()
    {
        mDC->Release();
        mBitmap->Release();
        mBitmap = nullptr;
        if (mReadBitmap) {
            mReadBitmap->Release();
        }
        mNativeDC = nullptr;  // so Super doesn't crash
    }

    void beginDraw() override
    {
        Super::beginDraw();
        if (mReadBitmap) {
            mReadBitmap->Release();
            mReadBitmap = nullptr;
        }
    }

    void endDraw() override
    {
        Super::endDraw();
    }

    Color pixelAt(int x, int y) override
    {
        if (mDrawingState == DrawingState::kDrawing) {
            printError("DrawContext::pixelAt() called before endDraw()");
            return Color::kPurple;
        }

        HRESULT err;
        if (!mReadBitmap) {
            D2D1_BITMAP_PROPERTIES1 bitmapProps;
            bitmapProps.pixelFormat = mPixelFormat;
            bitmapProps.dpiX = dpi();
            bitmapProps.dpiY = dpi();
            bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ |
                                        D2D1_BITMAP_OPTIONS_CANNOT_DRAW;  // required for CPU_READ
            bitmapProps.colorContext = nullptr;
            err = mDC->CreateBitmap({ UINT32(width()), UINT32(height()) },
                                    nullptr, 0,
                                    bitmapProps, &mReadBitmap);
            if (err != S_OK) {
                printError("Could not create readable bitmap", err);
                return Color::kPurple;
            }
            D2D1_POINT_2U destUpperLeft = { 0, 0 };
            D2D1_RECT_U srcRect = { 0, 0, UINT32(width()), UINT32(height()) };
            mReadBitmap->CopyFromBitmap(&destUpperLeft, mBitmap, &srcRect);
        }

        D2D1_MAPPED_RECT mappedRect;
        err = mReadBitmap->Map(D2D1_MAP_OPTIONS_READ, &mappedRect);
        if (err != S_OK) {
            printError("Could not map bitmap for reading", err);
            return Color::kPurple;
        }
        unsigned char* rgba = mappedRect.bits + y * mappedRect.pitch + x * mBytesPerPixel;

        Color c;
        switch (mType) {
            case kBitmapRGBA: {
                // RGBA is premultiplied alpha
                float b = rgba[0] / 255.0f;
                float g = rgba[1] / 255.0f;
                float r = rgba[2] / 255.0f;
                float a = rgba[3] / 255.0f;
                // Prevent divide-by-zero errors. If alpha is zero, it doesn't
                // matter what rgb are (and if they are premultiplied by zero, they
                // will be zero anyway), so just pass r, g, b directly.
                if (a < 0.000001) {
                    a = 1.0;
                }
                c = Color(r / a, g / a, b / a, a);
                break;
            }
            case kBitmapRGB:  // is BGR(a)
                c = Color(int(rgba[2]), int(rgba[1]), int(rgba[0]), 255);
                break;
            case kBitmapGreyscale:
                c = Color(int(rgba[0]), int(rgba[0]), int(rgba[0]), 255);
                break;
            case kBitmapAlpha:
                c = Color(0, 0, 0, int(rgba[0]));
                break;
        }

        mReadBitmap->Unmap();
        return c;
    }
};
//--------------------------- DrawContext -------------------------------------
std::shared_ptr<DrawContext> DrawContext::fromHwnd(void* hwnd)
{
    return std::make_shared<Direct2DWindow>((HWND)hwnd);
}

std::shared_ptr<DrawContext> DrawContext::createDirect2DBitmap(BitmapType type, int width, int height,
                                                               float dpi /*= 72.0f*/)
{
    return std::make_shared<Direct2DBitmap>(type, width, height, dpi);
}

} // namespace $ND_NAMESPACE

#endif // _WIN32 || _WIN64
