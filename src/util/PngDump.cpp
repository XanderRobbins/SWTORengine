#include "PngDump.h"

#include <winrt/base.h>
#include <wincodec.h>
#include <vector>

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
                SUCCEEDED(frame->SetPixelFormat(&format))) {
                // SetPixelFormat NEGOTIATES: the PNG encoder has no native
                // 32bppRGBA, so it hands back BGRA — writing RGBA bytes as-is
                // would swap red/blue in the file. Swizzle when it changed.
                const bool swizzle = IsEqualGUID(srcFormat, GUID_WICPixelFormat32bppRGBA) &&
                                     !IsEqualGUID(format, GUID_WICPixelFormat32bppRGBA);
                const BYTE* src = static_cast<const BYTE*>(mapped.pData);
                HRESULT whr;
                if (swizzle) {
                    std::vector<BYTE> row(desc.Width * 4);
                    whr = S_OK;
                    for (UINT y = 0; y < desc.Height && SUCCEEDED(whr); ++y) {
                        const BYTE* s = src + static_cast<size_t>(y) * mapped.RowPitch;
                        for (UINT x = 0; x < desc.Width; ++x) {
                            row[x * 4 + 0] = s[x * 4 + 2];
                            row[x * 4 + 1] = s[x * 4 + 1];
                            row[x * 4 + 2] = s[x * 4 + 0];
                            row[x * 4 + 3] = s[x * 4 + 3];
                        }
                        whr = frame->WritePixels(1, desc.Width * 4,
                                                 static_cast<UINT>(row.size()), row.data());
                    }
                } else {
                    whr = frame->WritePixels(desc.Height, mapped.RowPitch,
                                             mapped.RowPitch * desc.Height,
                                             const_cast<BYTE*>(src));
                }
                if (SUCCEEDED(whr) && SUCCEEDED(frame->Commit()) &&
                    SUCCEEDED(encoder->Commit())) {
                    ok = true;
                }
            }
        }
    }

    ctx->Unmap(staging.get(), 0);
    return ok;
}

} // namespace util
