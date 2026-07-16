#include "App.h"

#include "../util/PngDump.h"

#include <winrt/Windows.System.h>
#include <DispatcherQueue.h>
#include <cstdio>

namespace app {

namespace {

constexpr wchar_t kMsgClassName[] = L"ChimeraMessageWindow";
constexpr UINT_PTR kTimerPoll = 1;       // rect re-sync + target re-acquire backstop
constexpr UINT_PTR kTimerUi = 2;         // settings panel redraw
constexpr UINT_PTR kTimerSmokeTest = 3;
constexpr int kHotkeySettings = 1; // Alt+Shift+O
constexpr int kHotkeyBypass = 2;   // Alt+Shift+B

// PostQuitMessage only works from the main thread; worker-thread code posts
// these to the message window instead.
constexpr UINT kMsgQuit = WM_APP + 1;         // wParam = exit code
constexpr UINT kMsgSelectTarget = WM_APP + 2; // consume pendingTarget_

App* g_app = nullptr; // single instance; wndproc trampoline

std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t slash = dir.find_last_of(L'\\');
    if (slash != std::wstring::npos) dir.resize(slash + 1);
    return dir;
}

bool RectsEqual(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

} // namespace

int App::Run(HINSTANCE instance, const AppOptions& options) {
    instance_ = instance;
    options_ = options;
    g_app = this;

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    qpcToMs_ = 1000.0 / static_cast<double>(freq.QuadPart);

    if (!options_.testFindTarget.empty()) return RunTestFind();

    // DispatcherQueue on this thread so WGC FrameArrived is delivered through
    // our own message loop (single-threaded, event-driven — no locks anywhere).
    winrt::Windows::System::DispatcherQueueController dispatcherController{nullptr};
    DispatcherQueueOptions dqOptions{sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT,
                                     DQTAT_COM_STA};
    if (FAILED(CreateDispatcherQueueController(
            dqOptions, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
                           winrt::put_abi(dispatcherController))))) {
        MessageBoxW(nullptr, L"Failed to create DispatcherQueue.", L"Chimera", MB_ICONERROR);
        return 1;
    }

    if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
        MessageBoxW(nullptr,
                    L"Windows.Graphics.Capture is not supported on this OS build.\n"
                    L"Windows 10 1903 or later is required.",
                    L"Chimera", MB_ICONERROR);
        return 1;
    }

    config_ = Config::Load();
    if (!options_.targetOverride.empty()) config_.targetTitle = options_.targetOverride;

    if (!InitCore()) return 1;
    if (!CreateMessageWindow()) return 1;

    if (options_.smokeTest) {
        SetTimer(msgWindow_, kTimerSmokeTest, 2000, nullptr);
        return MessageLoop();
    }

    const bool testMode =
        !options_.testCaptureTarget.empty() || !options_.testRateTarget.empty();
    const std::wstring needle = !options_.testCaptureTarget.empty() ? options_.testCaptureTarget
                                : !options_.testRateTarget.empty() ? options_.testRateTarget
                                                                   : config_.targetTitle;

    if (!testMode) {
        hotkeys_.Register(msgWindow_, kHotkeySettings, MOD_ALT | MOD_SHIFT, 'O',
                          [this] { settings_.Toggle(); });
        hotkeys_.Register(msgWindow_, kHotkeyBypass, MOD_ALT | MOD_SHIFT, 'B', [this] {
            bypassed_ = !bypassed_;
            UpdatePresenterVisibility();
        });
        SetTimer(msgWindow_, kTimerPoll, 500, nullptr);
        SetTimer(msgWindow_, kTimerUi, 16, nullptr);
    }

    if (!AcquireTarget(needle) && testMode) {
        wprintf(L"test-capture: no window matching '%s'\n", needle.c_str());
        return 2;
    }
    if (testMode) {
        wprintf(L"test: d3d adapter = %s\n", d3d_.AdapterName().c_str());
        fflush(stdout);
    }
    if (testMode && capture_.IsActive()) {
        // Watchdog: fail instead of hanging if no frames ever arrive.
        SetTimer(msgWindow_, kTimerSmokeTest, 10000, nullptr);
        exitCode_ = 3;
    }

    return MessageLoop();
}

