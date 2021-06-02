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

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifndef ND_NAMESPACE
#define ND_NAMESPACE eb
#endif // ND_NAMESPACE

namespace ND_NAMESPACE {
class Image;
class DrawContext;
}

// Because drawing might be GPU-accelerated we cannot just time how
// long it takes to execute the draw commands. Instead, we need to
// call runNext() and then immediately schedule another draw. We will
// draw the same scene multiple times and divide into the total time
// to get a more realistic value. Disable VSync, or the results will
// not be very meaningful.
class Timings
{
public:
    Timings();

    enum State { CONTINUE, DONE };
    State runNext(ND_NAMESPACE::DrawContext *dc);

private:
    struct Run {
        std::string name;
        int nObjs;
        std::function<void(ND_NAMESPACE::DrawContext&, int)> func;
    };
    std::vector<Run> mRuns;
    int mRunIdx = 0;

    std::shared_ptr<ND_NAMESPACE::Image> mImg100;

    struct Result {
        int n = 0;
        std::chrono::time_point<std::chrono::high_resolution_clock> start;
        std::chrono::time_point<std::chrono::high_resolution_clock> end;
    };
    std::map<std::string, Result> mResults;

private:
    void printResult(const Run& run, const Result& result);
};
