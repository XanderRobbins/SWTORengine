// Phase 0 smoke test: verify the toolchain by opening and cleanly closing
// a minimal Win32 window. Later phases replace this with the real app loop.
//
// Run with --smoke-test to auto-close after 2 seconds (exit code 0 on success),
// so the exit criterion is verifiable without manual interaction.

#include <windows.h>
#include <string>

namespace {

constexpr wchar_t kWindowClassName[] = L"ChimeraOverlayMainWindow";
constexpr UINT_PTR kSmokeTestTimerId = 1;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == kSmokeTestTimerId) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    const bool smokeTest =
        pCmdLine && std::wstring(pCmdLine).find(L"--smoke-test") != std::wstring::npos;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        L"Chimera Overlay (Phase 0)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (smokeTest) {
        SetTimer(hwnd, kSmokeTestTimerId, 2000, nullptr);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
