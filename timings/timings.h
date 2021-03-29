#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace eb {
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
    State runNext(eb::DrawContext *dc);

private:
    struct Run {
        std::string name;
        int nObjs;
        std::function<void(eb::DrawContext&, int)> func;
    };
    std::vector<Run> mRuns;
    int mRunIdx = 0;

    struct Result {
        int n = 0;
        std::chrono::time_point<std::chrono::high_resolution_clock> start;
        std::chrono::time_point<std::chrono::high_resolution_clock> end;
    };
    std::map<std::string, Result> mResults;

private:
    void printResult(const Run& run, const Result& result);
};
