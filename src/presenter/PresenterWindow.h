#pragma once

#include <winrt/base.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>

namespace gpu { class D3DContext; }

namespace presenter {

// The load-bearing trick: a borderless WS_EX_LAYERED | WS_EX_TRANSPARENT |
// WS_EX_TOPMOST window. It renders normally but the OS skips it during mouse
// hit-testing, so all input lands on the game window underneath.
class PresenterWindow {
public:
    bool Create(HINSTANCE instance, gpu::D3DContext& d3d);
    void Destroy();

    // Pin exactly over the target rect (screen coords); resizes swapchain if needed.
    void MatchRect(const RECT& rect);

    void ShowNoActivate();
    void Hide();
    bool IsShown() const { return shown_; }
    HWND Hwnd() const { return hwnd_; }

    // Copy the processed texture (R8G8B8A8, sized to the current rect) into
    // the backbuffer and present immediately (no vsync wait — latency first).
    void PresentFrame(ID3D11DeviceContext* ctx, ID3D11Texture2D* processed);

    UINT Width() const { return width_; }
    UINT Height() const { return height_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    gpu::D3DContext* d3d_ = nullptr;
    winrt::com_ptr<IDXGISwapChain1> swapchain_;
    UINT width_ = 0, height_ = 0;
    bool shown_ = false;
};

} // namespace presenter
