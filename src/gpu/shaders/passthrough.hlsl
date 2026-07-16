// Debug/bypass pass: plain bilinear resample of input to output size.
// Used to validate the capture->dispatch->present plumbing without FSR.

Texture2D InputTexture : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer PassthroughConstants : register(b0) {
    uint2 OutputSize;
    float2 InputViewportScale; // (viewportW/texW, viewportH/texH)
};

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= OutputSize.x || id.y >= OutputSize.y) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(OutputSize) * InputViewportScale;
    OutputTexture[id.xy] = float4(InputTexture.SampleLevel(LinearSampler, uv, 0).rgb, 1.0);
}
