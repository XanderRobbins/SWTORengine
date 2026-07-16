// Bloom stage 1: extract pixels above the brightness threshold into a
// half-resolution buffer (the 2x downsample comes free via bilinear sampling).

Texture2D InputTexture : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer BrightConstants : register(b0) {
    float Threshold; // luma above this starts to glow
    float Knee;      // soft ramp width
    float2 HalfSize;
};

static const float3 kLuma = float3(0.299, 0.587, 0.114);

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)HalfSize.x || id.y >= (uint)HalfSize.y) return;

    const float2 uv = (float2(id.xy) + 0.5) / HalfSize;
    const float3 c = InputTexture.SampleLevel(LinearSampler, uv, 0).rgb;

    float w = saturate((dot(c, kLuma) - Threshold) / max(Knee, 1e-3));
    w *= w; // quadratic soft knee
    OutputTexture[id.xy] = float4(c * w, 1.0);
}
