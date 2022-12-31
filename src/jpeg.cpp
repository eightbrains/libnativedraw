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

#include <jpeglib.h>
#include <setjmp.h>

namespace ND_NAMESPACE {

std::unique_ptr<ImageData> readJPEG(const uint8_t *jpegdata, int size)
{
    // The default error "handler" calls exit. !!?? That's the last thing we
    // want, so we need to set up our own handler. We also do not want to
    // print errors, since we call this function speculatively: is this data
    // jpeg? Empty point back, huh, guess it is not jpeg.
    struct nonfatal_error_mgr {
        struct jpeg_error_mgr pub;    // "public" fields
        jmp_buf setjmp_buffer;        // for return to caller

        static void error_exit(j_common_ptr cinfo)
        {
            nonfatal_error_mgr *err = (nonfatal_error_mgr*)cinfo->err;
            longjmp(err->setjmp_buffer, 1);
        }

        static void emit_message(j_common_ptr cinfo, int msg_level) {}
    };

    nonfatal_error_mgr jerr;
    struct jpeg_decompress_struct decompress;
    decompress.err = jpeg_std_error(&jerr.pub);  // default calls exit() !?
    jerr.pub.error_exit = nonfatal_error_mgr::error_exit;
    jerr.pub.emit_message = nonfatal_error_mgr::emit_message;
    if (setjmp(jerr.setjmp_buffer)) {
        // An error happened, and now here we are...
        jpeg_destroy_decompress(&decompress);
        return std::unique_ptr<ImageData>();  // nullptr
    }

    jpeg_create_decompress(&decompress);
    jpeg_mem_src(&decompress, jpegdata, size);
    jpeg_read_header(&decompress, TRUE);  // TRUE flag tables-only data as an error

    // Set output parameters here, after jpeg_read_header() initializes them.
    // BGRX is native on every platform.
    decompress.out_color_space = JCS_EXT_BGRA;

    if (!jpeg_start_decompress(&decompress)) {
        return std::unique_ptr<ImageData>();  // nullptr
    }

    int width = decompress.output_width;
    int height = decompress.output_height;
    // JPEG does not support alpha channels, so use BGRX32, which is native
    // to all platforms.
    auto imgData = std::make_unique<ImageData>(width, height, kImageBGRX32);
    int rowStride = 4 * width;

    while (decompress.output_scanline < height) {
        // Read one scanline (the function requires an array of pointers,
        // which is cumbersome, and the scanlines might not be in order).
        uint8_t* outAddrs[1] = { imgData->bgra + decompress.output_scanline * rowStride };
        jpeg_read_scanlines(&decompress, outAddrs, 1);
    }

    jpeg_finish_decompress(&decompress);
    jpeg_destroy_decompress(&decompress);

    // We could check for data-corruption warnings (jerr.pub.num_warnings != 0),
    // but we do not have a facility to pass errors back at this point.

    return imgData;
}

}  // namespace NAMESPACE_ND
