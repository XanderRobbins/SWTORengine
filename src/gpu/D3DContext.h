#pragma once

#include "../util/D3DHelpers.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <string>

namespace gpu {

// Owns the single D3D11 device/context shared by capture, processing and presentation.
class D3DContext {
public:
    bool Init();

    // Description of the adapter the device was created on (diagnostics).
    const std::wstring& AdapterName() const { return adapterName_; }

    ID3D11Device* Device() const { return device_.get(); }
    ID3D11DeviceContext* Context() const { return context_.get(); }
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice WinRTDevice() const {
        return winrtDevice_;
    }

    // Flip-model swapchain on an HWND, R8G8B8A8_UNORM (matches pipeline output
    // so the final blit is a plain resource copy).
    winrt::com_ptr<IDXGISwapChain1> CreateSwapChainForWindow(HWND hwnd, UINT width, UINT height);

private:
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice_{nullptr};
    std::wstring adapterName_;
};

} // namespace gpu
