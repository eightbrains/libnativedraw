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

#if defined(_WIN32) || defined(_WIN64)  // _WIN32 covers everything except 64-bit ARM
#include "nativedraw.h"

#include "nativedraw_private.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// @#$! Windows!
#undef min
#undef max
#undef DrawText
#include <comdef.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_3.h>
#include <stringapiset.h>
#include <wincodec.h>

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

// By default Direct2D graphics contexts use device-independent pixels equal to 1/96 inch.
inline FLOAT toD2D(const PicaPt& p) { return p.toPixels(96.0f); }
inline D2D1_POINT_2F toD2DPoint(const PicaPt& x, const PicaPt& y) {
    return { toD2D(x), toD2D(y) };
}
inline PicaPt fromD2D(float d2d) { return PicaPt::fromPixels(d2d, 96.0f); }

// Converts utf8 to WCHAR* using new[]. Caller must delete[].
WCHAR* newBrackets_Utf16FromUtf8(const std::string& utf8, int *length = nullptr)
{
    const int kNullTerminated = -1;
    int nCharsNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), kNullTerminated, NULL, 0);
    WCHAR* wtext = new WCHAR[nCharsNeeded + 1];  // nCharsNeeded includes \0, but +1 just in case
    wtext[0] = '\0';  // in case conversion fails
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), kNullTerminated, wtext, nCharsNeeded);
    if (length) {
        *length = nCharsNeeded;
    }
    return wtext;
}

std::string utf8FromUtf16(wchar_t *wstr)
{
    const int kNullTerminated = -1;
    int nCharsNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr, kNullTerminated,
                                           NULL, 0, NULL, NULL);
    char *str = new char[nCharsNeeded + 1];
    str[0] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, wstr, kNullTerminated,
                        str, nCharsNeeded, NULL, NULL);
    std::string utf8(str);
    delete [] str;
    return utf8;
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
    ID3D11DeviceContext* d3d11DeviceContext() { return mD3DDeviceContext; }
    IWICImagingFactory* wicFactory() { return mWicFactory; }

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

        // You'd think we could try CoCreateInstance() and if it fails being uninitialized,
        // call CoInitializeEx() and then try again. However, it seems that if you do that
        // and then start  a message loop than mWicFactory->Release() will crash.
        // To my understanding, it is always safe to call CoInitializeEx(), although it
        // might not return S_OK. The docs say that you need to call CoUnitialize() for
        // "successful call made to CoInitialze[Ex](), including any call that returns S_FALSE".
        // It's unclear to me what an unsuccesful call would be?!
        CoInitialize(0);
        err = CoCreateInstance(CLSID_WICImagingFactory,
                               NULL,
                               CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&mWicFactory));
        if (err != S_OK) {
            printError("fatal: Could not create WICImagingFactory!", err);
            return;
        }

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
                                D3D11_CREATE_DEVICE_SINGLETHREADED    // better performance
                                  | D3D11_CREATE_DEVICE_BGRA_SUPPORT  // required for Direct2D
                                //  | D3D11_CREATE_DEVICE_DEBUG
                                , featureLevels,
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
        if (mWicFactory) { mWicFactory->Release(); }

        // The docs say that you need to call CoUnitialize() for every
        // "successful call made to CoInitialze[Ex](), including any call that
        // returns S_FALSE". It's unclear to me what an unsuccesful call would be,
        // then, so always call it.
        CoUninitialize();
    }

private:
    static std::unique_ptr<Direct2D> gInstance;
    IWICImagingFactory *mWicFactory = nullptr;
    ID2D1Factory1* mD2DFactory = nullptr;
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
            if (isFilled) {
                sink->SetFillMode(D2D1_FILL_MODE_WINDING);  // D2D defaults to D2D1_FILL_MODE_ALTERNATE
            }

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
//-------------------------------- Font ---------------------------------------
namespace {
int CALLBACK fontFamilyCallback(const LOGFONTW *logFont,
                                const TEXTMETRICW *tm,
                                DWORD  fontType,
                                LPARAM lparam)
{
    auto *fonts = (std::set<std::string>*)lparam;
    auto *info = (ENUMLOGFONTEXW*)logFont;
    auto name = utf8FromUtf16(info->elfFullName);
    bool isScalable = (info->elfLogFont.lfOutPrecision == OUT_TT_PRECIS ||
                       info->elfLogFont.lfOutPrecision == OUT_STROKE_PRECIS);
    bool isVertical = (!name.empty() && name[0] == '@');
    bool isDOSFont = (info->elfLogFont.lfCharSet == OEM_CHARSET);
    // Windows 10 has some icon fonts that only use the private Unicode characters;
    // these fonts cannot even display their own names, so do not include in a font
    // enumeration. However, creating the font directly by name will still work.
    bool isWindowsPrivateIcons = (name.find(" MDL2 Assets") != std::string::npos);
    if (isScalable && !isVertical && !isDOSFont && !isWindowsPrivateIcons) {
        fonts->insert(name);
    }
    return 1;  // non-zero continues enumeration
}
} // namespace

std::vector<std::string> Font::availableFontFamilies()
{
    // Using DEFAULT_CHARSET, the callback will be called for all the charsets a font supports.
    // We cannot just pick a charset (how do we know the user wants that one), but we do not
    // want duplicate names.
    std::set<std::string> fonts;
    LOGFONTW specs;
    specs.lfHeight = 0;
    specs.lfWidth = 0;
    specs.lfEscapement = 0;  // tenths of a degree
    specs.lfOrientation = 0;  // docs say should be same value as lfEscapement
    specs.lfWeight = FW_DONTCARE; // aka 0
    specs.lfItalic = FALSE;
    specs.lfUnderline = FALSE;
    specs.lfStrikeOut = FALSE;
    specs.lfCharSet = DEFAULT_CHARSET;
    specs.lfOutPrecision = OUT_DEFAULT_PRECIS;  // how closely the fonts must match the spec
    specs.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    specs.lfQuality = DEFAULT_QUALITY;
    specs.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    specs.lfFaceName[0] = NULL;
    EnumFontFamiliesExW(GetDC(NULL), &specs, fontFamilyCallback, (LPARAM)&fonts, 0 /*must be zero*/);

    std::vector<std::string> sortedFonts;
    sortedFonts.reserve(fonts.size());
    sortedFonts.insert(sortedFonts.begin(), fonts.begin(), fonts.end());
    std::sort(sortedFonts.begin(), sortedFonts.end());
    return sortedFonts;
}

//------------------------------ Gradients ------------------------------------
namespace {

class Direct2DGradient;
static std::unordered_map<Gradient::Id, Direct2DGradient*> gGradientId2Gradient;

class Direct2DGradient : public Gradient
{
public:
    Direct2DGradient(const GradientInfo& info, bool makeValid = true)
        : mInfo(info)
    {
        if (makeValid) {
            mId = gNextId++;
            assert(gGradientId2Gradient.find(mId) == gGradientId2Gradient.end());
            gGradientId2Gradient[mId] = this;
        } else {
            mId = 0;
            setInvalid();
        }
    }

    ~Direct2DGradient()
    {
        auto it = gGradientId2Gradient.find(mId);
        if (it != gGradientId2Gradient.end()) {
            gGradientId2Gradient.erase(it);
        }

        if (mLinear) {
            mLinear->Release();
            mLinear = nullptr;
        }
        for (auto &it : mRadials) {
            if (it.second) {
                it.second->Release();
            }
        }
        mRadials.clear();
    }

    bool isValid() const override { return ((!mLinearFailed || !mRadialFailed) && mId != 0); }

    Id id() const override { return mId; }

    const GradientInfo& info() const { return mInfo; }

    void setInvalid()
    {
        mLinear = nullptr;
        mLinearFailed = true;
        mRadialFailed = true;
    }

    ID2D1LinearGradientBrush* linearBrush()
    {
        if (!mLinear && !mLinearFailed) {
            auto *dc = (ID2D1DeviceContext*)mInfo.context->nativeDC();
            auto *stopCollection = createStops(dc, mInfo.stops);
            HRESULT err = dc->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(1.0f, 0.0f)),
                stopCollection, &mLinear);
            assert(err == S_OK);
            if (err != S_OK) {
                mLinearFailed = true;
            }
            stopCollection->Release();
        }
        return mLinear;
    }

    ID2D1RadialGradientBrush* radialBrush(float startRadius)
    {
        // Direct2D has limited radial gradients compared to macOS and Cairo.
        // Although we cannot implement independent start/end points with their own radii,
        // we can at least implement a non-zero start radius by adjusting the locations.

        // Quantize the float so that floating point error doesn't an entirely
        // new brush (and presumably, bitmap). In reality this probably isn't necessary,
        // since most gradients are probably going to be a constant value, but just in case.
        const float kQuantizations = 8192.0f;
        assert(startRadius >= 0.0f && startRadius <= 1.0f);
        uint16_t quantized = uint16_t(std::round(startRadius * kQuantizations));
        startRadius = float(quantized) / kQuantizations;

        auto it = mRadials.find(startRadius);
        if (it == mRadials.end() && !mRadialFailed) {
            auto stops = mInfo.stops;  // copies
            float newWidth = 1.0f - startRadius;
            for (auto& s : stops) {
                s.location = startRadius + s.location * newWidth;
            }

            auto* dc = (ID2D1DeviceContext*)mInfo.context->nativeDC();
            auto* stopCollection = createStops(dc, stops);
            ID2D1RadialGradientBrush* brush = nullptr;
            HRESULT err = dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
                D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, 0.0f), 1.0f, 1.0f),
                stopCollection, &brush);
            assert(err == S_OK);
            if (err != S_OK) {
                mRadialFailed = true;
            }
            stopCollection->Release();
            mRadials[startRadius] = brush;
            it = mRadials.find(startRadius);
        }
        return it->second;
    }

