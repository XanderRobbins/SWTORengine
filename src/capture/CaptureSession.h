#pragma once

#include "../util/D3DHelpers.h"
#include <winrt/Windows.Graphics.Capture.h>
#include <functional>

namespace capture {

// Wraps Windows.Graphics.Capture for one target HWND. Created on a thread
// with a DispatcherQueue; FrameArrived fires on that same thread (no locking
// needed anywhere in the app).
class CaptureSession {
public:
    // Texture is only valid for the duration of the callback.
    using FrameCallback =
        std::function<void(ID3D11Texture2D* frame, UINT contentW, UINT contentH)>;
    using ClosedCallback = std::function<void()>;

    ~CaptureSession();

    bool Start(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
               HWND target, FrameCallback onFrame, ClosedCallback onClosed);
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
};

} // namespace capture
