#include "PresenterWindow.h"

#include "../gpu/D3DContext.h"
#include <algorithm>

namespace presenter {

namespace {
constexpr wchar_t kClassName[] = L"ChimeraPresenterWindow";
}

LRESULT CALLBACK PresenterWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT; // belt-and-suspenders on top of WS_EX_TRANSPARENT
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool PresenterWindow::Create(HINSTANCE instance, gpu::D3DContext& d3d) {
    d3d_ = &d3d;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc); // ok if already registered

    // TOOLWINDOW keeps it out of alt-tab; NOACTIVATE keeps focus on the game.
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kClassName, L"Chimera Presenter", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!hwnd_) return false;

    // A layered window only renders once an alpha/colorkey is set; fully opaque.
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);

    // Never let the presenter itself be captured (prevents feedback loops if
    // the user points a capture tool — or us — at it). Win10 2004+; best effort.
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);

    return true;
}

void PresenterWindow::Destroy() {
    swapchain_ = nullptr;
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void PresenterWindow::MatchRect(const RECT& rect) {
    if (!hwnd_) return;

    const UINT w = static_cast<UINT>(std::max<LONG>(rect.right - rect.left, 1));
    const UINT h = static_cast<UINT>(std::max<LONG>(rect.bottom - rect.top, 1));

    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, static_cast<int>(w),
                 static_cast<int>(h),
                 SWP_NOACTIVATE | (shown_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

    if (!swapchain_) {
        swapchain_ = d3d_->CreateSwapChainForWindow(hwnd_, w, h);
        width_ = w;
        height_ = h;
    } else if (w != width_ || h != height_) {
        swapchain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        width_ = w;
        height_ = h;
    }
}

void PresenterWindow::ShowNoActivate() {
    if (!hwnd_ || shown_) return;
    ShowWindow(hwnd_, SW_SHOWNA);
    shown_ = true;
}

void PresenterWindow::Hide() {
    if (!hwnd_ || !shown_) return;
    ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
}

HRESULT PresenterWindow::PresentFrame(ID3D11DeviceContext* ctx, ID3D11Texture2D* processed) {
    if (!swapchain_ || !processed) return S_OK;

    winrt::com_ptr<ID3D11Texture2D> backbuffer;
    HRESULT hr =
        swapchain_->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backbuffer.put_void());
    if (FAILED(hr)) return hr;

    D3D11_TEXTURE2D_DESC src{}, dst{};
    processed->GetDesc(&src);
    backbuffer->GetDesc(&dst);

    // Sizes can disagree for one frame mid-resize; copy the overlapping region.
    D3D11_BOX box{};
    box.right = std::min(src.Width, dst.Width);
    box.bottom = std::min(src.Height, dst.Height);
    box.back = 1;
    ctx->CopySubresourceRegion(backbuffer.get(), 0, 0, 0, 0, processed, 0, &box);

    return swapchain_->Present(0, 0);
}

} // namespace presenter