private:
    ID2D1GradientStopCollection* createStops(ID2D1DeviceContext* context,
                                             const std::vector<Gradient::Stop>& stops)
    {
        std::vector<D2D1_GRADIENT_STOP> d2dStops(stops.size());
        for (size_t i = 0; i < stops.size(); ++i) {
            d2dStops[i].color.r = stops[i].color.red();
            d2dStops[i].color.g = stops[i].color.green();
            d2dStops[i].color.b = stops[i].color.blue();
            d2dStops[i].color.a = stops[i].color.alpha();
            d2dStops[i].position = stops[i].location;
        }
        ID2D1GradientStopCollection* stopCollection = nullptr;
        // We can use an overloaded call to specify more details and get a ID2D1GradientStopCollection1
        // if we need.
        HRESULT err = context->CreateGradientStopCollection(d2dStops.data(), (UINT32)d2dStops.size(), &stopCollection);
        assert(err == S_OK);
        return stopCollection;
    }

private:
    static Id gNextId;

    Id mId;
    GradientInfo mInfo;
    ID2D1LinearGradientBrush* mLinear = nullptr;
    std::unordered_map<float, ID2D1RadialGradientBrush*> mRadials;
    bool mLinearFailed = false;
    bool mRadialFailed = false;
};
Gradient::Id Direct2DGradient::gNextId = 1;

Direct2DGradient* createGradient(const GradientInfo& info, float /*dpi*/)
{
    return new Direct2DGradient(info);
}

void destroyGradient(Direct2DGradient* gradient)
{
    delete gradient;
}

static ResourceManager<GradientInfo, Direct2DGradient*> gGradientMgr(createGradient, destroyGradient);

} // namepsace
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

// DirectWrite makes us set the font in a run by name, so to avoid converting
// all the time, cache the UTF16 family name. Make this a class, so it will
// self-destruct on exit and not show up as leak-noise in leak-checkers.
class UTF16Cache
{
public:
    ~UTF16Cache()
    {
        for (auto& kv : m8to16) {
            delete [] kv.second.utf16;
            kv.second.utf16 = nullptr;  // just in case
        }
    }

    // This pointer is essentially a reference, does not transfer ownership to caller
    WCHAR* get(const std::string& utf8, int *length = nullptr)
    {
        auto it = m8to16.find(utf8);
        if (it == m8to16.end()) {
            Utf16Str s;
            s.utf16 = newBrackets_Utf16FromUtf8(utf8, &s.length);  // calls new[]
            m8to16[utf8] = s;
            it = m8to16.find(utf8);
        }
        return it->second.utf16;
    }

private:
    struct Utf16Str {
        WCHAR *utf16;
        int length;
    };
    std::map<std::string, Utf16Str> m8to16;
};
static UTF16Cache gUTF16Cache;

struct FontInfo
{
    IDWriteTextFormat *format;
    Font::Metrics metrics;
    FLOAT d2dSize;
    DWRITE_FONT_STYLE d2dStyle;
    DWRITE_FONT_WEIGHT d2dWeight;
    DWRITE_FONT_STRETCH d2dStretch;

    FontInfo() : format(nullptr) {}  // for STL containers
    FontInfo(IDWriteTextFormat* f, const Font::Metrics& fm,
             FLOAT sz, DWRITE_FONT_STYLE s, DWRITE_FONT_WEIGHT w, DWRITE_FONT_STRETCH fs)
        : format(f), metrics(fm), d2dSize(sz), d2dStyle(s), d2dWeight(w), d2dStretch(fs)
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
    IDWriteFactory* factory = Direct2D::instance().writeFactory();

    IDWriteFontCollection* collection;
    factory->GetSystemFontCollection(&collection, true);

    FLOAT d2dSize = toD2D(desc.pointSize());
    DWRITE_FONT_WEIGHT d2dWeight = gWeightToD2D[desc.weight()];
    DWRITE_FONT_STYLE d2dStyle = ((desc.style() & kStyleItalic) ? DWRITE_FONT_STYLE_ITALIC
        : DWRITE_FONT_STYLE_NORMAL);
    DWRITE_FONT_STRETCH d2dStretch = DWRITE_FONT_STRETCH_NORMAL;
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    IDWriteTextFormat* format = nullptr;

    BOOL exists = false;
    UINT32 idx;
    // EnumFontFamilies[Ex]() and D2D's FindFamilyName() disagree over what constitutes a
    // family name. The former lists things like "Arial Black" and "Arial Semilight", but
    // the latter expects just "Arial" with the appropriate weight/style. So detect that
    // and substitute accordingly. (Google Chrome also used this fix in Blink)
    std::string fontFamily = desc.family();
    for (int tryNo = 0; tryNo < 2; ++tryNo) {
        int nCharsNeeded = 0;
        WCHAR* family = gUTF16Cache.get(fontFamily, &nCharsNeeded);
        collection->FindFamilyName(family, &idx, &exists);
        if (exists) {
            break;
        }
        else if (tryNo == 0) {
            struct WeightSubstitution {
                std::string text;
                DWRITE_FONT_WEIGHT weight;
            };
            struct StretchSubstitution {
                std::string text;
                DWRITE_FONT_STRETCH stretch;
            };
            static std::vector<WeightSubstitution> weightSubstitutions = {
                { " thin", DWRITE_FONT_WEIGHT_THIN }, // 100
                { " extralight", DWRITE_FONT_WEIGHT_EXTRA_LIGHT }, // 200
                { " ultralight", DWRITE_FONT_WEIGHT_ULTRA_LIGHT }, // 200
                { " light", DWRITE_FONT_WEIGHT_LIGHT }, // 300
                { " semilight", DWRITE_FONT_WEIGHT_SEMI_LIGHT }, // 350
                { " normal", DWRITE_FONT_WEIGHT_NORMAL }, // 400
                { " regular", DWRITE_FONT_WEIGHT_REGULAR }, // 400
                { " medium", DWRITE_FONT_WEIGHT_MEDIUM }, // 500
                { " demibold", DWRITE_FONT_WEIGHT_DEMI_BOLD }, // 600
                { " semibold", DWRITE_FONT_WEIGHT_SEMI_BOLD }, // 600
                { " bold", DWRITE_FONT_WEIGHT_BOLD }, // 700
                { " extrabold", DWRITE_FONT_WEIGHT_EXTRA_BOLD }, // 800
                { " ultrabold", DWRITE_FONT_WEIGHT_ULTRA_BOLD }, // 800
                { " black", DWRITE_FONT_WEIGHT_BLACK }, // 900
                { " heavy", DWRITE_FONT_WEIGHT_HEAVY }, // 900
                { " extrablack", DWRITE_FONT_WEIGHT_EXTRA_BLACK }, // 950
                { " ultrablack", DWRITE_FONT_WEIGHT_ULTRA_BLACK }, // 950
            };
            static std::vector<StretchSubstitution> stretchSubstitutions = {
                { " ultracondensed", DWRITE_FONT_STRETCH_ULTRA_CONDENSED }, // 1
                { " extracondensed", DWRITE_FONT_STRETCH_EXTRA_CONDENSED }, // 2
                { " condensed", DWRITE_FONT_STRETCH_CONDENSED }, // 3
                { " semicondensed", DWRITE_FONT_STRETCH_SEMI_CONDENSED }, // 4
                { " semiexpanded", DWRITE_FONT_STRETCH_SEMI_EXPANDED }, // 6
                { " expanded", DWRITE_FONT_STRETCH_EXPANDED }, // 7
                { " extraexpanded", DWRITE_FONT_STRETCH_EXTRA_EXPANDED }, // 8
                { " ultraexpanded", DWRITE_FONT_STRETCH_ULTRA_EXPANDED }, // 9
            };
            // tolower: we specifically only want to do ASCII (although the D2D functions
            // are case insensitive), but unclear what std::tolower() does if locale is not "C".
            for (auto& c : fontFamily) {
                if (c >= 'A' && c <= 'Z') {
                    c += 32;
                }
            }
            // Try font weight substitutions
            for (const auto& sub : weightSubstitutions) {
                auto idx = fontFamily.find(sub.text);
                if (idx != std::string::npos) {
                    fontFamily.replace(idx, sub.text.size(), "");
                    d2dWeight = sub.weight;
                }
            }
            // Try font stretch substitutions
            for (const auto& sub : stretchSubstitutions) {
                auto idx = fontFamily.find(sub.text);
                if (idx != std::string::npos) {
                    fontFamily.replace(idx, sub.text.size(), "");
                    d2dStretch = sub.stretch;
                }
            }

            // Ok, try the new font in the next iteration. If it still does not work,
            // then it really must not exist. (Do not substitute a known font; a non-existant
            // font is supposed to return nothing by contract.)
        }
    }

    int nCharsNeeded;
    WCHAR* family = gUTF16Cache.get(fontFamily, &nCharsNeeded);  // this is a ref, do not delete
    HRESULT err = factory->CreateTextFormat(family, collection,
                                            d2dWeight, d2dStyle, d2dStretch,
                                            toD2D(desc.pointSize()), locale,
                                            &format);
    if (err != S_OK) {
        // Using a non-existant family name does not cause an error, this will
        // only happen for parameter errors (which would be a programming error).
        printError("Error calling CreateTextFormat()", err);
    }

    Font::Metrics metrics;
    metrics.ascent = PicaPt::kZero;
    metrics.capHeight = PicaPt::kZero;
    metrics.descent = PicaPt::kZero;
    metrics.leading = PicaPt::kZero;
    metrics.lineHeight = PicaPt::kZero;
    metrics.xHeight = PicaPt::kZero;
    metrics.underlineOffset = PicaPt::kZero;
    metrics.underlineThickness = PicaPt::kZero;
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
                metrics.underlineOffset = PicaPt::fromPixels(float(-d2dMetrics.underlinePosition) * scaling, 96.0f);
                metrics.underlineThickness = PicaPt::fromPixels(float(d2dMetrics.underlineThickness) * scaling, 96.0f);
                font->Release();
            }
            fontFamily->Release();
        }
    }
    collection->Release();
    metrics.lineHeight = metrics.ascent + metrics.descent + metrics.leading;
    return FontInfo(format, metrics, d2dSize, d2dStyle, d2dWeight, d2dStretch);
}

