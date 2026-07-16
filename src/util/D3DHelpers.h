#pragma once

// unknwn.h must precede any C++/WinRT header so classic-COM interop
// interfaces (IGraphicsCaptureItemInterop etc.) are visible to winrt.
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <d3d11.h>

namespace util {

inline winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
CreateDirect3DDevice(IDXGIDevice* dxgiDevice) {
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, inspectable.put()));
    return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

template <typename T>
winrt::com_ptr<T> GetDXGIInterfaceFromObject(winrt::Windows::Foundation::IInspectable const& object) {
    auto access = object.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<T> result;
    winrt::check_hresult(access->GetInterface(winrt::guid_of<T>(), result.put_void()));
    return result;
}

} // namespace util
