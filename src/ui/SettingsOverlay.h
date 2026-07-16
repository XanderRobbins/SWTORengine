#pragma once

#include <winrt/base.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <functional>
#include <string>
#include <vector>

#include "../app/Config.h"
#include "../capture/WindowFinder.h"

namespace gpu { class D3DContext; }

namespace ui {

// ImGui settings panel in its own normal window (the presenter is
// WS_EX_TRANSPARENT and cannot receive mouse input, so UI drawn there
// would be un-clickable).
class SettingsOverlay {
public:
    struct Stats {
        float processMs = 0.0f;  // CPU cost of dispatch+present per frame
        float fps = 0.0f;        // frames presented per second
        float captureFps = 0.0f; // frames delivered by WGC per second
        UINT inW = 0, inH = 0, outW = 0, outH = 0;
        bool captureActive = false;
        bool bypassed = false;
        std::wstring targetTitle;
    };

    std::function<void(HWND)> onTargetSelected;
    std::function<void()> onConfigChanged;

    bool Init(HINSTANCE instance, gpu::D3DContext& d3d, app::Config* config);
    void Shutdown();

    void Toggle();
    void Hide();
    bool IsVisible() const { return visible_; }
    HWND Hwnd() const { return hwnd_; }

    // Called ~60Hz by the app while visible.
    void Render(const Stats& stats);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    void EnsureRenderTarget();

    HWND hwnd_ = nullptr;
    gpu::D3DContext* d3d_ = nullptr;
    app::Config* config_ = nullptr;
    winrt::com_ptr<IDXGISwapChain1> swapchain_;
    winrt::com_ptr<ID3D11RenderTargetView> rtv_;
    bool visible_ = false;
    bool imguiReady_ = false;
    bool resizePending_ = false;

    std::vector<capture::WindowInfo> pickerWindows_;
};

} // namespace ui
