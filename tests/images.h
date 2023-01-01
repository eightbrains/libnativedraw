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

#ifndef TEST_IMAGES_H
#define TEST_IMAGES_H

#include <cstdint>
#include <vector>

// These values appear in the PNG/JPEG image file-data.
constexpr uint8_t kTestGreyRed = 0x80;
constexpr uint8_t kTestGreyGreen = 0xc0;
constexpr uint8_t kTestGreyBlue = 0x40;
constexpr uint8_t kTestGreyWhite = 0xff;

enum class TestImage {
    kNone = 0,
    kBadImage,
    kPNG_Grey8,
    kPNG_GreyAlpha16,
    kPNG_RGB,
    kPNG_RGB_Interlaced,
    kPNG_RGBA,
    kPNG_Grey16,       // 16 bits per channel
    kPNG_GreyAlpha32,  // 16 bits per channel
    kPNG_RGB48,        // 16 bits per channel
    kPNG_RGBA64,       // 16 bits per channel
    kPNG_bad,
    kJPEG,
    kJPEG_Progressive,
    kJPEG_bad,
    kGIF,
    kGIF_bad,
};
std::vector<uint8_t> loadImage(TestImage img);

#endif // TEST_IMAGES_H
