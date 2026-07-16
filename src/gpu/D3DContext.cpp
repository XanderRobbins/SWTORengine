#include "D3DContext.h"

namespace gpu {

namespace {

// On hybrid-GPU laptops the default D3D adapter can differ from the one
// driving the display; WGC then copies every frame across adapters through
// system memory, throttling capture hard. Pick the adapter that owns the
// primary monitor's output.
winrt::com_ptr<IDXGIAdapter1> AdapterForPrimaryDisplay() {
    winrt::com_ptr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(winrt::guid_of<IDXGIFactory1>(), factory.put_void()))) {
        return nullptr;
    }
    for (UINT a = 0;; ++a) {
        winrt::com_ptr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, adapter.put()) != S_OK) break;
        for (UINT o = 0;; ++o) {
            winrt::com_ptr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, output.put()) != S_OK) break;
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop) {
                MONITORINFO mi{sizeof(MONITORINFO)};
                if (GetMonitorInfoW(desc.Monitor, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY)) {
                    return adapter;
                }
            }
        }
    }
    return nullptr;
}

} // namespace

bool D3DContext::Init() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    winrt::com_ptr<IDXGIAdapter1> preferred = AdapterForPrimaryDisplay();

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = E_FAIL;
    if (preferred) {
        hr = D3D11CreateDevice(preferred.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, device_.put(),
                               nullptr, context_.put());
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               device_.put(), nullptr, context_.put());
    }
#ifdef _DEBUG
    if (FAILED(hr)) { // debug layer not installed — retry without it
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               device_.put(), nullptr, context_.put());
    }
#endif
    if (FAILED(hr)) return false;

    if (auto dxgiDev = device_.try_as<IDXGIDevice>()) {
        winrt::com_ptr<IDXGIAdapter> actual;
        if (SUCCEEDED(dxgiDev->GetAdapter(actual.put()))) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(actual->GetDesc(&desc))) adapterName_ = desc.Description;
        }
    }

    auto dxgiDevice = device_.as<IDXGIDevice>();

    // Cap the present queue (default 3 buffers extra latency). The budget is
    // device-wide and two swapchains share it (presenter + settings panel);
    // 1 makes them block on each other, so use 2. Flip-discard still shows
    // only the newest frame at compose, so display latency stays minimal.
    if (auto dxgiDevice1 = device_.try_as<IDXGIDevice1>()) {
        dxgiDevice1->SetMaximumFrameLatency(2);
    }

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
