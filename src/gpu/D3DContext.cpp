#include "D3DContext.h"

namespace gpu {

bool D3DContext::Init() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                   device_.put(), nullptr, context_.put());
#ifdef _DEBUG
    if (FAILED(hr)) { // debug layer not installed — retry without it
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               device_.put(), nullptr, context_.put());
    }
#endif
    if (FAILED(hr)) return false;

    auto dxgiDevice = device_.as<IDXGIDevice>();
    winrtDevice_ = util::CreateDirect3DDevice(dxgiDevice.get());
    return true;
}

winrt::com_ptr<IDXGISwapChain1> D3DContext::CreateSwapChainForWindow(HWND hwnd, UINT width,
                                                                     UINT height) {
    auto dxgiDevice = device_.as<IDXGIDevice>();
    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::check_hresult(dxgiDevice->GetAdapter(adapter.put()));
    winrt::com_ptr<IDXGIFactory2> factory;
    winrt::check_hresult(adapter->GetParent(winrt::guid_of<IDXGIFactory2>(), factory.put_void()));

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    winrt::com_ptr<IDXGISwapChain1> swapchain;
    winrt::check_hresult(factory->CreateSwapChainForHwnd(device_.get(), hwnd, &desc, nullptr,
                                                         nullptr, swapchain.put()));
    // We manage z-order and fullscreen state ourselves.
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    return swapchain;
}

} // namespace gpu
