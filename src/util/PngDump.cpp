#include "PngDump.h"

#include <winrt/base.h>
#include <wincodec.h>

namespace util {

bool SaveTexturePng(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* texture,
                    const std::wstring& path) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    winrt::com_ptr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, staging.put()))) return false;
    ctx->CopyResource(staging.get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    bool ok = false;
    {
        winrt::com_ptr<IWICImagingFactory> factory;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(factory.put())))) {
            winrt::com_ptr<IWICStream> stream;
            winrt::com_ptr<IWICBitmapEncoder> encoder;
            winrt::com_ptr<IWICBitmapFrameEncode> frame;

            const WICPixelFormatGUID srcFormat = (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
                                                     ? GUID_WICPixelFormat32bppRGBA
                                                     : GUID_WICPixelFormat32bppBGRA;
            WICPixelFormatGUID format = srcFormat;

            if (SUCCEEDED(factory->CreateStream(stream.put())) &&
                SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
                SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                                 encoder.put())) &&
                SUCCEEDED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) &&
                SUCCEEDED(encoder->CreateNewFrame(frame.put(), nullptr)) &&
                SUCCEEDED(frame->Initialize(nullptr)) &&
                SUCCEEDED(frame->SetSize(desc.Width, desc.Height)) &&
                SUCCEEDED(frame->SetPixelFormat(&format)) &&
                SUCCEEDED(frame->WritePixels(desc.Height, mapped.RowPitch,
                                             mapped.RowPitch * desc.Height,
                                             static_cast<BYTE*>(mapped.pData))) &&
                SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
                ok = true;
            }
        }
    }

    ctx->Unmap(staging.get(), 0);
    return ok;
}

} // namespace util