bool App::InitCore() {
    if (!d3d_.Init()) {
        MessageBoxW(nullptr, L"Failed to create D3D11 device.", L"Chimera", MB_ICONERROR);
        return false;
    }
    std::wstring shaderError;
    if (!pipeline_.Init(d3d_.Device(), ExeDir() + L"shaders\\", &shaderError)) {
        MessageBoxW(nullptr, shaderError.c_str(), L"Chimera — shader compile", MB_ICONERROR);
        return false;
    }
    if (!presenter_.Create(instance_, d3d_)) return false;

    if (options_.testCaptureTarget.empty() && !options_.smokeTest) {
        if (!settings_.Init(instance_, d3d_, &config_)) return false;
        // Deferred to the main thread: restarting capture tears down the
        // session, which must not happen while the settings render holds the
        // GPU lock (the frame worker could be blocked holding the session
        // lock and waiting for the GPU lock — a deadlock cycle).
        settings_.onTargetSelected = [this](HWND hwnd) {
            wchar_t title[512]{};
            GetWindowTextW(hwnd, title, 512);
            config_.targetTitle = title;
            config_.Save();
            pendingTarget_ = hwnd;
            PostMessageW(msgWindow_, kMsgSelectTarget, 0, 0);
        };
        settings_.onConfigChanged = [this] {
            config_.Save();
            UpdatePresenterVisibility();
            SyncPresenterRect();
        };
    }
    return true;
}

bool App::CreateMessageWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgWndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kMsgClassName;
    RegisterClassExW(&wc);

    msgWindow_ = CreateWindowExW(0, kMsgClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                 instance_, nullptr);
    return msgWindow_ != nullptr;
}

LRESULT CALLBACK App::MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_app && hwnd == g_app->msgWindow_) return g_app->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case kMsgQuit:
        PostQuitMessage(static_cast<int>(wParam));
        return 0;
    case kMsgSelectTarget:
        if (pendingTarget_ && IsWindow(pendingTarget_)) {
            wchar_t title[512]{};
            GetWindowTextW(pendingTarget_, title, 512);
            StartCaptureOn(pendingTarget_, title);
        }
        pendingTarget_ = nullptr;
        return 0;
    case WM_HOTKEY:
        if (hotkeys_.Dispatch(static_cast<int>(wParam))) return 0;
        break;
    case WM_TIMER:
        switch (wParam) {
        case kTimerSmokeTest:
            // smoke test: clean exit; test-capture: watchdog fired (exitCode_ 3)
            PostQuitMessage(exitCode_);
            return 0;
        case kTimerPoll:
            if (tracker_.IsAttached()) {
                SyncPresenterRect();
                // Window went fullscreen (or left it) — switch capture source.
                if (capture_.IsActive() && capture_.ActiveSource() != DesiredSource()) {
                    HWND target = tracker_.Target();
                    std::wstring title = targetTitleLive_;
                    StartCaptureOn(target, title);
                }
            } else if (!config_.targetTitle.empty()) {
                AcquireTarget(config_.targetTitle); // game may have (re)started
            }
            return 0;
        case kTimerUi:
            if (settings_.IsVisible()) {
                ui::SettingsOverlay::Stats stats;
                stats.processMs = processMsEma_;
                stats.fps = fps_;
                stats.captureFps = captureFps_;
                stats.inW = lastInW_;
                stats.inH = lastInH_;
                stats.outW = presenter_.Width();
                stats.outH = presenter_.Height();
                stats.captureActive = capture_.IsActive();
                stats.bypassed = bypassed_;
                stats.targetTitle = targetTitleLive_;
                try {
                    std::lock_guard<std::recursive_mutex> lock(gpuMutex_);
                    settings_.Render(stats);
                } catch (...) {
                    RestartSelf();
                }
            }
            return 0;
        }
        break;
    }
    return DefWindowProcW(msgWindow_, msg, wParam, lParam);
}

