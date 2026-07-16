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

struct FxaaConstants {
    float texW, texH;
    float viewportW, viewportH;
    float textProtect;
    float pad[3];
};

struct EasuConstants {
    AU1 con0[4];
    AU1 con1[4];
    AU1 con2[4];
    AU1 con3[4];
};

struct RcasConstants {
    AU1 con[4];
};

struct PostConstants {
    float vibrance, saturation, contrast, gamma;
    float exposure, filmic, vignette, grain;
    float debandStrength, frameIndex, outW, outH;
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

bool MakeUAVTexture(ID3D11Device* device, UINT w, UINT h, winrt::com_ptr<ID3D11Texture2D>& tex,
                    winrt::com_ptr<ID3D11UnorderedAccessView>* uav,
                    winrt::com_ptr<ID3D11ShaderResourceView>* srv) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // guaranteed typed-UAV-store format
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, tex.put()))) return false;
    if (uav && FAILED(device->CreateUnorderedAccessView(tex.get(), nullptr, uav->put())))
        return false;
    if (srv && FAILED(device->CreateShaderResourceView(tex.get(), nullptr, srv->put())))
        return false;
    return true;
}

} // namespace

bool ShaderPipeline::Init(ID3D11Device* device, const std::wstring& shaderDir,
                          std::wstring* error) {
    device_.copy_from(device);

    if (!CompileCS(shaderDir + L"fxaa.hlsl", fxaaCS_, error)) return false;
    if (!CompileCS(shaderDir + L"fsr1_easu.hlsl", easuCS_, error)) return false;
    if (!CompileCS(shaderDir + L"fsr1_rcas.hlsl", rcasCS_, error)) return false;
    if (!CompileCS(shaderDir + L"post_grade.hlsl", postCS_, error)) return false;
    if (!CompileCS(shaderDir + L"passthrough.hlsl", passthroughCS_, error)) return false;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device_->CreateSamplerState(&sampler, linearClamp_.put()))) return false;

    cbFxaa_ = MakeConstantBuffer(device_.get(), sizeof(FxaaConstants));
    cbEasu_ = MakeConstantBuffer(device_.get(), sizeof(EasuConstants));
    cbRcas_ = MakeConstantBuffer(device_.get(), sizeof(RcasConstants));
    cbPost_ = MakeConstantBuffer(device_.get(), sizeof(PostConstants));
    cbPassthrough_ = MakeConstantBuffer(device_.get(), sizeof(PassthroughConstants));
    return cbFxaa_ && cbEasu_ && cbRcas_ && cbPost_ && cbPassthrough_;
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

bool ShaderPipeline::EnsurePreTexture(UINT w, UINT h) {
    if (w == preW_ && h == preH_ && pre_) return true;
    pre_ = nullptr; preUAV_ = nullptr; preSRV_ = nullptr;
    if (!MakeUAVTexture(device_.get(), w, h, pre_, &preUAV_, &preSRV_)) return false;
    preW_ = w;
    preH_ = h;
    return true;
}

