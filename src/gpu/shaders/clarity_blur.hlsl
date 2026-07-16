// Separable 13-tap Gaussian (sigma ~4px), one direction per dispatch.
// Feeds the clarity (local contrast) mix in post_grade.hlsl.

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer BlurConstants : register(b0) {
    float2 Direction; // (1,0) or (0,1), in texels
    float2 OutSize;
};

static const float kWeights[7] = {0.1974, 0.1746, 0.1210, 0.0656, 0.0278, 0.0092, 0.0024};

float3 LoadClamped(int2 p) {
    p = clamp(p, int2(0, 0), int2(OutSize) - 1);
    return InputTexture.Load(int3(p, 0)).rgb;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)OutSize.x || id.y >= (uint)OutSize.y) return;

    const int2 pos = int2(id.xy);
    const int2 dir = int2(Direction);

    float3 sum = LoadClamped(pos) * kWeights[0];
    [unroll]
    for (int i = 1; i < 7; ++i) {
        sum += (LoadClamped(pos + dir * i) + LoadClamped(pos - dir * i)) * kWeights[i];
    }
    OutputTexture[pos] = float4(sum, 1.0);
}
