// Chimera Overlay — entry point.
// Captures a target window via Windows.Graphics.Capture, enhances frames on
// the GPU (FSR1 EASU + RCAS), and presents them in a click-through topmost
// window pinned over the target. Never touches the target process.

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

#include "app/App.h"

namespace {

// GUI-subsystem app: stdout goes nowhere useful, so --test-* modes log to a
// file next to the exe where a script (or human) can read the results.
void RedirectStdoutToLogFile() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring log(path);
    size_t slash = log.find_last_of(L'\\');
    if (slash != std::wstring::npos) log.resize(slash + 1);
    log += L"chimera-test.log";
    FILE* unused;
    _wfreopen_s(&unused, log.c_str(), L"w", stdout);
}

// One live overlay at a time. RestartSelf() spawns the replacement while the
// old process is still winding down, so on collision we wait for the mutex to
// be released (or abandoned) instead of bailing immediately.
bool ClaimSingleInstance() {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ChimeraOverlaySingleInstance");
    if (!mutex) return true; // can't tell — don't refuse to run
    if (GetLastError() != ERROR_ALREADY_EXISTS) return true;
    const DWORD wait = WaitForSingleObject(mutex, 10000);
    return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Must precede any window creation or rect math (Pitfalls: DPI scaling).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    app::AppOptions options;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto nextArg = [&](std::wstring& out) {
            if (i + 1 < argc) out = argv[++i];
        };
        if (arg == L"--smoke-test") options.smokeTest = true;
        else if (arg == L"--test-capture") nextArg(options.testCaptureTarget);
        else if (arg == L"--test-find") nextArg(options.testFindTarget);
        else if (arg == L"--test-rate") nextArg(options.testRateTarget);
        else if (arg == L"--test-rate-monitor") { nextArg(options.testRateTarget); options.testRateMonitor = true; }
        else if (arg == L"--target") nextArg(options.targetOverride);
        else if (arg == L"--outdir") nextArg(options.outDir);
    }
    LocalFree(argv);

    const bool testMode = !options.testCaptureTarget.empty() ||
                          !options.testFindTarget.empty() || !options.testRateTarget.empty();
    if (testMode) RedirectStdoutToLogFile();

    // Test modes may run alongside a live overlay; only the real overlay is
    // single-instance (startup copy + manual launch must not stack).
    if (!testMode && !ClaimSingleInstance()) return 0;

    app::App application;
    return application.Run(hInstance, options);
}