int App::MessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    StopCapture();
    settings_.Shutdown();
    presenter_.Destroy();
    return static_cast<int>(msg.wParam);
}

bool App::AcquireTarget(const std::wstring& needle) {
    auto info = capture::WindowFinder::FindByTitleOrExe(needle);
    if (!info.hwnd) return false;
    StartCaptureOn(info.hwnd, info.title);
    return true;
}

void App::StartCaptureOn(HWND target, const std::wstring& title) {
    StopCapture();

    targetTitleLive_ = title;
    targetMinimized_ = IsIconic(target);
    targetForeground_ = (GetForegroundWindow() == target);

    presenter::WindowTracker::Callbacks callbacks;
    callbacks.onRectChanged = [this] { SyncPresenterRect(); };
    callbacks.onMinimizedChanged = [this](bool minimized) { OnTargetMinimized(minimized); };
    callbacks.onDestroyed = [this] { OnTargetDestroyed(); };
    callbacks.onForegroundChanged = [this](HWND fg) { OnForegroundChanged(fg); };
    if (!tracker_.Attach(target, std::move(callbacks))) return;

    capture::CaptureSession::Source source =
        options_.testRateMonitor ? capture::CaptureSession::Source::Monitor : DesiredSource();
    if (!capture_.Start(
            d3d_.WinRTDevice(), target, source,
            [this](ID3D11Texture2D* frame, UINT w, UINT h) { OnFrame(frame, w, h); },
            [this] { OnTargetDestroyed(); })) {
        tracker_.Detach();
        return;
    }

    lastRect_ = RECT{};
    SyncPresenterRect();
    UpdatePresenterVisibility();
}

capture::CaptureSession::Source App::DesiredSource() const {
    // Monitor capture delivers at full compositor rate where window capture
    // is throttled (~60fps) — use it whenever the target covers the monitor
    // (borderless fullscreen). Window capture remains the windowed fallback.
    if (!tracker_.IsAttached()) return capture::CaptureSession::Source::Window;
    RECT r = tracker_.GetRect();
    HMONITOR monitor = MonitorFromWindow(tracker_.Target(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &mi)) return capture::CaptureSession::Source::Window;
    auto closeTo = [](LONG a, LONG b) { return (a > b ? a - b : b - a) <= 2; };
    const RECT& m = mi.rcMonitor;
    const bool covers = closeTo(r.left, m.left) && closeTo(r.top, m.top) &&
                        closeTo(r.right, m.right) && closeTo(r.bottom, m.bottom);
    return covers ? capture::CaptureSession::Source::Monitor
                  : capture::CaptureSession::Source::Window;
}

void App::StopCapture() {
    capture_.Stop();
    tracker_.Detach();
    presenter_.Hide();
    targetTitleLive_.clear();
}

