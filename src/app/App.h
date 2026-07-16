#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "Config.h"
#include "../capture/CaptureSession.h"
#include "../capture/WindowFinder.h"
#include "../gpu/D3DContext.h"
#include "../gpu/ShaderPipeline.h"
#include "../presenter/PresenterWindow.h"
#include "../presenter/WindowTracker.h"
#include "../ui/HotkeyManager.h"
#include "../ui/SettingsOverlay.h"

namespace app {

struct AppOptions {
    bool smokeTest = false;             // open/close cleanly, exit 0
    std::wstring testCaptureTarget;     // capture N frames, dump raw+processed PNGs, exit
    std::wstring testFindTarget;        // print target rect for a few seconds, exit
    std::wstring testRateTarget;        // measure capture arrival rate for ~5s, exit
    bool testRateMonitor = false;       // force monitor capture for --test-rate
    std::wstring targetOverride;        // overrides config targetTitle
    std::wstring outDir;                // where test PNGs go (default: exe dir)
};

class App {
public:
    int Run(HINSTANCE instance, const AppOptions& options);

private:
    // main loop plumbing
    bool InitCore();
    bool CreateMessageWindow();
    int MessageLoop();
    static LRESULT CALLBACK MsgWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // capture lifecycle
    bool AcquireTarget(const std::wstring& needle);
    void StartCaptureOn(HWND target, const std::wstring& title);
    capture::CaptureSession::Source DesiredSource() const;
    void StopCapture();
    void OnFrame(ID3D11Texture2D* frame, UINT contentW, UINT contentH);

    // tracker events
    void SyncPresenterRect();
    void OnTargetMinimized(bool minimized);
    void OnTargetDestroyed();
    void OnForegroundChanged(HWND foreground);

    void UpdatePresenterVisibility();
    bool PresenterShouldShow() const;

    // Presents a texture on the presenter (locks, counts stats, handles
    // device-removed). Called from the frame worker and the interp thread.
    void PresentTexture(ID3D11Texture2D* texture);

    // Phase 6: paced present thread doubling perceived frame rate by
    // inserting a blended frame between real ones (real frames delayed by
    // half an interval).
    void StartInterpolationThread();
    void StopInterpolationThread();
    void InterpolationLoop();

    // Device-lost recovery: every GPU object (capture session, swapchains,
    // ImGui backend) hangs off the dead device, so relaunch cleanly instead
    // of attempting piecemeal reconstruction.
    void RestartSelf();

    // test modes
    int RunTestFind();
    void HandleTestCaptureFrame(ID3D11Texture2D* frame, UINT w, UINT h);
    void HandleTestRateFrame();

    HINSTANCE instance_ = nullptr;
    AppOptions options_;

    Config config_;
    gpu::D3DContext d3d_;
    gpu::ShaderPipeline pipeline_;
    capture::CaptureSession capture_;
    presenter::WindowTracker tracker_;
    presenter::PresenterWindow presenter_;
    ui::SettingsOverlay settings_;
    ui::HotkeyManager hotkeys_;

    HWND msgWindow_ = nullptr;
    std::wstring targetTitleLive_;

    // Frame callbacks arrive on a WGC worker thread (free-threaded pool);
    // the shared D3D context / swapchains are guarded by this. Recursive
    // because config-change handlers re-enter SyncPresenterRect while the
    // settings render already holds it.
    std::recursive_mutex gpuMutex_;
    HWND pendingTarget_ = nullptr; // set by UI, consumed on the main thread
    RECT lastRect_{};
    bool bypassed_ = false;
    bool targetForeground_ = true;
    bool targetMinimized_ = false;

    // stats
    double qpcToMs_ = 0.0;
    float processMsEma_ = 0.0f;
    float fps_ = 0.0f;
    UINT statFrames_ = 0;
    LONGLONG statWindowStart_ = 0;
    UINT lastInW_ = 0, lastInH_ = 0;
    UINT frameCounter_ = 0;
    // capture arrival rate (counts every FrameArrived, even when not presenting)
    float captureFps_ = 0.0f;
    UINT arrivalFrames_ = 0;
    LONGLONG arrivalWindowStart_ = 0;
    // --test-rate accumulators
    UINT rateFrames_ = 0;
    LONGLONG rateStart_ = 0;

    // interpolation thread state
    std::thread interpThread_;
    std::atomic<bool> interpRun_{false};
    HANDLE newFrameEvent_ = nullptr;
    std::atomic<float> arrivalIntervalMs_{11.1f};
    LONGLONG lastArrivalQpc_ = 0;

    gpu::ShaderPipeline::ProcessParams BuildProcessParams();

    int testFramesSeen_ = 0;
    int exitCode_ = 0;
    bool restarting_ = false;
};

} // namespace app
