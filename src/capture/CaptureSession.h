#pragma once

#include "../util/D3DHelpers.h"
#include <winrt/Windows.Graphics.Capture.h>
#include <functional>
#include <mutex>

namespace capture {

// Wraps Windows.Graphics.Capture for one target HWND. Uses a FREE-THREADED
// frame pool: FrameArrived fires on a WGC worker thread at full compositor
// rate (the dispatcher-based pool coalesces delivery to ~60/s regardless of
// refresh rate). Callers must synchronize their own GPU state — the frame
// callback runs concurrently with the UI thread.
class CaptureSession {
public:
    // Texture is only valid for the duration of the callback.
    using FrameCallback =
        std::function<void(ID3D11Texture2D* frame, UINT contentW, UINT contentH)>;
    using ClosedCallback = std::function<void()>;

    ~CaptureSession();

    // Window capture is throttled (~60fps) on some systems regardless of
    // refresh rate; monitor capture rides the compositor at full compose
    // rate. Use monitor mode when the target covers the monitor.
    enum class Source { Window, Monitor };

    bool Start(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
               HWND target, Source source, FrameCallback onFrame, ClosedCallback onClosed);

    Source ActiveSource() const { return source_; }
    void Stop();
    bool IsActive() const { return session_ != nullptr; }

private:
    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& pool,
        winrt::Windows::Foundation::IInspectable const&);

    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token frameArrivedToken_{};
    winrt::event_token closedToken_{};
    winrt::Windows::Graphics::SizeInt32 poolSize_{};
    FrameCallback onFrame_;
    ClosedCallback onClosed_;
    std::mutex mutex_; // serializes frame callbacks against Stop()
    Source source_ = Source::Window;
};

} // namespace capture
