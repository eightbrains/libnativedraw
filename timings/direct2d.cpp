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

#include "timings.h"
#include "../src/nativedraw.h"

#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
    static const int kWidth = 1024;
    static const int kHeight = 768;

    std::shared_ptr<Timings> gTimings;
    std::shared_ptr<ND_NAMESPACE::DrawContext> gDC;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    // Windows apps have no way to get console output, which we need
    // to see our results. So create a console or attach to the
    // terminal's, as appropriate.
    bool inTerminal = AttachConsole(ATTACH_PARENT_PROCESS);
    if (inTerminal || AllocConsole()) {
        freopen("CONIN$", "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = WndProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = sizeof(LONG_PTR);
    wcex.hInstance     = hInstance;
    wcex.hbrBackground = NULL;
    wcex.lpszMenuName  = NULL;
    wcex.hCursor       = LoadCursor(NULL, IDI_APPLICATION);
    wcex.lpszClassName = "LND_Timings";
    RegisterClassEx(&wcex);

    HWND w = CreateWindow("LND_Timings",
                          "Timings",
                          WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          kWidth, kHeight,
                          NULL,  // parent
                          NULL,  // menu
                          hInstance,
                          NULL); // passed to WM_CREATE message, useful for a this pointer
    ShowWindow(w, SW_SHOWNORMAL);
    UpdateWindow(w);

    gTimings = std::make_shared<Timings>();
    gDC = ND_NAMESPACE::DrawContext::fromHwnd(w, kWidth, kHeight,
                                              float(GetDpiForWindow(w)));

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    assert(!gDC);
    gTimings.reset();

    // If we are not running a terminal, then we created a console, which
    // will disappear as soon as we are finished, inhibiting the ability
    // to read the results, so read from stdin to keep from exiting.
    if (!inTerminal) {
        std::cout << std::endl << "[Press RETURN to close]" << std::endl;
        char c = getc(stdin);
    }

    // msg is WM_QUIT, and WinMain return value should be wParam
    // (unless it exits before running the message loop, then it
    // it is zero by convention).
    return int(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_PAINT:
            if (!gTimings) {
                return 0;  // paint message arrived before gTimings was created
            }
            if (gTimings->runNext(gDC.get()) == Timings::CONTINUE) {
                InvalidateRect(hwnd, NULL, NULL);
            } else {
                gDC.reset();
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(1);
            return 0;
        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
}
