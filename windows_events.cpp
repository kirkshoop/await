// windows_events.cpp : Defines the entry point for the application.
//
// In VS 2015 x64 Native prompt:
// CL.exe /Zi /nologo /W3 /GS- /Od /D _DEBUG /D WIN32 /D _UNICODE /D UNICODE /Gm /EHsc /MDd /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TP /await windows_events.cpp
//

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ole32.lib")

#include <new>
#include <utility>
#include <memory>
#include <type_traits>
#include <tuple>

#include <experimental/resumable>
#include <experimental/generator>
namespace ex = std::experimental;

#include "unwinder.h"

#include "windows_user.h"
namespace wu = windows_user;

#include "async_generator.h"
#include "async_operators.h"
namespace ao = async::operators;
#include "async_subjects.h"
namespace asub = async::subjects;
#include "async_windows_user.h"
namespace awu = async::windows_user;

struct RootWindow : public awu::async_messages
{
    using window_class = wu::window_class<RootWindow>;
    static LPCWSTR class_name() {return L"Scratch";}
    static void change_window_class(WNDCLASSEX& wcex) {}

    HWND window;

    ~RootWindow() {
        PostQuitMessage(0);
    }

    RootWindow(HWND w, LPCREATESTRUCT) : window(w) {
        OnPaint();
        OnPrintClient();
        OnKeyDown();
    }

    void PaintContent(PAINTSTRUCT& ) {}

    auto OnKeyDown() -> std::future<void> {
        for __await (auto& m : messages<WM_KEYDOWN>()) {
            m.handled();
            MessageBox(window, L"KeyDown", L"RootWindow", MB_OK);
        }
    }

    auto OnPaint() -> std::future<void> {
        for __await (auto& m : messages<WM_PAINT>()) {
            m.handled();
            PAINTSTRUCT ps;
            BeginPaint(window, &ps);
            PaintContent(ps);
            EndPaint(window, &ps);
        }
    }

    auto OnPrintClient() -> std::future<void> {
        for __await (auto& m : messages<WM_PRINTCLIENT, HDC>()) {
            m.handled();
            PAINTSTRUCT ps;
            ps.hdc = m.wParam;
            GetClientRect(window, &ps.rcPaint);
            PaintContent(ps);
        }
    }
};

int PASCAL
wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR, int nShowCmd)
{
    HRESULT hr = S_OK;

    hr = CoInitialize(NULL);
    if (FAILED(hr))
    {
        return FALSE;
    }
    ON_UNWIND_AUTO([&]{CoUninitialize();});

    InitCommonControls();

    RootWindow::window_class::Register();

    LONG winerror = 0;

    HWND window = CreateWindow(
        RootWindow::window_class::Name(), L"Scratch Title",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL,
        hinst,
        NULL);
    if (!window) {winerror = GetLastError();}

    if (!!winerror || !window)
    {
        return winerror;
    }

    ShowWindow(window, nShowCmd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
