#include "ShaderPipeline.h"

#include <d3dcompiler.h>
#include <math.h>
#include <string.h>

// AMD's reference constant-setup functions (FsrEasuCon / FsrRcasCon), compiled
// for CPU. Same MIT-licensed headers the shaders include on the GPU side.
#define A_CPU 1
#include "shaders/ffx_a.h"
#include "shaders/ffx_fsr1.h"

namespace gpu {

namespace {

struct EasuConstants {
    AU1 con0[4];
    AU1 con1[4];
    AU1 con2[4];
    AU1 con3[4];
};

struct RcasConstants {
    AU1 con[4];
};

struct PassthroughConstants {
    UINT outW, outH;
    float scaleX, scaleY;
};

winrt::com_ptr<ID3D11Buffer> MakeConstantBuffer(ID3D11Device* device, UINT bytes) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    winrt::com_ptr<ID3D11Buffer> buffer;
    device->CreateBuffer(&desc, nullptr, buffer.put());
    return buffer;
}

} // namespace

bool ShaderPipeline::Init(ID3D11Device* device, const std::wstring& shaderDir,
                          std::wstring* error) {
    device_.copy_from(device);

    if (!CompileCS(shaderDir + L"fsr1_easu.hlsl", easuCS_, error)) return false;
    if (!CompileCS(shaderDir + L"fsr1_rcas.hlsl", rcasCS_, error)) return false;
    if (!CompileCS(shaderDir + L"passthrough.hlsl", passthroughCS_, error)) return false;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device_->CreateSamplerState(&sampler, linearClamp_.put()))) return false;

    cbEasu_ = MakeConstantBuffer(device_.get(), sizeof(EasuConstants));
    cbRcas_ = MakeConstantBuffer(device_.get(), sizeof(RcasConstants));
    cbPassthrough_ = MakeConstantBuffer(device_.get(), sizeof(PassthroughConstants));
    return cbEasu_ && cbRcas_ && cbPassthrough_;
}

bool ShaderPipeline::CompileCS(const std::wstring& path,
                               winrt::com_ptr<ID3D11ComputeShader>& out, std::wstring* error) {
    winrt::com_ptr<ID3DBlob> blob;
    winrt::com_ptr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    "mainCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                    blob.put(), errors.put());
    if (FAILED(hr)) {
        if (error) {
            *error = L"Shader compile failed: " + path;
            if (errors) {
                const char* msg = static_cast<const char*>(errors->GetBufferPointer());
                *error += L"\n";
                *error += std::wstring(msg, msg + errors->GetBufferSize());
            }
        }
        return false;
    }
    return SUCCEEDED(device_->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                  nullptr, out.put()));
}

bool ShaderPipeline::EnsureOutputTextures(UINT outW, UINT outH) {
    if (outW == outW_ && outH == outH_ && out_) return true;

    mid_ = nullptr; midUAV_ = nullptr; midSRV_ = nullptr;
    out_ = nullptr; outUAV_ = nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = outW;
    desc.Height = outH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // guaranteed typed-UAV-store format
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device_->CreateTexture2D(&desc, nullptr, mid_.put()))) return false;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, out_.put()))) return false;
    if (FAILED(device_->CreateUnorderedAccessView(mid_.get(), nullptr, midUAV_.put()))) return false;
    if (FAILED(device_->CreateShaderResourceView(mid_.get(), nullptr, midSRV_.put()))) return false;
    if (FAILED(device_->CreateUnorderedAccessView(out_.get(), nullptr, outUAV_.put()))) return false;

    outW_ = outW;
    outH_ = outH;
    return true;
}

ID3D11ShaderResourceView* ShaderPipeline::GetInputSRV(ID3D11Texture2D* input) {
    if (input == cachedInput_ && cachedInputSRV_) return cachedInputSRV_.get();
    cachedInputSRV_ = nullptr;
    if (FAILED(device_->CreateShaderResourceView(input, nullptr, cachedInputSRV_.put()))) {
        cachedInput_ = nullptr;
        return nullptr;
    }
    cachedInput_ = input;
    return cachedInputSRV_.get();
}

ID3D11Texture2D* ShaderPipeline::Process(ID3D11DeviceContext* ctx, ID3D11Texture2D* input,
                                         UINT viewportW, UINT viewportH, UINT outW, UINT outH,
                                         float sharpnessStops, Mode mode) {
    if (!input || !outW || !outH || !viewportW || !viewportH) return nullptr;
    if (!EnsureOutputTextures(outW, outH)) return nullptr;

    ID3D11ShaderResourceView* inputSRV = GetInputSRV(input);
    if (!inputSRV) return nullptr;

    D3D11_TEXTURE2D_DESC inputDesc{};
    input->GetDesc(&inputDesc);

    ID3D11SamplerState* samplers[] = {linearClamp_.get()};
    ctx->CSSetSamplers(0, 1, samplers);

    ID3D11ShaderResourceView* nullSRV[] = {nullptr};
    ID3D11UnorderedAccessView* nullUAV[] = {nullptr};

    if (mode == Mode::Passthrough) {
        PassthroughConstants pc{outW, outH,
                                static_cast<float>(viewportW) / inputDesc.Width,
                                static_cast<float>(viewportH) / inputDesc.Height};
        ctx->UpdateSubresource(cbPassthrough_.get(), 0, nullptr, &pc, 0, 0);

        ID3D11Buffer* cbs[] = {cbPassthrough_.get()};
        ID3D11ShaderResourceView* srvs[] = {inputSRV};
        ID3D11UnorderedAccessView* uavs[] = {outUAV_.get()};
        ctx->CSSetShader(passthroughCS_.get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
        ctx->CSSetShaderResources(0, 1, nullSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
        return out_.get();
    }

    // --- EASU: spatial upscale input viewport -> output resolution ---
    EasuConstants easu{};
    FsrEasuCon(easu.con0, easu.con1, easu.con2, easu.con3,
               static_cast<AF1>(viewportW), static_cast<AF1>(viewportH),
               static_cast<AF1>(inputDesc.Width), static_cast<AF1>(inputDesc.Height),
               static_cast<AF1>(outW), static_cast<AF1>(outH));
    ctx->UpdateSubresource(cbEasu_.get(), 0, nullptr, &easu, 0, 0);

    const UINT groupsX = (outW + 15) / 16;
    const UINT groupsY = (outH + 15) / 16;

    {
        ID3D11Buffer* cbs[] = {cbEasu_.get()};
        ID3D11ShaderResourceView* srvs[] = {inputSRV};
        ID3D11UnorderedAccessView* uavs[] = {midUAV_.get()};
        ctx->CSSetShader(easuCS_.get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        ctx->Dispatch(groupsX, groupsY, 1);
        ctx->CSSetShaderResources(0, 1, nullSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // --- RCAS: sharpen at output resolution ---
    RcasConstants rcas{};
    FsrRcasCon(rcas.con, sharpnessStops);
    ctx->UpdateSubresource(cbRcas_.get(), 0, nullptr, &rcas, 0, 0);

    {
        ID3D11Buffer* cbs[] = {cbRcas_.get()};
        ID3D11ShaderResourceView* srvs[] = {midSRV_.get()};
        ID3D11UnorderedAccessView* uavs[] = {outUAV_.get()};
        ctx->CSSetShader(rcasCS_.get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ctx->CSSetShaderResources(0, 1, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        ctx->Dispatch(groupsX, groupsY, 1);
        ctx->CSSetShaderResources(0, 1, nullSRV);
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    return out_.get();
}

} // namespace gpu