void destroyFont(FontInfo rsrc)
{
    rsrc.format->Release();
}

static ResourceManager<Font, FontInfo> gFontMgr(createFont, destroyFont);

} // namespace

//---------------------- Custom text renderer ---------------------------------
class COMifiedIndex : public IUnknown
{
public:
    explicit COMifiedIndex(int index) : mRef(0), mIndex(index) {}
    ~COMifiedIndex() {}

    int value() const { return mIndex;  }

    HRESULT COMifiedIndex::QueryInterface(REFIID riid, LPVOID *ppvObj)
    {
        if (!ppvObj) {
           return E_INVALIDARG;
        }
        *ppvObj = NULL;
        if (riid == IID_IUnknown) {
            *ppvObj = (LPVOID)this;
            AddRef();
            return NOERROR;
        }
        return E_NOINTERFACE;
    }

    ULONG COMifiedIndex::AddRef()
    {
        InterlockedIncrement(&mRef);
        return mRef;
    }

    ULONG COMifiedIndex::Release()
    {
        ULONG ulRefCount = InterlockedDecrement(&mRef);
        if (0 == mRef) {
            delete this;
        }
        return ulRefCount;
    }

private:
    ULONG mRef;
    int mIndex;
};
// Direct2D's IDWriteTextLayout expects us to allocate an object for every
// text run to describe what needs to happen. We'll get much better cache
// locality and get clearer ownership understanding if we do not new() objects
// all over the heap. Instead, we're doing something similar to what we do
// for Cairo, which is that we create a little rendering program for each
// run. (Cairo does it for the whole text, but since D2D sends us the glyphs
// in callbacks, we have to compromise.) We execute commands until we get
// to kDone, which tells us to stop and return from the D2D callback function.
class DrawRunCommands
{
public:
    enum Cmd {
        kSetFG, kDrawRect, kDrawText, kStrokedText,
        kStroke, kDoubleStroke, kDottedStroke, kWavyStroke, kDone
    };
    // The idea is to keep this fairly small. On 64-bit systems the glyph
    // pointer will be the largest, and we can fit two floats into 64-bits.
    // It would be more convenient to have strokes and rects have the four
    // floats they need rather than have to assume another item in the draw
    // stack, but this way reduces memory.
    struct Command {
        Cmd cmd;
        union {
            int rgba;
            struct {
                float ascent;
                float descent;
            } rect;
            struct {
                float dy;  // change from baseline
                float w;
            } stroke;
            struct {
                float baselineOffset;  // +ve is down
            } text;
        } arg;
    };

    DrawRunCommands(float dpi) : mDPI(dpi) {}

    size_t size() const { return mCmds.size(); }

    void addColor(int rgba)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kSetFG;
        mCmds.back().arg.rgba = rgba;
    }

    void addRect(float d2dAscent, float d2dDescent)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kDrawRect;
        mCmds.back().arg.rect.ascent = d2dAscent;
        mCmds.back().arg.rect.descent = d2dDescent;
    }

    void addText(float d2dBaselineOffset)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kDrawText;
        mCmds.back().arg.text.baselineOffset = d2dBaselineOffset;
    }

    void addStrokedText(float d2dThickness, float d2dBaselineOffset)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kStrokedText;
        mCmds.back().arg.stroke.w = d2dThickness;
        mCmds.emplace_back();
        mCmds.back().cmd = kDrawText;
        mCmds.back().arg.text.baselineOffset = d2dBaselineOffset;
    }

    void addLine(Cmd type, float d2dDeltaY, float d2dStrokeWidth)
    {
        mCmds.emplace_back();
        mCmds.back().cmd = type;
        mCmds.back().arg.stroke.dy = d2dDeltaY;
        float widthPx = std::max(1.0f, fromD2D(d2dStrokeWidth).toPixels(mDPI));
        mCmds.back().arg.stroke.w = toD2D(PicaPt::fromPixels(widthPx, mDPI));
    }

    void addDone()
    {
        mCmds.emplace_back();
        mCmds.back().cmd = kDone;
    }

    void drawRun(ID2D1RenderTarget *gc, D2D1::Matrix3x2F &matrix,
                 ID2D1PathGeometry *glyphs, ID2D1SolidColorBrush *brush,
                 float width, int startIdx) const
    {
        // Note that (0, 0) is at left of baseline
        // Also note that any offsets from TextObj have already been applied
        // (otherwise the text would not be in the right place), and since
        // all the calculations are done relative to the baseline, the offset
        // is unnecessary.
        const float originX = 0.0f;
        const float originY = 0.0f;
#if kDebugDraw
        std::cout << "[debug] draw()" << std::endl;
#endif
        bool done = false;
        size_t i = size_t(startIdx);
        while (i < mCmds.size() && !done) {
            auto& cmd = mCmds[i];
            ++i;
            switch (cmd.cmd) {
            case kSetFG: {
                float r = float((cmd.arg.rgba & 0xff000000) >> 24) / 255.0f;
                float g = float((cmd.arg.rgba & 0x00ff0000) >> 16) / 255.0f;
                float b = float((cmd.arg.rgba & 0x0000ff00) >> 8) / 255.0f;
                float a = float(cmd.arg.rgba & 0x000000ff) / 255.0f;
                brush->SetColor({ r, g, b, a });
#if kDebugDraw
                std::cout << "[debug]   set fg: " << r << ", " << g << ", "
                          << b << ", " << a << std::endl;
#endif
                break;
            }
            case kDrawRect: {
                D2D1_RECT_F r{ originX, originY - cmd.arg.rect.ascent,
                               originX + width, originY + cmd.arg.rect.descent };
                gc->FillRectangle(r, brush);
#if kDebugDraw
                std::cout << "[debug]   draw rect: " << origin.arg.pt.x
                          << ", " << origin.arg.pt.y << ", "
                          << size.arg.size.w << ", " << size.arg.size.h
                          << std::endl;
#endif
                break;
            }
            case kStroke:
            case kDoubleStroke:
            case kDottedStroke: {
                float d2dX0 = originX;
                float d2dX1 = originX + width;
                float d2dY = originY + cmd.arg.stroke.dy;
                float strokeWidthPx = fromD2D(cmd.arg.stroke.w).toPixels(mDPI);
                if (strokeWidthPx < 1.5f && strokeWidthPx > 0.75f) {
                    float yPx = fromD2D(d2dY).toPixels(mDPI);
                    yPx = std::round(yPx) + 0.5f;
                    d2dY = toD2D(PicaPt::fromPixels(yPx, mDPI));
                }
#if kDebugDraw
                std::cout << "[debug]   line: ("
                          << d2dX0 << ", " << d2dY << ") - (" << d2dX1 << ", " << d2dY << ")"
                          << std::endl;
#endif
                ID2D1StrokeStyle* strokeStyle = nullptr;  // nullptr = solid stroke
                auto path = std::make_shared<Direct2DPath>();
                auto y = fromD2D(d2dY);
                path->moveTo(Point(fromD2D(d2dX0), y));
                path->lineTo(Point(fromD2D(d2dX1), y));
                if (cmd.cmd == kDoubleStroke) {
                    float yPx = (y + 2.0f * fromD2D(cmd.arg.stroke.w)).toPixels(mDPI);
                    yPx = std::round(yPx) + 0.5f;
                    y = PicaPt::fromPixels(yPx, mDPI);
                    path->moveTo(Point(fromD2D(d2dX0), y));
                    path->lineTo(Point(fromD2D(d2dX1), y));
                } else if (cmd.cmd == kDottedStroke) {
                    // We will create the style here since dotted underlines are rather
                    // rare (I only expect them when inputting CJK text), so no reason to
                    // force the object to store a 64-bit pointer just so we can pre-allocate
                    // in addLine(). I expect we will have lots of text lying around, at least
                    // in user interfaces.
                    float dotLenPx = std::round(PicaPt(2.0f).toPixels(mDPI));
                    FLOAT d2dDotLen = toD2D(PicaPt::fromPixels(dotLenPx, mDPI));
                    FLOAT pattern[] = { d2dDotLen, d2dDotLen };
                    FLOAT offset = originX / d2dDotLen;
                    offset = offset - std::floor(offset);
                    auto strokeProperties = D2D1::StrokeStyleProperties();
                    strokeProperties.dashStyle = D2D1_DASH_STYLE_CUSTOM;
                    strokeProperties.dashOffset = -offset;
                    auto* d2d = Direct2D::instance().factory();
                    HRESULT err = d2d->CreateStrokeStyle(strokeProperties, pattern,
                                                         sizeof(pattern) / sizeof(FLOAT), &strokeStyle);
                    if (err != S_OK) {
                        printError("Could not create dotted underline stroke", err);
                        strokeStyle = nullptr;  // fallback to solid
                    }
                }
                gc->DrawGeometry((ID2D1PathGeometry*)path->nativePathForDPI(mDPI, false),
                                 brush, cmd.arg.stroke.w, strokeStyle);
                if (strokeStyle) {
                    strokeStyle->Release();
                }
                break;
            }
            case kWavyStroke: {
                float x0 = originX;
                float y = originY + cmd.arg.stroke.dy;
                float x1 = originX + width;
#if kDebugDraw
                std::cout << "[debug]   wavy line: ("
                          << x0 << ", " << y << ") - (" << x1 << ", " << y << ")" << std::endl;
#endif
                auto pts = createWavyLinePoints(x0, y, x1, cmd.arg.stroke.w);

                auto path = std::make_shared<Direct2DPath>();
                path->moveTo(Point(fromD2D(pts[0]), fromD2D(pts[1])));
                for (size_t i = 2; i < pts.size(); i += 2) {
                    path->lineTo(Point(fromD2D(pts[i]), fromD2D(pts[i + 1])));
                }
                gc->DrawGeometry((ID2D1PathGeometry*)path->nativePathForDPI(mDPI, false),
                                 brush, cmd.arg.stroke.w, nullptr /*= solid stroke*/);
                break;
            }
            case kDrawText: {
                if (cmd.arg.text.baselineOffset == 0.0f) {
                    gc->FillGeometry(glyphs, brush);
                } else {
                    D2D1::Matrix3x2F newM;
                    newM.SetProduct(D2D1::Matrix3x2F::Translation(D2D1::SizeF(0.0f, cmd.arg.text.baselineOffset)), matrix);
                    gc->SetTransform(newM);
                    gc->FillGeometry(glyphs, brush);
                    gc->SetTransform(matrix);
                }
#if kDebugDraw
                std::cout << "[debug]   draw text" << std::endl;
#endif
                break;
            }
            case kStrokedText: {
                auto text = mCmds[i++];
                if (cmd.arg.text.baselineOffset == 0.0f) {
                    gc->DrawGeometry(glyphs, brush, cmd.arg.stroke.w, nullptr /*= solid stroke*/);
                }
                else {
                    D2D1::Matrix3x2F newM;
                    newM.SetProduct(D2D1::Matrix3x2F::Translation(D2D1::SizeF(0.0f, cmd.arg.text.baselineOffset)), matrix);
                    gc->SetTransform(newM);
                    gc->DrawGeometry(glyphs, brush, cmd.arg.stroke.w, nullptr /*= solid stroke*/);
                    gc->SetTransform(matrix);
                }
#if kDebugDraw
                std::cout << "[debug]   stroke text: " << std::endl;
#endif
                break;
            }
            case kDone:
                done = true;
                break;
            }
        }

#if kDebugDraw
        std::cout << "[debug] done drawing" << std::endl;
#endif
    }

