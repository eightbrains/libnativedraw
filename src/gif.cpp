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

#include "nativedraw_private.h"

#include <gif_lib.h>
#include <string.h>

namespace ND_NAMESPACE {

Image readGIF(const uint8_t *gifdata, int size)
{
    struct InputData {
        const uint8_t *data;
        int size;
        int pos = 0;
    };
    InputData inputData { gifdata, size };

    // A lambda that captures cannot convert to a function pointer, so
    // we cannot just capture gidata, size, and a pos variable.
    auto read = [](GifFileType *ft, GifByteType *dest, int len) -> int {
        auto *input = (InputData*)ft->UserData;
        if (input->pos + len > input->size) {
            len = input->size - input->pos;
        }
        memcpy(dest, input->data + input->pos, len);
        input->pos += len;
        return len;
    };

    int err;
    GifFileType* gif = DGifOpen(&inputData, read, &err);
    if (!gif) {
        return Image();
    }
    if (DGifSlurp(gif) == GIF_ERROR) {
        DGifCloseFile(gif, &err);
        return Image();
    }
    if (gif->ImageCount == 0) {
        DGifCloseFile(gif, &err);
        return Image();
    }

    ColorMapObject* commonMap = gif->SColorMap;

    // We could iterate over all the images in the GIF with gif->ImageCount,
    // but we do not support animated GIFs.
    int i = 0;
    const SavedImage& image = gif->SavedImages[i];
    const GifImageDesc& desc = image.ImageDesc;
    GraphicsControlBlock gcb;
    DGifSavedExtensionToGCB(gif, i, &gcb);
    const ColorMapObject* colorMap = desc.ColorMap ? desc.ColorMap : commonMap;

    int width = desc.Width;
    int height = desc.Height;
    Image imgData(width, height, kImageBGRA32_Premultiplied);
    // The documentation at https://giflib.sourceforge.net/gif_lib.html
    // (very bottom) says that gif->SBackGroundColor is an unused feature
    // of the GIF spec. (This is handy, because there is also no documentation
    // on how the int is interpreted.)

    if (colorMap) {
        auto *src = image.RasterBits;
        auto *srcEnd = src + width * height;
        uint8_t *bgra = imgData.data();
        while (src < srcEnd) {
            int c = *src++;
            if (c != gcb.TransparentColor) {
                GifColorType rgb = colorMap->Colors[c];
                *bgra++ = rgb.Blue;
                *bgra++ = rgb.Green;
                *bgra++ = rgb.Red;
                *bgra++ = 0xff;
            } else {
                // Not only is this transparent, it is also premultiplied
                *bgra++ = 0x00;
                *bgra++ = 0x00;
                *bgra++ = 0x00;
                *bgra++ = 0x00;
            }
        }
    } else {
        // Presumably not having a colormap is an internal error in the gif file
        // and should return D_GIF_ERR_NO_COLOR_MAP. In which case, uninitialized
        // data seems like a fairly appropriate result. A fixed color seems like
        // it would be incorrect.
        ; // do nothing
    }

    // We do not need to premultiply, since we only have alpha 0xff and 0x00,
    // and premultiplying 0xff is exactly the same, and the 0x00 is already
    // premultiplied.

    DGifCloseFile(gif, &err);
    return imgData;
}

}  // namespace ND_NAMESPACE
