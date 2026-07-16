#pragma once

#include <d3d11.h>
#include <string>

namespace util {

// Copies a GPU texture (any 32bpp RGBA/BGRA UNORM format) to CPU and writes a
// PNG via WIC. Test/diagnostic path only — never on the hot loop.
bool SaveTexturePng(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* texture,
                    const std::wstring& path);

} // namespace util
