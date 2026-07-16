#pragma once

#include <winrt/base.h>
#include <d3d11.h>
#include <string>

namespace gpu {

// Compiles and dispatches the compute passes:
//   FSR mode:         input --EASU(upscale)--> mid --RCAS(sharpen)--> out
//   Passthrough mode: input --bilinear-------------------------------> out
// Output textures are owned by the pipeline and reused across frames.
class ShaderPipeline {
public:
    enum class Mode { FSR, Passthrough };

    bool Init(ID3D11Device* device, const std::wstring& shaderDir, std::wstring* error = nullptr);

    // viewportW/H: valid content region of the input texture (may be smaller
    // than the texture itself mid-resize). Returns the processed texture
    // (outW x outH, R8G8B8A8_UNORM) or nullptr on failure.
    ID3D11Texture2D* Process(ID3D11DeviceContext* ctx, ID3D11Texture2D* input,
                             UINT viewportW, UINT viewportH, UINT outW, UINT outH,
                             float sharpnessStops, Mode mode);

private:
    bool CompileCS(const std::wstring& path, winrt::com_ptr<ID3D11ComputeShader>& out,
                   std::wstring* error);
    bool EnsureOutputTextures(UINT outW, UINT outH);
    ID3D11ShaderResourceView* GetInputSRV(ID3D11Texture2D* input);

    winrt::com_ptr<ID3D11Device> device_;

    winrt::com_ptr<ID3D11ComputeShader> easuCS_;
    winrt::com_ptr<ID3D11ComputeShader> rcasCS_;
    winrt::com_ptr<ID3D11ComputeShader> passthroughCS_;
    winrt::com_ptr<ID3D11SamplerState> linearClamp_;

    winrt::com_ptr<ID3D11Buffer> cbEasu_;        // 4x uint4
    winrt::com_ptr<ID3D11Buffer> cbRcas_;        // 1x uint4
    winrt::com_ptr<ID3D11Buffer> cbPassthrough_; // uint2 + float2

    winrt::com_ptr<ID3D11Texture2D> mid_;
    winrt::com_ptr<ID3D11UnorderedAccessView> midUAV_;
    winrt::com_ptr<ID3D11ShaderResourceView> midSRV_;
    winrt::com_ptr<ID3D11Texture2D> out_;
    winrt::com_ptr<ID3D11UnorderedAccessView> outUAV_;
    UINT outW_ = 0, outH_ = 0;

    // WGC's frame pool cycles between a small fixed set of textures, so a
    // one-entry SRV cache hits almost every frame.
    ID3D11Texture2D* cachedInput_ = nullptr;
    winrt::com_ptr<ID3D11ShaderResourceView> cachedInputSRV_;
};

} // namespace gpu