private:
    std::vector<Command> mCmds;
    float mDPI;
};

// Note:  to make sure that AddRef() and Release() work right, this should
//        be created with new(), NOT on the stack!. The new object will have
//        the refcount already at 1, so you will need to call Release().
class BaseTextRenderer : public IDWriteTextRenderer
{
public:
    virtual ~BaseTextRenderer() {}

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
        // We want to do the snapping ourselves, since Windows does not do it
        // in a predictable, undoable fashion.
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
    size_t mRefCount = 1;
};

struct RunStyle
{
    int fgRGBA;
    int bgRGBA;

};

// We need our own renderer so that we can draw outlines
class CustomTextRenderer : public BaseTextRenderer
{
public:
    // Note:  to make sure that AddRef() and Release() work right, this should
    //        be created with new(), NOT on the stack!. The new object will have
    //        the refcount already at 1, so you will need to call Release().
    CustomTextRenderer(D2D1::Matrix3x2F& matrix, ID2D1SolidColorBrush *solidBrush,
                       const DrawRunCommands& drawCmds)
        : mMatrix(matrix), mSolid(solidBrush), mDrawCmds(drawCmds)
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
        if (!glyphRun || glyphRun->glyphCount == 0) {
            return S_OK;
        }

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

        int startIndex = ((COMifiedIndex*)clientDrawingEffect)->value();
        int nGlyphs = glyphRun->glyphCount;
        FLOAT width;
        FLOAT dir = ((glyphRun->bidiLevel & 0x1) == 0) ? 1.0f : -1.0f;
        width = 0.0f;
        for (int i = 0;  i < nGlyphs;  ++i) {
            width += dir * glyphRun->glyphAdvances[i];
        }
        mDrawCmds.drawRun(gc, runMatrix, geometry, mSolid, width, startIndex);

        gc->SetTransform(mMatrix);

        geometry->Release();
        sink->Release();

        return err;
    }

protected:
    D2D1::Matrix3x2F mMatrix;
    ID2D1SolidColorBrush *mSolid;
    const DrawRunCommands& mDrawCmds;
    size_t mRefCount = 1;
};

//--------------------------- Text layout object ------------------------------
class GlyphGetter : public BaseTextRenderer
{
public:
    // Note:  to make sure that AddRef() and Release() work right, this should
    //        be created with new(), NOT on the stack!. The new object will have
    //        the refcount already at 1, so you will need to call Release().
    GlyphGetter(float dpi, float d2dOffsetY, const std::vector<int>& utf16To8,
                DWRITE_LINE_METRICS lineMetrics[],
                std::vector<TextLayout::Glyph> *glyphs)
        : mDPI(dpi), mD2DOffsetY(d2dOffsetY), mUTF16To8(utf16To8), mLineMetrics(lineMetrics)
        , mGlyphs(glyphs)
    {}

    ~GlyphGetter()
    {
    }

    STDMETHOD(DrawGlyphRun)(void *drawContext,
                            FLOAT baselineOriginX,
                            FLOAT baselineOriginY,
                            DWRITE_MEASURING_MODE measuringMode,
                            DWRITE_GLYPH_RUN const *glyphRun,
                            DWRITE_GLYPH_RUN_DESCRIPTION const *glyphRunDesc,
                            IUnknown* clientDrawingEffect)
    {
        FLOAT d2dFmAscent = 0.0f;
        FLOAT d2dHeight = glyphRun->fontEmSize;
        if (glyphRun->fontFace) {  // this is marked _Notnull_, but just in case
            DWRITE_FONT_METRICS d2dMetrics;
            glyphRun->fontFace->GetMetrics(&d2dMetrics);
            FLOAT scaling = glyphRun->fontEmSize / (float)d2dMetrics.designUnitsPerEm;
            d2dFmAscent = d2dMetrics.ascent * scaling;
            d2dHeight = (d2dMetrics.ascent + d2dMetrics.descent) * scaling;
        }

        auto addGlyph = [this, baselineOriginX, baselineOriginY, glyphRun, d2dFmAscent, d2dHeight]
                        (int utf16idx, DWRITE_GLYPH_OFFSET const *glyphOffset, FLOAT glyphAdvance, FLOAT *advance) {
            FLOAT x, y;
            FLOAT dir = ((glyphRun->bidiLevel & 0x1) == 0) ? 1.0f : -1.0f;
            FLOAT w, h;
            if (!glyphRun->isSideways) {
                x = baselineOriginX + (*advance);
                y = baselineOriginY - d2dFmAscent + mD2DOffsetY;
                if (glyphOffset) {
                    x += dir * glyphOffset->advanceOffset;
                    y -= glyphOffset->ascenderOffset;
                }
                w = glyphAdvance;
                h = d2dHeight;
            } else {
                x = baselineOriginX - d2dFmAscent + mD2DOffsetY;
                y = baselineOriginY + (*advance);
                if (glyphOffset) {
                    y += dir * glyphOffset->advanceOffset;
                    x -= glyphOffset->ascenderOffset;
                }
                w = d2dHeight;
                h = glyphAdvance;
            }
            (*advance) += dir * glyphAdvance;
            Rect r(fromD2D(x), fromD2D(y), fromD2D(w), fromD2D(h));
            int utf8idx = mUTF16To8[utf16idx];
            if (!mGlyphs->empty()) {
                mGlyphs->back().indexOfNext = utf8idx;
            }
            mGlyphs->emplace_back(utf8idx, mLineNo, r);
        };

        if (baselineOriginY > mLastBaselineY) {
            mLineNo += 1;
            mLastBaselineY = baselineOriginY;
        }

        mGlyphs->reserve(mGlyphs->size() + glyphRun->glyphCount);
        FLOAT advance = FLOAT(0.0);
        for (unsigned int i = 0;  i < glyphRun->glyphCount;  ++i) {
            int utf16idx = glyphRunDesc->textPosition + glyphRunDesc->clusterMap[i];
            auto *glyphOffset = (glyphRun->glyphOffsets ? &glyphRun->glyphOffsets[i] : nullptr);
            addGlyph(utf16idx, glyphOffset, glyphRun->glyphAdvances[i], &advance);
        }
        if (glyphRun->glyphCount == 0) {  // this is a \n
            auto *glyphOffset = (glyphRun->glyphOffsets ? &glyphRun->glyphOffsets[0] : nullptr);
            addGlyph(glyphRunDesc->textPosition, glyphOffset, glyphRun->glyphAdvances[0], &advance);
        }

        if (!mGlyphs->empty()) {
            mGlyphs->back().indexOfNext = glyphRunDesc->textPosition + glyphRunDesc->stringLength;
        }

        return NOERROR;
    }

protected:
    float mDPI;
    float mD2DOffsetY;
    FLOAT mLastBaselineY = -1e9f;
    int mLineNo = -1;
    const std::vector<int> &mUTF16To8;
    DWRITE_LINE_METRICS *mLineMetrics;
    std::vector<TextLayout::Glyph> *mGlyphs;
};