void App::SyncPresenterRect() {
    if (!tracker_.IsAttached()) return;

    RECT rect = tracker_.GetRect();
    if (config_.fitToMonitor) {
        HMONITOR monitor = MonitorFromWindow(tracker_.Target(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(monitor, &mi)) rect = mi.rcMonitor;
    }

    if (!RectsEqual(rect, lastRect_)) {
        lastRect_ = rect;
        // MatchRect resizes the swapchain — exclusive with the frame worker's
        // Present.
        std::lock_guard<std::recursive_mutex> lock(gpuMutex_);
        presenter_.MatchRect(rect);
    }
}

void App::OnFrame(ID3D11Texture2D* frame, UINT contentW, UINT contentH) {
    if (!options_.testCaptureTarget.empty()) {
        HandleTestCaptureFrame(frame, contentW, contentH);
        return;
    }
    if (!options_.testRateTarget.empty()) {
        HandleTestRateFrame();
        return;
    }

    lastInW_ = contentW;
    lastInH_ = contentH;

    // Arrival rate, counted before any gating — this is what WGC delivers.
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    ++arrivalFrames_;
    if (arrivalWindowStart_ == 0) arrivalWindowStart_ = now.QuadPart;
    const double arrivalWindowMs = (now.QuadPart - arrivalWindowStart_) * qpcToMs_;
    if (arrivalWindowMs >= 1000.0) {
        captureFps_ = static_cast<float>(arrivalFrames_ * 1000.0 / arrivalWindowMs);
        arrivalFrames_ = 0;
        arrivalWindowStart_ = now.QuadPart;
    }

    if (!PresenterShouldShow()) return;

    LARGE_INTEGER t0{}, t1{};
    QueryPerformanceCounter(&t0);

    const UINT outW = presenter_.Width();
    const UINT outH = presenter_.Height();
    if (!outW || !outH) return;

    HRESULT presentHr = S_OK;
    try {
        std::lock_guard<std::recursive_mutex> lock(gpuMutex_);
        ID3D11Texture2D* processed = pipeline_.Process(d3d_.Context(), frame, contentW,
                                                       contentH, outW, outH,
                                                       BuildProcessParams());
        if (!processed) return;
        presentHr = presenter_.PresentFrame(d3d_.Context(), processed);
    } catch (...) {
        // GPU work threw (device removed mid-frame on display/GPU switch)
        RestartSelf();
        return;
    }
    if (presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET) {
        RestartSelf();
        return;
    }
    presenter_.ShowNoActivate();

    QueryPerformanceCounter(&t1);
    const float ms = static_cast<float>((t1.QuadPart - t0.QuadPart) * qpcToMs_);
    processMsEma_ = processMsEma_ == 0.0f ? ms : processMsEma_ * 0.95f + ms * 0.05f;

    ++statFrames_;
    if (statWindowStart_ == 0) statWindowStart_ = t1.QuadPart;
    const double windowMs = (t1.QuadPart - statWindowStart_) * qpcToMs_;
    if (windowMs >= 1000.0) {
        fps_ = static_cast<float>(statFrames_ * 1000.0 / windowMs);
        statFrames_ = 0;
        statWindowStart_ = t1.QuadPart;
    }
}

gpu::ShaderPipeline::ProcessParams App::BuildProcessParams() {
    gpu::ShaderPipeline::ProcessParams params;
    params.mode = config_.passthrough ? gpu::ShaderPipeline::Mode::Passthrough
                                      : gpu::ShaderPipeline::Mode::FSR;
    params.sharpness = config_.sharpness;
    params.fxaa = config_.fxaa;
    params.fxaaTextProtect = config_.fxaaTextProtect;
    params.debandStrength = config_.debandStrength;
    params.clarity = config_.clarity;
    params.bloom = config_.bloom;
    params.bloomThreshold = config_.bloomThreshold;
    params.vibrance = config_.vibrance;
    params.saturation = config_.saturation;
    params.contrast = config_.contrast;
    params.gamma = config_.gamma;
    params.exposure = config_.exposure;
    params.filmic = config_.filmic;
    params.vignette = config_.vignette;
    params.grain = config_.grain;
    params.frameIndex = frameCounter_++;
    return params;
}

bool App::PresenterShouldShow() const {
    return capture_.IsActive() && config_.enabled && !bypassed_ && !targetMinimized_ &&
           targetForeground_;
}

void App::UpdatePresenterVisibility() {
    if (PresenterShouldShow()) {
        SyncPresenterRect();
        // becomes visible on the next presented frame (avoids flashing a stale one)
    } else {
        presenter_.Hide();
    }
}

void App::RestartSelf() {
    if (restarting_) return;
    restarting_ = true;

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = GetCommandLineW(); // CreateProcess wants it mutable
    if (CreateProcessW(exe, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                       &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    // May be called from the frame worker thread; route through the main loop.
    PostMessageW(msgWindow_, kMsgQuit, 0, 0);
}

void App::OnTargetMinimized(bool minimized) {
    targetMinimized_ = minimized;
    UpdatePresenterVisibility();
}

void App::OnTargetDestroyed() {
    StopCapture();
    // kTimerPoll keeps trying to re-acquire the target by title.
}

void App::OnForegroundChanged(HWND foreground) {
    if (!tracker_.IsAttached()) return;

    DWORD fgPid = 0;
    GetWindowThreadProcessId(foreground, &fgPid);

    // Keep showing when the game — or one of our own windows — is foreground.
    targetForeground_ =
        (foreground == tracker_.Target()) || (fgPid == GetCurrentProcessId());
    UpdatePresenterVisibility();
}

int App::RunTestFind() {
    auto info = capture::WindowFinder::FindByTitleOrExe(options_.testFindTarget);
    if (!info.hwnd) {
        wprintf(L"test-find: no window matching '%s'\n", options_.testFindTarget.c_str());
        return 2;
    }
    wprintf(L"test-find: found '%s' (%s) hwnd=%p\n", info.title.c_str(), info.exeName.c_str(),
            static_cast<void*>(info.hwnd));

    presenter::WindowTracker tracker;
    presenter::WindowTracker::Callbacks callbacks; // rect polled below
    tracker.Attach(info.hwnd, std::move(callbacks));

    for (int i = 0; i < 10; ++i) {
        if (!IsWindow(info.hwnd)) {
            wprintf(L"test-find: window closed\n");
            return 0;
        }
        RECT r = tracker.GetRect();
        wprintf(L"test-find: rect (%ld,%ld)-(%ld,%ld) %ldx%ld minimized=%d\n", r.left, r.top,
                r.right, r.bottom, r.right - r.left, r.bottom - r.top,
                tracker.IsMinimized() ? 1 : 0);
        fflush(stdout);
        // pump messages so WinEvent hooks stay healthy during the test
        MSG msg{};
        ULONGLONG end = GetTickCount64() + 500;
        while (GetTickCount64() < end) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(20);
        }
    }
    return 0;
}

void App::HandleTestRateFrame() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (rateStart_ == 0) {
        rateStart_ = now.QuadPart;
        rateFrames_ = 0;
        return;
    }
    ++rateFrames_;
    const double elapsedMs = (now.QuadPart - rateStart_) * qpcToMs_;
    if (elapsedMs >= 5000.0) {
        const double rate = rateFrames_ * 1000.0 / elapsedMs;
        wprintf(L"test-rate: %.1f frames/sec over %.0f ms (%u frames)\n", rate, elapsedMs,
                rateFrames_);
        fflush(stdout);
        exitCode_ = 0;
        PostMessageW(msgWindow_, kMsgQuit, 0, 0); // worker thread — post to main
    }
}

void App::HandleTestCaptureFrame(ID3D11Texture2D* frame, UINT w, UINT h) {
    // Dump the first frame: WGC only produces frames when window content
    // changes, so a static window may never deliver a second one.
    if (++testFramesSeen_ != 1) return;

    const std::wstring dir = options_.outDir.empty() ? ExeDir() : options_.outDir + L"\\";

    const bool rawOk =
        util::SaveTexturePng(d3d_.Device(), d3d_.Context(), frame, dir + L"capture_raw.png");

    // Process at 1.5x to exercise a real EASU upscale, then dump.
    bool fsrOk = false;
    ID3D11Texture2D* processed = pipeline_.Process(d3d_.Context(), frame, w, h, w * 3 / 2,
                                                   h * 3 / 2, BuildProcessParams());
    if (processed) {
        fsrOk = util::SaveTexturePng(d3d_.Device(), d3d_.Context(), processed,
                                     dir + L"capture_fsr.png");
    }

    wprintf(L"test-capture: input %ux%u raw=%s fsr=%s dir=%s\n", w, h, rawOk ? L"ok" : L"FAIL",
            fsrOk ? L"ok" : L"FAIL", dir.c_str());
    fflush(stdout);
    exitCode_ = (rawOk && fsrOk) ? 0 : 4;
    PostMessageW(msgWindow_, kMsgQuit, exitCode_, 0); // worker thread — post to main
}

} // namespace app
