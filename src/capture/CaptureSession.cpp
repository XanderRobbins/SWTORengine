#include "CaptureSession.h"

#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <algorithm>

namespace capture {

using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;

CaptureSession::~CaptureSession() {
    Stop();
}

bool CaptureSession::Start(
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device, HWND target,
    FrameCallback onFrame, ClosedCallback onClosed) {
    Stop();

    device_ = device;
    onFrame_ = std::move(onFrame);
    onClosed_ = std::move(onClosed);

    // GraphicsCaptureItem::CreateForWindow (the COM interop path for Win32 apps).
    auto factory = winrt::get_activation_factory<GraphicsCaptureItem>();
    auto interop = factory.as<IGraphicsCaptureItemInterop>();
    HRESULT hr = interop->CreateForWindow(target, winrt::guid_of<GraphicsCaptureItem>(),
                                          winrt::put_abi(item_));
    if (FAILED(hr) || !item_) {
        item_ = nullptr;
        return false;
    }

    poolSize_ = item_.Size();
    if (poolSize_.Width <= 0 || poolSize_.Height <= 0) poolSize_ = {1, 1};

    framePool_ = Direct3D11CaptureFramePool::Create(
        device_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize_);
    frameArrivedToken_ = framePool_.FrameArrived({this, &CaptureSession::OnFrameArrived});

    closedToken_ = item_.Closed([this](auto&&, auto&&) {
        if (onClosed_) onClosed_();
    });

    session_ = framePool_.CreateCaptureSession(item_);

    // Best-effort niceties; both can be unavailable/denied depending on OS build.
    try {
        session_.IsCursorCaptureEnabled(false); // OS draws the real cursor on top anyway
    } catch (...) {}
    try {
        session_.IsBorderRequired(false); // suppress the yellow capture border
    } catch (...) {}

    session_.StartCapture();
    return true;
}

void CaptureSession::Stop() {
    if (framePool_ && frameArrivedToken_.value) {
        framePool_.FrameArrived(frameArrivedToken_);
        frameArrivedToken_ = {};
    }
    if (item_ && closedToken_.value) {
        item_.Closed(closedToken_);
        closedToken_ = {};
    }
    if (session_) {
        session_.Close();
        session_ = nullptr;
    }
    if (framePool_) {
        framePool_.Close();
        framePool_ = nullptr;
    }
    item_ = nullptr;
    onFrame_ = nullptr;
    onClosed_ = nullptr;
}

void CaptureSession::OnFrameArrived(Direct3D11CaptureFramePool const& pool,
                                    winrt::Windows::Foundation::IInspectable const&) {
    // Drain to the newest frame; processing stale ones would add latency that
    // never recovers once we fall behind the game's present rate.
    auto frame = pool.TryGetNextFrame();
    if (!frame) return;
    while (auto newer = pool.TryGetNextFrame()) {
        frame = newer;
    }

    const SizeInt32 contentSize = frame.ContentSize();

    auto texture =
        util::GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());

    if (onFrame_) {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        // Content can lag/lead the pool texture size mid-resize; clamp.
        const UINT w = std::min<UINT>(static_cast<UINT>(std::max(contentSize.Width, 1)), desc.Width);
        const UINT h = std::min<UINT>(static_cast<UINT>(std::max(contentSize.Height, 1)), desc.Height);
        onFrame_(texture.get(), w, h);
    }

    // Grow the pool when the window got bigger than the pool textures.
    if (contentSize.Width > poolSize_.Width || contentSize.Height > poolSize_.Height) {
        poolSize_ = contentSize;
        framePool_.Recreate(device_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize_);
    }
}

} // namespace capture