bool ShaderPipeline::EnsureOutputTextures(UINT outW, UINT outH) {
    if (outW == outW_ && outH == outH_ && out_) return true;

    mid_ = nullptr; midUAV_ = nullptr; midSRV_ = nullptr;
    out_ = nullptr; outUAV_ = nullptr; outSRV_ = nullptr;
    post_ = nullptr; postUAV_ = nullptr;

    if (!MakeUAVTexture(device_.get(), outW, outH, mid_, &midUAV_, &midSRV_)) return false;
    if (!MakeUAVTexture(device_.get(), outW, outH, out_, &outUAV_, &outSRV_)) return false;
    if (!MakeUAVTexture(device_.get(), outW, outH, post_, &postUAV_, nullptr)) return false;

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

void ShaderPipeline::DispatchPass(ID3D11DeviceContext* ctx, ID3D11ComputeShader* cs,
                                  ID3D11Buffer* cb, ID3D11ShaderResourceView* srv,
                                  ID3D11UnorderedAccessView* uav, UINT groupsX, UINT groupsY) {
    ID3D11Buffer* cbs[] = {cb};
    ID3D11ShaderResourceView* srvs[] = {srv};
    ID3D11UnorderedAccessView* uavs[] = {uav};
    ID3D11ShaderResourceView* nullSRV[] = {nullptr};
    ID3D11UnorderedAccessView* nullUAV[] = {nullptr};

    ctx->CSSetShader(cs, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->CSSetShaderResources(0, 1, srvs);
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    ctx->Dispatch(groupsX, groupsY, 1);
    ctx->CSSetShaderResources(0, 1, nullSRV);
    ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}

ID3D11Texture2D* ShaderPipeline::Process(ID3D11DeviceContext* ctx, ID3D11Texture2D* input,
                                         UINT viewportW, UINT viewportH, UINT outW, UINT outH,
                                         const ProcessParams& params) {
    if (!input || !outW || !outH || !viewportW || !viewportH) return nullptr;
    if (!EnsureOutputTextures(outW, outH)) return nullptr;

    ID3D11ShaderResourceView* inputSRV = GetInputSRV(input);
    if (!inputSRV) return nullptr;

    D3D11_TEXTURE2D_DESC inputDesc{};
    input->GetDesc(&inputDesc);

    ID3D11SamplerState* samplers[] = {linearClamp_.get()};
    ctx->CSSetSamplers(0, 1, samplers);

    if (params.mode == Mode::Passthrough) {
        PassthroughConstants pc{outW, outH,
                                static_cast<float>(viewportW) / inputDesc.Width,
                                static_cast<float>(viewportH) / inputDesc.Height};
        ctx->UpdateSubresource(cbPassthrough_.get(), 0, nullptr, &pc, 0, 0);
        DispatchPass(ctx, passthroughCS_.get(), cbPassthrough_.get(), inputSRV, outUAV_.get(),
                     (outW + 7) / 8, (outH + 7) / 8);
        return out_.get();
    }

    // --- FXAA at capture resolution (FSR wants anti-aliased input) ---
    ID3D11ShaderResourceView* easuInput = inputSRV;
    float easuTexW = static_cast<float>(inputDesc.Width);
    float easuTexH = static_cast<float>(inputDesc.Height);
    if (params.fxaa) {
        if (!EnsurePreTexture(viewportW, viewportH)) return nullptr;
        FxaaConstants fc{static_cast<float>(inputDesc.Width), static_cast<float>(inputDesc.Height),
                         static_cast<float>(viewportW), static_cast<float>(viewportH),
                         params.fxaaTextProtect, {}};
        ctx->UpdateSubresource(cbFxaa_.get(), 0, nullptr, &fc, 0, 0);
        DispatchPass(ctx, fxaaCS_.get(), cbFxaa_.get(), inputSRV, preUAV_.get(),
                     (viewportW + 7) / 8, (viewportH + 7) / 8);
        easuInput = preSRV_.get();
        easuTexW = static_cast<float>(viewportW);
        easuTexH = static_cast<float>(viewportH);
    }

    // --- EASU: spatial upscale viewport -> output resolution ---
    EasuConstants easu{};
    FsrEasuCon(easu.con0, easu.con1, easu.con2, easu.con3,
               static_cast<AF1>(viewportW), static_cast<AF1>(viewportH),
               static_cast<AF1>(easuTexW), static_cast<AF1>(easuTexH),
               static_cast<AF1>(outW), static_cast<AF1>(outH));
    ctx->UpdateSubresource(cbEasu_.get(), 0, nullptr, &easu, 0, 0);

    const UINT groupsX = (outW + 15) / 16;
    const UINT groupsY = (outH + 15) / 16;
    DispatchPass(ctx, easuCS_.get(), cbEasu_.get(), easuInput, midUAV_.get(), groupsX, groupsY);

    // --- RCAS: sharpen at output resolution ---
    RcasConstants rcas{};
    FsrRcasCon(rcas.con, params.sharpness);
    ctx->UpdateSubresource(cbRcas_.get(), 0, nullptr, &rcas, 0, 0);
    DispatchPass(ctx, rcasCS_.get(), cbRcas_.get(), midSRV_.get(), outUAV_.get(), groupsX,
                 groupsY);

    // --- Deband + color grade (skipped entirely when neutral) ---
    if (!params.PostPassNeeded()) return out_.get();

    PostConstants post{params.vibrance,       params.saturation,
                       params.contrast,       params.gamma,
                       params.exposure,       params.filmic,
                       params.vignette,       params.grain,
                       params.debandStrength, static_cast<float>(params.frameIndex % 1024),
                       static_cast<float>(outW), static_cast<float>(outH)};
    ctx->UpdateSubresource(cbPost_.get(), 0, nullptr, &post, 0, 0);
    DispatchPass(ctx, postCS_.get(), cbPost_.get(), outSRV_.get(), postUAV_.get(),
                 (outW + 7) / 8, (outH + 7) / 8);
    return post_.get();
}

} // namespace gpu
