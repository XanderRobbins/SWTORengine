#pragma once

#include <windows.h>
#include <string>

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
    void StopCapture();
    void OnFrame(ID3D11Texture2D* frame, UINT contentW, UINT contentH);

    // tracker events
    void SyncPresenterRect();
    void OnTargetMinimized(bool minimized);
    void OnTargetDestroyed();
    void OnForegroundChanged(HWND foreground);

    void UpdatePresenterVisibility();
    bool PresenterShouldShow() const;

    // Device-lost recovery: every GPU object (capture session, swapchains,
    // ImGui backend) hangs off the dead device, so relaunch cleanly instead
    // of attempting piecemeal reconstruction.
    void RestartSelf();

    // test modes
    int RunTestFind();
    void HandleTestCaptureFrame(ID3D11Texture2D* frame, UINT w, UINT h);

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

    gpu::ShaderPipeline::ProcessParams BuildProcessParams();

    int testFramesSeen_ = 0;
    int exitCode_ = 0;
    bool restarting_ = false;
};

} // namespace app