class TextObj : public TextLayout
{
public:
    TextObj(const DrawContext& dc, const Text& text,
            const Size& size, int alignment,
            TextWrapping wordWrap,
            Font defaultReplacementFont = kDefaultReplacementFont,
            const Color& defaultReplacementColor = kDefaultReplacementColor)
        : mDraw(dc.dpi())
    {
        mDPI = dc.dpi();
        mEmptyLastLine = (text.text().empty() || text.text().back() == '\n');

        // In case anyone passes Font() to defaultReplacement font, replace with kDefaultFont
        if (defaultReplacementFont.family().empty()) {
            auto pointSize = defaultReplacementFont.pointSize();
            if (pointSize > PicaPt::kZero) {
                defaultReplacementFont = kDefaultReplacementFont.fontWithPointSize(pointSize);
            } else {
                defaultReplacementFont = kDefaultReplacementFont;
            }
        }

        // Convert from UTF8 -> WCHAR
        int nCharsNeeded = 0;
        WCHAR *wtext = newBrackets_Utf16FromUtf8(text.text(), &nCharsNeeded);  // calls new[]

        // We might need to get the glyphs later. This will mean either
        // copying the char* now (ugh) so we can use it later, or generating
        // the utf16 -> utf8 mappings. Copying the char* will require two scans,
        // so we're just going to make the mapping.
        mUTF16To8 = utf8IndicesForUTF16Indices(text.text().c_str());
        assert(mUTF16To8.size() == nCharsNeeded);  // might fail on invalid UTF-8

        // Set alignment
        auto *format = gFontMgr.get(defaultReplacementFont, mDPI).format;
        switch (alignment & Alignment::kHorizMask) {
            case Alignment::kLeft:
                format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                break;
            case Alignment::kHCenter:
                format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                break;
            case Alignment::kRight:
                format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                break;
        }
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR); // top
        if (size.width == PicaPt::kZero || wordWrap == kWrapNone) {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        } else {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }

        // Create the text layout
        float w = 10000.0f;
        if (size.width > PicaPt::kZero) {
            w = toD2D(size.width);
        }
        HRESULT err = Direct2D::instance().writeFactory()
                              ->CreateTextLayout(wtext, nCharsNeeded - 1, // don't pass the \0
                                                 format, w, 10000.0f, &mLayout);
        delete [] wtext;

        if (text.lineHeightMultiple() > 0.0f) {
            IDWriteTextLayout3* layout3 = nullptr;
            err = mLayout->QueryInterface(IID_PPV_ARGS(&layout3));
            if (err == S_OK) {
                DWRITE_LINE_SPACING opts;
                opts.method = DWRITE_LINE_SPACING_METHOD_PROPORTIONAL;
                opts.height = text.lineHeightMultiple();
                opts.baseline = text.lineHeightMultiple();
                opts.leadingBefore = 0.0f;
                opts.fontLineGapUsage = DWRITE_FONT_LINE_GAP_USAGE_ENABLED;  // use font's leading
                layout3->SetLineSpacing(&opts);
                layout3->Release();
            }
        }

        // Set the text runs
        int currentColor = 0;  // transparent: pretty much guaranteed to force fg to be set
        Font::Metrics currentMetrics;
        std::vector<Font::Metrics> runMetrics;
        runMetrics.reserve(text.runs().size());
        auto utf8to16 = utf16IndicesForUTF8Indices(text.text().c_str());
        for (size_t i = 0; i < text.runs().size(); ++i) {
            auto& run = text.runs()[i];
            assert(run.font.isSet);
            assert(run.color.isSet);
            DWRITE_TEXT_RANGE range = {
                UINT32(utf8to16[run.startIndex]),
                UINT32(utf8to16[run.startIndex + run.length] - utf8to16[run.startIndex]) };

            bool hasSuperscript = (run.superscript.isSet && run.superscript.value);
            bool hasSubscript = (run.subscript.isSet && run.subscript.value);
            FLOAT d2dBaselineOffset = 0.0f;  // positive is down

            // DirectWrite does not allow us to store enough information in the runs,
            // so we encode drawing information ourselves. The drawing effect is an
            // application-defined pointer, and we define it to be the starting index
            // into the drawing commands.
            if (i > 0) {
                currentColor = currentColor;
            }
            mLayout->SetDrawingEffect(new COMifiedIndex(int(mDraw.size())), range);

            Font font = run.font.value;
            if (!run.font.isSet || isFamilyDefault(font)) {
                font.setFamily(defaultReplacementFont.family());
                if (isPointSizeDefault(font)) {
                    font.setPointSize(defaultReplacementFont.pointSize());
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

            // For the purposes of determining the first line ascent we want
            // the normal font, not the super-/sub-scripted size.
            currentMetrics = font.metrics(dc);
            runMetrics.push_back(currentMetrics);
            {
                auto *fontInfo = &gFontMgr.get(font, mDPI);
                if (hasSuperscript || hasSubscript) {
                    font = fontSizedForSuperSubscript(font);
                    auto* smallFontInfo = &gFontMgr.get(font, mDPI);
                    if (hasSuperscript) {
                        d2dBaselineOffset = -toD2D(fontInfo->metrics.capHeight - smallFontInfo->metrics.capHeight);
                    } else if (hasSubscript) {
                        d2dBaselineOffset = toD2D(fontInfo->metrics.descent - smallFontInfo->metrics.descent);
                    }
                    fontInfo = smallFontInfo;
                    currentMetrics = font.metrics(dc);
                }
                WCHAR* utf16Family = gUTF16Cache.get(font.family());
                mLayout->SetFontFamilyName(utf16Family, range);
                mLayout->SetFontSize(fontInfo->d2dSize, range);
                mLayout->SetFontStyle(fontInfo->d2dStyle, range);
                mLayout->SetFontWeight(fontInfo->d2dWeight, range);
            }

            bool bgSet = (run.backgroundColor.isSet && run.backgroundColor.value.alpha() > 0.0f);
            bool underlineSet = (run.underlineStyle.isSet
                                 && run.underlineStyle.value != kUnderlineNone
                                 && !(run.underlineColor.isSet
                                    && run.underlineColor.value.alpha() == 0.0f));
            bool strikethroughSet = (run.strikethrough.isSet
                                     && run.strikethrough.value);

            // If a background color is set, it needs to be drawn first.
            if (bgSet) {
                auto bgRGBA = run.backgroundColor.value.toRGBA();
                if (bgRGBA != currentColor) {
                    mDraw.addColor(bgRGBA);
                    currentColor = bgRGBA;
                }
                mDraw.addRect(toD2D(currentMetrics.ascent), toD2D(currentMetrics.descent));
            }

            bool isFgDefault = false;
            Color fg = run.color.value;
            if ((run.color.value.red() == Color::kTextDefault.red() &&
                run.color.value.green() == Color::kTextDefault.green() &&
                run.color.value.blue() == Color::kTextDefault.blue())) {
                fg = defaultReplacementColor;
                fg.setAlpha(run.color.value.alpha());
            }
            int fgRGBA = fg.toRGBA();
            if (fgRGBA != currentColor) {
                mDraw.addColor(fgRGBA);
                currentColor = fgRGBA;
            }

            // Draw underline *before* text, so text descenders are on top
            if (underlineSet) {
                if (run.underlineColor.isSet) {
                    int rgba = run.underlineColor.value.toRGBA();
                    if (rgba != currentColor) {
                        mDraw.addColor(rgba);
                        currentColor = rgba;
                    }
                }

                // Note that underline position is *above* the baseline
                // (so usually negative).
                auto d2dDeltaY = toD2D(currentMetrics.underlineOffset);
                auto d2dWidth = toD2D(currentMetrics.underlineThickness);
                DrawRunCommands::Cmd cmd = DrawRunCommands::kStroke;
                switch (run.underlineStyle.value) {
                case kUnderlineNone: // to make compiler happy about enum
                case kUnderlineSingle:
                    cmd = DrawRunCommands::kStroke;
                    break;
                case kUnderlineDouble:
                    cmd = DrawRunCommands::kDoubleStroke;
                    break;
                case kUnderlineDotted:
                    cmd = DrawRunCommands::kDottedStroke;
                    break;
                case kUnderlineWavy:
                    cmd = DrawRunCommands::kWavyStroke;
                    break;
                }
                mDraw.addLine(cmd, d2dDeltaY + d2dBaselineOffset, d2dWidth);
            }

            // Draw the actual text (unless transparent)
            if (fgRGBA != currentColor) { // underline might have changed
                mDraw.addColor(fgRGBA);
                currentColor = fgRGBA;
            }
            if (fg.alpha() > 0.0f) {
                mDraw.addText(d2dBaselineOffset);
            }

            // Draw outlined text
            bool isOutlineSet = (run.outlineColor.isSet && run.outlineColor.value.alpha() > 0.0f &&
                                 !(run.outlineStrokeWidth.isSet && run.outlineStrokeWidth.value == PicaPt::kZero));
            if (isOutlineSet) {
                int rgba;
                if (run.outlineColor.isSet) {
                    rgba = run.outlineColor.value.toRGBA();
                } else {
                    rgba = fgRGBA;
                }
                if (rgba != currentColor) {
                    mDraw.addColor(rgba);
                    currentColor = rgba;
                }

                PicaPt thickness;
                if (run.outlineStrokeWidth.isSet) {
                    thickness = run.outlineStrokeWidth.value;
                } else {
                    thickness = PicaPt(1.0f);
                }
                mDraw.addStrokedText(toD2D(thickness), d2dBaselineOffset);
            }

            // Draw strikethroughs *after* text
            if (run.strikethroughColor.isSet && run.strikethroughColor.value.alpha() > 0.0f) {
                int rgba = run.strikethroughColor.value.toRGBA();
                if (rgba != currentColor) {
                    mDraw.addColor(rgba);
                    currentColor = rgba;
                }
            }
            if (strikethroughSet) {
                mDraw.addLine(DrawRunCommands::kStroke,
                              toD2D(-0.5f * currentMetrics.xHeight) + d2dBaselineOffset,
                              toD2D(currentMetrics.underlineThickness));
            }

            // Character spacing
            if (run.characterSpacing.isSet && run.characterSpacing.value != PicaPt::kZero) {
                IDWriteTextLayout1* layout = nullptr;
                err = mLayout->QueryInterface(IID_PPV_ARGS(&layout));
                if (err == S_OK) {
                    // *sigh* We'd like to specify the space in-between characters,
                    // but Microsoft only lets us specify the leading and trailing spacing,
                    // which will lead to extra space at the beginning and end.
                    float d2dSpacing = toD2D(run.characterSpacing.value);
                    layout->SetCharacterSpacing(0.5f * d2dSpacing, 0.5f * d2dSpacing, 0.0f,
                        range);
                    layout->Release();
                }
            }

            mDraw.addDone();
        }

        // Snapping to pixel sometimes puts the text above the baseline
        // (as desired) or on the pixel of the baseline (not desired).
        // Do our own snapping so that we can get consistent results.
        // This requires getting the origin of the line ourself.
        if (mLayout) {
            DWRITE_LINE_METRICS* lineMetrics = nullptr;
            UINT32 nLines;
            mLayout->GetLineMetrics(lineMetrics, 0, &nLines);
            lineMetrics = new DWRITE_LINE_METRICS[nLines];
            mLayout->GetLineMetrics(lineMetrics, nLines, &nLines);
            mBaseline = fromD2D(float(lineMetrics->baseline));
            delete[] lineMetrics;

            // Q: Does DWRITE_LINE_METRICS::length which is "number of text positions"
            //    (including trailing whitespace/newlines) mean the number of code
            //    points, or the number of WCHARs you need to advance by? If the latter,
            //    we could pass the length to calcFirstLineMetrics and avoid ever
            //    calculating the glyphs.
        }

        // Handle vertical alignment
        Font::Metrics firstLineMetrics;
        if (!runMetrics.empty()) {
            firstLineMetrics = calcFirstLineMetrics(runMetrics, text.runs());
        } else {
            firstLineMetrics = defaultReplacementFont.metrics(dc);
        }
        mFirstLineAscent = firstLineMetrics.ascent;
        mFirstLineLeading = firstLineMetrics.leading;
        mAlignmentOffset = calcOffsetForAlignment(alignment, size, firstLineMetrics);
        if (text.lineHeightMultiple() > 0.0f) {
            if (alignment & Alignment::kBottom) {
                mAlignmentOffset.y += (text.lineHeightMultiple() - 1.0f) * (firstLineMetrics.lineHeight + firstLineMetrics.leading);
            } else if (alignment & Alignment::kVCenter) {
                mAlignmentOffset.y += 0.5f * (text.lineHeightMultiple() - 1.0f) * (firstLineMetrics.lineHeight + firstLineMetrics.leading);
            }
        }

        // So this is kind of hacky: calcFirstLineMetrics *might* have created
        // the glyphs in order to find line boundaries. We need to deallocate
        // them (see the comment for TextLayout in the header file). Also, they
        // will have been created without any alignment offsets (since that was
        // what we were computing), so they will be wrong anyway.
        if (!mGlyphs.empty()) {
            mGlyphs.clear();
            mGlyphs.shrink_to_fit();  // clear() does not release memory
        }
    }

    ~TextObj()
    {
        if (mLayout) {  // handle creation failure
            mLayout->Release();
        }
    }

    const TextMetrics& metrics() const override
    {
        if (!mMetricsValid) {
            DWRITE_TEXT_METRICS winMetrics;
            mLayout->GetMetrics(&winMetrics);
            mMetrics.width = fromD2D(winMetrics.width);
            mMetrics.height = fromD2D(winMetrics.height);
            if (mMetrics.width == PicaPt::kZero) {
                mMetrics.height = PicaPt::kZero;
            }
            mMetrics.advanceX = mMetrics.width;

            if (mEmptyLastLine) {
                auto& chars = glyphs();
                if (chars.size() <= 1) {
                    mMetrics.height = PicaPt::kZero;
                } else {
                    mMetrics.height -= chars[chars.size() - 1].frame.height + (mBaseline - mFirstLineAscent);
                }
            }

            if (winMetrics.lineCount <= 1) {
                mMetrics.advanceY = PicaPt::kZero;
            } else {
                mMetrics.advanceY = mMetrics.height;
            }
            mMetricsValid = true;
        }
        return mMetrics;
    }

    const std::vector<Glyph>& glyphs() const override
    {
        if (!mUTF16To8.empty() && mGlyphs.empty() && mLayout) {
            DWRITE_LINE_METRICS* lineMetrics = nullptr;
            UINT32 nLines;
            mLayout->GetLineMetrics(lineMetrics, 0, &nLines);
            lineMetrics = new DWRITE_LINE_METRICS[nLines];
            mLayout->GetLineMetrics(lineMetrics, nLines, &nLines);

            // Same calculation as draw() does, except for the pixel-snapping.
            auto offsetY = toD2D(mAlignmentOffset.y - (mBaseline - mFirstLineAscent));
            auto* glyphGetter = new GlyphGetter(mDPI, offsetY, mUTF16To8, lineMetrics, &mGlyphs);
            mLayout->Draw(nullptr, glyphGetter, 0.0f, 0.0f);
            glyphGetter->Release();

            delete[] lineMetrics;
        }
        return mGlyphs;
    }

    void draw(ID2D1DeviceContext *gc, ID2D1SolidColorBrush *solidBrush,
              D2D1::Matrix3x2F& matrix, const Point& topLeft) const
    {
        auto* textRenderer = new CustomTextRenderer(matrix, solidBrush, mDraw);

        // We need to disable snapping to pixel because Windows is inconsistent about
        // how it snaps; this is done in BaseTextRenderer::IsSnappingDisabled().
        // So we need to do the snapping ourselves. The expectation of this function is
        // that topLeft.y + font.ascent = baselineY. This is the mathematical line;
        // the pixel "at" baselineY begins at the baseline and continues below,
        // so visually the pixel "at" baselineY is "below" the baseline. At any
        // rate, the ascent pixels of the glyph should all be above the baseline.
        // It seems that y + ascent != baseline in Direct2D, so we need to take
        // the actual baseline, and offset topLeft.y so that the result is that
        // the ascending pixels stop exactly at the mathematical baseline.
        PicaPt actualBaseline = topLeft.y + mAlignmentOffset.y + mBaseline;
        PicaPt expectedBaseline = topLeft.y + mAlignmentOffset.y + mFirstLineAscent;

        // The GC uses units of DIPs (96-dpi) which may or may not correspond to actual
        // pixels. Since we need to snap to actual pixels, we need toPixels(), not toD2D().
        // Q: Should we take into account the scale factor?
        // A: No, this way applying a scale factor keeps the position of the text,
        //    and if you are applying a scale factor, all bets are of on pixel snapping
        //    in general.
        float offsetPx = actualBaseline.toPixels(mDPI) - std::floor(expectedBaseline.toPixels(mDPI));

        // Since we are calling a D2D function, we need to make sure that our offset is
        // converted from pixels to DIPs. The easiest way to do that is to convert
        // the offset to PicaPt.
        mLayout->Draw(gc, textRenderer,
                      toD2D(topLeft.x),
                      toD2D(topLeft.y + mAlignmentOffset.y - PicaPt::fromPixels(offsetPx, mDPI)));

        textRenderer->Release();
    }

private:
    float mDPI;
    bool mEmptyLastLine;
    Point mAlignmentOffset;
    PicaPt mBaseline;
    PicaPt mFirstLineAscent;
    PicaPt mFirstLineLeading;

    IDWriteTextLayout *mLayout = nullptr;
    DrawRunCommands mDraw;

    mutable bool mMetricsValid = false;
    mutable TextMetrics mMetrics;

    std::vector<int> mUTF16To8;
    mutable std::vector<TextLayout::Glyph> mGlyphs;
};

//---------------------------- Drawable Image ---------------------------------
class Direct2DImage : public DrawableImage
{
public:
    using NativeType = ID2D1Bitmap1*;

