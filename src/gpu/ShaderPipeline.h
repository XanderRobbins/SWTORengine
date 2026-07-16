#pragma once

#include <winrt/base.h>
#include <d3d11.h>
#include <string>

namespace gpu {

// Compiles and dispatches the compute chain:
//   FSR mode:  input -[FXAA]-> pre -EASU-> mid -RCAS-> out -[deband/grade]-> post
//   Passthrough mode: input -bilinear-> out
// Bracketed passes are skipped when disabled/neutral. Output textures are
// owned by the pipeline and reused across frames.
class ShaderPipeline {
public:
    enum class Mode { FSR, Passthrough };

    struct ProcessParams {
        Mode mode = Mode::FSR;
        float sharpness = 0.25f; // RCAS stops, 0 = max
        bool fxaa = true;
        float fxaaTextProtect = 0.75f; // 0..1, keeps UI glyphs unfiltered
        float debandStrength = 1.0f;   // 0 = off
        float vibrance = 0.0f;       // 0 neutral
        float saturation = 1.0f;     // 1 neutral
        float contrast = 1.0f;       // 1 neutral
        float gamma = 1.0f;          // 1 neutral
        float exposure = 0.0f;       // 0 neutral
        float filmic = 0.0f;         // 0 neutral
        float vignette = 0.0f;       // 0 neutral
        float grain = 0.0f;          // 0 neutral
        float clarity = 0.0f;        // 0 neutral, local contrast ("depth")
        UINT frameIndex = 0;         // animates grain/deband jitter

        bool PostPassNeeded() const {
            return debandStrength > 0.0f || vibrance != 0.0f || saturation != 1.0f ||
                   contrast != 1.0f || gamma != 1.0f || exposure != 0.0f || filmic != 0.0f ||
                   vignette != 0.0f || grain != 0.0f || clarity > 0.0f;
        }
    };

    bool Init(ID3D11Device* device, const std::wstring& shaderDir, std::wstring* error = nullptr);

    // viewportW/H: valid content region of the input texture (may be smaller
    // than the texture itself mid-resize). Returns the processed texture
    // (outW x outH, R8G8B8A8_UNORM) or nullptr on failure.
    ID3D11Texture2D* Process(ID3D11DeviceContext* ctx, ID3D11Texture2D* input,
                             UINT viewportW, UINT viewportH, UINT outW, UINT outH,
                             const ProcessParams& params);

private:
    bool CompileCS(const std::wstring& path, winrt::com_ptr<ID3D11ComputeShader>& out,
                   std::wstring* error);
    bool EnsurePreTexture(UINT w, UINT h);
    bool EnsureOutputTextures(UINT outW, UINT outH);
    ID3D11ShaderResourceView* GetInputSRV(ID3D11Texture2D* input);
    void DispatchPass(ID3D11DeviceContext* ctx, ID3D11ComputeShader* cs, ID3D11Buffer* cb,
                      ID3D11ShaderResourceView* srv, ID3D11UnorderedAccessView* uav,
                      UINT groupsX, UINT groupsY);

    winrt::com_ptr<ID3D11Device> device_;

    winrt::com_ptr<ID3D11ComputeShader> fxaaCS_;
    winrt::com_ptr<ID3D11ComputeShader> easuCS_;
    winrt::com_ptr<ID3D11ComputeShader> rcasCS_;
    winrt::com_ptr<ID3D11ComputeShader> blurCS_;
    winrt::com_ptr<ID3D11ComputeShader> postCS_;
    winrt::com_ptr<ID3D11ComputeShader> passthroughCS_;
    winrt::com_ptr<ID3D11SamplerState> linearClamp_;

    winrt::com_ptr<ID3D11Buffer> cbFxaa_;
    winrt::com_ptr<ID3D11Buffer> cbEasu_;
    winrt::com_ptr<ID3D11Buffer> cbRcas_;
    winrt::com_ptr<ID3D11Buffer> cbBlurX_;
    winrt::com_ptr<ID3D11Buffer> cbBlurY_;
    winrt::com_ptr<ID3D11Buffer> cbPost_;
    winrt::com_ptr<ID3D11Buffer> cbPassthrough_;

    // pre: FXAA output at capture viewport size
    winrt::com_ptr<ID3D11Texture2D> pre_;
    winrt::com_ptr<ID3D11UnorderedAccessView> preUAV_;
    winrt::com_ptr<ID3D11ShaderResourceView> preSRV_;
    UINT preW_ = 0, preH_ = 0;

    // mid (EASU), out (RCAS), post (grade) at output size
    winrt::com_ptr<ID3D11Texture2D> mid_;
    winrt::com_ptr<ID3D11UnorderedAccessView> midUAV_;
    winrt::com_ptr<ID3D11ShaderResourceView> midSRV_;
    winrt::com_ptr<ID3D11Texture2D> out_;
    winrt::com_ptr<ID3D11UnorderedAccessView> outUAV_;
    winrt::com_ptr<ID3D11ShaderResourceView> outSRV_;
    winrt::com_ptr<ID3D11Texture2D> post_;
    winrt::com_ptr<ID3D11UnorderedAccessView> postUAV_;
    // clarity blur ping-pong (lazily created on first use)
    winrt::com_ptr<ID3D11Texture2D> blurTmp_;
    winrt::com_ptr<ID3D11UnorderedAccessView> blurTmpUAV_;
    winrt::com_ptr<ID3D11ShaderResourceView> blurTmpSRV_;
    winrt::com_ptr<ID3D11Texture2D> blur_;
    winrt::com_ptr<ID3D11UnorderedAccessView> blurUAV_;
    winrt::com_ptr<ID3D11ShaderResourceView> blurSRV_;
    UINT outW_ = 0, outH_ = 0;

    // WGC's frame pool cycles between a small fixed set of textures, so a
    // one-entry SRV cache hits almost every frame.
    ID3D11Texture2D* cachedInput_ = nullptr;
    winrt::com_ptr<ID3D11ShaderResourceView> cachedInputSRV_;
};

} // namespace gpu
