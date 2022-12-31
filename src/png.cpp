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

#include <setjmp.h>
#include <string.h>
#include <png.h>

namespace ND_NAMESPACE {

std::unique_ptr<ImageData> readPNG(const uint8_t *pngdata, int size)
{
    // Check if the header is actually a PNG header, for quick failure.
    const png_size_t kPNGSignatureLength = 8;
    if (png_sig_cmp(pngdata, (png_size_t)0, kPNGSignatureLength) != 0) {
        return std::unique_ptr<ImageData>();
    }

    // libpng 1.6 has a simplified API, but that does not seem to work properly
    // for 16-bit PNGs.

    struct ReadData {
        const uint8_t *data;
        int len;
        int pos = 0;
    };
    ReadData inputData = { pngdata, size };

    // Custom read and error functions. Note that a capture-less lambda can
    // convert to a function pointer, but not a lambda that captures, so we
    // cannot just capture pngbytes, size, and a pos variable for the read
    // function.
    auto read = [](png_structp png, png_bytep dest, png_size_t readLen) {
        auto *input = (ReadData*)png_get_io_ptr(png);
        if (input->pos + readLen <= input->len) {
            memcpy(dest, input->data + input->pos, readLen);
            input->pos += readLen;
        } else {
            png_error(png, "not enough data");
        }
    };
    jmp_buf jmpbuf;
    auto noErr = [](png_structp png, png_const_charp message) {
        auto *jmp = (jmp_buf*)png_get_error_ptr(png);
        longjmp(*jmp, 1);
    };
    auto noWarn = [](png_structp, png_const_charp) {};
    auto png = png_create_read_struct(PNG_LIBPNG_VER_STRING, jmpbuf, noErr, noWarn);
    if (png == NULL) {
        return std::unique_ptr<ImageData>();
    }
    auto pngInfo = png_create_info_struct(png);
    if (pngInfo == NULL)
    {
        png_destroy_read_struct(&png, NULL, NULL);
        return std::unique_ptr<ImageData>();
    }

    // We set our own jump buffer instead of setjmp(png_jmpbuf(png)), since
    // the default handlers print out errors/warnings and we want to be
    // silent.
    if (setjmp(jmpbuf)) {
        // error happened
        png_destroy_read_struct(&png, &pngInfo, NULL);
        return std::unique_ptr<ImageData>();
    }

    png_set_read_fn(png, &inputData, read);  // read from buffer instead of FILE*
    png_set_sig_bytes(png, 0);  // have consumed 0 bytes of the signature

    png_read_png(png, pngInfo,
                 PNG_TRANSFORM_PACKING |      // expand 1-, 2-, 4-bit to 8-bit
                 PNG_TRANSFORM_GRAY_TO_RGB |  // expand grey to rgb
                 PNG_TRANSFORM_SCALE_16 |     // scal 16-bit to 8-bit smoothly
                 PNG_TRANSFORM_BGR,           // rgb -> bgr, rgba -> bgra
                 NULL);

    // We have read the entire image, write to our buffer
    // (If we failed, we would have longjmped to the error handling above)
    auto imgData = std::make_unique<ImageData>(png_get_image_width(png, pngInfo),
                                               png_get_image_height(png, pngInfo),
                                               kImageBGRA32_Premultiplied);
    auto rowStride = png_get_rowbytes(png, pngInfo);
    auto rows = png_get_rows(png, pngInfo);
    if (rowStride >= 4 * imgData->width) {
        for (int y = 0;  y < imgData->height;  ++y) {
            memcpy(imgData->bgra + y * 4 * imgData->width, rows[y],
                   4 * imgData->width);
        }
        premultiplyBGRA(imgData->bgra, imgData->width, imgData->height);
    } else if (rowStride >= 3 * imgData->width) {
        for (int y = 0;  y < imgData->height;  ++y) {
            auto *bgra = imgData->bgra + y * 4 * imgData->width;
            for (int x = 0;  x < imgData->width;  ++x) {
                bgra[4 * x    ] = rows[y][3 * x    ];
                bgra[4 * x + 1] = rows[y][3 * x + 1];
                bgra[4 * x + 2] = rows[y][3 * x + 2];
                bgra[4 * x + 3] = 0xff;
            }
        }
    } else {
        // We should not get here: we have expanded 1, 2, 4 bit to 8-bit
        // and 1 channel to 3.
        assert(false);
    }
  
    // Cleanup
    png_destroy_read_struct(&png, &pngInfo, NULL);

    return imgData;
}

} // namespace ND_NAMESPACE