    Direct2DImage(NativeType bitmap, int width, int height, float dpi)
        : DrawableImage(bitmap, width, height, dpi)
    {}

    ~Direct2DImage() {
        if (mNativeHandle) {
            ((NativeType)mNativeHandle)->Release();
        }
    }
};

//-------------------------------- Image --------------------------------------
uint8_t* createNativeCopy(const uint8_t *data, int width, int height, ImageFormat format,
                          D2D1_PIXEL_FORMAT *d2dFormat, ImageFormat *nativeFormat)
{
    D2D1_PIXEL_FORMAT nullD2D;
    if (!d2dFormat) {
        d2dFormat = &nullD2D;
    }
    ImageFormat nullF;
    if (!nativeFormat) {
        nativeFormat = &nullF;
    }

    uint8_t *nativeCopy = nullptr;
    *nativeFormat = kImageBGRA32_Premultiplied;
    d2dFormat->format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d2dFormat->alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

    switch (format) {
        case kImageRGBA32:
            nativeCopy = createBGRAFromRGBA(data, width, height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageRGBA32_Premultiplied:
            nativeCopy = createBGRAFromRGBA(data, width, height);
            break;
        case kImageBGRA32:
            nativeCopy = new uint8_t[4 * width * height];  // this is sooo close to native...
            memcpy(nativeCopy, data, 4 * width * height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageBGRA32_Premultiplied:  // this is native
            nativeCopy = new uint8_t[4 * width * height];
            memcpy(nativeCopy, data, 4 * width * height);
            break;
        case kImageARGB32:
            nativeCopy = createBGRAFromARGB(data, width, height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageARGB32_Premultiplied:
            nativeCopy = createBGRAFromARGB(data, width, height);
            break;
        case kImageABGR32:
            nativeCopy = createBGRAFromABGR(data, width, height);
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageABGR32_Premultiplied:
            nativeCopy = createBGRAFromABGR(data, width, height);
            break;
        case kImageRGBX32:
            d2dFormat->alphaMode = D2D1_ALPHA_MODE_IGNORE;
            *nativeFormat = kImageBGRX32;
            nativeCopy = createBGRAFromRGBA(data, width, height);
            break;
        case kImageBGRX32:  // this is native
            d2dFormat->alphaMode = D2D1_ALPHA_MODE_IGNORE;
            *nativeFormat = kImageBGRX32;
            nativeCopy = new uint8_t[4 * width * height];
            memcpy(nativeCopy, data, 4 * width * height);
            break;
        case kImageRGB24:
            d2dFormat->alphaMode = D2D1_ALPHA_MODE_IGNORE;
            *nativeFormat = kImageBGRX32;
            nativeCopy = createBGRAFromRGB(data, width, height);
            break;
        case kImageBGR24:
            d2dFormat->alphaMode = D2D1_ALPHA_MODE_IGNORE;
            *nativeFormat = kImageBGRX32;
            nativeCopy = createBGRAFromBGR(data, width, height);
            break;
        case kImageGreyscaleAlpha16:
            nativeCopy = createBGRAFromGreyAlpha(data, width, height);
            *nativeFormat = kImageBGRX32;
            premultiplyBGRA(nativeCopy, width, height);
            break;
        case kImageGreyscale8:
            d2dFormat->alphaMode = D2D1_ALPHA_MODE_IGNORE;
            *nativeFormat = kImageBGRX32;
            nativeCopy = createBGRAFromGrey(data, width, height);
            break;
        case kImageEncodedData_internal:
            assert(false);
            *nativeFormat = kImageEncodedData_internal;
            break;
    }
    assert(nativeCopy);

    return nativeCopy;
}

Image Image::fromFile(const char *path)
{
    auto data = readFile(path);
    if (!data.empty()) {
        return Image::fromEncodedData(data.data(), int(data.size()));
    }
    return Image();
}

Image Image::fromEncodedData(const uint8_t *encodedData, int size)
{
    // It appears that we need to decode the image in order to get the size and resolution.
    // Unfortunately, it seems that we cannot cache the frame (IWICBitmapFrameDecode*) and
    // create the context-specific bitmap. We probably also have to cache the stream, but
    // Direct2D is such a pain that it is just not worth it. So it is likely that we will
    // decode twice, although I have hopes that the actual pixels are not decoded until
    // asked for.

    HRESULT err;
    IWICStream* stream = nullptr;
    err = Direct2D::instance().wicFactory()->CreateStream(&stream);
    if (err != S_OK) {
        // Most errors here indicate a programming error, so try to write them out
        printError("Could not create IWICStream", err);
        stream->Release();
        return Image();
    }
    err = stream->InitializeFromMemory((WICInProcPointer)encodedData, DWORD(size));  // WICInProcPointer = BYTE* = unsigned char*
    if (err != S_OK) {
        printError("Could not initalize stream from buffer", err);
        stream->Release();
        return Image();
    }

    IWICBitmapDecoder* decoder = nullptr;
    if (err == S_OK) {
        err = Direct2D::instance().wicFactory()->CreateDecoderFromStream(
            stream, NULL, WICDecodeMetadataCacheOnDemand, &decoder);
        // An error here means this is not a decodable file
    }
    IWICBitmapFrameDecode* frame = nullptr;
    UINT width, height;
    double dpiX, dpiY;
    if (err == S_OK) {
        err = decoder->GetFrame(0, &frame);
        frame->GetSize(&width, &height);
        frame->GetResolution(&dpiX, &dpiY);
    }

    if (decoder) { decoder->Release(); }
    if (stream) { stream->Release(); }
    if (frame) { frame->Release(); }
    if (err != S_OK) {
        return Image();
    }

    uint8_t *copy = new uint8_t[size];
    memcpy(copy, encodedData, size);
    return Image(copy, size, int(width), int(height), kImageEncodedData_internal, float(dpiX));
}

Image Image::fromCopyOfBytes(const uint8_t *bytes, int width, int height,
                             ImageFormat format, float dpi /*=0.0f*/)
{
    if (format == kImageEncodedData_internal) {
        assert(false);
        return Image();
    }
    ImageFormat nativeFormat;
    uint8_t *nativeCopy = createNativeCopy(bytes, width, height, format, nullptr, &nativeFormat);
    size_t size = size_t(calcPixelBytes(format) * width * height);
    return Image(nativeCopy /* takes ownership */, size, width, height, nativeFormat,
                 (dpi != 0 ? dpi : kDefaultImageDPI));
}

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
    mutable ID2D1StrokeStyle *mStrokeStyle = nullptr;

    bool mAntialias = true;

public:
    Direct2DContext(void* renderTarget, int width, int height, float dpi)
        : DrawContext(renderTarget, width, height, dpi, dpi)
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

    std::shared_ptr<DrawableImage> createDrawableImage(const Image& image) const override
    {
        if (image.format() == kImageEncodedData_internal) {
            HRESULT err;
            IWICStream* stream = nullptr;
            err = Direct2D::instance().wicFactory()->CreateStream(&stream);
            if (err != S_OK) {
                // Most errors here indicate a programming error, so try to write them out
                printError("Could not create IWICStream", err);
                stream->Release();
                return std::make_shared<Direct2DImage>(nullptr, 0, 0, mDPI);
            }
            err = stream->InitializeFromMemory((WICInProcPointer)image.data(), DWORD(image.size()));  // WICInProcPointer = BYTE* = unsigned char*
            if (err != S_OK) {
                printError("Could not initalize stream from buffer", err);
                stream->Release();
                return std::make_shared<Direct2DImage>(nullptr, 0, 0, mDPI);
            }

            IWICBitmapDecoder* decoder = nullptr;
            if (err == S_OK) {
                err = Direct2D::instance().wicFactory()->CreateDecoderFromStream(
                    stream, NULL, WICDecodeMetadataCacheOnDemand, &decoder);
                // An error here means this is not a decodable file
            }
            IWICBitmapFrameDecode* frame = nullptr;
            if (err == S_OK) {
                err = decoder->GetFrame(0, &frame);
            }
            IWICFormatConverter* converter = nullptr;
            if (err == S_OK) {
                err = Direct2D::instance().wicFactory()->CreateFormatConverter(&converter);
            }
            if (err == S_OK) {
                err = converter->Initialize(frame,
                                            GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapDitherTypeNone,
                                            NULL,                            // no particular palette
                                            0.0f,                            // alpha threshold
                                            WICBitmapPaletteTypeCustom);
            }
            ID2D1Bitmap1* bitmap = nullptr;
            if (err == S_OK) {
                err = ((ID2D1DeviceContext*)mNativeDC)->CreateBitmapFromWicBitmap(converter, &bitmap);
            }
            if (converter) { converter->Release(); }
            if (frame) { frame->Release(); }
            if (decoder) { decoder->Release(); }
            if (stream) { stream->Release(); }

            if (bitmap) {
                float dpiX, dpiY;
                bitmap->GetDpi(&dpiX, &dpiY);
                auto size = bitmap->GetPixelSize();
                return std::make_shared<Direct2DImage>(bitmap, size.width, size.height, dpiX);
            } else {
                return std::make_shared<Direct2DImage>(nullptr, 0, 0, mDPI);
            }
        } else {
            int width = image.widthPx();
            int height = image.heightPx();

            uint8_t *nativeCopy = nullptr;
            const int bytesPerPixel = 4;
            D2D1_PIXEL_FORMAT pixelFormat;
            if (image.format() == kImageBGRA32_Premultiplied) {
                pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            } else if (image.format() == kImageBGRX32) {
                pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
            } else {
                nativeCopy = createNativeCopy(image.data(), width, height,
                                              image.format(), &pixelFormat, nullptr);
            }

            const uint8_t *nativeBytes = (nativeCopy ? nativeCopy : image.data());

            D2D1_BITMAP_PROPERTIES1 bitmapProps;
            bitmapProps.pixelFormat = pixelFormat;
            bitmapProps.dpiX = image.dpi();
            bitmapProps.dpiY = image.dpi();
            bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;  // can use for SetTarget()
            bitmapProps.colorContext = nullptr;

            ID2D1Bitmap1 *bitmap = nullptr;
            HRESULT err = ((ID2D1DeviceContext*)mNativeDC)->CreateBitmap(
                                                        { UINT32(width), UINT32(height) },
                                                        nativeBytes, bytesPerPixel * width,
                                                        &bitmapProps, &bitmap);
            if (nativeCopy) {
                delete[] nativeCopy;
            }

            if (err == S_OK) {
                return std::make_shared<Direct2DImage>(bitmap, width, height, image.dpi());
            } else {
                return std::make_shared<Direct2DImage>(nullptr, 0, 0, 0.0f);
            }
        }
    }

    std::shared_ptr<BezierPath> createBezierPath() const override
    {
        return std::make_shared<Direct2DPath>();
    }

    std::shared_ptr<TextLayout> createTextLayout(
                         const char *utf8, const Font& font, const Color& color,
                         const Size& size /*= Size::kZero*/,
                         int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                         TextWrapping wrap /*= kWrapWord*/) const override
    {
        return std::make_shared<TextObj>(*this, Text(utf8, font, color),
                                         size, alignment, wrap);
    }

    std::shared_ptr<TextLayout> createTextLayout(
                         const Text& t,
                         const Size& size /*= Size::kZero*/,
                         int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                         TextWrapping wrap /*= kWrapWord*/) const
    {
        return std::make_shared<TextObj>(*this, t, size, alignment, wrap);
    }

    std::shared_ptr<TextLayout> createTextLayout(
                         const Text& t,
                         const Font& defaultReplacementFont,
                         const Color& defaultReplacementColor,
                         const Size& size /*= Size::kZero*/,
                         int alignment /*= Alignment::kLeft | Alignment::kTop*/,
                         TextWrapping wrap /*= kWrapWord*/) const
    {
        return std::make_shared<TextObj>(*this, t, size, alignment, wrap,
                                         defaultReplacementFont, defaultReplacementColor);
    }

    Gradient& getGradient(const std::vector<Gradient::Stop>& stops) override
    {
        return *gGradientMgr.get({ this, stops }, mDPI);
    }

    /// Returns a reference to a gradient.
    Gradient& getGradient(size_t id) const override
    {
        static Direct2DGradient gBad(GradientInfo(), false /* make invalid */);

        auto it = gGradientId2Gradient.find(id);
        if (it != gGradientId2Gradient.end()) {
            return *it->second;
        }
        return gBad;
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

        std::vector<Gradient::Id> toErase;
        for (auto it = gGradientId2Gradient.begin();  it != gGradientId2Gradient.end();  ++it) {
            if (it->second->info().context == this) {
                toErase.push_back(it->first);
            }
        }
        for (auto &id : toErase) {
            auto it = gGradientId2Gradient.find(id);
            if (it != gGradientId2Gradient.end()) {
                gGradientMgr.destroy(it->second->info(), mDPI);  // this will also remove from gGradientId2Gradient
            }
        }
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

    void calcContextPixel(const Point& point, float *x, float *y) override
    {
        D2D1_POINT_2F p = toD2DPoint(point.x, point.y);
        auto& m = mStateStack.back().transform;
        auto newP = m.TransformPoint(p);
        // We set the DPI of the context, so Direct2D scales automatically
        // for us if we use it's assumed 96 dpi pixels. Since we want the
        // actual real pixels, we need to apply the scaling manually.
        if (x) {
            *x = newP.x * mDPI / 96.0f;
        }
        if (y) {
            *y = newP.y * mDPI / 96.0f;
        }
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
        switch (mStateStack.back().strokeProperties.endCap) {
            case D2D1_CAP_STYLE_FLAT:
                return kEndCapButt;
            case D2D1_CAP_STYLE_ROUND:
                return kEndCapRound;
            case D2D1_CAP_STYLE_SQUARE:
                return kEndCapSquare;
        }
        assert(false);
        return kEndCapButt;
    }

    JoinStyle strokeJoinStyle() const override
    {
        switch (mStateStack.back().strokeProperties.lineJoin) {
            case D2D1_LINE_JOIN_MITER_OR_BEVEL:
                return kJoinMiter;
            case D2D1_LINE_JOIN_ROUND:
                return kJoinRound;
            case D2D1_LINE_JOIN_BEVEL:
                return kJoinBevel;
        }
        assert(false);
        return kJoinMiter;
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

    void Direct2DContext::drawLinearGradientPath(std::shared_ptr<BezierPath> path, Gradient& gradient,
                                                 const Point& start, const Point& end) override
    {
        auto dx = toD2D(end.x - start.x);
        auto dy = toD2D(end.y - start.y);
        auto dist = std::sqrt(dx * dx + dy * dy);
        auto rotationRad = std::atan2(dy, dx);

        auto* brush = ((Direct2DGradient*)&gradient)->linearBrush();
        if (!brush || dist < 1e-6) {  // if dist == 0 the gradient will be invisible (and we cannot invert the matrix)
            return;
        }

        save();
        clipToPath(path);

        // We created the brush with the gradient going from (0, 0) to (1, 0). So we can
        // avoid creating different brushes (which presumably creates a gradient bitmap
        // for each one) for each start/end that uses the same stops
        // by just transforming the draw matrix so that drawing from (0, 0) to (1, 0)
        // gives us the desired result.
        translate(start.x, start.y);
        scale(dist, dist);
        rotate(-rotationRad * 180.0f / 3.14159265f);

        drawGradient(brush);
        restore();
    }

    void Direct2DContext::drawRadialGradientPath(std::shared_ptr<BezierPath> path, Gradient& gradient,
                                                 const Point& center, const PicaPt& startRadius,
                                                 const PicaPt& endRadius) override
    {
        auto r0 = toD2D(startRadius);
        auto r1 = toD2D(endRadius);
        if (r1 < 1e-6) {
            return;
        }

        auto* brush = ((Direct2DGradient*)&gradient)->radialBrush(r0 / r1);
        if (!brush) {
            return;
        }

        save();
        clipToPath(path);

        // We created the brush with the gradient going from (0, 0) to (1, 0). So we can
        // avoid creating different brushes (which presumably creates a gradient bitmap
        // for each one) for each start/end that uses the same stops
        // by just transforming the draw matrix so that drawing from (0, 0) to (1, 0)
        // gives us the desired result.
        translate(center.x, center.y);
        scale(r1, r1);

        drawGradient(brush);
        restore();
    }

private:
    void Direct2DContext::drawGradient(ID2D1Brush* brush)
    {
        auto* gc = deviceContext();

        // Draw a rect the size of the entire context using the gradient brush;
        // the clipping will limit to the path.
        // Note that while width/height are in pixels, the D2D functions are in DIPs,
        // so as far as D2D is concerned, the context is not mWidth DIPs wide,
        // it is mWidth*dpi/96 DIPs.
        auto ul = D2D1::Point2F(0.0f, 0.0f);
        auto lr = D2D1::Point2F(toD2D(PicaPt::fromPixels(float(mWidth), mDPI)),
                                toD2D(PicaPt::fromPixels(float(mHeight), mDPI)));
        auto ur = D2D1::Point2F(lr.x, 0.0f);
        auto ll = D2D1::Point2F(0.0, lr.y);

        // We need to transform the corners of the context-rect so that
        // they are in the same coordinate system as the current transform matrix.
        // To do that, we transform by the inverse of the transform matrix.
        D2D1::Matrix3x2F m;
        gc->GetTransform(&m);
        auto success = m.Invert();
        assert(success);  // all translation matrix are invertible (if not, the x or y scale is probably 0)
        ul = m.TransformPoint(ul);
        ur = m.TransformPoint(ur);
        lr = m.TransformPoint(lr);
        ll = m.TransformPoint(ll);

        // Draw the full-context rect. (Direct2D's API is so unweildy it is
        // easier to convert to PicaPt, use our API [which converts back] than
        // to do this natively.)
        auto path = createBezierPath();
        path->moveTo(Point(fromD2D(ul.x), fromD2D(ul.y)));
        path->lineTo(Point(fromD2D(ur.x), fromD2D(ur.y)));
        path->lineTo(Point(fromD2D(lr.x), fromD2D(lr.y)));
        path->lineTo(Point(fromD2D(ll.x), fromD2D(ll.y)));
        path->close();
        gc->FillGeometry((ID2D1PathGeometry*)path->nativePathForDPI(mDPI, true), brush, nullptr);
    }

public:
    void drawText(const char* textUTF8, const Point& topLeft, const Font& font,
                  PaintMode mode) override
    {
        drawText(textForCurrentStyle(textUTF8, font, mode), topLeft);
    }

    TextMetrics textMetrics(const char *textUTF8, const Font& font,
                            PaintMode mode /*=kPaintFill*/) const override
    {
        return textForCurrentStyle(textUTF8, font, mode).metrics();
    }

    void drawText(const TextLayout& layout, const Point& topLeft) override
    {
        auto& state = mStateStack.back();
        auto* gc = deviceContext();

        // We know we have a TextObj, because that is the only thing
        // we give out, so dynamic casting would be unnecessarily slow,
        // which is not what you want in your drawing functions. But we
        // do not want to make it a virtual function, either, otherwise
        // it end up visible in the user-visible interface.
        const TextObj *text = static_cast<const TextObj*>(&layout);
        text->draw(gc, mSolidBrush, state.transform, topLeft);
    }

    void drawImage(std::shared_ptr<DrawableImage> image, const Rect& rect) override
    {
        auto *gc = deviceContext();
        auto destRect = D2D1::RectF(toD2D(rect.x), toD2D(rect.y),
                                    toD2D(rect.maxX()), toD2D(rect.maxY()));
        // RectF constructor is RectF(xMin, yMin, xMax, yMax), but if
        // we start at (0, 0) then it is the same as (x, y, w, h).
        auto srcRect = D2D1::RectF(0.0f, 0.0f,
                                   toD2D(PicaPt::fromPixels(float(image->widthPx()), image->dpi())),
                                   toD2D(PicaPt::fromPixels(float(image->heightPx()), image->dpi())));
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

    std::shared_ptr<DrawableImage> copyToImage() override
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

    ID2D1StrokeStyle* getStrokeStyle() const
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

    TextObj textForCurrentStyle(const char *textUTF8, const Font& font, PaintMode mode) const
    {
        auto& state = mStateStack.back();

        Color fillColor = Color::kTransparent;
        Color strokeColor = Color::kTransparent;
        PicaPt strokeWidth = PicaPt::kZero;
        ID2D1StrokeStyle* strokeStyle = nullptr;
        if (mode & kPaintFill) {
            fillColor = state.fillColor;
        }
        Text t(textUTF8, font, fillColor);
        if (mode & kPaintStroke) {
            t.setOutlineColor(state.strokeColor);
            t.setOutlineStrokeWidth(state.strokeWidth);
            //strokeStyle = getStrokeStyle();
        }
        return TextObj(*this, t, Size::kZero, Alignment::kLeft, kWrapNone);
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
    ID2D1DeviceContext* mDC = nullptr;
    IDXGISwapChain1* mSwapChain = nullptr;
    ID2D1Bitmap1* mBackingStore = nullptr;

public:
    explicit Direct2DWindow(HWND hwnd, int width, int height, float dpi)
        : Direct2DContext(nullptr, width, height, dpi)
    {
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
        // someone writes, maybe this is something that could be, I don't know, made
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
                                            &bitmapProps, &mBitmap);
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
std::shared_ptr<DrawContext> DrawContext::fromHwnd(void* hwnd, int width, int height, float dpi)
{
    return std::make_shared<Direct2DWindow>((HWND)hwnd, width, height, dpi);
}

std::shared_ptr<DrawContext> DrawContext::createDirect2DBitmap(BitmapType type, int width, int height,
                                                               float dpi /*= 72.0f*/)
{
    return std::make_shared<Direct2DBitmap>(type, width, height, dpi);
}

} // namespace $ND_NAMESPACE

#endif // _WIN32 || _WIN64
